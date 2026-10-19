/*
 * FM6: trava contra page fault da GPU (17_fm6.txt "DIAGNOSTICO DO PAGE FAULT"). O FM6 para com a GPU lendo um
 * descritor com bytes 0xdeadbeef (V# da AMD -> endereco 0xbeefdeadb000); cada page fault e um TDR, e uma sequencia
 * deles chegou a derrubar o driver de video (BSOD 0x3b, 2026-10-18). A trava fica no device D3D12 do Azimuth
 * (ligada pelo hook do D3D12CreateDevice em debug12_fm5.c; o device nasce antes de o jogo ser conhecido, entao vale
 * para todos, mas so muda algo onde a GPU daria page fault) e age antes de a GPU ver o dado.
 * Sempre ligado so o ultimo item (UAVs nulos, a correcao da causa); os outros sao defesas/diagnostico com custo de CPU
 * por draw/tabela e ficam so com o rastreio (D3D12Debug>=3):
 *  - heaps CBV/SRV/UAV: bitmap de slots gravados (Create*View / destino de CopyDescriptors*); copiar um slot de
 *    origem nunca gravado loga a pilha e grava um SRV nulo nele antes (o handle de CPU na AMD nao aponta para os
 *    bytes do descritor: o conteudo nao da para conferir, so o bitmap);
 *  - Set*RootDescriptorTable: tamanho de cada tabela CBV/SRV/UAV lido da root signature (CreateRootSignature);
 *    slot nunca gravado dentro da tabela no heap visivel pela GPU -> relatorio + SRV nulo nele antes do draw;
 *  - endereco de GPU envenenado (0xdeadbeef / 0xbeefdead.. / fora de 48 bits) em CBV, root views, vertex/index
 *    buffer e stream out: loga a pilha; CBV vira nulo; root view/buffer vira 0 (nulo) no lugar do endereco;
 *  - tabela grafica so de UAV nao definida na hora do draw (17_fm6.txt PASSO 29: os GS do FM6 gravam em u0 sem o jogo
 *    ligar UAV; no console o slot vazio descarta a escrita, no D3D12 a tabela fica indefinida e a AMD usa o endereco
 *    placeholder 0xdeadbeef...) -> liga uma tabela de UAVs nulos (GG_NULL_UAVS slots a mais no heap do Azimuth).
 * Com o rastreio (D3D12Debug>=3): CopyTextureRegion conferido (subrecurso/footprint/regiao, gg_copy_check) e chamadas
 * de residencia (MakeResident/Evict/...) contadas; ambos so registram. Draws (DrawInstanced/DrawIndexedInstanced/
 * ExecuteIndirect) e ClearRenderTargetView entram no rastreio por lista com a root signature grafica e o PSO atual.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <d3d12.h>

extern uint8_t *g_base;
extern uint8_t *g_fm6_game;
extern void wlog(const char *fmt, ...);

#define GG_MAX_HEAPS 512
#define GG_MAX_REPORTS 40
#define GG_NULL_UAVS 64             /* UAVs nulos no fim do heap visivel do Azimuth (gg_null_uav_heap) */
#define GG_AZ_VISIBLE_HEAP 0xf4000  /* tamanho fixo do heap CBV/SRV/UAV visivel do Azimuth (0x14010e08d) */

/* espelho de um descritor (o que o Create*View gravou no slot) */
typedef struct { void *res; uint64_t va; uint32_t w, h; uint16_t fmt; uint8_t kind, dim, rdim, flags; } gg_desc;

typedef struct {
    uintptr_t start;
    uint32_t count, inc;
    volatile LONG *bits;
    uint64_t gpu;                 /* inicio no endereco de GPU (heap visivel pela GPU), 0 senao */
    gg_desc *desc;                /* espelho por slot (NULL se faltou memoria) */
} gg_heap;

enum { GG_DK_NONE, GG_DK_CBV, GG_DK_SRV, GG_DK_UAV, GG_DK_GUARD_NULL };
#define GG_DF_RESERVED 1           /* GetHeapProperties falha: recurso reservado (tiled) */
#define GG_DF_NO_RESOURCE 2        /* view nula */
void gg_desc_set(uintptr_t h, const gg_desc *d);

static gg_heap heaps[GG_MAX_HEAPS];
static int nheaps;
static SRWLOCK heap_lock = SRWLOCK_INIT;
static volatile LONG reports;

/* ---- logica pura (testada em tests/t_d3d.c) ---- */

/* endereco de GPU com o veneno 0xdeadbeef: inteiro (fora de 48 bits) ou truncado (0xbeefdead....) */
int gg_va_poisoned(uint64_t va)
{
    uint32_t hi = (uint32_t)(va >> 32);
    if (va >> 48) return 1;
    return (hi & 0xffff) == 0xbeef && ((uint32_t)va >> 20) == 0xdea;
}

/* registra um heap (substitui os que se sobrepoem: memoria reaproveitada de um heap solto) */
void gg_heap_add(uintptr_t start, uint32_t count, uint32_t inc, uint64_t gpu)
{
    int i;
    volatile LONG *bits = (volatile LONG *)calloc((count + 31) / 32, sizeof(LONG));
    uintptr_t end = start + (uintptr_t)count * inc;
    if (!bits || !inc) { free((void *)bits); return; }
    AcquireSRWLockExclusive(&heap_lock);
    for (i = 0; i < nheaps; i++) {
        if (heaps[i].start < end && start < heaps[i].start + (uintptr_t)heaps[i].count * heaps[i].inc) {
            free((void *)heaps[i].bits);
            free(heaps[i].desc);
            heaps[i] = heaps[--nheaps];
            i--;
        }
    }
    if (nheaps < GG_MAX_HEAPS) heaps[nheaps++] = (gg_heap){ start, count, inc, bits, gpu, calloc(count, sizeof(gg_desc)) };
    else free((void *)bits);
    ReleaseSRWLockExclusive(&heap_lock);
}

void gg_heap_reset(void)
{
    AcquireSRWLockExclusive(&heap_lock);
    while (nheaps) { nheaps--; free((void *)heaps[nheaps].bits); free(heaps[nheaps].desc); }
    ReleaseSRWLockExclusive(&heap_lock);
}

/* marca n slots a partir de h como gravados (ou consulta: devolve -1 fora de heap conhecido, 0/1 senao) */
static int gg_slots(uintptr_t h, uint32_t n, int mark)
{
    int i, r = -1;
    AcquireSRWLockShared(&heap_lock);
    for (i = 0; i < nheaps; i++) {
        gg_heap *p = &heaps[i];
        uintptr_t idx;
        if (h < p->start || h >= p->start + (uintptr_t)p->count * p->inc) continue;
        idx = (h - p->start) / p->inc;
        r = 1;
        for (; n && idx < p->count; n--, idx++) {
            if (mark) InterlockedOr(&p->bits[idx / 32], (LONG)(1u << (idx % 32)));
            else if (!(p->bits[idx / 32] & (1u << (idx % 32)))) r = 0;
        }
        break;
    }
    ReleaseSRWLockShared(&heap_lock);
    return r;
}

void gg_mark(uintptr_t h, uint32_t n) { gg_slots(h, n, 1); }
int gg_written(uintptr_t h) { return gg_slots(h, 1, 0); }

/* handle de GPU de um heap visivel pela GPU -> handle de CPU do mesmo slot (0 se nao for de heap conhecido) */
uintptr_t gg_gpu_to_cpu(uint64_t g)
{
    int i;
    uintptr_t r = 0;
    AcquireSRWLockShared(&heap_lock);
    for (i = 0; i < nheaps; i++)
        if (heaps[i].gpu && g >= heaps[i].gpu && g < heaps[i].gpu + (uint64_t)heaps[i].count * heaps[i].inc) {
            r = heaps[i].start + (uintptr_t)(g - heaps[i].gpu);
            break;
        }
    ReleaseSRWLockShared(&heap_lock);
    return r;
}

/* tabela so com ranges de UAV (o GS do FM6 grava num UAV que o jogo nao liga, ver gg_null_uavs) */
int gg_table_uav_only(const D3D12_ROOT_DESCRIPTOR_TABLE1 *t)
{
    uint32_t i;
    for (i = 0; i < t->NumDescriptorRanges; i++)
        if (t->pDescriptorRanges[i].RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_UAV) return 0;
    return t->NumDescriptorRanges != 0;
}

/* tamanho (em descritores) de uma tabela CBV/SRV/UAV: maior fim dos ranges (APPEND continua do anterior);
 * 0 = tabela de sampler, vazia ou sem limite (nao conferida) */
uint32_t gg_table_size(const D3D12_ROOT_DESCRIPTOR_TABLE1 *t)
{
    uint32_t i, cur = 0, size = 0;
    for (i = 0; i < t->NumDescriptorRanges; i++) {
        const D3D12_DESCRIPTOR_RANGE1 *r = &t->pDescriptorRanges[i];
        uint32_t start = r->OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND ? cur
                         : r->OffsetInDescriptorsFromTableStart;
        if (r->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER || r->NumDescriptors == UINT_MAX) return 0;
        cur = start + r->NumDescriptors;
        if (cur > size) size = cur;
    }
    return size;
}

/* ---- root signatures e root signature atual por command list (tabelas fixas, endereco aberto) ---- */

#define GG_MAX_PARAMS 64
#define GG_RS_SLOTS 4096
#define GG_CL_SLOTS 2048

typedef struct { void *rs; uint32_t n; uint32_t size[GG_MAX_PARAMS]; uint64_t uav; } gg_rs;   /* uav: tabelas so de UAV */
typedef struct { void *cl; void *rs[3]; uint64_t gtab; } gg_cl;   /* rs[0] grafica, rs[1] compute, rs[2] = PSO atual (GG_CL_PSO);
                                                                     gtab: tabelas graficas definidas desde a root signature */
#define GG_CL_PSO 2

static gg_rs rs_tab[GG_RS_SLOTS];
static gg_cl cl_tab[GG_CL_SLOTS];
static SRWLOCK rs_lock = SRWLOCK_INIT, cl_lock = SRWLOCK_INIT;

static unsigned hptr(const void *p, unsigned n) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) & (n - 1); }

/* tira `key` (1o campo de cada registro) de uma tabela de endereco aberto com sondagem linear por hptr, sem quebrar
   as cadeias: cada registro seguinte que pode ocupar o buraco volta para ele (o chamador segura o lock da tabela).
   Sem isso as listas destruidas ficavam para sempre, a tabela enchia e cada draw sondava a tabela inteira (o fps
   caia ate travar, 17_fm6.txt PASSO 33) */
static int lp_remove(void *tab, size_t esz, unsigned n, const void *key)
{
    uint8_t *t = tab;
    unsigned i = hptr(key, n), j, c;
    if (!key) return 0;
#define LP_KEY(s) (*(void *const *)(t + (size_t)(s) * esz))
    for (c = 0; LP_KEY(i) != key; c++, i = (i + 1) & (n - 1))
        if (c == n || !LP_KEY(i)) return 0;
    for (j = i, c = 0; c < n; c++) {
        unsigned k;
        j = (j + 1) & (n - 1);
        if (j == i || !LP_KEY(j)) break;
        k = hptr(LP_KEY(j), n);
        if (((j - k) & (n - 1)) >= ((j - i) & (n - 1))) {   /* a casa k vem antes do buraco (ou e ele) */
            memcpy(t + (size_t)i * esz, t + (size_t)j * esz, esz);
            i = j;
        }
    }
#undef LP_KEY
    memset(t + (size_t)i * esz, 0, esz);
    return 1;
}

void gg_rs_add(void *rs, const uint32_t *size, uint32_t n)
{
    unsigned h = hptr(rs, GG_RS_SLOTS), k;
    if (n > GG_MAX_PARAMS) n = GG_MAX_PARAMS;
    AcquireSRWLockExclusive(&rs_lock);
    for (k = 0; k < GG_RS_SLOTS; k++, h = (h + 1) & (GG_RS_SLOTS - 1))
        if (!rs_tab[h].rs || rs_tab[h].rs == rs) {
            rs_tab[h].rs = rs;
            rs_tab[h].n = n;
            memcpy(rs_tab[h].size, size, n * sizeof *size);
            break;
        }
    ReleaseSRWLockExclusive(&rs_lock);
}

uint32_t gg_rs_table(void *rs, uint32_t param)
{
    unsigned h = hptr(rs, GG_RS_SLOTS), k;
    uint32_t r = 0;
    if (!rs) return 0;
    AcquireSRWLockShared(&rs_lock);
    for (k = 0; k < GG_RS_SLOTS && rs_tab[h].rs; k++, h = (h + 1) & (GG_RS_SLOTS - 1))
        if (rs_tab[h].rs == rs) { r = param < rs_tab[h].n ? rs_tab[h].size[param] : 0; break; }
    ReleaseSRWLockShared(&rs_lock);
    return r;
}

/* tabelas so de UAV de uma root signature (bit por parametro) */
void gg_rs_set_uav(void *rs, uint64_t mask)
{
    unsigned h = hptr(rs, GG_RS_SLOTS), k;
    AcquireSRWLockExclusive(&rs_lock);
    for (k = 0; k < GG_RS_SLOTS && rs_tab[h].rs; k++, h = (h + 1) & (GG_RS_SLOTS - 1))
        if (rs_tab[h].rs == rs) { rs_tab[h].uav = mask; break; }
    ReleaseSRWLockExclusive(&rs_lock);
}

uint64_t gg_rs_uav(void *rs)
{
    unsigned h = hptr(rs, GG_RS_SLOTS), k;
    uint64_t r = 0;
    if (!rs) return 0;
    AcquireSRWLockShared(&rs_lock);
    for (k = 0; k < GG_RS_SLOTS && rs_tab[h].rs; k++, h = (h + 1) & (GG_RS_SLOTS - 1))
        if (rs_tab[h].rs == rs) { r = rs_tab[h].uav; break; }
    ReleaseSRWLockShared(&rs_lock);
    return r;
}

/* tabelas graficas definidas na lista: op 0 = zera (root signature nova / Reset), 1 = marca `bits`; devolve a mascara */
uint64_t gg_cl_gtab(void *cl, int op, uint64_t bits)
{
    unsigned h = hptr(cl, GG_CL_SLOTS), k;
    uint64_t r = 0;
    AcquireSRWLockExclusive(&cl_lock);
    for (k = 0; k < GG_CL_SLOTS; k++, h = (h + 1) & (GG_CL_SLOTS - 1)) {
        if (!cl_tab[h].cl) { cl_tab[h].cl = cl; cl_tab[h].gtab = 0; }
        if (cl_tab[h].cl != cl) continue;
        if (op == 0) cl_tab[h].gtab = 0; else cl_tab[h].gtab |= bits;
        r = cl_tab[h].gtab;
        break;
    }
    ReleaseSRWLockExclusive(&cl_lock);
    return r;
}

/* root signature atual de uma command list (compute 0/1; GG_CL_PSO = PSO): do_set grava `set`, senao so consulta */
void *gg_cl_rs(void *cl, int compute, void *set, int do_set)
{
    unsigned h = hptr(cl, GG_CL_SLOTS), k;
    void *r = NULL;
    if (do_set) AcquireSRWLockExclusive(&cl_lock); else AcquireSRWLockShared(&cl_lock);
    for (k = 0; k < GG_CL_SLOTS; k++, h = (h + 1) & (GG_CL_SLOTS - 1)) {
        if (cl_tab[h].cl == cl) { if (do_set) cl_tab[h].rs[compute] = set; r = cl_tab[h].rs[compute]; break; }
        if (!cl_tab[h].cl) { if (do_set) { cl_tab[h].cl = cl; cl_tab[h].rs[compute] = set; r = set; } break; }
    }
    if (do_set) ReleaseSRWLockExclusive(&cl_lock); else ReleaseSRWLockShared(&cl_lock);
    return r;
}

/* ---- diagnostico (D3D12Debug=3): quem grava Dispatch/CopyTextureRegion em cada command list ----
 * O DRED so da a contagem de operacoes da ultima lista antes do page fault (64 Dispatch + 64 CopyTextureRegion);
 * aqui cada lista guarda os pontos de chamada distintos (pilha em game/azimuth + root signature de compute), zerados
 * no Reset, e o dred_watch grava os das listas que mostra. */

#define GG_SITE_FRAMES 6
#define GG_SITES 24
#define GG_TRACE_SLOTS 2048

enum { GG_OP_DISPATCH, GG_OP_COPY_TEXTURE, GG_OP_DRAW, GG_OP_DRAW_INDEXED, GG_OP_EXECUTE_INDIRECT, GG_OP_CLEAR_RTV };
static const char *const gg_op_name[] = {
    "Dispatch", "CopyTextureRegion", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect", "ClearRenderTargetView",
};

typedef struct {
    uint32_t hash, count, op, nframes;
    void *rs, *pso;               /* root signature (compute no Dispatch, grafica no resto); PSO da 1a ocorrencia */
    uint32_t arg[3];              /* 1a ocorrencia: grupos do Dispatch / destino x,y,z da copia / contagens do draw */
    void *frames[GG_SITE_FRAMES];
} gg_site;
typedef struct { void *cl; uint32_t n, dropped; gg_site s[GG_SITES]; } gg_trace;

/* D3D12Debug - 2: 1 = rastreio; 2 = + pula a compute interna; 5 = + pula CopyTextureRegion; 6 = + pula os draws;
   7 = + pula os Dispatch fora da compute interna */
int gg_trace_on;
static void *volatile internal_rs; /* root signature "k12 + tab[SRV 1, UAV 1]" (compute interna do Azimuth) */

/* forma da root signature da compute interna do Azimuth (resolve/conversao 1920x1080 + copia, suspeita do page fault) */
int gg_is_internal_rs(const D3D12_ROOT_SIGNATURE_DESC1 *d)
{
    const D3D12_ROOT_PARAMETER1 *p = d->pParameters;
    return d->NumParameters == 2 && p[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
           p[0].Constants.Num32BitValues == 12 && p[1].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
           p[1].DescriptorTable.NumDescriptorRanges == 2 &&
           p[1].DescriptorTable.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
           p[1].DescriptorTable.pDescriptorRanges[1].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
}
static gg_trace trace_tab[GG_TRACE_SLOTS];
static SRWLOCK trace_lock = SRWLOCK_INIT;

/* registro da lista (cria se `make`); chamado com trace_lock */
static gg_trace *gg_trace_slot(void *cl, int make)
{
    unsigned h = hptr(cl, GG_TRACE_SLOTS), k;
    for (k = 0; k < GG_TRACE_SLOTS; k++, h = (h + 1) & (GG_TRACE_SLOTS - 1)) {
        if (trace_tab[h].cl == cl) return &trace_tab[h];
        if (!trace_tab[h].cl) {
            if (!make) return NULL;
            trace_tab[h].cl = cl;
            return &trace_tab[h];
        }
    }
    return NULL;
}

void gg_trace_add(void *cl, uint32_t op, void *rs, void *pso, const uint32_t *arg, void *const *frames, uint32_t nframes)
{
    uint32_t hash = 2166136261u ^ op, i;
    gg_trace *t;
    if (nframes > GG_SITE_FRAMES) nframes = GG_SITE_FRAMES;
    for (i = 0; i < nframes; i++) hash = (hash ^ (uint32_t)(uintptr_t)frames[i]) * 16777619u;
    hash = (hash ^ (uint32_t)((uintptr_t)rs >> 4)) * 16777619u;
    AcquireSRWLockExclusive(&trace_lock);
    if ((t = gg_trace_slot(cl, 1))) {
        for (i = 0; i < t->n; i++)
            if (t->s[i].hash == hash && t->s[i].op == op && t->s[i].rs == rs) { t->s[i].count++; break; }
        if (i == t->n) {
            if (t->n < GG_SITES) {
                gg_site *s = &t->s[t->n++];
                s->hash = hash; s->count = 1; s->op = op; s->rs = rs; s->pso = pso; s->nframes = nframes;
                memcpy(s->arg, arg, sizeof s->arg);
                memcpy(s->frames, frames, nframes * sizeof *frames);
            } else t->dropped++;
        }
    }
    ReleaseSRWLockExclusive(&trace_lock);
}

void gg_trace_reset(void *cl)
{
    gg_trace *t;
    AcquireSRWLockExclusive(&trace_lock);
    if ((t = gg_trace_slot(cl, 0))) { t->n = 0; t->dropped = 0; }
    ReleaseSRWLockExclusive(&trace_lock);
}

/* copia (para o teste e para o log) dos pontos da lista; devolve quantos */
uint32_t gg_trace_get(void *cl, gg_site *out, uint32_t max, uint32_t *dropped)
{
    gg_trace *t;
    uint32_t n = 0;
    AcquireSRWLockShared(&trace_lock);
    if ((t = gg_trace_slot(cl, 0))) {
        n = t->n < max ? t->n : max;
        memcpy(out, t->s, n * sizeof *out);
        if (dropped) *dropped = t->dropped;
    }
    ReleaseSRWLockShared(&trace_lock);
    return n;
}

/* ---- validacao de CopyTextureRegion (17_fm6.txt PASSO 26) ----
 * O +0x10000 do endereco do page fault (0xbeefdeadb000 -> 0xbeefdeaeb000) parece o "subrecurso seguinte" de um
 * objeto sem memoria; a ultima lista do DRED sempre tem dezenas de CopyTextureRegion. Confere subrecurso,
 * footprint dentro do buffer e regiao dentro dos dois lados. Limites por baixo (sem tamanho de texel por formato):
 * so acusa o que esta certamente fora. */
static int gg_is_bc(DXGI_FORMAT f) { return (f >= 70 && f <= 84) || (f >= 94 && f <= 99); }
static int gg_planes(DXGI_FORMAT f)
{
    return f == 19 || f == 20 || f == 21 || f == 22 || f == 44 || f == 45 || f == 46 || f == 47 ||
           (f >= 103 && f <= 108) || f == 110 ? 2 : 1;
}

/* extensao (w, h, d) de um lado da copia; 0 = ok, senao grava o motivo em why */
static int gg_copy_extent(const char *side, const D3D12_RESOURCE_DESC *r, const D3D12_TEXTURE_COPY_LOCATION *loc,
                          uint64_t *w, uint64_t *h, uint64_t *d, char *why)
{
    if (loc->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) {
        const D3D12_SUBRESOURCE_FOOTPRINT *f = &loc->PlacedFootprint.Footprint;
        uint64_t rows = gg_is_bc(f->Format) ? (f->Height + 3) / 4 : f->Height, need;
        if (r->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            return wsprintfA(why, "%s: footprint num recurso que nao e buffer (dim %d)", side, r->Dimension), 1;
        need = loc->PlacedFootprint.Offset + (uint64_t)f->RowPitch * (rows * (f->Depth ? f->Depth : 1) - 1) + 1;
        if (!f->Width || !f->Height || !f->Depth || need > r->Width)
            return wsprintfA(why, "%s: footprint fora do buffer (offset %lu pitch %u %ux%ux%u fmt %d, buffer %lu)", side,
                             (unsigned long)loc->PlacedFootprint.Offset, f->RowPitch, f->Width, f->Height, f->Depth,
                             f->Format, (unsigned long)r->Width), 1;
        *w = f->Width; *h = f->Height; *d = f->Depth;
        return 0;
    } else {
        uint32_t mips = r->MipLevels ? r->MipLevels : 1, mip;
        uint32_t arr = r->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : r->DepthOrArraySize;
        uint32_t n = mips * (arr ? arr : 1) * gg_planes(r->Format), i = loc->SubresourceIndex;
        if (r->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || r->Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
            return wsprintfA(why, "%s: subrecurso %u num recurso de dim %d", side, i, r->Dimension), 1;
        if (i >= n)
            return wsprintfA(why, "%s: subrecurso %u >= %u (%lux%u x%u mips %u fmt %d dim %d)", side, i, n,
                             (unsigned long)r->Width, r->Height, r->DepthOrArraySize, r->MipLevels, r->Format,
                             r->Dimension), 1;
        mip = i % mips;
        *w = r->Width >> mip ? r->Width >> mip : 1;
        *h = r->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? 1 : (r->Height >> mip ? r->Height >> mip : 1);
        *d = r->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? (r->DepthOrArraySize >> mip ? r->DepthOrArraySize >> mip : 1) : 1;
        if (gg_is_bc(r->Format)) { *w = (*w + 3) & ~3ull; *h = (*h + 3) & ~3ull; }   /* blocos 4x4 inteiros */
        return 0;
    }
}

int gg_copy_check(const D3D12_RESOURCE_DESC *dr, const D3D12_TEXTURE_COPY_LOCATION *dst, UINT x, UINT y, UINT z,
                  const D3D12_RESOURCE_DESC *sr, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *box, char *why)
{
    uint64_t dw, dh, dd, sw, sh, sd, cw, ch, cd;
    if (gg_copy_extent("destino", dr, dst, &dw, &dh, &dd, why) || gg_copy_extent("origem", sr, src, &sw, &sh, &sd, why))
        return 1;
    if (box) {
        if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back) return 0;   /* vazio: no-op */
        if (box->right > sw || box->bottom > sh || box->back > sd)
            return wsprintfA(why, "origem: box (%u,%u,%u)-(%u,%u,%u) fora de %ux%ux%u", box->left, box->top, box->front,
                             box->right, box->bottom, box->back, (unsigned)sw, (unsigned)sh, (unsigned)sd), 1;
        cw = box->right - box->left; ch = box->bottom - box->top; cd = box->back - box->front;
    } else {
        cw = sw; ch = sh; cd = sd;
    }
    if (x + cw > dw || y + ch > dh || z + cd > dd)
        return wsprintfA(why, "destino: regiao (%u,%u,%u) + %ux%ux%u fora de %ux%ux%u", x, y, z, (unsigned)cw,
                         (unsigned)ch, (unsigned)cd, (unsigned)dw, (unsigned)dh, (unsigned)dd), 1;
    return 0;
}

/* ---- hooks ---- */

static void (STDMETHODCALLTYPE *real_cbv)(ID3D12Device *, const D3D12_CONSTANT_BUFFER_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_srv)(ID3D12Device *, ID3D12Resource *, const D3D12_SHADER_RESOURCE_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_uav)(ID3D12Device *, ID3D12Resource *, ID3D12Resource *, const D3D12_UNORDERED_ACCESS_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_copy)(ID3D12Device *, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, UINT,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, D3D12_DESCRIPTOR_HEAP_TYPE);
static void (STDMETHODCALLTYPE *real_copy1)(ID3D12Device *, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
static HRESULT (STDMETHODCALLTYPE *real_heap)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_rootsig)(ID3D12Device *, UINT, const void *, SIZE_T, REFIID, void **);
typedef void (STDMETHODCALLTYPE *set_rs_fn)(ID3D12GraphicsCommandList *, ID3D12RootSignature *);
typedef void (STDMETHODCALLTYPE *set_table_fn)(ID3D12GraphicsCommandList *, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
static set_rs_fn real_crs, real_grs;
static set_table_fn real_ctab, real_gtab;
static HRESULT (STDMETHODCALLTYPE *real_list)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
                                              ID3D12PipelineState *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_list1)(ID3D12Device4 *, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void **);

typedef void (STDMETHODCALLTYPE *root_va_fn)(ID3D12GraphicsCommandList *, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
static root_va_fn real_ccbv, real_gcbv, real_csrv, real_gsrv, real_cuav, real_guav;
static void (STDMETHODCALLTYPE *real_ib)(ID3D12GraphicsCommandList *, const D3D12_INDEX_BUFFER_VIEW *);
static void (STDMETHODCALLTYPE *real_vb)(ID3D12GraphicsCommandList *, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW *);
static void (STDMETHODCALLTYPE *real_so)(ID3D12GraphicsCommandList *, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *);
static void (STDMETHODCALLTYPE *real_dispatch)(ID3D12GraphicsCommandList *, UINT, UINT, UINT);
static void (STDMETHODCALLTYPE *real_copytex)(ID3D12GraphicsCommandList *, const D3D12_TEXTURE_COPY_LOCATION *, UINT, UINT, UINT,
                                              const D3D12_TEXTURE_COPY_LOCATION *, const D3D12_BOX *);
static HRESULT (STDMETHODCALLTYPE *real_reset)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *, ID3D12PipelineState *);
static void (STDMETHODCALLTYPE *real_draw)(ID3D12GraphicsCommandList *, UINT, UINT, UINT, UINT);
static void (STDMETHODCALLTYPE *real_draw_indexed)(ID3D12GraphicsCommandList *, UINT, UINT, UINT, INT, UINT);
static void (STDMETHODCALLTYPE *real_exec_indirect)(ID3D12GraphicsCommandList *, ID3D12CommandSignature *, UINT, ID3D12Resource *,
                                                    UINT64, ID3D12Resource *, UINT64);
static void (STDMETHODCALLTYPE *real_clear_rtv)(ID3D12GraphicsCommandList *, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT *, UINT,
                                                const D3D12_RECT *);
static void (STDMETHODCALLTYPE *real_set_pso)(ID3D12GraphicsCommandList *, ID3D12PipelineState *);

static UINT res_inc;

static void label(char *out, void *p)
{
    uint8_t *a = p;
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)label, &self);
    if (g_fm6_game && a >= g_fm6_game && a < g_fm6_game + 0x8000000) wsprintfA(out, " game+%#x", (unsigned)(a - g_fm6_game));
    else if (a >= g_base && a < g_base + 0x1000000) wsprintfA(out, " azimuth+%#x", (unsigned)(a - g_base));
    else if (self && a >= (uint8_t *)self && a < (uint8_t *)self + 0x200000) wsprintfA(out, " winmm+%#x", (unsigned)(a - (uint8_t *)self));
    else out[0] = 0;
}

/* um relatorio por evento (limite GG_MAX_REPORTS): o que foi achado + a pilha nos modulos conhecidos */
static void report_to(volatile LONG *ctr, LONG max, const char *what, uint64_t v)
{
    void *frames[24];
    char buf[640], one[40];
    int len, n, i;
    LONG k = InterlockedIncrement(ctr);
    if (k > max) return;
    /* wsprintfA nao tem %llx */
    len = v >> 32 ? wsprintfA(buf, "gpuguard #%ld tid=%lu: %s 0x%lx%08lx; pilha:", k, GetCurrentThreadId(), what,
                              (unsigned long)(v >> 32), (unsigned long)v)
                  : wsprintfA(buf, "gpuguard #%ld tid=%lu: %s %#lx; pilha:", k, GetCurrentThreadId(), what, (unsigned long)v);
    n = RtlCaptureStackBackTrace(1, 24, frames, NULL);
    for (i = 0; i < n && len < 580; i++) {
        label(one, frames[i]);
        if (one[0]) len += wsprintfA(buf + len, "%s", one);
    }
    wlog("%s", buf);
}

static void report(const char *what, uint64_t v) { report_to(&reports, GG_MAX_REPORTS, what, v); }

/* pilha so com os quadros do jogo e do azimuth.exe */
static uint32_t known_frames(void **out, uint32_t max)
{
    void *frames[32];
    uint32_t n = RtlCaptureStackBackTrace(2, 32, frames, NULL), i, k = 0;
    for (i = 0; i < n && k < max; i++) {
        uint8_t *a = frames[i];
        if ((g_fm6_game && a >= g_fm6_game && a < g_fm6_game + 0x8000000) || (a >= g_base && a < g_base + 0x1000000))
            out[k++] = a;
    }
    return k;
}

/* grava no log os pontos de chamada guardados de uma lista (dred_watch) */
void gg_trace_dump(void *cl)
{
    gg_site s[GG_SITES];
    uint32_t dropped = 0, n = gg_trace_get(cl, s, GG_SITES, &dropped), i, j;
    for (i = 0; i < n; i++) {
        char buf[400], one[40];
        int len = wsprintfA(buf, "gpuguard: lista %p: %s x%u (1o %u,%u,%u) rs %p pso %p;", cl, gg_op_name[s[i].op],
                            s[i].count, s[i].arg[0], s[i].arg[1], s[i].arg[2], s[i].rs, s[i].pso);
        for (j = 0; j < s[i].nframes; j++) {
            label(one, s[i].frames[j]);
            len += wsprintfA(buf + len, "%s", one);
        }
        wlog("%s", buf);
    }
    if (dropped) wlog("gpuguard: lista %p: %u chamadas em pontos alem dos %d guardados", cl, dropped, GG_SITES);
}

static uint64_t guard_va(const char *what, uint64_t va)
{
    if (!va || !gg_va_poisoned(va)) return va;
    report(what, va);
    return 0;
}

static ID3D12Device *g_dev;

/* SRV nulo (buffer) no slot: leitura devolve 0 em vez de page fault */
static void write_null(uintptr_t h)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d = { 0 };
    d.Format = DXGI_FORMAT_R32_UINT;
    d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Buffer.NumElements = 1;
    real_srv(g_dev, NULL, &d, (D3D12_CPU_DESCRIPTOR_HANDLE){ h });
    gg_mark(h, 1);
    {
        gg_desc g = { 0 };
        g.kind = GG_DK_GUARD_NULL; g.dim = D3D12_SRV_DIMENSION_BUFFER; g.fmt = DXGI_FORMAT_R32_UINT; g.flags = GG_DF_NO_RESOURCE;
        gg_desc_set(h, &g);
    }
}

/* slot de origem nunca gravado -> relatorio + SRV nulo nele (copia segura) */
static void check_src(uintptr_t h)
{
    if (gg_written(h) != 0) return;
    report("CopyDescriptors: origem nunca gravada", h);
    write_null(h);
}

/* confere os slots da tabela no heap visivel pela GPU; nunca gravado -> relatorio (1o slot) + SRV nulo em cada */
static void check_table(ID3D12GraphicsCommandList *l, int compute, UINT param, D3D12_GPU_DESCRIPTOR_HANDLE g)
{
    uint32_t n = gg_rs_table(gg_cl_rs(l, compute, NULL, 0), param), i, bad = 0;
    uintptr_t cpu = n ? gg_gpu_to_cpu(g.ptr) : 0;
    if (!cpu) return;
    for (i = 0; i < n; i++) {
        uintptr_t h = cpu + (uintptr_t)i * res_inc;
        if (gg_written(h) != 0) continue;
        if (!bad++) {
            char what[112];
            wsprintfA(what, "%s param %u: slot %u de %u nunca gravado, handle de GPU",
                      compute ? "SetComputeRootDescriptorTable" : "SetGraphicsRootDescriptorTable", param, i, n);
            report(what, g.ptr + (uint64_t)i * res_inc);
        }
        write_null(h);
    }
}

/* resumo de uma root signature no log (D3D12Debug=3): parametros e tabelas, "U" = range sem limite */
static void log_rootsig(void *rs, const D3D12_ROOT_SIGNATURE_DESC1 *d)
{
    static volatile LONG shown;
    static const char *const kinds[] = { "SRV", "UAV", "CBV", "SMP" };
    char buf[900];
    int len;
    UINT i, j;
    if (InterlockedIncrement(&shown) > 120) return;
    len = wsprintfA(buf, "gpuguard: rs %p: %u params:", rs, d->NumParameters);
    for (i = 0; i < d->NumParameters && len < 820; i++) {
        const D3D12_ROOT_PARAMETER1 *p = &d->pParameters[i];
        if (p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            len += wsprintfA(buf + len, " %u=tab[", i);
            for (j = 0; j < p->DescriptorTable.NumDescriptorRanges && len < 820; j++) {
                const D3D12_DESCRIPTOR_RANGE1 *r = &p->DescriptorTable.pDescriptorRanges[j];
                if (r->NumDescriptors == UINT_MAX)
                    len += wsprintfA(buf + len, "%s%s U t%u s%u", j ? "," : "", kinds[r->RangeType & 3],
                                     r->BaseShaderRegister, r->RegisterSpace);
                else
                    len += wsprintfA(buf + len, "%s%s %u t%u s%u", j ? "," : "", kinds[r->RangeType & 3],
                                     r->NumDescriptors, r->BaseShaderRegister, r->RegisterSpace);
            }
            len += wsprintfA(buf + len, "]");
        } else if (p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
            len += wsprintfA(buf + len, " %u=k%u", i, p->Constants.Num32BitValues);
        } else {
            static const char *const views[] = { "cbv", "srv", "uav" };
            len += wsprintfA(buf + len, " %u=%s", i, views[(p->ParameterType - 2) % 3]);
        }
    }
    wlog("%s", buf);
}

static HRESULT STDMETHODCALLTYPE hook_rootsig(ID3D12Device *dev, UINT mask, const void *blob, SIZE_T len, REFIID riid, void **out)
{
    static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER deser;
    HRESULT hr = real_rootsig(dev, mask, blob, len, riid, out);
    ID3D12VersionedRootSignatureDeserializer *ds = NULL;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *vd;
    if (FAILED(hr) || !out || !*out) return hr;
    if (!deser) deser = (PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER)
        GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateVersionedRootSignatureDeserializer");
    if (deser && SUCCEEDED(deser(blob, len, &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&ds))) {
        if (SUCCEEDED(ds->lpVtbl->GetRootSignatureDescAtVersion(ds, D3D_ROOT_SIGNATURE_VERSION_1_1, &vd))) {
            uint32_t size[GG_MAX_PARAMS] = { 0 }, i, n = vd->Desc_1_1.NumParameters;
            uint64_t uav = 0;
            for (i = 0; i < n && i < GG_MAX_PARAMS; i++)
                if (vd->Desc_1_1.pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                    size[i] = gg_table_size(&vd->Desc_1_1.pParameters[i].DescriptorTable);
                    if (size[i] && size[i] <= GG_NULL_UAVS && gg_table_uav_only(&vd->Desc_1_1.pParameters[i].DescriptorTable))
                        uav |= 1ull << i;
                }
            gg_rs_add(*out, size, n);
            gg_rs_set_uav(*out, uav);
            if (gg_trace_on) log_rootsig(*out, &vd->Desc_1_1);
            if (gg_is_internal_rs(&vd->Desc_1_1)) internal_rs = *out;
        }
        ds->lpVtbl->Release(ds);
    }
    return hr;
}

/* ---- espelho do conteudo dos descritores (17_fm6.txt PASSO 28): o handle de CPU na AMD nao aponta para os bytes,
 * entao cada Create*View grava aqui o que foi criado e os CopyDescriptors* propagam; o dump do estado do draw mostra
 * o que cada tabela tem na hora do draw. ---- */

/* procura o slot (chamado com heap_lock) */
static gg_desc *desc_slot(uintptr_t h)
{
    int i;
    for (i = 0; i < nheaps; i++) {
        gg_heap *p = &heaps[i];
        if (h >= p->start && h < p->start + (uintptr_t)p->count * p->inc && p->desc)
            return &p->desc[(h - p->start) / p->inc];
    }
    return NULL;
}

void gg_desc_set(uintptr_t h, const gg_desc *d)
{
    gg_desc *s;
    AcquireSRWLockExclusive(&heap_lock);
    if ((s = desc_slot(h))) *s = *d;
    ReleaseSRWLockExclusive(&heap_lock);
}

int gg_desc_get(uintptr_t h, gg_desc *out)
{
    gg_desc *s;
    AcquireSRWLockShared(&heap_lock);
    if ((s = desc_slot(h))) *out = *s;
    ReleaseSRWLockShared(&heap_lock);
    return s != NULL;
}

/* CopyDescriptors: os ranges de destino e de origem andam juntos, descritor a descritor */
void gg_desc_copy(UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE *dst, const UINT *dsz, UINT ns,
                  const D3D12_CPU_DESCRIPTOR_HANDLE *src, const UINT *ssz, uint32_t inc)
{
    UINT di = 0, dj = 0, si = 0, sj = 0;
    AcquireSRWLockExclusive(&heap_lock);
    while (di < nd && si < ns) {
        UINT dn = dsz ? dsz[di] : 1, sn = ssz ? ssz[si] : 1;
        if (dj >= dn) { di++; dj = 0; continue; }
        if (sj >= sn) { si++; sj = 0; continue; }
        {
            gg_desc *d = desc_slot(dst[di].ptr + (uintptr_t)dj * inc), *s = desc_slot(src[si].ptr + (uintptr_t)sj * inc);
            if (d) { if (s) *d = *s; else memset(d, 0, sizeof *d); }
        }
        dj++; sj++;
    }
    ReleaseSRWLockExclusive(&heap_lock);
}

static void desc_from_resource(gg_desc *g, ID3D12Resource *r)
{
    D3D12_HEAP_PROPERTIES hp;
    D3D12_HEAP_FLAGS hf;
    if (!r) { g->flags |= GG_DF_NO_RESOURCE; return; }
    {
        D3D12_RESOURCE_DESC rd;
        r->lpVtbl->GetDesc(r, &rd);
        g->rdim = (uint8_t)rd.Dimension;
        g->w = (uint32_t)rd.Width; g->h = rd.Height;
        if (rd.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) g->va = r->lpVtbl->GetGPUVirtualAddress(r);
    }
    /* so com rastreio: em recurso reservado cada chamada vira um erro da debug layer (34 mil no log, PASSO 33) */
    if (gg_trace_on && FAILED(r->lpVtbl->GetHeapProperties(r, &hp, &hf))) g->flags |= GG_DF_RESERVED;
}

static const char *const gg_dk_name[] = { "vazio", "CBV", "SRV", "UAV", "nulo(trava)" };

/* uma linha por slot gravado no espelho */
static int desc_text(char *out, uint32_t i, const gg_desc *d)
{
    if (d->kind == GG_DK_NONE) return wsprintfA(out, " [%u]vazio", i);
    return wsprintfA(out, " [%u]%s d%u f%u res %p r%u %ux%u va 0x%lx%08lx%s%s", i, gg_dk_name[d->kind], d->dim, d->fmt,
                     d->res, d->rdim, d->w, d->h, (unsigned long)(d->va >> 32), (unsigned long)d->va,
                     d->flags & GG_DF_RESERVED ? " RESERVADO" : "", d->flags & GG_DF_NO_RESOURCE ? " sem-recurso" : "");
}

/* ---- estado grafico por lista para o dump: tabelas, root views, IB, VBs ---- */
#define GG_ROOT_PARAMS 32
#define GG_DUMP_VBS 8
typedef struct {
    void *cl;
    uint64_t tab[GG_ROOT_PARAMS], va[GG_ROOT_PARAMS];
    D3D12_INDEX_BUFFER_VIEW ib;
    D3D12_VERTEX_BUFFER_VIEW vb[GG_DUMP_VBS];
} gg_gfx;
static gg_gfx gfx_tab[GG_CL_SLOTS];
static SRWLOCK gfx_lock = SRWLOCK_INIT;

/* registro da lista (chamado com gfx_lock exclusivo) */
static gg_gfx *gfx_slot(void *cl)
{
    unsigned h = hptr(cl, GG_CL_SLOTS), k;
    for (k = 0; k < GG_CL_SLOTS; k++, h = (h + 1) & (GG_CL_SLOTS - 1)) {
        if (gfx_tab[h].cl == cl) return &gfx_tab[h];
        if (!gfx_tab[h].cl) { memset(&gfx_tab[h], 0, sizeof gfx_tab[h]); gfx_tab[h].cl = cl; return &gfx_tab[h]; }
    }
    return NULL;
}

/* what: 0 = tabela, 1 = root view, 2 = IB, 3 = VBs, 4 = Reset */
void gg_gfx_set(void *cl, int what, uint32_t i, uint64_t v, const void *views, uint32_t n)
{
    gg_gfx *g;
    AcquireSRWLockExclusive(&gfx_lock);
    if ((g = gfx_slot(cl))) {
        if (what == 0 && i < GG_ROOT_PARAMS) g->tab[i] = v;
        else if (what == 1 && i < GG_ROOT_PARAMS) g->va[i] = v;
        else if (what == 2) { if (views) g->ib = *(const D3D12_INDEX_BUFFER_VIEW *)views; else memset(&g->ib, 0, sizeof g->ib); }
        else if (what == 3) {
            uint32_t k;
            for (k = 0; k < n && i + k < GG_DUMP_VBS; k++)
                if (views) g->vb[i + k] = ((const D3D12_VERTEX_BUFFER_VIEW *)views)[k]; else memset(&g->vb[i + k], 0, sizeof g->vb[0]);
        } else if (what == 4) { memset(g, 0, sizeof *g); g->cl = cl; }
    }
    ReleaseSRWLockExclusive(&gfx_lock);
}

int gg_gfx_get(void *cl, gg_gfx *out)
{
    unsigned h = hptr(cl, GG_CL_SLOTS), k;
    int r = 0;
    AcquireSRWLockShared(&gfx_lock);
    for (k = 0; k < GG_CL_SLOTS && gfx_tab[h].cl; k++, h = (h + 1) & (GG_CL_SLOTS - 1))
        if (gfx_tab[h].cl == cl) { *out = gfx_tab[h]; r = 1; break; }
    ReleaseSRWLockShared(&gfx_lock);
    return r;
}

#define GG_DUMP_SLOTS 24

uint32_t gg_pso_info(void *pso);
void gg_pso_info_str(uint32_t info, char *out);

/* estado completo do draw no log (os primeiros DrawIndexed de cada tipo) */
static void dump_draw_state(ID3D12GraphicsCommandList *l, const char *what, const uint32_t *arg, uint32_t hash)
{
    static gg_gfx g;
    static SRWLOCK lock = SRWLOCK_INIT;
    void *rs = gg_cl_rs(l, 0, NULL, 0);
    char buf[2048];
    int len;
    uint32_t i, j;
    AcquireSRWLockExclusive(&lock);
    if (!gg_gfx_get(l, &g)) memset(&g, 0, sizeof g);
    wlog("gpuguard: estado %s (%u,%u,%u) hash %08lx lista %p rs %p; IB va 0x%lx%08lx %u bytes fmt %u", what, arg[0], arg[1],
         arg[2], (unsigned long)hash, (void *)l, rs, (unsigned long)(g.ib.BufferLocation >> 32), (unsigned long)g.ib.BufferLocation,
         g.ib.SizeInBytes, (unsigned)g.ib.Format);
    for (len = 0, i = 0; i < GG_DUMP_VBS; i++)
        if (g.vb[i].BufferLocation)
            len += wsprintfA(buf + len, " vb%u 0x%lx%08lx %u/%u", i, (unsigned long)(g.vb[i].BufferLocation >> 32),
                             (unsigned long)g.vb[i].BufferLocation, g.vb[i].SizeInBytes, g.vb[i].StrideInBytes);
    wlog("gpuguard:   VBs:%s", len ? buf : " nenhum");
    gg_pso_info_str(gg_pso_info(gg_cl_rs(l, GG_CL_PSO, NULL, 0)), buf);
    wlog("gpuguard:   PSO: %s", buf);
    for (i = 0; i < GG_ROOT_PARAMS; i++) {
        if (g.va[i])
            wlog("gpuguard:   p%u root view va 0x%lx%08lx", i, (unsigned long)(g.va[i] >> 32), (unsigned long)g.va[i]);
        if (g.tab[i]) {
            uint32_t n = gg_rs_table(rs, i);
            uintptr_t cpu = gg_gpu_to_cpu(g.tab[i]);
            len = wsprintfA(buf, "gpuguard:   p%u tabela gpu 0x%lx%08lx (%u slots):", i, (unsigned long)(g.tab[i] >> 32),
                            (unsigned long)g.tab[i], n);
            for (j = 0; cpu && j < n && j < GG_DUMP_SLOTS && len < 1800; j++) {
                gg_desc d;
                memset(&d, 0, sizeof d);
                gg_desc_get(cpu + (uintptr_t)j * res_inc, &d);
                len += desc_text(buf + len, j, &d);
            }
            wlog("%s", buf);
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

static void STDMETHODCALLTYPE hook_cbv(ID3D12Device *dev, const D3D12_CONSTANT_BUFFER_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    gg_desc g = { 0 };
    if (d && d->BufferLocation && !guard_va("CreateConstantBufferView", d->BufferLocation)) d = NULL;
    real_cbv(dev, d, h);
    gg_mark(h.ptr, 1);
    g.kind = GG_DK_CBV;
    if (d) { g.va = d->BufferLocation; g.w = d->SizeInBytes; } else g.flags = GG_DF_NO_RESOURCE;
    gg_desc_set(h.ptr, &g);
}

static void STDMETHODCALLTYPE hook_srv(ID3D12Device *dev, ID3D12Resource *r, const D3D12_SHADER_RESOURCE_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    gg_desc g = { 0 };
    real_srv(dev, r, d, h);
    gg_mark(h.ptr, 1);
    g.kind = GG_DK_SRV; g.res = r;
    if (d) { g.dim = (uint8_t)d->ViewDimension; g.fmt = (uint16_t)d->Format; }
    desc_from_resource(&g, r);
    if (d && d->ViewDimension == D3D12_SRV_DIMENSION_BUFFER) g.va += d->Buffer.FirstElement * (d->Buffer.StructureByteStride ? d->Buffer.StructureByteStride : 4);
    gg_desc_set(h.ptr, &g);
}

static void STDMETHODCALLTYPE hook_uav(ID3D12Device *dev, ID3D12Resource *r, ID3D12Resource *c, const D3D12_UNORDERED_ACCESS_VIEW_DESC *d,
                                       D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    gg_desc g = { 0 };
    real_uav(dev, r, c, d, h);
    gg_mark(h.ptr, 1);
    g.kind = GG_DK_UAV; g.res = r;
    if (d) { g.dim = (uint8_t)d->ViewDimension; g.fmt = (uint16_t)d->Format; }
    desc_from_resource(&g, r);
    gg_desc_set(h.ptr, &g);
}

static void STDMETHODCALLTYPE hook_copy1(ID3D12Device *dev, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        UINT i;
        for (i = 0; i < n; i++) check_src(src.ptr + (uintptr_t)i * res_inc);
        gg_mark(dst.ptr, n);
        gg_desc_copy(1, &dst, &n, 1, &src, &n, res_inc);
    }
    real_copy1(dev, n, dst, src, type);
}

static void STDMETHODCALLTYPE hook_copy(ID3D12Device *dev, UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE *dst, const UINT *dsz, UINT ns,
                                        const D3D12_CPU_DESCRIPTOR_HANDLE *src, const UINT *ssz, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        UINT i, j;
        for (i = 0; i < ns; i++)
            for (j = 0; j < (ssz ? ssz[i] : 1); j++) check_src(src[i].ptr + (uintptr_t)j * res_inc);
        for (i = 0; i < nd; i++) gg_mark(dst[i].ptr, dsz ? dsz[i] : 1);
        gg_desc_copy(nd, dst, dsz, ns, src, ssz, res_inc);
    }
    real_copy(dev, nd, dst, dsz, ns, src, ssz, type);
}

/* ---- UAVs nulos para tabelas de UAV nao definidas (17_fm6.txt PASSO 29) ----
 * O Azimuth dimensiona o heap visivel pela constante (nao le GetDesc): os GG_NULL_UAVS slots a mais no fim sao so nossos. */
static uint64_t null_uav_gpu;

int gg_null_uav_heap(const D3D12_DESCRIPTOR_HEAP_DESC *d)
{
    return d->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && (d->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
           d->NumDescriptors == GG_AZ_VISIBLE_HEAP;
}

static void null_uavs_fill(ID3D12Device *dev, uintptr_t cpu, uint64_t gpu)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC u;
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    uint32_t i;
    memset(&u, 0, sizeof u);
    u.Format = DXGI_FORMAT_R32_UINT;
    u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    for (i = 0; i < GG_NULL_UAVS; i++) {
        h.ptr = cpu + (uintptr_t)i * res_inc;
        dev->lpVtbl->CreateUnorderedAccessView(dev, NULL, NULL, &u, h);
    }
    null_uav_gpu = gpu;
    wlog("gpuguard: %u UAVs nulos no fim do heap visivel (gpu %#llx)", GG_NULL_UAVS, (unsigned long long)gpu);
}

void *gg_pso_rs(void *pso);

/* tabelas so de UAV da root signature grafica que a lista nao definiu (bit por parametro). So se o PSO atual foi
   criado com a root signature que a lista registrou: no boot com a debug layer a lista apontava uma root signature
   velha e o bind caia fora dos limites ("param 20, root signature com 1 parametro", PASSO 32) -> nada */
uint64_t gg_missing_uav_tables(void *cl)
{
    void *rs = gg_cl_rs(cl, 0, NULL, 0), *pso = gg_cl_rs(cl, GG_CL_PSO, NULL, 0), *prs;
    uint64_t need = gg_rs_uav(rs);
    if (!need) return 0;
    prs = gg_pso_rs(pso);
    if (prs != rs) {
        static volatile LONG odd;
        LONG k = InterlockedIncrement(&odd);
        if ((k & (k - 1)) == 0)
            wlog("gpuguard: root signature da lista %p != a do PSO %p (pso %p, lista %p, tid %lu) -> UAVs nulos nao ligados (#%ld)",
                 rs, prs, pso, cl, GetCurrentThreadId(), k);
        return 0;
    }
    return need & ~gg_cl_gtab(cl, 1, 0);
}

static void bind_null_uavs(ID3D12GraphicsCommandList *l)
{
    static volatile LONG bound;
    uint64_t miss = null_uav_gpu ? gg_missing_uav_tables(l) : 0;
    uint32_t i;
    for (i = 0; miss; i++, miss >>= 1) {
        D3D12_GPU_DESCRIPTOR_HANDLE g;
        LONG k;
        if (!(miss & 1)) continue;
        g.ptr = null_uav_gpu;
        if (gg_trace_on) gg_gfx_set(l, 0, i, g.ptr, NULL, 0);
        gg_cl_gtab(l, 1, 1ull << i);
        real_gtab(l, i, g);
        k = InterlockedIncrement(&bound);
        if ((k & (k - 1)) == 0) wlog("gpuguard: tabela de UAV %u nao definida no draw -> UAVs nulos (#%ld)", i, k);
    }
}

static HRESULT STDMETHODCALLTYPE hook_heap(ID3D12Device *dev, const D3D12_DESCRIPTOR_HEAP_DESC *d, REFIID riid, void **out)
{
    D3D12_DESCRIPTOR_HEAP_DESC big;
    int extra = d && gg_null_uav_heap(d);
    HRESULT hr;
    ID3D12DescriptorHeap *heap = NULL;
    if (extra) { big = *d; big.NumDescriptors += GG_NULL_UAVS; }
    hr = real_heap(dev, extra ? &big : d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && d->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        SUCCEEDED(((IUnknown *)*out)->lpVtbl->QueryInterface(*out, &IID_ID3D12DescriptorHeap, (void **)&heap))) {
        static volatile LONG shown;
        D3D12_CPU_DESCRIPTOR_HANDLE s;
        D3D12_GPU_DESCRIPTOR_HANDLE g = { 0 };
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &s);
        if (d->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap, &g);
        heap->lpVtbl->Release(heap);
        if (gg_trace_on) gg_heap_add(s.ptr, d->NumDescriptors, res_inc, g.ptr);   /* bitmap + espelho: so o rastreio le */
        if (extra) null_uavs_fill(dev, s.ptr + (uintptr_t)d->NumDescriptors * res_inc, g.ptr + (uint64_t)d->NumDescriptors * res_inc);
        if (InterlockedIncrement(&shown) <= 4)
            wlog("gpuguard: heap CBV/SRV/UAV cpu %p gpu %#llx x%u (flags %u)", (void *)s.ptr, (unsigned long long)g.ptr,
                 d->NumDescriptors, (unsigned)d->Flags);
    }
    return hr;
}

static void STDMETHODCALLTYPE hook_crs(ID3D12GraphicsCommandList *l, ID3D12RootSignature *rs) { gg_cl_rs(l, 1, rs, 1); real_crs(l, rs); }
static void STDMETHODCALLTYPE hook_grs(ID3D12GraphicsCommandList *l, ID3D12RootSignature *rs)
{
    gg_cl_rs(l, 0, rs, 1);
    gg_cl_gtab(l, 0, 0);
    real_grs(l, rs);
}
static void STDMETHODCALLTYPE hook_ctab(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE g) { check_table(l, 1, i, g); real_ctab(l, i, g); }
static void STDMETHODCALLTYPE hook_gtab(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE g)
{
    if (gg_trace_on) {
        check_table(l, 0, i, g);
        gg_gfx_set(l, 0, i, g.ptr, NULL, 0);
    }
    if (i < 64) gg_cl_gtab(l, 1, 1ull << i);
    real_gtab(l, i, g);
}

static void STDMETHODCALLTYPE hook_ccbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_ccbv(l, i, guard_va("SetComputeRootConstantBufferView", va)); }
static void STDMETHODCALLTYPE hook_gcbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    va = guard_va("SetGraphicsRootConstantBufferView", va);
    if (gg_trace_on) gg_gfx_set(l, 1, i, va, NULL, 0);
    real_gcbv(l, i, va);
}
static void STDMETHODCALLTYPE hook_csrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_csrv(l, i, guard_va("SetComputeRootShaderResourceView", va)); }
static void STDMETHODCALLTYPE hook_gsrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    va = guard_va("SetGraphicsRootShaderResourceView", va);
    if (gg_trace_on) gg_gfx_set(l, 1, i, va, NULL, 0);
    real_gsrv(l, i, va);
}
static void STDMETHODCALLTYPE hook_cuav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_cuav(l, i, guard_va("SetComputeRootUnorderedAccessView", va)); }
static void STDMETHODCALLTYPE hook_guav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    va = guard_va("SetGraphicsRootUnorderedAccessView", va);
    if (gg_trace_on) gg_gfx_set(l, 1, i, va, NULL, 0);
    real_guav(l, i, va);
}

/* ---- entrada do IA (17_fm6.txt PASSO 28): o page fault so some sem os DrawIndexedInstanced do replayer; o UMD da
 * AMD poe 0xdeadbeef.. como endereco de slot de vertex buffer sem buffer. Guarda, por PSO, os slots que o input
 * layout le e, por lista, os slots com buffer e se ha index buffer; o draw que le slot vazio vira relatorio. ---- */

#define GG_PSO_SLOTS 8192
typedef struct { void *pso, *rs; uint32_t slots, nelem, hash, info; } gg_pso;   /* hash: gg_shader_hash (estavel entre boots);
                                                                           rs: root signature da criacao */
#define GG_PSO_SO 0x20u
#define GG_PSO_EXTRA_STAGES (0x1cu | GG_PSO_SO)   /* DS, HS, GS ou stream output */
typedef struct { void *cl; uint32_t vb; int ib; } gg_ia;
static gg_pso pso_tab[GG_PSO_SLOTS];
static gg_ia ia_tab[GG_CL_SLOTS];
static SRWLOCK pso_lock = SRWLOCK_INIT, ia_lock = SRWLOCK_INIT;

/* mascara dos InputSlot lidos pelo input layout */
uint32_t gg_layout_slots(const D3D12_INPUT_LAYOUT_DESC *il)
{
    uint32_t m = 0, i;
    for (i = 0; il && il->pInputElementDescs && i < il->NumElements; i++)
        if (il->pInputElementDescs[i].InputSlot < 31) m |= 1u << il->pInputElementDescs[i].InputSlot;
    return m;
}

/* subobjeto `type` de um stream de pipeline (NULL se nao tem ou se ha subobjeto desconhecido antes dele) */
const void *gg_stream_find(const void *stream, SIZE_T size, uint32_t type)
{
#define GG_SUB(t) { sizeof(t), _Alignof(t) }
    static const struct { uint32_t size, align; } sub[] = {
        GG_SUB(void *), GG_SUB(D3D12_SHADER_BYTECODE), GG_SUB(D3D12_SHADER_BYTECODE), GG_SUB(D3D12_SHADER_BYTECODE),
        GG_SUB(D3D12_SHADER_BYTECODE), GG_SUB(D3D12_SHADER_BYTECODE), GG_SUB(D3D12_SHADER_BYTECODE),
        GG_SUB(D3D12_STREAM_OUTPUT_DESC), GG_SUB(D3D12_BLEND_DESC), GG_SUB(UINT), GG_SUB(D3D12_RASTERIZER_DESC),
        GG_SUB(D3D12_DEPTH_STENCIL_DESC), GG_SUB(D3D12_INPUT_LAYOUT_DESC), GG_SUB(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE),
        GG_SUB(D3D12_PRIMITIVE_TOPOLOGY_TYPE), GG_SUB(struct D3D12_RT_FORMAT_ARRAY), GG_SUB(DXGI_FORMAT), GG_SUB(DXGI_SAMPLE_DESC),
        GG_SUB(UINT), GG_SUB(D3D12_CACHED_PIPELINE_STATE), GG_SUB(D3D12_PIPELINE_STATE_FLAGS),
        GG_SUB(D3D12_DEPTH_STENCIL_DESC1), GG_SUB(D3D12_VIEW_INSTANCING_DESC), { 0, 0 },
        GG_SUB(D3D12_SHADER_BYTECODE), GG_SUB(D3D12_SHADER_BYTECODE),
    };
#undef GG_SUB
    const uint8_t *p = stream, *end = p + size;
    while (p && p + 4 <= end) {
        uint32_t t = *(const uint32_t *)p, off;
        if (t >= sizeof sub / sizeof *sub || !sub[t].size) return NULL;
        off = (4 + sub[t].align - 1) & ~(sub[t].align - 1);
        if (t == type) return p + off;
        p += (off + sub[t].size + 7) & ~7u;
    }
    return NULL;
}

const D3D12_INPUT_LAYOUT_DESC *gg_stream_layout(const void *stream, SIZE_T size)
{
    return gg_stream_find(stream, size, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT);
}

/* identidade do PSO entre boots: checksum do container DXBC/DXIL (bytes 4..19) de VS, PS, DS, HS e GS */
uint32_t gg_shader_hash(const D3D12_SHADER_BYTECODE *const bc[5])
{
    uint32_t h = 2166136261u, i, j;
    for (i = 0; i < 5; i++) {
        const uint8_t *b = bc[i] ? bc[i]->pShaderBytecode : NULL;
        h = (h ^ i) * 16777619u;
        if (!b || bc[i]->BytecodeLength < 20) continue;
        for (j = 4; j < 20; j++) h = (h ^ b[j]) * 16777619u;
    }
    return h;
}

/* resumo do PSO: bits 0..4 = VS PS DS HS GS presentes, bit 5 = stream output, bits 8..15 = tipo de topologia */
uint32_t gg_pso_info_of(const D3D12_SHADER_BYTECODE *const bc[5], const D3D12_STREAM_OUTPUT_DESC *so, uint32_t topo)
{
    uint32_t r = (topo & 0xff) << 8, i;
    for (i = 0; i < 5; i++)
        if (bc[i] && bc[i]->pShaderBytecode && bc[i]->BytecodeLength) r |= 1u << i;
    if (so && so->NumEntries) r |= GG_PSO_SO;
    return r;
}

static uint32_t desc_hash(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, uint32_t *info)
{
    const D3D12_SHADER_BYTECODE *bc[5] = { &d->VS, &d->PS, &d->DS, &d->HS, &d->GS };
    *info = gg_pso_info_of(bc, &d->StreamOutput, d->PrimitiveTopologyType);
    return gg_shader_hash(bc);
}

static uint32_t stream_hash(const D3D12_PIPELINE_STATE_STREAM_DESC *d, uint32_t *info)
{
    const void *st = d->pPipelineStateSubobjectStream;
    const D3D12_SHADER_BYTECODE *bc[5];
    const D3D12_PRIMITIVE_TOPOLOGY_TYPE *topo = gg_stream_find(st, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY);
    uint32_t i;
    for (i = 0; i < 5; i++)
        bc[i] = gg_stream_find(st, d->SizeInBytes, i + 1);   /* VS PS DS HS GS = 1..5 */
    *info = gg_pso_info_of(bc, gg_stream_find(st, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT),
                           topo ? (uint32_t)*topo : 0);
    return gg_shader_hash(bc);
}

void gg_pso_add(void *pso, const D3D12_INPUT_LAYOUT_DESC *il, uint32_t hash, uint32_t info, void *rs)
{
    unsigned h = hptr(pso, GG_PSO_SLOTS), k;
    AcquireSRWLockExclusive(&pso_lock);
    for (k = 0; k < GG_PSO_SLOTS; k++, h = (h + 1) & (GG_PSO_SLOTS - 1))
        if (!pso_tab[h].pso || pso_tab[h].pso == pso) {
            pso_tab[h].pso = pso;
            pso_tab[h].slots = gg_layout_slots(il);
            pso_tab[h].nelem = il ? il->NumElements : 0;
            pso_tab[h].hash = hash;
            pso_tab[h].info = info;
            pso_tab[h].rs = rs;
            break;
        }
    ReleaseSRWLockExclusive(&pso_lock);
}

/* hash dos shaders do PSO (0 = desconhecido) */
uint32_t gg_pso_hash(void *pso)
{
    unsigned h = hptr(pso, GG_PSO_SLOTS), k;
    uint32_t r = 0;
    if (!pso) return 0;
    AcquireSRWLockShared(&pso_lock);
    for (k = 0; k < GG_PSO_SLOTS && pso_tab[h].pso; k++, h = (h + 1) & (GG_PSO_SLOTS - 1))
        if (pso_tab[h].pso == pso) { r = pso_tab[h].hash; break; }
    ReleaseSRWLockShared(&pso_lock);
    return r;
}

/* gg_pso_info_of do PSO (0 = desconhecido) */
uint32_t gg_pso_info(void *pso)
{
    unsigned h = hptr(pso, GG_PSO_SLOTS), k;
    uint32_t r = 0;
    if (!pso) return 0;
    AcquireSRWLockShared(&pso_lock);
    for (k = 0; k < GG_PSO_SLOTS && pso_tab[h].pso; k++, h = (h + 1) & (GG_PSO_SLOTS - 1))
        if (pso_tab[h].pso == pso) { r = pso_tab[h].info; break; }
    ReleaseSRWLockShared(&pso_lock);
    return r;
}

/* root signature com que o PSO foi criado (NULL = desconhecido ou embutida no shader) */
void *gg_pso_rs(void *pso)
{
    unsigned h = hptr(pso, GG_PSO_SLOTS), k;
    void *r = NULL;
    if (!pso) return NULL;
    AcquireSRWLockShared(&pso_lock);
    for (k = 0; k < GG_PSO_SLOTS && pso_tab[h].pso; k++, h = (h + 1) & (GG_PSO_SLOTS - 1))
        if (pso_tab[h].pso == pso) { r = pso_tab[h].rs; break; }
    ReleaseSRWLockShared(&pso_lock);
    return r;
}

/* root signature do subobjeto ROOT_SIGNATURE do stream (NULL se nao tem) */
static void *gg_stream_rs(const D3D12_PIPELINE_STATE_STREAM_DESC *d)
{
    void *const *rs = gg_stream_find(d->pPipelineStateSubobjectStream, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE);
    return rs ? *rs : NULL;
}

/* "VS PS DS HS GS SO topo N" (out com 40 bytes) */
void gg_pso_info_str(uint32_t info, char *out)
{
    static const char *const name[6] = { "VS", "PS", "DS", "HS", "GS", "SO" };
    int len = 0, i;
    for (i = 0; i < 6; i++)
        if (info & (1u << i)) len += wsprintfA(out + len, "%s ", name[i]);
    wsprintfA(out + len, "topo %u", (unsigned)(info >> 8 & 0xff));
}

/* slots lidos pelo PSO; -1 = PSO desconhecido */
int64_t gg_pso_slots(void *pso)
{
    unsigned h = hptr(pso, GG_PSO_SLOTS), k;
    int64_t r = -1;
    if (!pso) return -1;
    AcquireSRWLockShared(&pso_lock);
    for (k = 0; k < GG_PSO_SLOTS && pso_tab[h].pso; k++, h = (h + 1) & (GG_PSO_SLOTS - 1))
        if (pso_tab[h].pso == pso) { r = pso_tab[h].slots; break; }
    ReleaseSRWLockShared(&pso_lock);
    return r;
}

/* estado do IA da lista: what 0 = consulta, 1 = vertex buffers [start, start+n) de v, 2 = index buffer (ib), 3 = Reset */
gg_ia gg_ia_state(void *cl, int what, uint32_t start, uint32_t n, const D3D12_VERTEX_BUFFER_VIEW *v, int ib)
{
    unsigned h = hptr(cl, GG_CL_SLOTS), k;
    gg_ia r = { cl, 0, 0 };
    if (what) AcquireSRWLockExclusive(&ia_lock); else AcquireSRWLockShared(&ia_lock);
    for (k = 0; k < GG_CL_SLOTS; k++, h = (h + 1) & (GG_CL_SLOTS - 1)) {
        gg_ia *e = &ia_tab[h];
        if (e->cl && e->cl != cl) continue;
        if (!e->cl) { if (!what) break; e->cl = cl; e->vb = 0; e->ib = 0; }
        if (what == 1) {
            uint32_t i;
            for (i = 0; i < n && start + i < 31; i++)
                if (v && v[i].BufferLocation) e->vb |= 1u << (start + i); else e->vb &= ~(1u << (start + i));
        } else if (what == 2) e->ib = ib;
        else if (what == 3) { e->vb = 0; e->ib = 0; }
        r = *e;
        break;
    }
    if (what) ReleaseSRWLockExclusive(&ia_lock); else ReleaseSRWLockShared(&ia_lock);
    return r;
}

/* slots que o draw le sem buffer (bit 31 = draw indexado sem index buffer); 0 = ok ou PSO desconhecido */
uint32_t gg_ia_missing(void *cl, void *pso, int indexed)
{
    int64_t need = gg_pso_slots(pso);
    gg_ia st;
    uint32_t miss;
    if (need < 0) return 0;
    st = gg_ia_state(cl, 0, 0, 0, NULL, 0);
    miss = (uint32_t)need & ~st.vb & 0x7fffffffu;
    if (indexed && !st.ib) miss |= 0x80000000u;
    return miss;
}

static volatile LONG ia_reports;

/* confere o draw; devolve a mascara do que falta (relatorio com pilha nos primeiros) */
static uint32_t check_ia(ID3D12GraphicsCommandList *l, uint32_t op, const uint32_t *arg)
{
    void *pso = gg_cl_rs(l, GG_CL_PSO, NULL, 0);
    uint32_t miss = gg_ia_missing(l, pso, op == GG_OP_DRAW_INDEXED);
    if (pso && gg_pso_slots(pso) < 0) {
        static volatile LONG unknown;
        LONG k = InterlockedIncrement(&unknown);
        if ((k & (k - 1)) == 0) wlog("gpuguard: draw com PSO de origem desconhecida %p (#%ld)", pso, k);
    }
    if (miss) {
        char what[200];
        wsprintfA(what, "%s (%u,%u,%u) le slots sem vertex buffer %#lx%s; pso %p lista %p, mascara", gg_op_name[op],
                  arg[0], arg[1], arg[2], (unsigned long)(miss & 0x7fffffffu), miss >> 31 ? " e sem index buffer" : "",
                  pso, (void *)l);
        report_to(&ia_reports, GG_MAX_REPORTS, what, miss);
    }
    return miss;
}

static HRESULT (STDMETHODCALLTYPE *real_gpso)(ID3D12Device *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_spso)(ID3D12Device2 *, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **);

static HRESULT STDMETHODCALLTYPE hook_gpso(ID3D12Device *dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d, REFIID riid, void **out)
{
    HRESULT hr = real_gpso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d) {
        uint32_t info, h = desc_hash(d, &info);
        gg_pso_add(*out, &d->InputLayout, h, info, d->pRootSignature);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_spso(ID3D12Device2 *dev, const D3D12_PIPELINE_STATE_STREAM_DESC *d, REFIID riid, void **out)
{
    HRESULT hr = real_spso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d) {
        uint32_t info, h = stream_hash(d, &info);
        gg_pso_add(*out, gg_stream_layout(d->pPipelineStateSubobjectStream, d->SizeInBytes), h, info, gg_stream_rs(d));
    }
    return hr;
}

/* o Azimuth carrega os PSOs da pipeline library (cache azimuth.pipelines): mesmos descritores, outro caminho */
static void swap(void **slot, void *hook, void **orig);
static HRESULT (STDMETHODCALLTYPE *real_lib)(ID3D12Device1 *, const void *, SIZE_T, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib_gfx)(ID3D12PipelineLibrary *, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *,
                                                 REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib_stream)(ID3D12PipelineLibrary1 *, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC *,
                                                    REFIID, void **);

/* EXPERIMENTO D3D12Debug=10 (17_fm6.txt PASSO 29): todos os draws do replayer que dao page fault usam PSO com GS
   vindo da library; aqui esses PSOs sao compilados do zero (CreateGraphicsPipelineState/CreatePipelineState) */
static int fresh_gs_pso(uint32_t info)
{
    static volatile LONG made;
    LONG k;
    if (gg_trace_on != 8 || !(info & 0x10)) return 0;
    k = InterlockedIncrement(&made);
    if ((k & (k - 1)) == 0) wlog("gpuguard: experimento: PSO com GS #%ld criado fora da pipeline library", k);
    return 1;
}

/* com o rastreio: VS e GS dos primeiros PSOs com GS em fm6_gs_dump\<hash>_{vs,gs}.dxbc (disassembly offline) */
static void dump_gs(uint32_t hash, uint32_t info, const D3D12_SHADER_BYTECODE *vs, const D3D12_SHADER_BYTECODE *gs)
{
    static volatile LONG n;
    const D3D12_SHADER_BYTECODE *bc[2] = { vs, gs };
    static const char *const tag[2] = { "vs", "gs" };
    int i;
    if (!gg_trace_on || !(info & 0x10) || InterlockedIncrement(&n) > 64) return;
    CreateDirectoryA("fm6_gs_dump", NULL);
    for (i = 0; i < 2; i++) {
        char path[64];
        HANDLE f;
        DWORD wr;
        if (!bc[i] || !bc[i]->pShaderBytecode) continue;
        wsprintfA(path, "fm6_gs_dump\\%08lx_%s.dxbc", (unsigned long)hash, tag[i]);
        f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        WriteFile(f, bc[i]->pShaderBytecode, (DWORD)bc[i]->BytecodeLength, &wr, NULL);
        CloseHandle(f);
    }
}

static HRESULT STDMETHODCALLTYPE hook_lib_gfx(ID3D12PipelineLibrary *lib, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d,
                                              REFIID riid, void **out)
{
    uint32_t info = 0, h = d ? desc_hash(d, &info) : 0;
    HRESULT hr;
    if (d) dump_gs(h, info, &d->VS, &d->GS);
    ID3D12Device *dev = NULL;
    if (d && fresh_gs_pso(info) && SUCCEEDED(lib->lpVtbl->GetDevice(lib, &IID_ID3D12Device, (void **)&dev))) {
        hr = dev->lpVtbl->CreateGraphicsPipelineState(dev, d, riid, out);
        dev->lpVtbl->Release(dev);
    } else {
        hr = real_lib_gfx(lib, name, d, riid, out);
    }
    if (SUCCEEDED(hr) && out && *out && d) gg_pso_add(*out, &d->InputLayout, h, info, d->pRootSignature);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_lib_stream(ID3D12PipelineLibrary1 *lib, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *d,
                                                 REFIID riid, void **out)
{
    uint32_t info = 0, h = d ? stream_hash(d, &info) : 0;
    HRESULT hr;
    ID3D12Device2 *dev = NULL;
    if (d) dump_gs(h, info, gg_stream_find(d->pPipelineStateSubobjectStream, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS),
                   gg_stream_find(d->pPipelineStateSubobjectStream, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS));
    if (d && fresh_gs_pso(info) && SUCCEEDED(lib->lpVtbl->GetDevice(lib, &IID_ID3D12Device2, (void **)&dev))) {
        hr = dev->lpVtbl->CreatePipelineState(dev, d, riid, out);
        dev->lpVtbl->Release(dev);
    } else {
        hr = real_lib_stream(lib, name, d, riid, out);
    }
    if (SUCCEEDED(hr) && out && *out && d)
        gg_pso_add(*out, gg_stream_layout(d->pPipelineStateSubobjectStream, d->SizeInBytes), h, info, gg_stream_rs(d));
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_lib(ID3D12Device1 *dev, const void *blob, SIZE_T len, REFIID riid, void **out)
{
    HRESULT hr = real_lib(dev, blob, len, riid, out);
    static volatile LONG done;
    if (SUCCEEDED(hr) && out && *out && !InterlockedCompareExchange(&done, 1, 0)) {
        ID3D12PipelineLibrary *l0 = NULL;
        ID3D12PipelineLibrary1 *l1 = NULL;
        IUnknown *u = *out;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12PipelineLibrary, (void **)&l0))) {
            swap((void **)&l0->lpVtbl->LoadGraphicsPipeline, hook_lib_gfx, (void **)&real_lib_gfx);
            l0->lpVtbl->Release(l0);
        }
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12PipelineLibrary1, (void **)&l1))) {
            swap((void **)&l1->lpVtbl->LoadPipeline, hook_lib_stream, (void **)&real_lib_stream);
            l1->lpVtbl->Release(l1);
        }
        wlog("gpuguard: pipeline library trocada (LoadGraphicsPipeline%s)", l1 ? ", LoadPipeline" : "");
    }
    return hr;
}

static void STDMETHODCALLTYPE hook_ib(ID3D12GraphicsCommandList *l, const D3D12_INDEX_BUFFER_VIEW *v)
{
    D3D12_INDEX_BUFFER_VIEW c;
    if (v && v->BufferLocation && gg_va_poisoned(v->BufferLocation)) {
        report("IASetIndexBuffer", v->BufferLocation);
        c = *v; c.BufferLocation = 0; c.SizeInBytes = 0; v = &c;
    }
    if (gg_trace_on) {               /* so o check_ia/dump do rastreio leem */
        gg_ia_state(l, 2, 0, 0, NULL, v && v->BufferLocation);
        gg_gfx_set(l, 2, 0, 0, v, 0);
    }
    real_ib(l, v);
}

static void STDMETHODCALLTYPE hook_vb(ID3D12GraphicsCommandList *l, UINT start, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v)
{
    D3D12_VERTEX_BUFFER_VIEW c[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT i;
    for (i = 0; v && i < n && i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        if (!v[i].BufferLocation || !gg_va_poisoned(v[i].BufferLocation)) continue;
        report("IASetVertexBuffers", v[i].BufferLocation);
        if (n > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) n = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        memcpy(c, v, n * sizeof *v);
        for (i = 0; i < n; i++) if (gg_va_poisoned(c[i].BufferLocation)) { c[i].BufferLocation = 0; c[i].SizeInBytes = 0; }
        v = c;
        break;
    }
    if (gg_trace_on) {
        gg_ia_state(l, 1, start, n, v, 0);
        gg_gfx_set(l, 3, start, 0, v, n);
    }
    real_vb(l, start, n, v);
}

static void STDMETHODCALLTYPE hook_so(ID3D12GraphicsCommandList *l, UINT start, UINT n, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *v)
{
    UINT i;
    for (i = 0; v && i < n; i++)
        if (gg_va_poisoned(v[i].BufferLocation) || gg_va_poisoned(v[i].BufferFilledSizeLocation)) {
            report("SOSetTargets", v[i].BufferLocation);
            v = NULL; n = 0;   /* desliga o stream out em vez de gravar no endereco envenenado */
            break;
        }
    real_so(l, start, n, v);
}

/* 1o uso de cada ponto de chamada no processo (a lista do page fault pode ja ter sido solta quando o DRED a mostra:
   pCommandList nulo no no) */
static void log_new_site(void *cl, uint32_t op, void *rs, void *pso, const uint32_t *arg, void *const *f, uint32_t n)
{
    static uint32_t seen[1024];
    static volatile LONG nseen;
    static SRWLOCK lock = SRWLOCK_INIT;
    uint32_t hash = 2166136261u ^ op, i;
    char buf[400], one[40];
    int len;
    for (i = 0; i < n; i++) hash = (hash ^ (uint32_t)(uintptr_t)f[i]) * 16777619u;
    hash = (hash ^ (uint32_t)((uintptr_t)rs >> 4)) * 16777619u;
    AcquireSRWLockExclusive(&lock);
    for (i = 0; i < (uint32_t)nseen && seen[i] != hash; i++) {}
    if (i < (uint32_t)nseen || nseen >= 1024) { ReleaseSRWLockExclusive(&lock); return; }
    seen[nseen++] = hash;
    ReleaseSRWLockExclusive(&lock);
    len = wsprintfA(buf, "gpuguard: ponto novo de %s (%u,%u,%u) rs %p pso %p lista %p tid=%lu;", gg_op_name[op],
                    arg[0], arg[1], arg[2], rs, pso, cl, GetCurrentThreadId());
    for (i = 0; i < n; i++) {
        label(one, f[i]);
        len += wsprintfA(buf + len, "%s", one);
    }
    wlog("%s", buf);
}

/* ---- anel dos ultimos draws/ClearRTV do processo (a lista do page fault ja foi solta quando o DRED a mostra) ---- */
#define GG_RING 128
typedef struct { void *cl, *rs, *pso; uint32_t seq, op, direct, arg[3], hash; } gg_ring_ent;
static gg_ring_ent ring[GG_RING];
static volatile LONG ring_seq;

void gg_ring_add(void *cl, uint32_t op, int direct, void *rs, void *pso, const uint32_t *arg)
{
    LONG k = InterlockedIncrement(&ring_seq);
    gg_ring_ent *e = &ring[(uint32_t)k % GG_RING];
    e->cl = cl; e->rs = rs; e->pso = pso; e->op = op; e->direct = direct; e->hash = gg_pso_hash(pso);
    memcpy(e->arg, arg, sizeof e->arg);
    e->seq = (uint32_t)k;
}

/* copia em ordem (mais antigo primeiro); devolve quantos */
uint32_t gg_ring_get(gg_ring_ent *out, uint32_t max)
{
    uint32_t last = (uint32_t)ring_seq, n = 0, k;
    uint32_t first = last >= GG_RING ? last - GG_RING + 1 : 1;
    if (max < GG_RING && last >= max) first = last - max + 1 > first ? last - max + 1 : first;
    for (k = first; k && k <= last && n < max; k++)
        if (ring[k % GG_RING].seq == k) out[n++] = ring[k % GG_RING];
    return n;
}

/* grava no log os ultimos draws (dred_watch, depois das listas) */
void gg_ring_dump(void)
{
    static gg_ring_ent e[GG_RING];
    uint32_t n = gg_ring_get(e, GG_RING), i;
    wlog("gpuguard: ultimos %u draws/ClearRTV (mais antigo primeiro):", n);
    for (i = 0; i < n; i++) {
        char st[40];
        gg_pso_info_str(gg_pso_info(e[i].pso), st);
        wlog("gpuguard:  #%u lista %p %s %s (%u,%u,%u) rs %p pso %p hash %08lx (%s)", e[i].seq, e[i].cl, gg_op_name[e[i].op],
             e[i].direct ? "direto" : "replayer", e[i].arg[0], e[i].arg[1], e[i].arg[2], e[i].rs, e[i].pso,
             (unsigned long)e[i].hash, st);
    }
}

/* registra a operacao na lista (rastreio + 1o uso do ponto); devolve a root signature usada e, em `direct`, se a
   pilha passa pelo jogo (draw gravado na thread do jogo; os do replayer do Azimuth vem da thread da fila) */
static void *trace_op(ID3D12GraphicsCommandList *l, uint32_t op, int compute, const uint32_t *arg, int *direct)
{
    void *f[GG_SITE_FRAMES], *rs = op == GG_OP_COPY_TEXTURE ? NULL : gg_cl_rs(l, compute, NULL, 0);
    void *pso = op == GG_OP_COPY_TEXTURE ? NULL : gg_cl_rs(l, GG_CL_PSO, NULL, 0);
    uint32_t n = known_frames(f, GG_SITE_FRAMES), i;
    gg_trace_add(l, op, rs, pso, arg, f, n);
    log_new_site(l, op, rs, pso, arg, f, n);
    if (direct)
        for (*direct = 0, i = 0; i < n; i++)
            if (g_fm6_game && (uint8_t *)f[i] >= g_fm6_game && (uint8_t *)f[i] < g_fm6_game + 0x8000000) *direct = 1;
    if (op >= GG_OP_DRAW) gg_ring_add(l, op, direct ? *direct : 0, rs, pso, arg);
    return rs;
}

/* experimento: a operacao nao chega ao D3D12 (log nas potencias de 2) */
static void skip_note(volatile LONG *ctr, const char *what)
{
    LONG k = InterlockedIncrement(ctr);
    if ((k & (k - 1)) == 0) wlog("gpuguard: experimento: %s #%ld pulado", what, k);
}

static void STDMETHODCALLTYPE hook_dispatch(ID3D12GraphicsCommandList *l, UINT x, UINT y, UINT z)
{
    uint32_t arg[3] = { x, y, z };
    void *rs = trace_op(l, GG_OP_DISPATCH, 1, arg, NULL);
    if (gg_trace_on == 2 && rs && rs == internal_rs) {
        static volatile LONG skipped;
        LONG k = InterlockedIncrement(&skipped);
        if ((k & (k - 1)) == 0) wlog("gpuguard: experimento: Dispatch #%ld da compute interna pulado (%u,%u,%u)", k, x, y, z);
        return;
    }
    if (gg_trace_on == 7 && rs != internal_rs) {   /* EXPERIMENTO D3D12Debug=9 (17_fm6.txt PASSO 28) */
        static volatile LONG skipped;
        skip_note(&skipped, "Dispatch fora da compute interna");
        return;
    }
    real_dispatch(l, x, y, z);
}

/* draws: EXPERIMENTO D3D12Debug=8 (17_fm6.txt PASSO 28) = nao chegam a GPU; DrawSkip no .ini escolhe quais:
   0 = todos, 1 = so os diretos (pilha no jogo), 2 = so os do replayer; 3/4/5 = so ExecuteIndirect/DrawInstanced/
   DrawIndexedInstanced do replayer; 6 = so os que leem slot do IA sem buffer (check_ia); 7 = so os
   DrawIndexedInstanced do replayer cujo hash de PSO esta em DrawSkipHashes (hex, separados por virgula/espaco);
   8 = so os draws (qualquer tipo/origem) com PSO de DS/HS/GS ou stream output (gg_skip_stages) */
static volatile LONG draws_skipped[2];   /* replayer, direto */
int gg_draw_skip;
#define GG_SKIP_HASHES 64
uint32_t gg_skip_hash[GG_SKIP_HASHES], gg_nskip_hash;

/* DrawSkipHashes do .ini -> gg_skip_hash */
void gg_parse_hashes(const char *str)
{
    gg_nskip_hash = 0;
    while (str && *str && gg_nskip_hash < GG_SKIP_HASHES) {
        char *end;
        unsigned long v = strtoul(str, &end, 16);
        if (end == str) { str++; continue; }
        gg_skip_hash[gg_nskip_hash++] = (uint32_t)v;
        str = end;
    }
}

static int hash_listed(uint32_t h)
{
    uint32_t i;
    for (i = 0; i < gg_nskip_hash; i++) if (gg_skip_hash[i] == h) return 1;
    return 0;
}

int gg_skip_draw(int direct, uint32_t op, uint32_t ia_miss, uint32_t pso_hash)
{
    static const uint32_t only[3] = { GG_OP_EXECUTE_INDIRECT, GG_OP_DRAW, GG_OP_DRAW_INDEXED };
    if (gg_trace_on != 6) return 0;
    if (gg_draw_skip == 6) {
        static volatile LONG skipped;
        if (!ia_miss) return 0;
        skip_note(&skipped, "draw com IA incompleto");
        return 1;
    }
    if (gg_draw_skip == 8) return 0;   /* gg_skip_stages */
    if (gg_draw_skip == 7) {
        static volatile LONG skipped;
        if (direct || op != GG_OP_DRAW_INDEXED || !hash_listed(pso_hash)) return 0;
        skip_note(&skipped, "DrawIndexedInstanced do replayer da lista de hashes");
        return 1;
    }
    if ((gg_draw_skip == 1 && !direct) || (gg_draw_skip >= 2 && direct)) return 0;
    if (gg_draw_skip >= 3 && gg_draw_skip <= 5 && op != only[gg_draw_skip - 3]) return 0;
    skip_note(&draws_skipped[direct != 0], direct ? "draw direto" : "draw do replayer");
    return 1;
}

/* DrawSkip=8: draw cujo PSO tem DS/HS/GS ou stream output (rings internos do driver) */
int gg_skip_stages(uint32_t info)
{
    static volatile LONG skipped;
    if (gg_trace_on != 6 || gg_draw_skip != 8 || !(info & GG_PSO_EXTRA_STAGES)) return 0;
    skip_note(&skipped, "draw com DS/HS/GS/SO");
    return 1;
}

/* 1o uso de cada hash de PSO nos DrawIndexedInstanced do replayer (lista para a bisseccao com DrawSkip=7) */
static void log_pso_hash(uint32_t h, const uint32_t *arg, int bv, uint32_t info)
{
    static uint32_t seen[512];
    static volatile LONG nseen;
    static SRWLOCK lock = SRWLOCK_INIT;
    uint32_t i;
    AcquireSRWLockExclusive(&lock);
    for (i = 0; i < (uint32_t)nseen && seen[i] != h; i++) {}
    if (i < (uint32_t)nseen || nseen >= 512) { ReleaseSRWLockExclusive(&lock); return; }
    seen[nseen++] = h;
    ReleaseSRWLockExclusive(&lock);
    {
        char st[40];
        gg_pso_info_str(info, st);
        wlog("gpuguard: hash de PSO novo no DrawIndexedInstanced do replayer: %08lx (#%ld) (%u,%u,%u) base %d; %s",
             (unsigned long)h, (long)nseen, arg[0], arg[1], arg[2], bv, st);
    }
}

static void STDMETHODCALLTYPE hook_draw(ID3D12GraphicsCommandList *l, UINT vc, UINT ic, UINT sv, UINT si)
{
    uint32_t arg[3] = { vc, ic, sv };
    int direct;
    trace_op(l, GG_OP_DRAW, 0, arg, &direct);
    if (gg_skip_draw(direct, GG_OP_DRAW, check_ia(l, GG_OP_DRAW, arg), 0)) return;
    if (gg_skip_stages(gg_pso_info(gg_cl_rs(l, GG_CL_PSO, NULL, 0)))) return;
    bind_null_uavs(l);
    real_draw(l, vc, ic, sv, si);
}

static void STDMETHODCALLTYPE hook_draw_indexed(ID3D12GraphicsCommandList *l, UINT idx, UINT ic, UINT si, INT bv, UINT sinst)
{
    uint32_t arg[3] = { idx, ic, si };
    int direct;
    trace_op(l, GG_OP_DRAW_INDEXED, 0, arg, &direct);
    void *pso = gg_cl_rs(l, GG_CL_PSO, NULL, 0);
    uint32_t h = gg_pso_hash(pso), info = gg_pso_info(pso);
    if (!direct) log_pso_hash(h, arg, bv, info);
    if (gg_trace_on) {               /* os primeiros de cada tipo: replayer (suspeito) x diretos (funcionam) */
        static volatile LONG dumped[2];
        if (InterlockedIncrement(&dumped[direct != 0]) <= (direct ? 4 : 12))
            dump_draw_state(l, direct ? "DrawIndexedInstanced direto" : "DrawIndexedInstanced do replayer", arg, h);
    }
    if (gg_skip_draw(direct, GG_OP_DRAW_INDEXED, check_ia(l, GG_OP_DRAW_INDEXED, arg), h)) return;
    if (gg_skip_stages(info)) return;
    bind_null_uavs(l);
    real_draw_indexed(l, idx, ic, si, bv, sinst);
}

static void STDMETHODCALLTYPE hook_exec_indirect(ID3D12GraphicsCommandList *l, ID3D12CommandSignature *sig, UINT max,
                                                 ID3D12Resource *args, UINT64 off, ID3D12Resource *cnt, UINT64 cnt_off)
{
    uint32_t arg[3] = { max, (uint32_t)off, cnt != NULL };
    int direct;
    trace_op(l, GG_OP_EXECUTE_INDIRECT, 0, arg, &direct);
    if (gg_skip_draw(direct, GG_OP_EXECUTE_INDIRECT, 0, 0)) return;
    if (gg_skip_stages(gg_pso_info(gg_cl_rs(l, GG_CL_PSO, NULL, 0)))) return;
    bind_null_uavs(l);
    real_exec_indirect(l, sig, max, args, off, cnt, cnt_off);
}

static void STDMETHODCALLTYPE hook_clear_rtv(ID3D12GraphicsCommandList *l, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT *color,
                                             UINT n, const D3D12_RECT *rects)
{
    uint32_t arg[3] = { n, 0, 0 };
    trace_op(l, GG_OP_CLEAR_RTV, 0, arg, NULL);
    real_clear_rtv(l, rtv, color, n, rects);
}

static void STDMETHODCALLTYPE hook_set_pso(ID3D12GraphicsCommandList *l, ID3D12PipelineState *ps)
{
    gg_cl_rs(l, GG_CL_PSO, ps, 1);
    real_set_pso(l, ps);
}

/* CopyTextureRegion: argumentos fora do recurso -> relatorio proprio (com pilha); formatos de copia novos -> log */
static volatile LONG copy_reports;

static void log_copy_shape(const D3D12_RESOURCE_DESC *d, const D3D12_TEXTURE_COPY_LOCATION *dst,
                           const D3D12_TEXTURE_COPY_LOCATION *src)
{
    static uint32_t seen[64];
    static volatile LONG nseen;
    static SRWLOCK lock = SRWLOCK_INIT;
    uint32_t h = 2166136261u, v[8], i;
    v[0] = d->Dimension; v[1] = (uint32_t)d->Width; v[2] = d->Height; v[3] = d->DepthOrArraySize;
    v[4] = d->MipLevels; v[5] = d->Format; v[6] = dst->Type; v[7] = src->Type;
    for (i = 0; i < 8; i++) h = (h ^ v[i]) * 16777619u;
    AcquireSRWLockExclusive(&lock);
    for (i = 0; i < (uint32_t)nseen && seen[i] != h; i++) {}
    if (i < (uint32_t)nseen || nseen >= 64) { ReleaseSRWLockExclusive(&lock); return; }
    seen[nseen++] = h;
    ReleaseSRWLockExclusive(&lock);
    wlog("gpuguard: copia nova: destino dim %u %ux%u x%u mips %u fmt %u (%s), origem %s, flags %#x", v[0], v[1], v[2],
         v[3], v[4], v[5], v[6] ? "footprint" : "subrecurso", v[7] ? "footprint" : "subrecurso", (unsigned)d->Flags);
}

static void check_copytex(const D3D12_TEXTURE_COPY_LOCATION *dst, UINT x, UINT y, UINT z,
                          const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *box)
{
    D3D12_RESOURCE_DESC dd, sd;
    char why[200];
    if (!dst || !src || !dst->pResource || !src->pResource) {
        report_to(&copy_reports, GG_MAX_REPORTS, "CopyTextureRegion com recurso nulo, destino", (uintptr_t)(dst ? dst->pResource : NULL));
        return;
    }
    dst->pResource->lpVtbl->GetDesc(dst->pResource, &dd);
    src->pResource->lpVtbl->GetDesc(src->pResource, &sd);
    log_copy_shape(&dd, dst, src);
    if (gg_copy_check(&dd, dst, x, y, z, &sd, src, box, why)) {
        uint64_t va = dd.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? dst->pResource->lpVtbl->GetGPUVirtualAddress(dst->pResource)
                    : sd.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? src->pResource->lpVtbl->GetGPUVirtualAddress(src->pResource) : 0;
        char what[260];
        wsprintfA(what, "CopyTextureRegion %s; VA do buffer", why);
        report_to(&copy_reports, GG_MAX_REPORTS, what, va);
    }
}

static void STDMETHODCALLTYPE hook_copytex(ID3D12GraphicsCommandList *l, const D3D12_TEXTURE_COPY_LOCATION *dst, UINT x, UINT y,
                                           UINT z, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *box)
{
    uint32_t arg[3] = { x, y, z };
    trace_op(l, GG_OP_COPY_TEXTURE, 0, arg, NULL);
    check_copytex(dst, x, y, z, src, box);
    if (gg_trace_on == 5) {         /* EXPERIMENTO D3D12Debug=7 (17_fm6.txt PASSO 27): nenhuma copia chega a GPU */
        static volatile LONG skipped;
        skip_note(&skipped, "CopyTextureRegion");
        return;
    }
    real_copytex(l, dst, x, y, z, src, box);
}

static HRESULT STDMETHODCALLTYPE hook_reset(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    gg_trace_reset(l);
    gg_cl_rs(l, 0, NULL, 1);
    gg_cl_rs(l, GG_CL_PSO, ps, 1);
    gg_ia_state(l, 3, 0, 0, NULL, 0);
    gg_gfx_set(l, 4, 0, 0, NULL, 0);
    gg_cl_gtab(l, 0, 0);
    return real_reset(l, a, ps);
}

/* sem rastreio (D3D12Debug < 3, o caso normal): so a correcao dos UAVs nulos (PASSO 29) nos draws; o Reset zera a
   root signature e a mascara das tabelas definidas e guarda o PSO inicial (conferido contra a root signature) */
static HRESULT STDMETHODCALLTYPE hook_reset_uav(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    gg_cl_rs(l, 0, NULL, 1);
    gg_cl_rs(l, GG_CL_PSO, ps, 1);
    gg_cl_gtab(l, 0, 0);
    return real_reset(l, a, ps);
}
static void STDMETHODCALLTYPE hook_draw_uav(ID3D12GraphicsCommandList *l, UINT vc, UINT ic, UINT sv, UINT si)
{
    bind_null_uavs(l);
    real_draw(l, vc, ic, sv, si);
}
static void STDMETHODCALLTYPE hook_draw_indexed_uav(ID3D12GraphicsCommandList *l, UINT idx, UINT ic, UINT si, INT bv, UINT sinst)
{
    bind_null_uavs(l);
    real_draw_indexed(l, idx, ic, si, bv, sinst);
}
static void STDMETHODCALLTYPE hook_exec_indirect_uav(ID3D12GraphicsCommandList *l, ID3D12CommandSignature *sig, UINT max,
                                                     ID3D12Resource *args, UINT64 off, ID3D12Resource *cnt, UINT64 cnt_off)
{
    bind_null_uavs(l);
    real_exec_indirect(l, sig, max, args, off, cnt, cnt_off);
}

/* troca um slot de vtable (memoria somente leitura da dll) e guarda o original */
static void swap(void **slot, void *hook, void **orig)
{
    DWORD old;
    if (*slot == hook) return;
    *orig = *slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

/* lista destruida (ou criada num endereco reaproveitado): tira o registro de todas as tabelas por lista */
static volatile LONG lists_live, lists_made;

int gg_cl_forget(void *cl)
{
    int n = 0;
    AcquireSRWLockExclusive(&cl_lock);
    n += lp_remove(cl_tab, sizeof *cl_tab, GG_CL_SLOTS, cl);
    ReleaseSRWLockExclusive(&cl_lock);
    AcquireSRWLockExclusive(&ia_lock);
    n += lp_remove(ia_tab, sizeof *ia_tab, GG_CL_SLOTS, cl);
    ReleaseSRWLockExclusive(&ia_lock);
    AcquireSRWLockExclusive(&gfx_lock);
    n += lp_remove(gfx_tab, sizeof *gfx_tab, GG_CL_SLOTS, cl);
    ReleaseSRWLockExclusive(&gfx_lock);
    AcquireSRWLockExclusive(&trace_lock);
    n += lp_remove(trace_tab, sizeof *trace_tab, GG_TRACE_SLOTS, cl);
    ReleaseSRWLockExclusive(&trace_lock);
    return n;
}

static ULONG (STDMETHODCALLTYPE *real_list_release)(ID3D12GraphicsCommandList *);
static ULONG STDMETHODCALLTYPE hook_list_release(ID3D12GraphicsCommandList *l)
{
    ULONG r = real_list_release(l);
    if (r == 0) {
        gg_cl_forget(l);
        InterlockedDecrement(&lists_live);
    }
    return r;
}

/* quem cria command lists / allocators (17_fm6.txt PASSO 36: ~4600 listas vivas e "Kernel memory failure" em 20 s):
   pontos distintos pela pilha em game/azimuth; ponto novo -> log com a pilha, depois contagem nas potencias de 2 */
#define GG_MAKE_SITES 48
typedef struct { uint32_t hash; volatile LONG count; } gg_make_site;
static gg_make_site make_sites[2][GG_MAKE_SITES];
static volatile LONG make_nsites[2];
static SRWLOCK make_lock = SRWLOCK_INIT;

static void note_make_site(int what)
{
    static const char *const names[] = { "command list", "command allocator" };
    void *f[8];
    uint32_t n = known_frames(f, 8), h = 2166136261u, i;
    LONG c = 0, idx = -1;
    for (i = 0; i < n && i < 6; i++) h = (h ^ (uint32_t)(uintptr_t)f[i]) * 16777619u;
    AcquireSRWLockExclusive(&make_lock);
    for (i = 0; i < (uint32_t)make_nsites[what]; i++)
        if (make_sites[what][i].hash == h) { idx = (LONG)i; break; }
    if (idx < 0 && make_nsites[what] < GG_MAKE_SITES) {
        idx = make_nsites[what]++;
        make_sites[what][idx].hash = h;
        make_sites[what][idx].count = 0;
    }
    if (idx >= 0) c = ++make_sites[what][idx].count;
    ReleaseSRWLockExclusive(&make_lock);
    if (idx < 0 || (c > 1 && (c < 64 || (c & (c - 1))))) return;
    {
        char buf[600], one[40];
        int len = wsprintfA(buf, "gpuguard: %s criada no ponto %ld (#%ld, tid %lu); pilha:", names[what], idx, c,
                            GetCurrentThreadId());
        for (i = 0; i < n && len < 540; i++) {
            label(one, f[i]);
            len += wsprintfA(buf + len, "%s", one);
        }
        wlog("%s", buf);
    }
}

/* lista nova: registro velho do mesmo endereco fora; contagem nas potencias de 2 (vazamento de listas = fps caindo) */
static void list_created(void *obj)
{
    LONG k = InterlockedIncrement(&lists_made), live = InterlockedIncrement(&lists_live);
    gg_cl_forget(obj);
    if (gg_trace_on) note_make_site(0);
    if (k >= 16 && (k & (k - 1)) == 0) wlog("gpuguard: %ld command lists criadas, %ld vivas", k, live);
}

static HRESULT (STDMETHODCALLTYPE *real_alloc)(ID3D12Device *, D3D12_COMMAND_LIST_TYPE, REFIID, void **);
static HRESULT STDMETHODCALLTYPE hook_alloc(ID3D12Device *dev, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **out)
{
    static volatile LONG made;
    HRESULT hr = real_alloc(dev, type, riid, out);
    if (SUCCEEDED(hr)) {
        LONG k = InterlockedIncrement(&made);
        note_make_site(1);
        if (k >= 16 && (k & (k - 1)) == 0) wlog("gpuguard: %ld command allocators criados", k);
    }
    return hr;
}

static void hook_list_vtable(void *obj)
{
    ID3D12GraphicsCommandList *l = NULL;
    static volatile LONG done;
    if (InterlockedCompareExchange(&done, 1, 0)) return;
    if (FAILED(((IUnknown *)obj)->lpVtbl->QueryInterface(obj, &IID_ID3D12GraphicsCommandList, (void **)&l))) { done = 0; return; }
    /* sempre: o que os UAVs nulos precisam (root signature grafica e tabelas definidas por lista, registro solto no
       Release); o resto (enderecos envenenados, slots nunca gravados, estado do draw) so com o rastreio */
    swap((void **)&l->lpVtbl->SetGraphicsRootSignature, hook_grs, (void **)&real_grs);
    swap((void **)&l->lpVtbl->SetGraphicsRootDescriptorTable, hook_gtab, (void **)&real_gtab);
    swap((void **)&l->lpVtbl->Release, hook_list_release, (void **)&real_list_release);
    if (gg_trace_on) {
        swap((void **)&l->lpVtbl->SetComputeRootConstantBufferView, hook_ccbv, (void **)&real_ccbv);
        swap((void **)&l->lpVtbl->SetGraphicsRootConstantBufferView, hook_gcbv, (void **)&real_gcbv);
        swap((void **)&l->lpVtbl->SetComputeRootShaderResourceView, hook_csrv, (void **)&real_csrv);
        swap((void **)&l->lpVtbl->SetGraphicsRootShaderResourceView, hook_gsrv, (void **)&real_gsrv);
        swap((void **)&l->lpVtbl->SetComputeRootUnorderedAccessView, hook_cuav, (void **)&real_cuav);
        swap((void **)&l->lpVtbl->SetGraphicsRootUnorderedAccessView, hook_guav, (void **)&real_guav);
        swap((void **)&l->lpVtbl->IASetIndexBuffer, hook_ib, (void **)&real_ib);
        swap((void **)&l->lpVtbl->IASetVertexBuffers, hook_vb, (void **)&real_vb);
        swap((void **)&l->lpVtbl->SOSetTargets, hook_so, (void **)&real_so);
        swap((void **)&l->lpVtbl->SetComputeRootSignature, hook_crs, (void **)&real_crs);
        swap((void **)&l->lpVtbl->SetComputeRootDescriptorTable, hook_ctab, (void **)&real_ctab);
        swap((void **)&l->lpVtbl->Dispatch, hook_dispatch, (void **)&real_dispatch);
        swap((void **)&l->lpVtbl->CopyTextureRegion, hook_copytex, (void **)&real_copytex);
        swap((void **)&l->lpVtbl->Reset, hook_reset, (void **)&real_reset);
        swap((void **)&l->lpVtbl->DrawInstanced, hook_draw, (void **)&real_draw);
        swap((void **)&l->lpVtbl->DrawIndexedInstanced, hook_draw_indexed, (void **)&real_draw_indexed);
        swap((void **)&l->lpVtbl->ExecuteIndirect, hook_exec_indirect, (void **)&real_exec_indirect);
        swap((void **)&l->lpVtbl->ClearRenderTargetView, hook_clear_rtv, (void **)&real_clear_rtv);
        swap((void **)&l->lpVtbl->SetPipelineState, hook_set_pso, (void **)&real_set_pso);
    } else {
        swap((void **)&l->lpVtbl->Reset, hook_reset_uav, (void **)&real_reset);
        swap((void **)&l->lpVtbl->DrawInstanced, hook_draw_uav, (void **)&real_draw);
        swap((void **)&l->lpVtbl->DrawIndexedInstanced, hook_draw_indexed_uav, (void **)&real_draw_indexed);
        swap((void **)&l->lpVtbl->ExecuteIndirect, hook_exec_indirect_uav, (void **)&real_exec_indirect);
        swap((void **)&l->lpVtbl->SetPipelineState, hook_set_pso, (void **)&real_set_pso);
    }
    l->lpVtbl->Release(l);
    wlog("gpuguard: vtable da command list trocada (UAVs nulos nos draws%s)",
         gg_trace_on ? "; rastreio: root views, IA, SO, tabelas, Dispatch/CopyTextureRegion/draws/ClearRTV/PSO" : "");
}

static HRESULT STDMETHODCALLTYPE hook_list(ID3D12Device *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *a,
                                           ID3D12PipelineState *ps, REFIID riid, void **out)
{
    HRESULT hr = real_list(dev, mask, type, a, ps, riid, out);
    if (SUCCEEDED(hr) && out && *out) {   /* copia: sem hooks, mas usa a mesma vtable (Release conta) */
        if (type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
        list_created(*out);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_list1(ID3D12Device4 *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS f,
                                            REFIID riid, void **out)
{
    HRESULT hr = real_list1(dev, mask, type, f, riid, out);
    if (SUCCEEDED(hr) && out && *out) {   /* copia: sem hooks, mas usa a mesma vtable (Release conta) */
        if (type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
        list_created(*out);
    }
    return hr;
}

/* residencia (17_fm6.txt PASSO 26): o placeholder 0xdeadbeef do UMD da AMD pode ser o de alocacao nao residente.
   So conta e registra (1as 8 chamadas de cada com pilha, depois potencias de 2); nada muda. */
static HRESULT (STDMETHODCALLTYPE *real_resident)(ID3D12Device *, UINT, ID3D12Pageable *const *);
static HRESULT (STDMETHODCALLTYPE *real_evict)(ID3D12Device *, UINT, ID3D12Pageable *const *);
static HRESULT (STDMETHODCALLTYPE *real_priority)(ID3D12Device1 *, UINT, ID3D12Pageable *const *, const D3D12_RESIDENCY_PRIORITY *);
static HRESULT (STDMETHODCALLTYPE *real_enqueue)(ID3D12Device3 *, D3D12_RESIDENCY_FLAGS, UINT, ID3D12Pageable *const *,
                                                 ID3D12Fence *, UINT64);
static volatile LONG residency_calls[4], residency_reports;

static void log_residency(int which, UINT n, ID3D12Pageable *const *objs)
{
    static const char *const names[] = { "MakeResident", "Evict", "SetResidencyPriority", "EnqueueMakeResident" };
    LONG k = InterlockedIncrement(&residency_calls[which]);
    char what[96];
    if (k > 8 && (k & (k - 1))) return;
    wsprintfA(what, "residencia: %s chamada #%ld com %u objetos, 1o", names[which], k, n);
    report_to(&residency_reports, 64, what, (uintptr_t)(n && objs ? objs[0] : NULL));
}

static HRESULT STDMETHODCALLTYPE hook_resident(ID3D12Device *d, UINT n, ID3D12Pageable *const *o) { log_residency(0, n, o); return real_resident(d, n, o); }
static HRESULT STDMETHODCALLTYPE hook_evict(ID3D12Device *d, UINT n, ID3D12Pageable *const *o) { log_residency(1, n, o); return real_evict(d, n, o); }
static HRESULT STDMETHODCALLTYPE hook_priority(ID3D12Device1 *d, UINT n, ID3D12Pageable *const *o, const D3D12_RESIDENCY_PRIORITY *p)
{
    log_residency(2, n, o);
    return real_priority(d, n, o, p);
}
static HRESULT STDMETHODCALLTYPE hook_enqueue(ID3D12Device3 *d, D3D12_RESIDENCY_FLAGS fl, UINT n, ID3D12Pageable *const *o,
                                              ID3D12Fence *f, UINT64 v)
{
    log_residency(3, n, o);
    return real_enqueue(d, fl, n, o, f, v);
}

static void hook_residency(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device1 *dev1 = NULL;
    ID3D12Device3 *dev3 = NULL;
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) {
        swap((void **)&dev->lpVtbl->MakeResident, hook_resident, (void **)&real_resident);
        swap((void **)&dev->lpVtbl->Evict, hook_evict, (void **)&real_evict);
        dev->lpVtbl->Release(dev);
    }
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device1, (void **)&dev1))) {
        swap((void **)&dev1->lpVtbl->SetResidencyPriority, hook_priority, (void **)&real_priority);
        dev1->lpVtbl->Release(dev1);
    }
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device3, (void **)&dev3))) {
        swap((void **)&dev3->lpVtbl->EnqueueMakeResident, hook_enqueue, (void **)&real_enqueue);
        dev3->lpVtbl->Release(dev3);
    }
    wlog("gpuguard: residencia rastreada (MakeResident/Evict/SetResidencyPriority/EnqueueMakeResident)");
}

/* chamado pelo hook do D3D12CreateDevice (debug12_fm5.c) no FM6 */
void gpuguard_install(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device4 *dev4 = NULL;
    static int done;
    if (done || FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) return;
    done = 1;
    g_dev = dev;   /* a referencia do Azimuth mantem o device vivo */
    res_inc = dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    swap((void **)&dev->lpVtbl->CreateRootSignature, hook_rootsig, (void **)&real_rootsig);
    swap((void **)&dev->lpVtbl->CreateDescriptorHeap, hook_heap, (void **)&real_heap);
    swap((void **)&dev->lpVtbl->CreateCommandList, hook_list, (void **)&real_list);
    if (gg_trace_on) {   /* bitmap/espelho dos descritores e pontos de criacao: so diagnostico */
        swap((void **)&dev->lpVtbl->CreateConstantBufferView, hook_cbv, (void **)&real_cbv);
        swap((void **)&dev->lpVtbl->CreateShaderResourceView, hook_srv, (void **)&real_srv);
        swap((void **)&dev->lpVtbl->CreateUnorderedAccessView, hook_uav, (void **)&real_uav);
        swap((void **)&dev->lpVtbl->CopyDescriptors, hook_copy, (void **)&real_copy);
        swap((void **)&dev->lpVtbl->CopyDescriptorsSimple, hook_copy1, (void **)&real_copy1);
        swap((void **)&dev->lpVtbl->CreateCommandAllocator, hook_alloc, (void **)&real_alloc);
    }
    swap((void **)&dev->lpVtbl->CreateGraphicsPipelineState, hook_gpso, (void **)&real_gpso);
    dev->lpVtbl->Release(dev);
    {
        ID3D12Device2 *dev2 = NULL;
        if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device2, (void **)&dev2))) {
            swap((void **)&dev2->lpVtbl->CreatePipelineState, hook_spso, (void **)&real_spso);
            swap((void **)&dev2->lpVtbl->CreatePipelineLibrary, hook_lib, (void **)&real_lib);
            dev2->lpVtbl->Release(dev2);
        }
    }
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device4, (void **)&dev4))) {
        swap((void **)&dev4->lpVtbl->CreateCommandList1, hook_list1, (void **)&real_list1);
        dev4->lpVtbl->Release(dev4);
    }
    if (gg_trace_on) hook_residency(obj);
    wlog("gpuguard: ligada no device D3D12 (descritor CBV/SRV/UAV de %u bytes%s)", res_inc,
         gg_trace_on ? ", com as defesas de descritores/enderecos do rastreio" : ", so UAVs nulos");
}
