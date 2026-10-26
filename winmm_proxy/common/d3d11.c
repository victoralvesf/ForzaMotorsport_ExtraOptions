/*
 * Device/contexto D3D11.X do Azimuth: o que as pontes do FM5 (fm5/d3d.c) e do FM6 (fm6/d3d.c) compartilham:
 * SetFast* sobre os Set* padrao, emulacao do aliasing da ESRAM, Quality MSAA devolvido ao jogo, FMASK emulada e as
 * tabelas de layout por tipo de objeto.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../proxy.h"
#include "d3d.h"

/* FM5 0x9a..0xad = X SetFast* (FH2 0xa0..0xb3). O Azimuth so tem o stub fatal nesses slots (o FH2 usa
 * SetFastResources); aqui viram o Set* padrao com 1 elemento. Slots no layout do Azimuth (com o slot 7):
 * VSSetConstantBuffers 8, PSSetShaderResources 9, PSSetSamplers 0xb, PSSetConstantBuffers 0x11,
 * IASetVertexBuffers 0x13, IASetIndexBuffer 0x14, GSSetConstantBuffers 0x17, VS SRV/S 0x19/0x1a,
 * GS SRV/S 0x1f/0x20, HS SRV/S/CB 0x3b/0x3d/0x3e, DS 0x3f/0x41/0x42, CS SRV 0x43, S 0x46, CB 0x47.
 * O Set* padrao segura uma referencia (o Fast nao); inofensivo. */

#define FAST(name, azslot) \
    void ctx_##name(void *real, UINT slot, void *obj) { \
        ((set_fn)(*(void ***)real)[azslot])(real, slot, 1, &obj); }

FAST(PSSetFastConstantBuffer, 0x11) FAST(PSSetFastShaderResource, 0x09)
FAST(VSSetFastConstantBuffer, 0x08) FAST(VSSetFastShaderResource, 0x19) FAST(VSSetFastSampler, 0x1a)
FAST(GSSetFastConstantBuffer, 0x17) FAST(GSSetFastShaderResource, 0x1f) FAST(GSSetFastSampler, 0x20)
FAST(CSSetFastConstantBuffer, 0x47) FAST(CSSetFastShaderResource, 0x43) FAST(CSSetFastSampler, 0x46)
FAST(HSSetFastConstantBuffer, 0x3e) FAST(HSSetFastShaderResource, 0x3b) FAST(HSSetFastSampler, 0x3d)
FAST(DSSetFastConstantBuffer, 0x42) FAST(DSSetFastShaderResource, 0x3f) FAST(DSSetFastSampler, 0x41)

/* IASetFastVertexBuffer(slot, buffer, stride): offset 0 (FM5 0x738a2e: (0, vb, 0x10)) */
typedef void (*setvb_fn)(void *ctx, UINT start, UINT n, void *const *pp, const UINT *strides, const UINT *offs);
void ctx_IASetFastVertexBuffer(void *real, UINT slot, void *buf, UINT stride)
{
    UINT off = 0;
    ((setvb_fn)(*(void ***)real)[0x13])(real, slot, 1, &buf, &stride, &off);
}

typedef void (*setib_fn)(void *ctx, UINT hwfmt, void *buf, UINT off);
/* IASetFastIndexBuffer(HardwareIndexFormat, buffer): offset 0 */
void ctx_IASetFastIndexBuffer(void *real, UINT hwfmt, void *buf)
{
    ((setib_fn)(*(void ***)real)[0x14])(real, hwfmt, buf, 0);
}

/* EMULACAO DO ALIASING (padrao ligado; FM5_ALIAS_TEST=0 desliga; PASSO 8 cont. 19, CONFIRMADO: a cena 3D aparece): a cena 3D e desenhada em cor 2x (DRAM) + profundidade 2x
 * (ESRAM 0), mas o compute de profundidade le uma 2a textura de profundidade no MESMO offset da ESRAM e o
 * pos-processamento le uma cor 1x (ESRAM 0xc00000) que nada escreve. Guarda as ultimas texturas de tela e:
 *   - no Dispatch do compute de profundidade (c0, (W/8, H/8, 1)): CopyResource(depth_b <- depth_a)
 *   - no ClearState seguinte do c0 (inicio do pos-processamento): ResolveSubresource(cor1x <- cor2x) */
int alias_test = -1;
SRWLOCK alias_lock = SRWLOCK_INIT;
void *al_color2x, *al_depth_a, *al_depth_b, *al_color1x;
UINT al_w, al_h;
volatile LONG al_pending, al_count;
void al_set(void **slot, void *tex)
{
    if (tex) ((ULONG (*)(void *))(*(void ***)tex)[1])(tex);
    if (*slot) ((ULONG (*)(void *))(*(void ***)*slot)[2])(*slot);
    *slot = tex;
}
/* esram = exige a cor 1x na ESRAM (misc 0x20000; FM5). No FM6 a cor 1x lida pelo pos e a 1a fmt 26 1x do mesmo
 * tamanho criada logo depois da cor MSAA e das profundidades (17_fm6.txt PASSO 44) */
void alias_track_ex(const UINT *desc, void *tex, int esram)
{
    if (alias_test < 0) { char v[8] = ""; alias_test = GetEnvironmentVariableA("FM5_ALIAS_TEST", v, sizeof v) ? atoi(v) : 1;
                          wlog("d3d: FM5_ALIAS_TEST=%d", alias_test); }
    /* > 2.5:1 = espelhos (768x256 escalados pelo reflections high/ultra, cont. 6), nao sao texturas de tela */
    if (alias_test <= 0 || !tex || desc[0] < 1280 || desc[0] * 2 > desc[1] * 5) return;
    AcquireSRWLockExclusive(&alias_lock);
    /* MSAA 2x (corrida) ou 4x (garagem/menus com carro, cont. 21: mesmo padrao, profundidades sem ESRAM) */
    if (desc[5] >= 2 && desc[4] == 26) { al_set(&al_color2x, tex); al_w = desc[0]; al_h = desc[1]; al_set(&al_color1x, NULL); }
    else if (desc[5] >= 2 && desc[4] == 19) { al_set(&al_depth_a, al_depth_b); al_set(&al_depth_b, tex); }
    else if (desc[5] <= 1 && desc[4] == 26 && (!esram || (desc[10] & 0x20000)) && !al_color1x && al_color2x &&
             desc[0] == al_w && desc[1] == al_h) al_set(&al_color1x, tex);
    ReleaseSRWLockExclusive(&alias_lock);
    wlog("d3d: alias: color2x=%p depth=%p,%p color1x=%p (%ux%u)", al_color2x, al_depth_a, al_depth_b, al_color1x, al_w, al_h);
}
void alias_track(const UINT *desc, void *tex) { alias_track_ex(desc, tex, 1); }

/* FM6 (17_fm6.txt PASSO 44): mesmo aliasing, mas o compute de profundidade e o pos rodam em contextos do FM6 (tabela
 * embutida, sem o proxy c0). RTVs criadas sobre a cor MSAA (al_rtv) dizem quando o contexto sai da cena: o Dispatch
 * (W/8, H/8, 1) copia prof a -> b e deixa o resolve pendente naquele contexto; o 1o OMSetRenderTargets dele com
 * outro RT (ou ClearState) faz o resolve cor2x -> cor1x depois de repassar a chamada. */
static struct { void *rtv, *res; } al_rtv[32];
static LONG al_rtv_n;
static void *volatile al_ctx;
void alias_rtv_note(void *rtv, void *res)
{
    LONG i, n;
    AcquireSRWLockExclusive(&alias_lock);
    n = al_rtv_n < 32 ? al_rtv_n : 32;
    for (i = 0; i < n && al_rtv[i].rtv != rtv; i++) ;
    if (i < n) al_rtv[i].res = res;                         /* ponteiro reaproveitado por outra view */
    else if (res && res == al_color2x) { i = al_rtv_n++ & 31; al_rtv[i].rtv = rtv; al_rtv[i].res = res; }
    ReleaseSRWLockExclusive(&alias_lock);
}
int alias_is_scene_rtv(void *rtv)
{
    LONG i, n;
    int r = 0;
    AcquireSRWLockShared(&alias_lock);
    n = al_rtv_n < 32 ? al_rtv_n : 32;
    for (i = 0; i < n && !r; i++) r = al_rtv[i].rtv == rtv && al_rtv[i].res == al_color2x;
    ReleaseSRWLockShared(&alias_lock);
    return r;
}
typedef void *(*alias_fn_lookup)(void *ctx, int slot);
int alias_fm6_dispatch(void *ctx, UINT x, UINT y, UINT z, int bundle, alias_fn_lookup fn)
{
    typedef void (*copy_fn)(void *, void *, void *);
    static volatile LONG nb;
    if (alias_test <= 0 || !al_w || x != (al_w + 7) / 8 || y != (al_h + 7) / 8 || z != 1) return 0;
    if (bundle) {
        if (InterlockedIncrement(&nb) <= 3) wlog("d3d: alias: Dispatch %ux%ux%u in draw bundle %p: ignored", x, y, z, ctx);
        return 0;
    }
    AcquireSRWLockShared(&alias_lock);
    if (al_depth_a && al_depth_b) ((copy_fn)fn(ctx, 0x2f))(ctx, al_depth_b, al_depth_a);
    ReleaseSRWLockShared(&alias_lock);
    al_ctx = ctx;
    if (InterlockedIncrement(&al_count) <= 3)
        wlog("d3d: alias: FM6 Dispatch %ux%ux%u on context %p -> copy depth %p -> %p, color resolve pending",
             x, y, z, ctx, al_depth_a, al_depth_b);
    return 1;
}
/* depois do OMSetRenderTargets (n RTs; ~0u = KEEP) ou do ClearState (clear = 1) do contexto ctx */
void alias_fm6_leave_scene(void *ctx, UINT n, void *const *rtvs, int clear, alias_fn_lookup fn)
{
    typedef void (*resolve_fn)(void *, void *, UINT, void *, UINT, UINT);
    static volatile LONG nr;
    if (!ctx || al_ctx != ctx) return;
    if (!clear && (n == 0xffffffffu || !n || !rtvs || !rtvs[0] || alias_is_scene_rtv(rtvs[0]))) return;
    if (InterlockedCompareExchangePointer((void *volatile *)&al_ctx, NULL, ctx) != ctx) return;
    AcquireSRWLockShared(&alias_lock);
    if (al_color2x && al_color1x) ((resolve_fn)fn(ctx, 0x39))(ctx, al_color1x, 0, al_color2x, 0, 26);
    ReleaseSRWLockShared(&alias_lock);
    if (InterlockedIncrement(&nr) <= 3)
        wlog("d3d: alias: FM6 context %p left the scene (%s) -> resolve color %p -> %p", ctx,
             clear ? "ClearState" : "other RT", al_color2x, al_color1x);
}

/* Quality original das texturas MSAA do FM5 (o D3D12 recebe D3D12_STANDARD_MULTISAMPLE_PATTERN, abaixo). O GetDesc
 * (FM5 slot 0xb) devolve ao JOGO o valor que ele pediu: o resolve MSAA do FM5 (0x6de160) escolhe o caminho pelo
 * SampleDesc lido via GetDesc (0x6a5ee0): Quality 0 -> ResolveSubresource; {2,2} / {4,3} -> shader com a FMASK;
 * qualquer outro valor -> sai sem desenhar (thumbnails da garagem/pintura pretos, PASSO 8 cont. 23). */
static SRWLOCK msaa_q_lock = SRWLOCK_INIT;
static struct { void *tex; UINT q; } msaa_q[256];
static unsigned msaa_q_next;

void msaa_q_set(void *tex, UINT q)
{
    unsigned i;
    AcquireSRWLockExclusive(&msaa_q_lock);
    for (i = 0; i < 256 && msaa_q[i].tex != tex; i++) ;
    if (i == 256) i = msaa_q_next++ & 255;
    msaa_q[i].tex = tex; msaa_q[i].q = q;
    ReleaseSRWLockExclusive(&msaa_q_lock);
}

int msaa_q_get(void *tex, UINT *q)
{
    unsigned i;
    int found = 0;
    AcquireSRWLockShared(&msaa_q_lock);
    for (i = 0; i < 256; i++) if (msaa_q[i].tex == tex) { *q = msaa_q[i].q; found = 1; break; }
    ReleaseSRWLockShared(&msaa_q_lock);
    return found;
}

/* devolve o Quality pedido so a chamadas do exe do jogo [game, game + size); Azimuth/proxy veem o do D3D12 */
void msaa_q_fix(void *tex, UINT *d, const uint8_t *ret, const uint8_t *game, size_t size)
{
    UINT q;
    if (d && d[5] > 1 && d[6] == 0xffffffff && game && ret >= game && ret < game + size && msaa_q_get(tex, &q))
        d[6] = q;
}

/* slot 7 CreateShaderResourceView. View R8_UINT/TEXTURE2D de uma textura MSAA = leitura da FMASK. O Azimuth
 * (0x14013dc40) emula a FMASK so p/ 4x (R8_UINT) e 8x (R32_UINT): cria uma textura com o mapeamento identidade
 * (amostra i -> fragmento i) e aponta a view para ela. O FM5 usa 2x -> o Azimuth repassava a view ao D3D12 sobre
 * a textura R11G11B10 (invalido -> device removido). Aqui (fmask_emul): 2x R8_UINT (1 bit/amostra) e 4x R32_UINT
 * (4 bits/amostra; cena apos "continuar" no fim da corrida, cont. 20), mapeamento conforme as amostras do host.
 * Objeto de textura do Azimuth: tipo u32 +0x38 (0xc = Texture2D), desc copiado em +0x70 (W, H, mips, arr, fmt,
 * samples +0x84), tamanho do array +0xe4. */
static SRWLOCK fmask_lock = SRWLOCK_INIT;
static struct { UINT w, h, samples, fmt; void *tex; } fmasks[16];
static int nfmasks;

/* amostras reais do host p/ uma textura MSAA do jogo (= 0x1400e74c0): --msaa "do console" (DAT_140318ea0 == 4)
 * repassa as do jogo; senao a configuracao (1 = AA desligado) */
UINT host_samples(UINT samples)
{
    UINT m = *(volatile UINT *)(g_base + 0x318ea0);
    return m == 4 ? samples : (m ? m : 1);
}

/* Valor de cada pixel da FMASK emulada. {2, 2} do XDK = EQAA 2 fragmentos / 4 amostras de cobertura: o resolve do
 * jogo (imageprocessor_eqaadownsamplecolor_2frag_4samp; FM5 0x6de160, FM6 0x40b62c) le R8 = 4 amostras x 2 bits
 * (0/1 = fragmento, >= 2 = amostra invalida) e faz a media ponderada pelos fragmentos (o do FM6 com tonemap por
 * fragmento). O host so tem 2 amostras reais -> amostras pares no fragmento 0, impares no 1 (0x44, pesos 2/2); com
 * 0x02 (1 bit/amostra) a amostra 0 era invalida e as outras caiam no fragmento 0 -> resolve so com a amostra 0 =
 * sem AA na tela do FM6 (PASSO 121). 4x R32: 4 bits/amostra (suposicao; o shader de {4, 3} nao foi visto).
 * host com as amostras todas -> identidade; host 2x -> fragmento i & 1; host 1x (AA desligado) -> tudo no 0. */
UINT fmask_value(UINT samples, UINT host, UINT fmt)
{
    UINT bits = fmt == 42 ? 4 : 2, val = 0, i;
    for (i = 0; i < 4; i++)
        val |= (host >= samples ? i % samples : host == 2 ? (i & 1) : 0) << (i * bits);
    return val;
}

/* FMASK emulada: textura WxH (R8_UINT ou R32_UINT) com fmask_value em cada pixel, uma por (tamanho, amostras,
 * formato), nunca liberada (o FM5 le a FMASK 4x como R32_UINT; o Azimuth so emula 4x R8 e 8x R32). */
void *fmask_emul(void *dev, tex2d_fn create, UINT w, UINT h, UINT samples, UINT fmt)
{
    UINT d[16] = { 0 };
    struct { const void *mem; UINT pitch, slice; } init;
    void *tex = NULL;
    uint8_t *buf;
    UINT host = host_samples(samples), bpp = fmt == 42 ? 4 : 1, val = fmask_value(samples, host, fmt);
    SIZE_T k, n = (SIZE_T)w * h;
    long hr;
    int j;
    AcquireSRWLockExclusive(&fmask_lock);
    for (j = 0; j < nfmasks; j++)
        if (fmasks[j].w == w && fmasks[j].h == h && fmasks[j].samples == samples && fmasks[j].fmt == fmt) {
            tex = fmasks[j].tex; goto done;
        }
    buf = HeapAlloc(GetProcessHeap(), 0, n * bpp);
    if (!buf) goto done;
    if (bpp == 1) memset(buf, (int)val, n);
    else for (k = 0; k < n; k++) ((UINT *)buf)[k] = val;
    d[0] = w; d[1] = h; d[2] = 1; d[3] = 1; d[4] = fmt; d[5] = 1; d[8] = 0x8 /* SHADER_RESOURCE */;
    init.mem = buf; init.pitch = w * bpp; init.slice = (UINT)(n * bpp);
    hr = create(dev, d, &init, &tex);
    HeapFree(GetProcessHeap(), 0, buf);
    wlog("d3d: FMASK %ux emulated %ux%u fmt %u (host %ux, value %#x) hr=%#lx tex=%p", samples, w, h, fmt, host, val, hr, tex);
    if (hr < 0) tex = NULL;
    else if (nfmasks < 16) {
        fmasks[nfmasks].w = w; fmasks[nfmasks].h = h; fmasks[nfmasks].samples = samples; fmasks[nfmasks].fmt = fmt;
        fmasks[nfmasks++].tex = tex;
    }
done:
    ReleaseSRWLockExclusive(&fmask_lock);
    return tex;
}

/* SRV com a FMASK emulada (FM5 e FM6: create/srv = originais do Azimuth para CreateTexture2D/CreateShaderResourceView) */
long srv_fmask(void *dev, void *res, const UINT *desc, void **out, tex2d_fn create, srv_fn srv)
{
    const uint8_t *r = res;
    UINT smp = r ? *(const UINT *)(r + 0x84) : 0;
    if (desc && r && desc[1] == 4 /* TEXTURE2D */ && *(const UINT *)(r + 0x38) == 0xc && *(const UINT *)(r + 0xe4) == 1 &&
        ((desc[0] == 62 && smp == 2) || (desc[0] == 42 && smp == 4))) {
        void *f = fmask_emul(dev, create, *(const UINT *)(r + 0x70), *(const UINT *)(r + 0x74), smp, desc[0]);
        if (f) {
            UINT v[8] = { desc[0], 4, 0, 1 };   /* Texture2D {MostDetailedMip 0, MipLevels 1} */
            return srv(dev, f, v, out);
        }
    }
    return srv(dev, res, desc, out);
}

/* Layouts das tabelas por tipo (nomes em 0x1400d93b0). O XDK do FH2 tem SetPrivateDataInterfaceGraphics
 * em ID3D11DeviceChild (slot 7), IDXGIObject (slot 5) e ID3D11Device (0x25). O FM5 TAMBEM tem o slot 7 nos
 * DeviceChild (0x6d3804: RTV[8] = GetResource; 0x6a5f23: Resource[8] = GetType); so o CONTEXTO nao tem (Map 0xe,
 * Azimuth 0xf; tratado pelo proxy). Nos recursos falta o GetDescriptor (Azimuth 0xb): FM5 0xb = GetDesc
 * (0x6bee40, 0x6d381c), Azimuth 0xc. DXGI igual (FM5 0x7136e0: IDXGIDevice[8] = GetAdapter, IDXGIAdapter[7] =
 * GetParent, Factory[0x11] = CreateSwapChainForCoreWindow) e device igual ate 0x32 (X em 0x33 = Azimuth 0x37).
 *   K_CHILD: D3D11DeviceChild (GetDevice 3, Get 4, Set 5, SetI 6, SPDIG 7)  -> sem deslocamento
 *   K_RES  : ID3D11Resource (8 GetType, 9/0xa EvictionPriority, 0xb GetDescriptor, 0xc GetDesc) -> FM5 s>=0xb = s+1
 *   K_DXGI : IDXGIObject (Set 3, SetI 4, SPDIG 5, Get 6)                     -> sem deslocamento
 *   K_CTX  : contextos (tipo 1 e diferido alternativo): layout tratado pelo proxy, so private data
 *   K_NONE : device (tipo 0, tem os proprios; mapeado em install_device_vtable)
 * Estados, queries, command list e DMA: sem deslocamento (NAO verificado no FM5). */
const uint8_t type_kind[NTYPES] = {
    K_NONE,  /* 0 device */
    K_CTX,   /* 1 contexto */
    K_DXGI,  /* 2 IDXGIDevice */
    K_DXGI,  /* 3 IDXGIAdapter */
    K_DXGI,  /* 4 IDXGIOutput */
    K_RES,   /* 5 recurso (Buffer/Texture*: GetType, Set/GetEvictionPriority, GetDescriptor, GetDesc) */
    K_CHILD, /* 6 DmaEngineContext (NAO verificado no FM5) */
    K_CHILD, /* 7 CommandList (GetContextFlags) */
    K_CHILD, /* 8 shaders etc. (so DeviceChild) */
    K_CHILD, /* 9 idem */
    K_CHILD, /* 0xa estados (GetDesc, GetDescX) */
    K_CHILD, /* 0xb views (GetResource, GetDesc, Get/SetFormatX) */
    K_RES,   /* 0xc recurso */
    K_RES,   /* 0xd recurso */
    K_DXGI,  /* 0xe IDXGIFactory */
    K_DXGI,  /* 0xf IDXGISwapChain */
    K_RES,   /* 0x10 recurso */
    K_CHILD, /* 0x11 Query/Asynchronous (GetDataSize, GetDesc) */
};

static void d3d_dropped_slot(void)
{
    wlog("d3d: a slot dropped from the Azimuth layout was called (no equivalent in the game's XDK)");
    RaiseException(0xE0F50002, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

/* tira um slot que o FM5 nao tem: FM5 s>=first = Azimuth s+1 (a tabela e alterada no lugar;
 * o Azimuth reconhece os objetos pelo ponteiro da vtable / tipo em +0x38, nao pelo conteudo) */
void drop_slot(void **t, int first)
{
    memmove(&t[first], &t[first + 1], (SLOTS - first - 1) * sizeof *t);
    t[SLOTS - 1] = (void *)d3d_dropped_slot;
}
