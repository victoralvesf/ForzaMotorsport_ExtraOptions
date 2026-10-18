/*
 * FM6: trava contra page fault da GPU (17_fm6.txt "DIAGNOSTICO DO PAGE FAULT"). O FM6 para com a GPU lendo um
 * descritor com bytes 0xdeadbeef (V# da AMD -> endereco 0xbeefdeadb000); cada page fault e um TDR, e uma sequencia
 * deles chegou a derrubar o driver de video (BSOD 0x3b, 2026-10-18). A trava fica no device D3D12 do Azimuth
 * (ligada pelo hook do D3D12CreateDevice em debug12_fm5.c; o device nasce antes de o jogo ser conhecido, entao vale
 * para todos, mas so muda algo onde a GPU daria page fault) e age antes de a GPU ver o dado:
 *  - heaps CBV/SRV/UAV: bitmap de slots gravados (Create*View / destino de CopyDescriptors*); copiar um slot de
 *    origem nunca gravado loga a pilha e grava um SRV nulo nele antes (o handle de CPU na AMD nao aponta para os
 *    bytes do descritor: o conteudo nao da para conferir, so o bitmap);
 *  - Set*RootDescriptorTable: tamanho de cada tabela CBV/SRV/UAV lido da root signature (CreateRootSignature);
 *    slot nunca gravado dentro da tabela no heap visivel pela GPU -> relatorio + SRV nulo nele antes do draw;
 *  - endereco de GPU envenenado (0xdeadbeef / 0xbeefdead.. / fora de 48 bits) em CBV, root views, vertex/index
 *    buffer e stream out: loga a pilha; CBV vira nulo; root view/buffer vira 0 (nulo) no lugar do endereco.
 * Com o rastreio (D3D12Debug>=3): CopyTextureRegion conferido (subrecurso/footprint/regiao, gg_copy_check) e chamadas
 * de residencia (MakeResident/Evict/...) contadas; ambos so registram.
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

typedef struct {
    uintptr_t start;
    uint32_t count, inc;
    volatile LONG *bits;
    uint64_t gpu;                 /* inicio no endereco de GPU (heap visivel pela GPU), 0 senao */
} gg_heap;

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
            heaps[i] = heaps[--nheaps];
            i--;
        }
    }
    if (nheaps < GG_MAX_HEAPS) heaps[nheaps++] = (gg_heap){ start, count, inc, bits, gpu };
    else free((void *)bits);
    ReleaseSRWLockExclusive(&heap_lock);
}

void gg_heap_reset(void)
{
    AcquireSRWLockExclusive(&heap_lock);
    while (nheaps) free((void *)heaps[--nheaps].bits);
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

typedef struct { void *rs; uint32_t n; uint32_t size[GG_MAX_PARAMS]; } gg_rs;
typedef struct { void *cl; void *rs[2]; } gg_cl;   /* rs[0] grafica, rs[1] compute */

static gg_rs rs_tab[GG_RS_SLOTS];
static gg_cl cl_tab[GG_CL_SLOTS];
static SRWLOCK rs_lock = SRWLOCK_INIT, cl_lock = SRWLOCK_INIT;

static unsigned hptr(const void *p, unsigned n) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) & (n - 1); }

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

/* root signature atual de uma command list (compute 0/1): do_set grava `set`, senao so consulta */
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
#define GG_SITES 12
#define GG_TRACE_SLOTS 2048

enum { GG_OP_DISPATCH, GG_OP_COPY_TEXTURE };

typedef struct {
    uint32_t hash, count, op, nframes;
    void *rs;
    uint32_t arg[3];              /* 1a ocorrencia: grupos do Dispatch / destino x,y,z da copia */
    void *frames[GG_SITE_FRAMES];
} gg_site;
typedef struct { void *cl; uint32_t n, dropped; gg_site s[GG_SITES]; } gg_trace;

int gg_trace_on;                  /* D3D12Debug - 2: 1 = rastreio; 2 = + pula a compute interna; 5 = + pula CopyTextureRegion */
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

void gg_trace_add(void *cl, uint32_t op, void *rs, const uint32_t *arg, void *const *frames, uint32_t nframes)
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
                s->hash = hash; s->count = 1; s->op = op; s->rs = rs; s->nframes = nframes;
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
        int len = wsprintfA(buf, "gpuguard: lista %p: %s x%u (1o %u,%u,%u) rs %p;", cl,
                            s[i].op == GG_OP_DISPATCH ? "Dispatch" : "CopyTextureRegion", s[i].count,
                            s[i].arg[0], s[i].arg[1], s[i].arg[2], s[i].rs);
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
            for (i = 0; i < n && i < GG_MAX_PARAMS; i++)
                if (vd->Desc_1_1.pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                    size[i] = gg_table_size(&vd->Desc_1_1.pParameters[i].DescriptorTable);
            gg_rs_add(*out, size, n);
            if (gg_trace_on) log_rootsig(*out, &vd->Desc_1_1);
            if (gg_is_internal_rs(&vd->Desc_1_1)) internal_rs = *out;
        }
        ds->lpVtbl->Release(ds);
    }
    return hr;
}

static void STDMETHODCALLTYPE hook_cbv(ID3D12Device *dev, const D3D12_CONSTANT_BUFFER_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (d && d->BufferLocation && !guard_va("CreateConstantBufferView", d->BufferLocation)) d = NULL;
    real_cbv(dev, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_srv(ID3D12Device *dev, ID3D12Resource *r, const D3D12_SHADER_RESOURCE_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    real_srv(dev, r, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_uav(ID3D12Device *dev, ID3D12Resource *r, ID3D12Resource *c, const D3D12_UNORDERED_ACCESS_VIEW_DESC *d,
                                       D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    real_uav(dev, r, c, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_copy1(ID3D12Device *dev, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        UINT i;
        for (i = 0; i < n; i++) check_src(src.ptr + (uintptr_t)i * res_inc);
        gg_mark(dst.ptr, n);
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
    }
    real_copy(dev, nd, dst, dsz, ns, src, ssz, type);
}

static HRESULT STDMETHODCALLTYPE hook_heap(ID3D12Device *dev, const D3D12_DESCRIPTOR_HEAP_DESC *d, REFIID riid, void **out)
{
    HRESULT hr = real_heap(dev, d, riid, out);
    ID3D12DescriptorHeap *heap = NULL;
    if (SUCCEEDED(hr) && out && *out && d && d->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        SUCCEEDED(((IUnknown *)*out)->lpVtbl->QueryInterface(*out, &IID_ID3D12DescriptorHeap, (void **)&heap))) {
        static volatile LONG shown;
        D3D12_CPU_DESCRIPTOR_HANDLE s;
        D3D12_GPU_DESCRIPTOR_HANDLE g = { 0 };
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &s);
        if (d->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap, &g);
        heap->lpVtbl->Release(heap);
        gg_heap_add(s.ptr, d->NumDescriptors, res_inc, g.ptr);
        if (InterlockedIncrement(&shown) <= 4)
            wlog("gpuguard: heap CBV/SRV/UAV cpu %p gpu %#llx x%u (flags %u)", (void *)s.ptr, (unsigned long long)g.ptr,
                 d->NumDescriptors, (unsigned)d->Flags);
    }
    return hr;
}

static void STDMETHODCALLTYPE hook_crs(ID3D12GraphicsCommandList *l, ID3D12RootSignature *rs) { gg_cl_rs(l, 1, rs, 1); real_crs(l, rs); }
static void STDMETHODCALLTYPE hook_grs(ID3D12GraphicsCommandList *l, ID3D12RootSignature *rs) { gg_cl_rs(l, 0, rs, 1); real_grs(l, rs); }
static void STDMETHODCALLTYPE hook_ctab(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE g) { check_table(l, 1, i, g); real_ctab(l, i, g); }
static void STDMETHODCALLTYPE hook_gtab(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE g) { check_table(l, 0, i, g); real_gtab(l, i, g); }

static void STDMETHODCALLTYPE hook_ccbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_ccbv(l, i, guard_va("SetComputeRootConstantBufferView", va)); }
static void STDMETHODCALLTYPE hook_gcbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_gcbv(l, i, guard_va("SetGraphicsRootConstantBufferView", va)); }
static void STDMETHODCALLTYPE hook_csrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_csrv(l, i, guard_va("SetComputeRootShaderResourceView", va)); }
static void STDMETHODCALLTYPE hook_gsrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_gsrv(l, i, guard_va("SetGraphicsRootShaderResourceView", va)); }
static void STDMETHODCALLTYPE hook_cuav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_cuav(l, i, guard_va("SetComputeRootUnorderedAccessView", va)); }
static void STDMETHODCALLTYPE hook_guav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_guav(l, i, guard_va("SetGraphicsRootUnorderedAccessView", va)); }

static void STDMETHODCALLTYPE hook_ib(ID3D12GraphicsCommandList *l, const D3D12_INDEX_BUFFER_VIEW *v)
{
    D3D12_INDEX_BUFFER_VIEW c;
    if (v && v->BufferLocation && gg_va_poisoned(v->BufferLocation)) {
        report("IASetIndexBuffer", v->BufferLocation);
        c = *v; c.BufferLocation = 0; c.SizeInBytes = 0; v = &c;
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
static void log_new_site(void *cl, uint32_t op, void *rs, const uint32_t *arg, void *const *f, uint32_t n)
{
    static uint32_t seen[256];
    static volatile LONG nseen;
    static SRWLOCK lock = SRWLOCK_INIT;
    uint32_t hash = 2166136261u ^ op, i;
    char buf[400], one[40];
    int len;
    for (i = 0; i < n; i++) hash = (hash ^ (uint32_t)(uintptr_t)f[i]) * 16777619u;
    hash = (hash ^ (uint32_t)((uintptr_t)rs >> 4)) * 16777619u;
    AcquireSRWLockExclusive(&lock);
    for (i = 0; i < (uint32_t)nseen && seen[i] != hash; i++) {}
    if (i < (uint32_t)nseen || nseen >= 256) { ReleaseSRWLockExclusive(&lock); return; }
    seen[nseen++] = hash;
    ReleaseSRWLockExclusive(&lock);
    len = wsprintfA(buf, "gpuguard: ponto novo de %s (%u,%u,%u) rs %p lista %p tid=%lu;",
                    op == GG_OP_DISPATCH ? "Dispatch" : "CopyTextureRegion", arg[0], arg[1], arg[2], rs, cl,
                    GetCurrentThreadId());
    for (i = 0; i < n; i++) {
        label(one, f[i]);
        len += wsprintfA(buf + len, "%s", one);
    }
    wlog("%s", buf);
}

static void STDMETHODCALLTYPE hook_dispatch(ID3D12GraphicsCommandList *l, UINT x, UINT y, UINT z)
{
    void *f[GG_SITE_FRAMES], *rs = gg_cl_rs(l, 1, NULL, 0);
    uint32_t arg[3] = { x, y, z }, n = known_frames(f, GG_SITE_FRAMES);
    gg_trace_add(l, GG_OP_DISPATCH, rs, arg, f, n);
    log_new_site(l, GG_OP_DISPATCH, rs, arg, f, n);
    if (gg_trace_on == 2 && rs && rs == internal_rs) {
        static volatile LONG skipped;
        LONG k = InterlockedIncrement(&skipped);
        if ((k & (k - 1)) == 0) wlog("gpuguard: experimento: Dispatch #%ld da compute interna pulado (%u,%u,%u)", k, x, y, z);
        return;
    }
    real_dispatch(l, x, y, z);
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
    void *f[GG_SITE_FRAMES];
    uint32_t arg[3] = { x, y, z }, n = known_frames(f, GG_SITE_FRAMES);
    gg_trace_add(l, GG_OP_COPY_TEXTURE, NULL, arg, f, n);
    log_new_site(l, GG_OP_COPY_TEXTURE, NULL, arg, f, n);
    check_copytex(dst, x, y, z, src, box);
    if (gg_trace_on == 5) {         /* EXPERIMENTO D3D12Debug=7 (17_fm6.txt PASSO 27): nenhuma copia chega a GPU */
        static volatile LONG skipped;
        LONG k = InterlockedIncrement(&skipped);
        if ((k & (k - 1)) == 0) wlog("gpuguard: experimento: CopyTextureRegion #%ld pulado", k);
        return;
    }
    real_copytex(l, dst, x, y, z, src, box);
}

static HRESULT STDMETHODCALLTYPE hook_reset(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    gg_trace_reset(l);
    return real_reset(l, a, ps);
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

static void hook_list_vtable(void *obj)
{
    ID3D12GraphicsCommandList *l = NULL;
    static volatile LONG done;
    if (InterlockedCompareExchange(&done, 1, 0)) return;
    if (FAILED(((IUnknown *)obj)->lpVtbl->QueryInterface(obj, &IID_ID3D12GraphicsCommandList, (void **)&l))) { done = 0; return; }
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
    swap((void **)&l->lpVtbl->SetGraphicsRootSignature, hook_grs, (void **)&real_grs);
    swap((void **)&l->lpVtbl->SetComputeRootDescriptorTable, hook_ctab, (void **)&real_ctab);
    swap((void **)&l->lpVtbl->SetGraphicsRootDescriptorTable, hook_gtab, (void **)&real_gtab);
    if (gg_trace_on) {
        swap((void **)&l->lpVtbl->Dispatch, hook_dispatch, (void **)&real_dispatch);
        swap((void **)&l->lpVtbl->CopyTextureRegion, hook_copytex, (void **)&real_copytex);
        swap((void **)&l->lpVtbl->Reset, hook_reset, (void **)&real_reset);
    }
    l->lpVtbl->Release(l);
    wlog("gpuguard: vtable da command list trocada (root signature, tabelas, root views, IA, SO%s)",
         gg_trace_on ? ", rastreio de Dispatch/CopyTextureRegion" : "");
}

static HRESULT STDMETHODCALLTYPE hook_list(ID3D12Device *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *a,
                                           ID3D12PipelineState *ps, REFIID riid, void **out)
{
    HRESULT hr = real_list(dev, mask, type, a, ps, riid, out);
    if (SUCCEEDED(hr) && out && *out && type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_list1(ID3D12Device4 *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS f,
                                            REFIID riid, void **out)
{
    HRESULT hr = real_list1(dev, mask, type, f, riid, out);
    if (SUCCEEDED(hr) && out && *out && type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
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
    swap((void **)&dev->lpVtbl->CreateConstantBufferView, hook_cbv, (void **)&real_cbv);
    swap((void **)&dev->lpVtbl->CreateShaderResourceView, hook_srv, (void **)&real_srv);
    swap((void **)&dev->lpVtbl->CreateUnorderedAccessView, hook_uav, (void **)&real_uav);
    swap((void **)&dev->lpVtbl->CopyDescriptors, hook_copy, (void **)&real_copy);
    swap((void **)&dev->lpVtbl->CopyDescriptorsSimple, hook_copy1, (void **)&real_copy1);
    swap((void **)&dev->lpVtbl->CreateCommandList, hook_list, (void **)&real_list);
    dev->lpVtbl->Release(dev);
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device4, (void **)&dev4))) {
        swap((void **)&dev4->lpVtbl->CreateCommandList1, hook_list1, (void **)&real_list1);
        dev4->lpVtbl->Release(dev4);
    }
    if (gg_trace_on) hook_residency(obj);
    wlog("gpuguard: ligada no device D3D12 (descritor CBV/SRV/UAV de %u bytes)", res_inc);
}
