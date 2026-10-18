/*
 * Ponte D3D11 do FM5 sobre o D3D11.X do Azimuth (PASSO 8 no docs\contexto\10_d3d_device_contexto_vtables.txt)
 *  - d3d11_x.dll!D3D11CreateDevice (o Azimuth so tem D3D11XCreateDeviceX) via hook no resolvedor de imports
 *  - device: vtable trocada pelo layout do XDK do FM5 (sem os 4 metodos do ID3D11Device2: X -4 slots)
 *  - contexto: objeto proxy com vtable + tabela embutida em +0x28 no layout do FM5 (sem os 6 metodos
 *    de tiles do ID3D11DeviceContext2: X -6 slots); o FH2 usa a tabela embutida em +0x3d0, o FM5 em +0x28,
 *    que no contexto do Azimuth e estado interno -> por isso o proxy (d3d_thunks.S)
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern uint8_t *g_base;
extern uint8_t *g_game;
extern void wlog(const char *fmt, ...);
extern int is_fm5(void);
extern int is_fm6(void);
extern void game_tls_apply(void);

/* ---- RVAs no azimuth.exe ---- */
#define RVA_RESOLVER       0x3a290   /* FARPROC resolve(ImportEntry*) : {std::string dll; std::string nome} */
#define RESOLVER_STOLEN    15        /* 3 x mov [rsp+..], reg */
#define RVA_CREATE_DEVICEX 0x58090   /* HLE de D3D11XCreateDeviceX(desc, ppDevice, ppContext) */

#define SLOTS        0x10e           /* tabelas de metodos do Azimuth: 0x870 bytes */
#define DEV_X_FIRST  0x33            /* FM5: GetImmediateContextX ; FH2 0x37 */
#define DEV_SHIFT    4
#define CTX_REAL_OFF (0x28 + SLOTS * 8)
#define PROXY_SIZE   (CTX_REAL_OFF + 8)

/* ------------------------------------------------------------------------ */
/* std::string do MSVC (x64): buf/ptr em +0, tamanho em +0x10, capacidade em +0x18 */
static const char *msvc_str(const uint8_t *s, size_t *len)
{
    *len = *(const size_t *)(s + 0x10);
    return *(const size_t *)(s + 0x18) > 15 ? *(const char *const *)s : (const char *)s;
}

static int str_is(const uint8_t *s, const char *want)
{
    size_t n, w = strlen(want);
    const char *p = msvc_str(s, &n);
    return n == w && memcmp(p, want, n) == 0;
}

/* ------------------------------------------------------------------------ */
/* Proxy do contexto                                                          */
/* ------------------------------------------------------------------------ */
extern void *ctx_proxy_vtbl[SLOTS];

static SRWLOCK proxy_lock = SRWLOCK_INIT;
static struct { void *real, *proxy; } proxies[64];
static int nproxies;

static int proxy_index(void *p)
{
    int i;
    if (!p) return -1;
    for (i = 0; i < nproxies; i++) if (proxies[i].proxy == p || proxies[i].real == p) return i;
    return -1;
}

static void *proxy_of(void *real)
{
    uint8_t *p = NULL;
    int i;
    if (!real) return NULL;
    AcquireSRWLockExclusive(&proxy_lock);
    for (i = 0; i < nproxies; i++)
        if (proxies[i].real == real) { p = proxies[i].proxy; break; }
    if (!p && nproxies < (int)(sizeof proxies / sizeof proxies[0])) {
        p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PROXY_SIZE);
        if (p) {
            *(void **)p = ctx_proxy_vtbl;
            memcpy(p + 8, (uint8_t *)real + 8, 0x20);          /* cabecalho: chute, ver PASSO 8 */
            memcpy(p + 0x28, ctx_proxy_vtbl, SLOTS * 8);        /* tabela embutida do FM5 */
            *(void **)(p + CTX_REAL_OFF) = real;
            proxies[nproxies].real = real; proxies[nproxies].proxy = p; nproxies++;
            wlog("d3d: contexto %p -> proxy %p (vtbl %p Draw %p DrawIndexed %p Map %p tipo %#x)", real, p, *(void **)real,
                 (*(void ***)real)[0xe], (*(void ***)real)[0xd], (*(void ***)real)[0xf], *(UINT *)((uint8_t *)real + 0x38));
        }
    }
    ReleaseSRWLockExclusive(&proxy_lock);
    if (!p) { wlog("d3d: sem proxy para o contexto %p", real); return real; }
    return p;
}

typedef long (*qi_fn)(void *self, const GUID *iid, void **out);

/* chamados pelos thunks com rcx = contexto real */
long ctx_wrap_qi(void *real, const GUID *iid, void **out)
{
    long hr = ((qi_fn)(*(void ***)real)[0])(real, iid, out);
    if (hr >= 0 && out && *out == real) *out = proxy_of(real);
    return hr;
}

/* FM5 0x18 = IASetPrimitiveTopology. No XDK do FH2 e inline (por isso a vtable do Azimuth nao tem):
 * ctx+0x170 = pacote PM4 da topologia, ctx+0x168 |= 0x46 (o Draw confere/limpa, 0x14012b800).
 * Pacote montado a partir da D3D11_PRIMITIVE_TOPOLOGY: dword baixo = cabecalho fixo; dword alto =
 *   bits 0..8   0x100 | (256 / k - 1), k = 1; 2 nas *_ADJ; n nos patch lists de n pontos de controle
 *   bits 9..15  tipo de primitiva do hardware (strip/list de triangulos trocados; 9 = patch)
 *   bits 20..   pontos de controle (patch lists)
 * Topologia indefinida (ou fora da faixa) -> tipo 0, igual a UNDEFINED. */
#define TOPO_PKT_HEADER 0xc0009e00u

static uint64_t topo_packet(UINT topo)
{
    uint32_t prim, k = 1, cp = 0;
    if (topo >= 33 && topo <= 64) { cp = topo - 32; k = cp; prim = 9; }
    else if (topo == 5) prim = 6;                                   /* TRIANGLESTRIP */
    else if (topo == 6) prim = 5;
    else if ((topo >= 1 && topo <= 4) || (topo >= 10 && topo <= 28)) prim = topo;
    else prim = 0;
    if (topo >= 10 && topo <= 13) k = 2;                            /* *_ADJ */
    return (uint64_t)(cp << 20 | prim << 9 | 0x100 | (256 / k - 1)) << 32 | TOPO_PKT_HEADER;
}
#define CTX_TOPO_PACKET 0x170
#define CTX_DIRTY       0x168
#define CTX_DIRTY_TOPO  0x46

void ctx_IASetPrimitiveTopology(void *real, UINT topo)
{
    uint8_t *c = real;
    *(uint64_t *)(c + CTX_TOPO_PACKET) = topo_packet(topo);
    *(volatile uint32_t *)(c + CTX_DIRTY) |= CTX_DIRTY_TOPO;
}

/* FM5 0x9a..0xad = X SetFast* (FH2 0xa0..0xb3). O Azimuth so tem o stub fatal nesses slots (o FH2 usa
 * SetFastResources); aqui viram o Set* padrao com 1 elemento. Slots no layout do Azimuth (com o slot 7):
 * VSSetConstantBuffers 8, PSSetShaderResources 9, PSSetSamplers 0xb, PSSetConstantBuffers 0x11,
 * IASetVertexBuffers 0x13, IASetIndexBuffer 0x14, GSSetConstantBuffers 0x17, VS SRV/S 0x19/0x1a,
 * GS SRV/S 0x1f/0x20, HS SRV/S/CB 0x3b/0x3d/0x3e, DS 0x3f/0x41/0x42, CS SRV 0x43, S 0x46, CB 0x47.
 * O Set* padrao segura uma referencia (o Fast nao); inofensivo. */
typedef void (*set_fn)(void *ctx, UINT start, UINT n, void *const *pp);

#define FAST(name, azslot) \
    void ctx_##name(void *real, UINT slot, void *obj) { \
        ((set_fn)(*(void ***)real)[azslot])(real, slot, 1, &obj); }

/* PSGetSamplers (FM5 = Azimuth 0x4b): stub fatal no Azimuth ("STOPPED ID3D11DeviceContextX::PSGetSamplers" em
 * game+0xb598b3, no menu de pausa da corrida) -> o proxy guarda os samplers do PS por contexto (PSSetSamplers FM5 0xa
 * = Azimuth 0xb e PSSetFastSampler); ClearState zera. Os samplers do FM5 sao criados uma vez e nao liberados. */
#define NSAMP 16
static void *ps_samplers[64][NSAMP];
static int proxy_index(void *p);
static void ps_samp_store(void *real, UINT start, UINT n, void *const *pp)
{
    int ci = proxy_index(real);
    UINT i;
    if (ci < 0 || ci >= 64) return;
    for (i = 0; i < n && start + i < NSAMP; i++) ps_samplers[ci][start + i] = pp ? pp[i] : NULL;
}

void ctx_PSSetSamplers(void *real, UINT start, UINT n, void *const *pp)
{
    ps_samp_store(real, start, n, pp);
    ((set_fn)(*(void ***)real)[0x0b])(real, start, n, pp);
}

void ctx_PSSetFastSampler(void *real, UINT slot, void *obj)
{
    ps_samp_store(real, slot, 1, &obj);
    ((set_fn)(*(void ***)real)[0x0b])(real, slot, 1, &obj);
}

void ctx_PSGetSamplers(void *real, UINT start, UINT n, void **pp)
{
    static volatile LONG logged;
    int ci = proxy_index(real);
    UINT i;
    if (!pp) return;
    for (i = 0; i < n; i++) {
        void *v = ci >= 0 && ci < 64 && start + i < NSAMP ? ps_samplers[ci][start + i] : NULL;
        if (v) ((ULONG (*)(void *))(*(void ***)v)[1])(v);      /* Get* devolve com AddRef */
        pp[i] = v;
    }
    if (InterlockedIncrement(&logged) <= 10)
        wlog("d3d: PSGetSamplers c%d start=%u n=%u -> %p", ci, start, n, n ? pp[0] : NULL);
}

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

/* IASetIndexBuffer do Azimuth (slot 0x14 = 0x14011c5f0, XDK do FH2) = (HardwareIndexFormat, buffer, offset):
 * 0 = 16 bits, 1 = 32 bits (validador 0x1401b3640: > 1 -> "graphics Xbox index format="). O FM5 usa a forma
 * padrao (buffer, DXGI_FORMAT, offset) no slot 0x13 (FM5 0xb5f09f: (ib, 57, 0)). */
typedef void (*setib_fn)(void *ctx, UINT hwfmt, void *buf, UINT off);
void ctx_IASetIndexBuffer(void *real, void *buf, UINT fmt, UINT off)
{
    UINT hw = fmt == 42 /* R32_UINT */ ? 1 : fmt == 57 /* R16_UINT */ || fmt == 0 ? 0 : fmt;
    ((setib_fn)(*(void ***)real)[0x14])(real, hw, buf, off);
}

/* IASetFastIndexBuffer(HardwareIndexFormat, buffer): offset 0 */
void ctx_IASetFastIndexBuffer(void *real, UINT hwfmt, void *buf)
{
    ((setib_fn)(*(void ***)real)[0x14])(real, hwfmt, buf, 0);
}

/* Draw* indexados/instanciados do Azimuth (XDK do FH2) recebem argumentos EMPACOTADOS em 64 bits
 * (0x14012be10 / 0x14012bf90 / 0x14012c120; os diferidos 0x14012b940 / 0x12ba20 / 0x12bae0 desempacotam):
 *   DrawIndexed(u64 {lo StartIndex, hi IndexCount}, Base)
 *   DrawInstanced(VertexCount, u64 {lo StartVertex, hi StartInstance}, InstanceCount)
 *   DrawIndexedInstanced(u64 {lo StartIndex, hi IndexCount}, u64 {lo Base, hi StartInstance}, InstanceCount)
 * O FM5 chama na forma padrao do D3D11 -> sem conversao o IndexCount virava 0 (tela preta no menu).
 * Draw(VertexCount, StartVertex) e igual nos dois. */
#define PACK(lo, hi) ((UINT64)(UINT)(lo) | (UINT64)(UINT)(hi) << 32)
typedef void (*drawidx_fn)(void *ctx, UINT64 a, INT base);
typedef void (*drawinst_fn)(void *ctx, UINT vc, UINT64 b, UINT ic);
typedef void (*drawidxinst_fn)(void *ctx, UINT64 a, UINT64 b, UINT ic);

void ctx_DrawIndexed(void *real, UINT count, UINT start, INT base)
{
    ((drawidx_fn)(*(void ***)real)[0x0d])(real, PACK(start, count), base);
}

void ctx_DrawInstanced(void *real, UINT vc, UINT ic, UINT sv, UINT si)
{
    ((drawinst_fn)(*(void ***)real)[0x16])(real, vc, PACK(sv, si), ic);
}

void ctx_DrawIndexedInstanced(void *real, UINT count, UINT ic, UINT start, INT base, UINT si)
{
    ((drawidxinst_fn)(*(void ***)real)[0x15])(real, PACK(start, count), PACK(base, si), ic);
}

void ctx_bad_slot(void *real, unsigned slot)
{
    wlog("d3d: contexto %p chamou o slot %#x do FM5, sem equivalente no Azimuth", real, slot);
    RaiseException(0xE0F50001, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

/* ------------------------------------------------------------------------ */
/* Device                                                                     */
/* ------------------------------------------------------------------------ */
void *dev_impl[SLOTS];          /* layout do FM5; [dev] = dev_thunk_vtbl (contam e saltam p/ ca) */
extern void *dev_thunk_vtbl[SLOTS];
static void **dev_orig;   /* tabela do Azimuth (layout do FH2) */

typedef long (*getctx_fn)(void *dev, void **out);
typedef long (*defctx_fn)(void *dev, UINT flags, void **out);

#define GETCTX(name, fh2slot) \
    static long name(void *dev, void **out) { \
        long hr = ((getctx_fn)dev_orig[fh2slot])(dev, out); \
        if (out && *out) *out = proxy_of(*out); \
        return hr; }
static UINT def_flags_override(UINT flags)
{
    char v[16] = "";
    if (flags == 0 && GetEnvironmentVariableA("FM5_DEFFLAGS", v, sizeof v)) return strtoul(v, NULL, 0);
    return flags;
}
/* FM5_TRACE_BUNDLES=1: trace detalhado SEMPRE nos draw bundles (flags 0x60000), alem dos frames de trace_frames */
static int trace_bundles = -1;
static uint8_t bundle_ctx[64];
volatile LONG ctx_trace_on;
static volatile LONG frame_trace_on;
static void trace_bundles_init(void)
{
    char v[8] = "";
    if (trace_bundles >= 0) return;
    GetEnvironmentVariableA("FM5_TRACE_BUNDLES", v, sizeof v);
    trace_bundles = atoi(v) != 0;
    if (trace_bundles) { ctx_trace_on = 1; wlog("d3d: FM5_TRACE_BUNDLES=1 (trace de todos os draw bundles)"); }
}
#define DEFCTX(name, fh2slot) \
    static long name(void *dev, UINT flags, void **out) { \
        long hr = ((defctx_fn)dev_orig[fh2slot])(dev, flags = def_flags_override(flags), out); \
        int ci; \
        trace_bundles_init(); \
        if (hr >= 0 && out && *out) *out = proxy_of(*out); \
        ci = hr >= 0 && out ? proxy_index(*out) : -1; \
        if (ci >= 0 && (flags & 0x60000)) bundle_ctx[ci] = 1; \
        wlog("d3d: " #name " flags=%#x hr=%#lx -> c%d", flags, hr, ci); \
        return hr; }

GETCTX(dev_GetImmediateContext,  0x29)
GETCTX(dev_GetImmediateContext1, 0x2c)
GETCTX(dev_GetImmediateContextX, 0x37)
DEFCTX(dev_CreateDeferredContext,  0x1b)
DEFCTX(dev_CreateDeferredContext1, 0x2d)
DEFCTX(dev_CreateDeferredContextX, 0x44)

/* FM5 0x37 = CreateDmaEngineContext (FH2 0x3b). O descritor do XDK do FM5 tem 2 campos
 * {CreateFlags, RingBufferSizeBytes}; o do Azimuth tem 3 e exige [2] == 0 -> lixo de pilha no FM5 */
typedef long (*dmactx_fn)(void *dev, const UINT *desc, void **out);
static long dev_CreateDmaEngineContext(void *dev, const UINT *desc, void **out)
{
    UINT d[4] = { 0 };
    long hr;
    if (!desc) return ((dmactx_fn)dev_orig[0x3b])(dev, desc, out);
    d[0] = desc[0]; d[1] = desc[1];
    hr = ((dmactx_fn)dev_orig[0x3b])(dev, d, out);
    wlog("d3d: CreateDmaEngineContext(flags=%#x ring=%#x) hr=%#lx dma=%p", d[0], d[1], hr, out ? *out : NULL);
    return hr;
}

/* slot 3 CreateBuffer. D3D11_BUFFER_DESC do XDK do FM5 = 6 dwords padrao + ESRAMOffsetBytes [6]; o do FH2 tem tambem
 * ESRAMUsageBytes [7], que o Azimuth (0x1400e8700) le com misc 0x20000 (ESRAM): [7] != 0 e < ByteWidth -> "CreateBuffer
 * partly in ESRAM". No FM5 o [7] e lixo de pilha (0 por sorte; com o RenderDoc injetado vira 0x29f -> STOPPED em
 * game+0x7131b3). Copia os 7 dwords do FM5 e [7] = 0 (buffer inteiro na ESRAM). */
typedef long (*buf_fn)(void *dev, const UINT *desc, const void *init, void **out);
static long dev_CreateBuffer(void *dev, const UINT *desc, const void *init, void **out)
{
    UINT d[8];
    if (!desc || !(desc[4] & 0x20000)) return ((buf_fn)dev_orig[3])(dev, desc, init, out);
    memcpy(d, desc, 7 * sizeof(UINT));
    d[7] = 0;
    return ((buf_fn)dev_orig[3])(dev, d, init, out);
}

/* slot 5 CreateTexture2D. Com --msaa "do console" (4) o Azimuth (0x1400e74f0) repassa o SampleDesc do jogo
 * ao D3D12; o FM5 pede Quality == Count (ex.: 2x -> {2, 2}), invalido no PC -> E_INVALIDARG em
 * CreateCommittedResource. Quality vira D3D12_STANDARD_MULTISAMPLE_PATTERN (o que o Azimuth usa nos outros
 * modos). Desc do FM5 = D3D11_TEXTURE2D_DESC (11 dwords) + ESRAM offset [0xb] (lido pelo Azimuth com misc 0x20000) */
typedef long (*tex2d_fn)(void *dev, const UINT *desc, const void *init, void **out);

/* EMULACAO DO ALIASING (padrao ligado; FM5_ALIAS_TEST=0 desliga; PASSO 8 cont. 19, CONFIRMADO: a cena 3D aparece): a cena 3D e desenhada em cor 2x (DRAM) + profundidade 2x
 * (ESRAM 0), mas o compute de profundidade le uma 2a textura de profundidade no MESMO offset da ESRAM e o
 * pos-processamento le uma cor 1x (ESRAM 0xc00000) que nada escreve. Guarda as ultimas texturas de tela e:
 *   - no Dispatch do compute de profundidade (c0, (W/8, H/8, 1)): CopyResource(depth_b <- depth_a)
 *   - no ClearState seguinte do c0 (inicio do pos-processamento): ResolveSubresource(cor1x <- cor2x) */
static int alias_test = -1;
static SRWLOCK alias_lock = SRWLOCK_INIT;
static void *al_color2x, *al_depth_a, *al_depth_b, *al_color1x;
static UINT al_w, al_h;
static volatile LONG al_pending, al_count;
static void al_set(void **slot, void *tex)
{
    if (tex) ((ULONG (*)(void *))(*(void ***)tex)[1])(tex);
    if (*slot) ((ULONG (*)(void *))(*(void ***)*slot)[2])(*slot);
    *slot = tex;
}
static void alias_track(const UINT *desc, void *tex)
{
    if (alias_test < 0) { char v[8] = ""; alias_test = GetEnvironmentVariableA("FM5_ALIAS_TEST", v, sizeof v) ? atoi(v) : 1;
                          wlog("d3d: FM5_ALIAS_TEST=%d", alias_test); }
    /* > 2.5:1 = espelhos (768x256 escalados pelo reflections high/ultra, cont. 6), nao sao texturas de tela */
    if (alias_test <= 0 || !tex || desc[0] < 1280 || desc[0] * 2 > desc[1] * 5) return;
    AcquireSRWLockExclusive(&alias_lock);
    /* MSAA 2x (corrida) ou 4x (garagem/menus com carro, cont. 21: mesmo padrao, profundidades sem ESRAM) */
    if (desc[5] >= 2 && desc[4] == 26) { al_set(&al_color2x, tex); al_w = desc[0]; al_h = desc[1]; al_set(&al_color1x, NULL); }
    else if (desc[5] >= 2 && desc[4] == 19) { al_set(&al_depth_a, al_depth_b); al_set(&al_depth_b, tex); }
    else if (desc[5] <= 1 && desc[4] == 26 && (desc[10] & 0x20000) && !al_color1x && al_color2x &&
             desc[0] == al_w && desc[1] == al_h) al_set(&al_color1x, tex);
    ReleaseSRWLockExclusive(&alias_lock);
    wlog("d3d: alias: cor2x=%p prof=%p,%p cor1x=%p (%ux%u)", al_color2x, al_depth_a, al_depth_b, al_color1x, al_w, al_h);
}

/* Quality original das texturas MSAA do FM5 (o D3D12 recebe D3D12_STANDARD_MULTISAMPLE_PATTERN, abaixo). O GetDesc
 * (FM5 slot 0xb) devolve ao JOGO o valor que ele pediu: o resolve MSAA do FM5 (0x6de160) escolhe o caminho pelo
 * SampleDesc lido via GetDesc (0x6a5ee0): Quality 0 -> ResolveSubresource; {2,2} / {4,3} -> shader com a FMASK;
 * qualquer outro valor -> sai sem desenhar (thumbnails da garagem/pintura pretos, PASSO 8 cont. 23). */
#define FM5_IMAGE_SIZE 0x279d000
static SRWLOCK msaa_q_lock = SRWLOCK_INIT;
static struct { void *tex; UINT q; } msaa_q[256];
static unsigned msaa_q_next;

static void msaa_q_set(void *tex, UINT q)
{
    unsigned i;
    AcquireSRWLockExclusive(&msaa_q_lock);
    for (i = 0; i < 256 && msaa_q[i].tex != tex; i++) ;
    if (i == 256) i = msaa_q_next++ & 255;
    msaa_q[i].tex = tex; msaa_q[i].q = q;
    ReleaseSRWLockExclusive(&msaa_q_lock);
}

static int msaa_q_get(void *tex, UINT *q)
{
    unsigned i;
    int found = 0;
    AcquireSRWLockShared(&msaa_q_lock);
    for (i = 0; i < 256; i++) if (msaa_q[i].tex == tex) { *q = msaa_q[i].q; found = 1; break; }
    ReleaseSRWLockShared(&msaa_q_lock);
    return found;
}

typedef void (*tex2d_getdesc_fn)(void *tex, UINT *d);
static tex2d_getdesc_fn orig_tex2d_getdesc;
static void tex2d_GetDesc(void *tex, UINT *d)
{
    const uint8_t *ret = __builtin_return_address(0);
    UINT q;
    orig_tex2d_getdesc(tex, d);
    if (d && d[5] > 1 && d[6] == 0xffffffff && ret >= g_game && ret < g_game + FM5_IMAGE_SIZE && msaa_q_get(tex, &q))
        d[6] = q;
}

static long dev_CreateTexture2D(void *dev, const UINT *desc, const void *init, void **out)
{
    UINT d[16] = { 0 };
    long hr;
    if (desc && desc[0] == 1920 && desc[1] == 1080) {
        /* o codigo do jogo nao tem unwind registrado: varre a pilha atras de enderecos no .text do FM5 */
        const uintptr_t *sp = (const uintptr_t *)__builtin_frame_address(0);
        char buf[256]; int k, nb = 0, n = 0;
        for (k = 0; k < 0x300 && n < 8; k++)
            if (sp[k] > 0x140001000 && sp[k] < 0x141400000) {
                nb += snprintf(buf + nb, sizeof buf - nb, " %#llx", (unsigned long long)(sp[k] - 0x140000000)); n++;
            }
        wlog("d3d:   pilha (game+):%s", n ? buf : " -");
    }
    if (desc && desc[0] >= 1280 && desc[5] <= 1) {     /* diagnostico: texturas de tela (ESRAM/aliasing) */
        hr = ((tex2d_fn)dev_orig[5])(dev, desc, init, out);
        wlog("d3d: CreateTexture2D %ux%u fmt=%u samples=%u bind=%#x misc=%#x esram[0xb]=%#x -> hr=%#lx tex=%p ret=%p", desc[0],
             desc[1], desc[4], desc[5], desc[8], desc[10], desc[11], hr, out ? *out : NULL, __builtin_return_address(0));
        if (hr >= 0 && out) alias_track(desc, *out);
        return hr;
    }
    if (!desc || desc[5] <= 1 || desc[6] == 0 || desc[6] == 0xffffffff)
        return ((tex2d_fn)dev_orig[5])(dev, desc, init, out);
    memcpy(d, desc, 12 * sizeof(UINT));
    d[6] = 0xffffffff;
    hr = ((tex2d_fn)dev_orig[5])(dev, d, init, out);
    if (hr >= 0 && out && *out) msaa_q_set(*out, desc[6]);
    wlog("d3d: CreateTexture2D %ux%u fmt=%u samples=%u quality %#x -> padrao, bind=%#x misc=%#x esram[0xb]=%#x hr=%#lx tex=%p ret=%p",
         d[0], d[1], d[4], d[5], desc[6], d[8], d[10], d[11], hr, out ? *out : NULL, __builtin_return_address(0));
    if (hr >= 0 && out) alias_track(desc, *out);
    return hr;
}

/* slot 7 CreateShaderResourceView. View R8_UINT/TEXTURE2D de uma textura MSAA = leitura da FMASK. O Azimuth
 * (0x14013dc40) emula a FMASK so p/ 4x (R8_UINT) e 8x (R32_UINT): cria uma textura com o mapeamento identidade
 * (amostra i -> fragmento i) e aponta a view para ela. O FM5 usa 2x -> o Azimuth repassava a view ao D3D12 sobre
 * a textura R11G11B10 (invalido -> device removido). Aqui (fmask_emul): 2x R8_UINT (1 bit/amostra) e 4x R32_UINT
 * (4 bits/amostra; cena apos "continuar" no fim da corrida, cont. 20), mapeamento conforme as amostras do host.
 * Objeto de textura do Azimuth: tipo u32 +0x38 (0xc = Texture2D), desc copiado em +0x70 (W, H, mips, arr, fmt,
 * samples +0x84), tamanho do array +0xe4. */
typedef long (*srv_fn)(void *dev, void *res, const UINT *desc, void **out);
static SRWLOCK fmask_lock = SRWLOCK_INIT;
static struct { UINT w, h, samples, fmt; void *tex; } fmasks[16];
static int nfmasks;

/* amostras reais do host p/ uma textura MSAA do jogo (= 0x1400e74c0): --msaa "do console" (DAT_140318ea0 == 4)
 * repassa as do jogo; senao a configuracao (1 = AA desligado) */
static UINT host_samples(UINT samples)
{
    UINT m = *(volatile UINT *)(g_base + 0x318ea0);
    return m == 4 ? samples : (m ? m : 1);
}

/* FMASK emulada: textura WxH (R8_UINT ou R32_UINT) com o mapeamento amostra -> fragmento de cada pixel, uma por
 * (tamanho, amostras, formato), nunca liberada. Bits por amostra: 2x R8 = 1, 4x R32 = 4 (o FM5 le a FMASK 4x como
 * R32_UINT; o Azimuth so emula 4x R8 e 8x R32). Mapeamento igual ao do Azimuth (0x14013dc40): host com as amostras
 * todas -> identidade; host 2x -> amostra i -> fragmento i & 1; host 1x (AA desligado) -> tudo no fragmento 0. */
static void *fmask_emul(void *dev, tex2d_fn create, UINT w, UINT h, UINT samples, UINT fmt)
{
    UINT d[16] = { 0 };
    struct { const void *mem; UINT pitch, slice; } init;
    void *tex = NULL;
    uint8_t *buf;
    UINT host = host_samples(samples), bpp = fmt == 42 ? 4 : 1, bits = fmt == 42 ? 4 : (samples == 2 ? 1 : 2);
    UINT val = 0, i;
    SIZE_T k, n = (SIZE_T)w * h;
    long hr;
    int j;
    for (i = 0; i < samples; i++)
        val |= (host >= samples ? i : host == 2 ? (i & 1) : 0) << (i * bits);
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
    wlog("d3d: FMASK %ux emulada %ux%u fmt %u (host %ux, valor %#x) hr=%#lx tex=%p", samples, w, h, fmt, host, val, hr, tex);
    if (hr < 0) tex = NULL;
    else if (nfmasks < 16) {
        fmasks[nfmasks].w = w; fmasks[nfmasks].h = h; fmasks[nfmasks].samples = samples; fmasks[nfmasks].fmt = fmt;
        fmasks[nfmasks++].tex = tex;
    }
done:
    ReleaseSRWLockExclusive(&fmask_lock);
    return tex;
}

static long dev_CreateShaderResourceView_(void *dev, void *res, const UINT *desc, void **out);
static long dev_CreateShaderResourceView(void *dev, void *res, const UINT *desc, void **out)
{
    long hr = dev_CreateShaderResourceView_(dev, res, desc, out);
    const uint8_t *r = res;
    wlog("d3d: CreateShaderResourceView res=%p (tipo %#x %ux%u fmt %u placement %p) view fmt=%u dim=%u -> hr=%#lx srv=%p",
         res, r ? *(const UINT *)(r + 0x38) : 0, r ? *(const UINT *)(r + 0x70) : 0, r ? *(const UINT *)(r + 0x74) : 0,
         r ? *(const UINT *)(r + 0x80) : 0, r ? *(void *const *)(r + 0x20) : 0, desc ? desc[0] : 0, desc ? desc[1] : 0,
         hr, out ? *out : NULL);
    return hr;
}

/* FM5 0x3c = CreatePlacementTexture2D (Azimuth 0x40): (desc, TileMode, Pitch, endereco, out) */
typedef long (*ptex2d_fn)(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out);
static long dev_CreatePlacementTexture2D(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out)
{
    long hr = ((ptex2d_fn)dev_orig[0x40])(dev, desc, tile, pitch, addr, out);
    MEMORY_BASIC_INFORMATION mi = {0};
    void *pages[4]; ULONG_PTR np = 4; ULONG gran = 0; UINT ww;
    VirtualQuery(addr, &mi, sizeof mi);
    ww = GetWriteWatch(0, mi.AllocationBase, 0x1000, pages, &np, &gran);
    wlog("d3d:   write watch da alocacao: %s (err %lu) regiao %p..+%#zx", ww == 0 ? "SIM" : "NAO", ww ? GetLastError() : 0,
         mi.AllocationBase, (size_t)mi.RegionSize);
    wlog("d3d: CreatePlacementTexture2D %ux%u mips=%u arr=%u fmt=%u samples=%u usage=%u bind=%#x cpu=%#x misc=%#x tile=%u "
         "pitch=%u addr=%p (alloc %p type %#lx prot %#lx) -> hr=%#lx tex=%p", desc[0], desc[1], desc[2], desc[3], desc[4],
         desc[5], desc[7], desc[8], desc[9], desc[10], tile, pitch, addr, mi.AllocationBase, mi.Type, mi.Protect, hr,
         out ? *out : NULL);
    return hr;
}

/* SRV com a FMASK emulada (FM5 e FM6: create/srv = originais do Azimuth para CreateTexture2D/CreateShaderResourceView) */
static long srv_fmask(void *dev, void *res, const UINT *desc, void **out, tex2d_fn create, srv_fn srv)
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

static long dev_CreateShaderResourceView_(void *dev, void *res, const UINT *desc, void **out)
{
    return srv_fmask(dev, res, desc, out, (tex2d_fn)dev_orig[5], (srv_fn)dev_orig[7]);
}

typedef long (*state_fn)(void *dev, const UINT *desc, void **out);
static long dev_CreateRasterizerState(void *dev, const UINT *d, void **out)
{
    long hr = ((state_fn)dev_orig[0x16])(dev, d, out);
    wlog("d3d: CreateRasterizerState fill=%u cull=%u ccw=%u bias=%d clip=%u scissor=%u msaa=%u aa=%u -> %p", d[0], d[1],
         d[2], (int)d[3], d[6], d[7], d[8], d[9], out ? *out : NULL);
    return hr;
}
static long dev_CreateBlendState(void *dev, const UINT *d, void **out)
{
    long hr = ((state_fn)dev_orig[0x14])(dev, d, out);
    wlog("d3d: CreateBlendState a2c=%u indep=%u rt0 {en=%u src=%u dst=%u op=%u srcA=%u dstA=%u opA=%u mask=%#x} -> %p",
         d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], out ? *out : NULL);
    return hr;
}

typedef long (*rtv_fn)(void *dev, void *res, const UINT *desc, void **out);
static void *sc_buffer, *sc_rtv;   /* backbuffer (GetBuffer) e a 1a RTV criada sobre ele */
static int dbg_clear = -1;         /* FM5_DEBUG_CLEAR: 1 = backbuffer vermelho no Present; 2 = RT anterior verde */
static long dev_CreateRenderTargetView(void *dev, void *res, const UINT *desc, void **out)
{
    long hr = ((rtv_fn)dev_orig[9])(dev, res, desc, out);
    if (hr >= 0 && res && res == sc_buffer && !sc_rtv && out) sc_rtv = *out;
    const uint8_t *r = res;
    wlog("d3d: CreateRenderTargetView res=%p (tipo %#x %ux%u fmt %u) desc fmt=%u dim=%u -> hr=%#lx rtv=%p", res,
         r ? *(const UINT *)(r + 0x38) : 0, r ? *(const UINT *)(r + 0x70) : 0, r ? *(const UINT *)(r + 0x74) : 0,
         r ? *(const UINT *)(r + 0x80) : 0, desc ? desc[0] : 0, desc ? desc[1] : 0, hr, out ? *out : NULL);
    return hr;
}

typedef long (*getbuf_fn)(void *sc, UINT i, const GUID *iid, void **out);
static getbuf_fn orig_sc_getbuffer;
static long sc_GetBuffer(void *sc, UINT i, const GUID *iid, void **out)
{
    long hr = orig_sc_getbuffer(sc, i, iid, out);
    if (hr >= 0 && out && i == 0 && !sc_buffer) sc_buffer = *out;
    wlog("d3d: swapchain %p GetBuffer(%u) -> hr=%#lx buf=%p", sc, i, hr, out ? *out : NULL);
    return hr;
}

static void dev_bad_slot(void)
{
    wlog("d3d: device chamou um slot do FM5 sem equivalente no Azimuth");
    RaiseException(0xE0F50002, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

static void install_device_vtable(void *dev)
{
    int s;
    if (!dev_orig) {
        dev_orig = *(void ***)dev;
        for (s = 0; s < SLOTS; s++) {
            int src = s < DEV_X_FIRST ? s : s + DEV_SHIFT;
            dev_impl[s] = src < SLOTS ? dev_orig[src] : (void *)dev_bad_slot;
        }
        /* slots do FM5 que devolvem contextos -> proxy */
        dev_impl[3] = (void *)dev_CreateBuffer;
        dev_impl[5] = (void *)dev_CreateTexture2D;
        dev_impl[7] = (void *)dev_CreateShaderResourceView;
        dev_impl[9] = (void *)dev_CreateRenderTargetView;
        dev_impl[0x14] = (void *)dev_CreateBlendState;
        dev_impl[0x16] = (void *)dev_CreateRasterizerState;
        dev_impl[0x40 - DEV_SHIFT] = (void *)dev_CreatePlacementTexture2D;     /* FM5 0x3c */
        dev_impl[0x1b] = (void *)dev_CreateDeferredContext;
        dev_impl[0x29] = (void *)dev_GetImmediateContext;
        dev_impl[0x2c] = (void *)dev_GetImmediateContext1;
        dev_impl[0x2d] = (void *)dev_CreateDeferredContext1;
        dev_impl[0x37 - DEV_SHIFT] = (void *)dev_GetImmediateContextX;          /* FM5 0x33 */
        dev_impl[0x44 - DEV_SHIFT] = (void *)dev_CreateDeferredContextX;        /* FM5 0x40 */
        dev_impl[0x3b - DEV_SHIFT] = (void *)dev_CreateDmaEngineContext;        /* FM5 0x37 */
    }
    /* o Azimuth identifica o device pelo tipo em +0x38, nao pela vtable (0x1400d9250) */
    *(void ***)dev = dev_thunk_vtbl;
}

/* ------------------------------------------------------------------------ */
/* Private data (ID3D11DeviceChild slots 4/5/6)                               */
/* ------------------------------------------------------------------------ */
/* So o device (tipo 0) implementa; nos outros tipos os slots sao o stub fatal padrao (0x1400f4b00).
 * O FM5 grava nos shaders {GUID 0a51ffd4-..: ptr da copia do bytecode, 8B} e {GUID @0x178b9d0: tamanho,
 * 4B} (0x6a2ba0) e le de volta na reflexao (0x6a3bb0) e no destrutor (0x6bb040). */
#define RVA_VTBL_TABLES   0x37d1e0   /* tipo t: +t*0x870 (montadas por 0x1400f36d0) */
#define RVA_VTBL_GET      0xf47a0    /* garante a montagem e devolve a tabela do tipo */
#define RVA_VTBL_ALTCTX   0xf4710    /* contexto diferido alternativo */
#define NTYPES            0x12
#define RVA_STUB_GETPD    0xf6330    /* stubs padrao dos slots 4, 5, 6 */
#define RVA_STUB_SETPD    0xf6430
#define RVA_STUB_SETPDI   0xf6530
#define DXGI_ERROR_NOT_FOUND_ 0x887A0002L
#define DXGI_ERROR_MORE_DATA_ 0x887A0003L

/* Release (0x1400da9e0) com hook no inicio (cont. 22): o Azimuth tambem chama o Release DIRETO (~40 sites:
 * estado do contexto, fila de destruicao...), sem passar pela vtable -> a ultima referencia caia fora do
 * pd_Release antigo e os dados ficavam orfaos. O hook pega todos. */
#define RVA_RELEASE       0xda9e0
#define RELEASE_HEX       "48895c241048896c2418488974242057"
#define RELEASE_STOLEN    16
/* bytecode dos shaders (cont. 22): o destrutor 0x6bb040 faz Get -> free(ptr) -> Release. Crash 0xC0000374
 * (heap_failure_block_not_busy = double free) em game+0x6bb08c na volta ao menu: o mesmo ptr saiu duas vezes. */
#define RVA_FM5_GUID_BYTECODE 0x1479b08   /* {0a51ffd4-060b-4bef-b593-86f1536f7218}, 8 bytes */
#define RVA_FM5_DTOR_GET_RET  0x6bb07d    /* retorno do GetPrivateData no destrutor do shader */
#define PD_BUCKETS        4096

typedef struct pdata {
    struct pdata *next;
    void *obj;
    GUID guid;
    UINT size;
    IUnknown *iface;
    void *setter;                       /* retorno do SetPrivateData (diagnostico) */
    volatile LONG taken;                /* bytecode: o destrutor do jogo ja leu (e liberou) o ptr */
    uint8_t data[];
} pdata;

static SRWLOCK pd_lock = SRWLOCK_INIT;
static pdata *pd_tab[PD_BUCKETS];
static ULONG (*orig_release)(void *obj);   /* trampolim do 0x1400da9e0 */
static volatile LONG pd_anomalies;

static unsigned pd_hash(const void *obj)
{
    uintptr_t v = (uintptr_t)obj;
    return (unsigned)(v >> 4 ^ v >> 16) & (PD_BUCKETS - 1);
}

static pdata **pd_find(void *obj, const GUID *g)
{
    pdata **pp;
    for (pp = &pd_tab[pd_hash(obj)]; *pp; pp = &(*pp)->next)
        if ((*pp)->obj == obj && IsEqualGUID(&(*pp)->guid, g)) return pp;
    return NULL;
}

static int pd_is_bytecode(const pdata *e)
{
    return e->size == 8 && !e->iface && IsEqualGUID(&e->guid, (const GUID *)(g_game + RVA_FM5_GUID_BYTECODE));
}

static uintptr_t game_off(const void *p) { return (uintptr_t)p - (uintptr_t)g_game; }

static void pd_anomaly(const char *what, const pdata *e, void *caller)
{
    LONG n = InterlockedIncrement(&pd_anomalies);
    if (n <= 30 || (n & (n - 1)) == 0)
        wlog("d3d: private data #%ld: %s obj=%p ptr=%p refs=%d tipo=%#x set em game+%#llx, chamador %p", n, what,
             e->obj, *(void *const *)e->data, *(volatile LONG *)((uint8_t *)e->obj + 8),
             *(int *)((uint8_t *)e->obj + 0x38), (unsigned long long)game_off(e->setter), caller);
}

static IUnknown *pd_unlink(pdata **pp)
{
    pdata *e = *pp;
    IUnknown *u = e->iface;
    *pp = e->next;
    HeapFree(GetProcessHeap(), 0, e);
    return u;
}

static long pd_store(void *obj, const GUID *g, UINT size, const void *data, IUnknown *iface, void *caller)
{
    pdata **pp, *e = NULL;
    IUnknown *old = NULL;
    if (!g) return E_INVALIDARG;
    if (data || iface) {
        e = HeapAlloc(GetProcessHeap(), 0, sizeof *e + (iface ? sizeof iface : size));
        if (!e) return E_OUTOFMEMORY;
        e->obj = obj; e->guid = *g; e->iface = iface; e->setter = caller; e->taken = 0;
        e->size = iface ? sizeof iface : size;
        if (iface) { iface->lpVtbl->AddRef(iface); memcpy(e->data, &iface, sizeof iface); }
        else memcpy(e->data, data, size);
    }
    AcquireSRWLockExclusive(&pd_lock);
    if ((pp = pd_find(obj, g))) {
        if (pd_is_bytecode(*pp) && !(*pp)->taken) pd_anomaly("Set sobre bytecode ainda nao liberado", *pp, caller);
        old = pd_unlink(pp);
    }
    if (e) { pp = &pd_tab[pd_hash(obj)]; e->next = *pp; *pp = e; }
    ReleaseSRWLockExclusive(&pd_lock);
    if (old) old->lpVtbl->Release(old);
    return S_OK;
}

static long pd_GetPrivateData(void *obj, const GUID *g, UINT *size, void *data)
{
    void *caller = __builtin_return_address(0);
    pdata **pp;
    long hr = S_OK;
    if (!g || !size) return E_INVALIDARG;
    AcquireSRWLockShared(&pd_lock);
    if (!(pp = pd_find(obj, g))) { *size = 0; hr = DXGI_ERROR_NOT_FOUND_; }
    else if (!data) *size = (*pp)->size;
    else if (*size < (*pp)->size) { *size = (*pp)->size; hr = DXGI_ERROR_MORE_DATA_; }
    else if (caller == g_game + RVA_FM5_DTOR_GET_RET && pd_is_bytecode(*pp) &&
             InterlockedExchange(&(*pp)->taken, 1)) {
        /* o destrutor ja levou este ptr (o jogo da free nele): devolver de novo = double free */
        pd_anomaly("2o Get do destrutor (double free evitado)", *pp, caller);
        *size = 0; hr = DXGI_ERROR_NOT_FOUND_;
    } else {
        *size = (*pp)->size;
        memcpy(data, (*pp)->data, (*pp)->size);
        if ((*pp)->iface) (*pp)->iface->lpVtbl->AddRef((*pp)->iface);
    }
    ReleaseSRWLockShared(&pd_lock);
    return hr;
}

static long pd_SetPrivateData(void *obj, const GUID *g, UINT size, const void *data)
{
    return pd_store(obj, g, size, size ? data : NULL, NULL, __builtin_return_address(0));
}

static long pd_SetPrivateDataInterface(void *obj, const GUID *g, IUnknown *iface)
{
    return pd_store(obj, g, 0, NULL, iface, __builtin_return_address(0));
}

/* hook do Release: ao chegar a 0 descarta os dados do objeto (o endereco pode ser reaproveitado).
 * Chegar a 0 com o bytecode ainda nao lido pelo destrutor = o objeto morreu com o jogo ainda segurando. */
static ULONG pd_release_hook(void *obj)
{
    ULONG n = orig_release(obj);
    if (n == 0 && pd_tab[pd_hash(obj)]) {          /* leitura sem lock: so filtra o caso comum */
        void *caller = __builtin_return_address(0);
        pdata **pp = &pd_tab[pd_hash(obj)];
        IUnknown *drop[16];
        int nd = 0, i;
        AcquireSRWLockExclusive(&pd_lock);
        while (*pp) {
            if ((*pp)->obj == obj) {
                IUnknown *u;
                if (pd_is_bytecode(*pp) && !(*pp)->taken) pd_anomaly("Release final antes do destrutor do jogo", *pp, caller);
                u = pd_unlink(pp);
                if (u && nd < 16) drop[nd++] = u;
            } else pp = &(*pp)->next;
        }
        ReleaseSRWLockExclusive(&pd_lock);
        for (i = 0; i < nd; i++) drop[i]->lpVtbl->Release(drop[i]);
    }
    return n;
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
enum { K_NONE, K_CHILD, K_RES, K_DXGI, K_CTX };
static const uint8_t type_kind[NTYPES] = {
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

static void pd_patch_table(void **t, int idx, int kind)
{
    /* slots ORIGINAIS (layout do Azimuth) de Get/Set/SetInterface; stubs padrao sao por slot */
    int get = kind == K_DXGI ? 6 : 4, set = kind == K_DXGI ? 3 : 5, seti = kind == K_DXGI ? 4 : 6;
    static void *const stub_of[7] = { 0, 0, 0, (void *)0xf6230, (void *)RVA_STUB_GETPD,
                                      (void *)RVA_STUB_SETPD, (void *)RVA_STUB_SETPDI };
    int n = 0;
    if (t[get]  == g_base + (uintptr_t)stub_of[get])  { t[get]  = (void *)pd_GetPrivateData; n++; }
    if (t[set]  == g_base + (uintptr_t)stub_of[set])  { t[set]  = (void *)pd_SetPrivateData; n++; }
    if (t[seti] == g_base + (uintptr_t)stub_of[seti]) { t[seti] = (void *)pd_SetPrivateDataInterface; n++; }
    if (n && t[2] != g_base + RVA_RELEASE)
        wlog("d3d: tipo %#x com Release diferente (%p), dados privados nao sao limpos", idx, t[2]);
}

/* tira um slot que o FM5 nao tem: FM5 s>=first = Azimuth s+1 (a tabela e alterada no lugar;
 * o Azimuth reconhece os objetos pelo ponteiro da vtable / tipo em +0x38, nao pelo conteudo) */
static void drop_slot(void **t, int first)
{
    memmove(&t[first], &t[first + 1], (SLOTS - first - 1) * sizeof *t);
    t[SLOTS - 1] = (void *)dev_bad_slot;
}

extern void *dma_thunk_vtbl[SLOTS];
void *dma_orig[SLOTS];                 /* tabela original do DmaEngineContext (destino dos dma_thunk_*) */

static void install_object_layouts(void)
{
    typedef void **(*get_fn)(int);
    typedef void **(*alt_fn)(void);
    get_fn get = (get_fn)(g_base + RVA_VTBL_GET);
    static int done;
    int t;
    if (done) return;
    done = 1;
    for (t = 0; t < NTYPES; t++) {
        void **tab = get(t);
        if (type_kind[t] == K_NONE) continue;
        pd_patch_table(tab, t, type_kind[t]);
        if (type_kind[t] == K_RES) drop_slot(tab, 0xb);   /* GetDescriptor */
        if (t == 0xc) {                                   /* Texture2D: GetDesc devolve o Quality do jogo */
            orig_tex2d_getdesc = (tex2d_getdesc_fn)tab[0xb];
            tab[0xb] = (void *)tex2d_GetDesc;
            wlog("d3d: Texture2D GetDesc %#llx com wrapper (Quality do MSAA do FM5)",
                 (unsigned long long)((uint8_t *)orig_tex2d_getdesc - g_base));
        }
        if (t == 6) {                                     /* DMA: contagem/trace por slot (d3d_thunks.S) */
            int i;
            for (i = 0; i < SLOTS; i++) { dma_orig[i] = tab[i]; tab[i] = dma_thunk_vtbl[i]; }
            for (i = 0; i < 0x20; i += 8)
                wlog("d3d: DMA vtbl[%#x..] %#llx %#llx %#llx %#llx %#llx %#llx %#llx %#llx", i,
                     (unsigned long long)((uint8_t *)dma_orig[i] - g_base), (unsigned long long)((uint8_t *)dma_orig[i + 1] - g_base),
                     (unsigned long long)((uint8_t *)dma_orig[i + 2] - g_base), (unsigned long long)((uint8_t *)dma_orig[i + 3] - g_base),
                     (unsigned long long)((uint8_t *)dma_orig[i + 4] - g_base), (unsigned long long)((uint8_t *)dma_orig[i + 5] - g_base),
                     (unsigned long long)((uint8_t *)dma_orig[i + 6] - g_base), (unsigned long long)((uint8_t *)dma_orig[i + 7] - g_base));
        }
        if (t == 0xf) { orig_sc_getbuffer = (getbuf_fn)tab[0xa]; tab[0xa] = (void *)sc_GetBuffer; }
    }
    pd_patch_table(((alt_fn)(g_base + RVA_VTBL_ALTCTX))(), -1, K_CTX);
    wlog("d3d: vtables dos objetos no layout do FM5 + Get/SetPrivateData (Release com hook, trampolim %p)", orig_release);
}

/* ------------------------------------------------------------------------ */
/* Trace: contagem de chamadas por slot (thunks em d3d_thunks.S)              */
/* ------------------------------------------------------------------------ */
/* A cada TRACE_MS loga as chamadas do periodo com o nome do metodo (tabelas de nomes do Azimuth, layout do
 * FH2: contexto 0x1402acfa0, device 0x1402ad810) e o slot do FM5. */
#define RVA_NAMES_CTX 0x2acfa0
#define RVA_NAMES_DEV 0x2ad810
#define TRACE_MS      4000
#define TRACE_ROUNDS  45
volatile LONG ctx_counts[SLOTS], dev_counts[SLOTS], dma_counts[SLOTS], present_count;

static int ctx_fh2_slot(int i) { return i >= 0x86 ? i + 6 : (i >= 7 && i < 0x18) ? i + 1 : i; }

static const char *slot_name(int rva_tab, int s, int n)
{
    const char *const *t = (const char *const *)(g_base + rva_tab);
    return s < n && t[s] ? t[s] : "?";
}

static void trace_dump(const char *what, volatile LONG *c, LONG *prev, int dev)
{
    char line[512];
    int i, len = 0;
    for (i = 0; i < SLOTS; i++) {
        LONG v = c[i], d = v - prev[i];
        const char *nm;
        prev[i] = v;
        if (!d) continue;
        if (dev == 2) nm = "dma";
        else if (dev) nm = slot_name(RVA_NAMES_DEV, i < DEV_X_FIRST ? i : i + DEV_SHIFT, 0x56 + DEV_SHIFT);
        else nm = i == 0x18 ? "IASetPrimitiveTopology" : slot_name(RVA_NAMES_CTX, ctx_fh2_slot(i), SLOTS);
        len += _snprintf(line + len, sizeof line - len, " %s[%#x]=%ld", nm, i, d);
        if (len > 400) { wlog("trace %s:%s", what, line); len = 0; }
    }
    if (len) wlog("trace %s:%s", what, line);
}

/* trace detalhado de alguns frames (ctx_trace_on, ligado no DXGIXPresentArray). Chamado pelos thunks ANTES do
 * metodo: slot do FM5, proxy, 3 primeiros args e ponteiro p/ os args da pilha (5o em diante). */
static const uint32_t trace_frames[] = { 300, 301, 1700, 1701 };

void ctx_trace(unsigned slot, void *proxy, uintptr_t a1, uintptr_t a2, uintptr_t a3, const uintptr_t *stk)
{
    const char *nm = slot == 0x18 ? "IASetPrimitiveTopology" : slot_name(RVA_NAMES_CTX, ctx_fh2_slot(slot), SLOTS);
    int ci = proxy_index(proxy);
    DWORD tid = GetCurrentThreadId();
    if (!frame_trace_on && !(ci >= 0 && bundle_ctx[ci])) return;
    if (slot & 0x1000) {                                         /* DmaEngineContext (slot do Azimuth) */
        wlog("T %5lu DMA[%#x] %p %#llx %#llx %#llx %#llx", tid, slot & 0xfff, proxy, (unsigned long long)a1,
             (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)stk[0]);
        return;
    }
    switch (slot) {
    case 0x1: case 0x2: return;                                  /* AddRef/Release: ruido */
    case 0x21: {                                                 /* OMSetRenderTargets(n, rtv*, dsv) */
        void *const *r = (void *const *)a2;
        wlog("T %5lu c%d %s n=%u rtv=%p %p %p dsv=%p", tid, ci, nm, (UINT)a1, r && a1 > 0 ? r[0] : 0,
             r && a1 > 1 ? r[1] : 0, r && a1 > 2 ? r[2] : 0, (void *)a3);
        return; }
    case 0x32: {                                                 /* ClearRenderTargetView(rtv, color) */
        const float *c = (const float *)a2;
        wlog("T %5lu c%d %s rtv=%p color=%.2f %.2f %.2f %.2f", tid, ci, nm, (void *)a1, c ? c[0] : 0, c ? c[1] : 0,
             c ? c[2] : 0, c ? c[3] : 0);
        return; }
    case 0x2c: {                                                 /* RSSetViewports(n, vp) */
        const float *v = (const float *)a2;
        char buf[160]; int k, nb = 0, n = 0;              /* enderecos do FM5 na pilha (ret = stk[-5]) */
        for (k = -5; k < 0x40 && n < 6; k++)
            if (stk[k] > 0x140001000 && stk[k] < 0x141400000)
                { nb += snprintf(buf + nb, sizeof buf - nb, " %#llx", (unsigned long long)(stk[k] - 0x140000000)); n++; }
        wlog("T %5lu c%d %s n=%u %.0f,%.0f %.0fx%.0f game+:%s", tid, ci, nm, (UINT)a1, v ? v[0] : 0, v ? v[1] : 0,
             v ? v[2] : 0, v ? v[3] : 0, n ? buf : " -");
        return; }
    default:
        wlog("T %5lu c%d %s[%#x] %#llx %#llx %#llx %#llx", tid, ci, nm, slot, (unsigned long long)a1,
             (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)stk[0]);
    }
}

static void *last_rtv, *imm_rtv;   /* debug: RT anterior ao backbuffer / RT atual do imediato */

/* FM5 0x23 OMSetBlendState. Debug 4: blend opaco (NULL) nos contextos diferidos */
void ctx_OMSetBlendState(void *real, void *bs, const float *factor, UINT mask)
{
    typedef void (*blend_fn)(void *, void *, const float *, UINT);
    if (dbg_clear == 4 && nproxies && real != proxies[0].real) bs = NULL;
    ((blend_fn)(*(void ***)real)[0x23])(real, bs, factor, mask);
}

/* FM5 0x3a ExecuteCommandList. Debug 3: antes de executar uma CL no imediato, pinta de verde o RT ligado */
void ctx_ExecuteCommandList(void *real, void *cl, BOOL restore)
{
    typedef void (*exec_fn)(void *, void *, BOOL);
    typedef void (*clear_fn)(void *ctx, void *rtv, const float *c);
    static const float green[4] = { 0, 1, 0, 1 };
    if ((dbg_clear == 3 || dbg_clear == 4 || dbg_clear == 5) && nproxies && real == proxies[0].real && imm_rtv)
        ((clear_fn)(*(void ***)real)[0x32])(real, imm_rtv, green);
    ((exec_fn)(*(void ***)real)[0x3a])(real, cl, restore);
}

/* FM5 0x21 OMSetRenderTargets (Azimuth 0x21). Debug 2: antes de ligar o backbuffer no contexto imediato, pinta
 * de verde o RT que estava ligado (o que o passe final vai ler) */
long ctx_OMSetRenderTargets(void *real, UINT n, void *const *rtvs, void *dsv)
{
    typedef long (*omset_fn)(void *, UINT, void *const *, void *);
    typedef void (*clear_fn)(void *ctx, void *rtv, const float *c);
    static const float green[4] = { 0, 1, 0, 1 };
    if (nproxies && real == proxies[0].real && n && rtvs && rtvs[0] != sc_rtv) imm_rtv = rtvs[0];
    if (dbg_clear == 5 && nproxies && real != proxies[0].real && n && rtvs && rtvs[0]) {
        static const float red[4] = { 1, 0, 0, 1 };
        ((clear_fn)(*(void ***)real)[0x32])(real, rtvs[0], red);
    }
    if (dbg_clear == 2 && nproxies && real == proxies[0].real && n && rtvs) {
        if (rtvs[0] == sc_rtv && sc_rtv && last_rtv) ((clear_fn)(*(void ***)real)[0x32])(real, last_rtv, green);
        else if (rtvs[0] != sc_rtv) last_rtv = rtvs[0];
    }
    return ((omset_fn)(*(void ***)real)[0x21])(real, n, rtvs, dsv);
}

/* FM5 0x39 ResolveSubresource (Azimuth 0x39). O FM5 resolve tambem texturas de 1 amostra (no Xbox vira copia);
 * o Azimuth so troca por copia quando a origem e MSAA e o host usa 1 amostra (--msaa=1, 0x1401a7da0) -> com
 * origem de 1 amostra chegava ao D3D12 -> E_INVALIDARG no Close da command list (crash no loading da corrida).
 * Origem de 1 amostra -> CopySubresourceRegion (Azimuth 0x2e) do subrecurso inteiro. Objeto do Azimuth: tipo
 * +0x38 (0xc = Texture2D), W/H +0x70/+0x74, fmt +0x80, samples +0x84. */
void ctx_ResolveSubresource(void *real, void *dst, UINT dsub, void *src, UINT ssub, UINT fmt)
{
    typedef void (*resolve_fn)(void *, void *, UINT, void *, UINT, UINT);
    typedef void (*copyreg_fn)(void *, void *, UINT, UINT, UINT, UINT, void *, UINT, const void *);
    static volatile LONG logged;
    const uint8_t *s = src, *d = dst;
    UINT ssamples = (s && *(const UINT *)(s + 0x38) == 0xc) ? *(const UINT *)(s + 0x84) : 0;
    {
        static void *seen[64][2];
        static int nseen;
        int k, novo = 1;
        for (k = 0; k < nseen; k++) if (seen[k][0] == src && seen[k][1] == dst) novo = 0;
        if (novo && nseen < 64) { seen[nseen][0] = src; seen[nseen][1] = dst; nseen++; }
        if (novo)
            wlog("d3d: ResolveSubresource c%d src %p (%ux%u fmt %u samples %u) -> dst %p (%ux%u fmt %u samples %u) fmt %u",
                 proxy_index(real), src, s ? *(const UINT *)(s + 0x70) : 0, s ? *(const UINT *)(s + 0x74) : 0,
                 s ? *(const UINT *)(s + 0x80) : 0, ssamples, dst, d ? *(const UINT *)(d + 0x70) : 0,
                 d ? *(const UINT *)(d + 0x74) : 0, d ? *(const UINT *)(d + 0x80) : 0, d ? *(const UINT *)(d + 0x84) : 0, fmt);
    }
    if (s && ssamples <= 1) {
        if (InterlockedIncrement(&logged) <= 20)
            wlog("d3d: ResolveSubresource c%d de 1 amostra -> copia: src %p sub %u (%ux%u fmt %u tipo %#x) dst %p sub %u"
                 " (%ux%u fmt %u samples %u) fmt %u", proxy_index(real), src, ssub, *(const UINT *)(s + 0x70),
                 *(const UINT *)(s + 0x74), *(const UINT *)(s + 0x80), *(const UINT *)(s + 0x38), dst, dsub,
                 d ? *(const UINT *)(d + 0x70) : 0, d ? *(const UINT *)(d + 0x74) : 0, d ? *(const UINT *)(d + 0x80) : 0,
                 d ? *(const UINT *)(d + 0x84) : 0, fmt);
        ((copyreg_fn)(*(void ***)real)[0x2e])(real, dst, dsub, 0, 0, 0, src, ssub, NULL);
        return;
    }
    ((resolve_fn)(*(void ***)real)[0x39])(real, dst, dsub, src, ssub, fmt);
}

/* RSSetScissorRects / RSGetScissorRects (FM5 = Azimuth 0x2d / 0x60). O Azimuth nao implementa o Get (stub fatal:
 * "STOPPED ID3D11DeviceContextX::RSGetScissorRects" ao pausar a corrida) -> o proxy guarda o ultimo Set por
 * contexto; ClearState e FinishCommandList zeram. */
typedef struct { LONG left, top, right, bottom; } rect_t;
static struct { UINT n; rect_t r[16]; } scissors[64];

void ctx_RSSetScissorRects(void *real, UINT n, const rect_t *r)
{
    typedef void (*setsc_fn)(void *, UINT, const rect_t *);
    int ci = proxy_index(real);
    if (ci >= 0) {
        scissors[ci].n = r ? (n > 16 ? 16 : n) : 0;
        if (scissors[ci].n) memcpy(scissors[ci].r, r, scissors[ci].n * sizeof *r);
    }
    ((setsc_fn)(*(void ***)real)[0x2d])(real, n, r);
}

void ctx_RSGetScissorRects(void *real, UINT *n, rect_t *r)
{
    static volatile LONG logged;
    int ci = proxy_index(real);
    UINT have = ci >= 0 ? scissors[ci].n : 0, i;
    if (!n) return;
    if (r) {
        for (i = 0; i < *n; i++) {
            if (i < have) r[i] = scissors[ci].r[i];
            else memset(&r[i], 0, sizeof r[i]);
        }
    }
    if (InterlockedIncrement(&logged) <= 10)
        wlog("d3d: RSGetScissorRects c%d n=%u rects=%p -> %u", ci, *n, (void *)r, have);
    if (!r || *n > have) *n = have;
}

void ctx_ClearState(void *real)
{
    typedef void (*cs_fn)(void *);
    typedef void (*resolve_fn)(void *, void *, UINT, void *, UINT, UINT);
    int ci = proxy_index(real);
    if (ci >= 0) scissors[ci].n = 0;
    if (ci >= 0 && ci < 64) memset(ps_samplers[ci], 0, sizeof ps_samplers[ci]);
    ((cs_fn)(*(void ***)real)[0x6e])(real);
    if (ci == 0 && al_pending && InterlockedExchange(&al_pending, 0)) {   /* experimento de aliasing: cor */
        AcquireSRWLockShared(&alias_lock);
        if (al_color2x && al_color1x) ((resolve_fn)(*(void ***)real)[0x39])(real, al_color1x, 0, al_color2x, 0, 26);
        ReleaseSRWLockShared(&alias_lock);
    }
}

/* FM5 0x29 Dispatch (= Azimuth 0x29). Experimento de aliasing: profundidade antes do compute de profundidade */
void ctx_Dispatch(void *real, UINT x, UINT y, UINT z)
{
    typedef void (*disp_fn)(void *, UINT, UINT, UINT);
    typedef void (*copy_fn)(void *, void *, void *);
    if (alias_test > 0 && al_w && x == (al_w + 7) / 8 && y == (al_h + 7) / 8 && z == 1 && nproxies &&
        real == proxies[0].real) {
        AcquireSRWLockShared(&alias_lock);
        if (al_depth_a && al_depth_b) ((copy_fn)(*(void ***)real)[0x2f])(real, al_depth_b, al_depth_a);
        ReleaseSRWLockShared(&alias_lock);
        al_pending = 1;
        if (InterlockedIncrement(&al_count) <= 3)
            wlog("d3d: alias: Dispatch %ux%ux%u -> copia profundidade %p -> %p, resolve da cor pendente", x, y, z,
                 al_depth_a, al_depth_b);
    }
    ((disp_fn)(*(void ***)real)[0x29])(real, x, y, z);
}

void ctx_RSGetViewports(void *real, UINT *n, float *vp)
{
    typedef void (*getvp_fn)(void *, UINT *, float *);
    static volatile LONG logged;
    UINT in = n ? *n : 0;
    ((getvp_fn)(*(void ***)real)[0x5f])(real, n, vp);
    if (InterlockedIncrement(&logged) <= 20)
        wlog("d3d: RSGetViewports c%d n=%u -> %u: %.1f,%.1f %.1fx%.1f z %.2f..%.2f", proxy_index(real), in, n ? *n : 0,
             vp ? vp[0] : -1, vp ? vp[1] : -1, vp ? vp[2] : -1, vp ? vp[3] : -1, vp ? vp[4] : -1, vp ? vp[5] : -1);
}

long ctx_FinishCommandList(void *real, BOOL restore, void **out)
{
    typedef long (*fin_fn)(void *, BOOL, void **);
    long hr = ((fin_fn)(*(void ***)real)[0x72])(real, restore, out);
    int ci = proxy_index(real);
    if (!restore && ci >= 0) scissors[ci].n = 0;
    if (frame_trace_on || (ci >= 0 && bundle_ctx[ci]))
        wlog("T %5lu c%d FinishCommandList -> hr=%#lx cl=%p", GetCurrentThreadId(), proxy_index(real), hr,
             out ? *out : NULL);
    return hr;
}

void trace_skips(void);
static DWORD WINAPI trace_thread(void *arg)
{
    static LONG pc[SLOTS], pd[SLOTS], pm[SLOTS];
    LONG pp = 0;
    int r;
    (void)arg;
    for (r = 0; r < TRACE_ROUNDS; r++) {
        Sleep(TRACE_MS);
        wlog("trace ---- %d ms: %ld presents", (r + 1) * TRACE_MS, present_count - pp);
        pp = present_count;
        trace_dump("dev", dev_counts, pd, 1);
        trace_dump("ctx", ctx_counts, pc, 0);
        trace_dump("dma", dma_counts, pm, 2);
        trace_skips();
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* D3D11CreateDevice                                                          */
/* ------------------------------------------------------------------------ */
typedef long (*createx_fn)(const void *desc, void **dev, void **ctx);

static long WINAPI fm5_D3D11CreateDevice(void *adapter, UINT type, HMODULE sw, UINT flags,
                                         const UINT *levels, UINT nlevels, UINT sdk,
                                         void **out_dev, UINT *out_level, void **out_ctx)
{
    /* descritor exigido pelo HLE ("differs from the game's verified startup"), o do FH2 */
    uint32_t desc[16] = { 0x1000b, 0x4000000, 0, 0, 0, 0, 0x3f };
    void *dev = NULL, *ctx = NULL;
    long hr;

    wlog("d3d: D3D11CreateDevice(adapter=%p type=%u flags=%#x levels=%u:%#x sdk=%#x)",
         adapter, type, flags, nlevels, levels && nlevels ? levels[0] : 0, sdk);
    (void)sw;
    if (out_dev) *out_dev = NULL;
    if (out_ctx) *out_ctx = NULL;
    hr = ((createx_fn)(g_base + RVA_CREATE_DEVICEX))(desc, &dev, &ctx);
    wlog("d3d: D3D11XCreateDeviceX hr=%#lx device=%p contexto=%p", hr, dev, ctx);
    if (hr < 0) return hr;

    install_object_layouts();
    install_device_vtable(dev);
    ctx = proxy_of(ctx);
    if (out_level) *out_level = 0xb100;
    if (out_dev) *out_dev = dev;
    if (out_ctx) *out_ctx = ctx;
    CloseHandle(CreateThread(NULL, 0, trace_thread, NULL, 0, NULL));
    /* sem ppDevice/ppContext as referencias ficam com o Azimuth (unico device do processo) */
    return hr;
}

/* Device do FM6: o driver do OS "era" (EmbeddedXvd\...\umd.dll, vtable em .data 0x17f760) tem o mesmo layout
   do driver do FH2 (0x16f2e0) slot a slot (tools\umd_vtdiff.py). Alguns metodos la sao triviais e no Azimuth sao
   o stub fatal padrao (o FH2 nunca chama): ex. FM6 game+0x3a676c PlaceSwapChainView(0x14) = "ret" no console.
   So os slots que ainda sao o stub padrao recebem o comportamento do driver. */
#define RVA_STUB_SLOT(s)  (0xf5f30 + (uintptr_t)(s) * 0x100)   /* stub fatal padrao do slot s */
static long dev_ret0(void) { return 0; }
static long dev_notimpl(void) { return E_NOTIMPL; }
static const struct { uint8_t slot; long (*fn)(void); } fm6_dev_trivial[] = {
    { 0x1c, dev_notimpl },   /* OpenSharedResource */
    { 0x1f, dev_ret0 },      /* CheckCounterInfo: ret */
    { 0x28, dev_notimpl },   /* GetDeviceRemovedReason */
    { 0x2a, dev_notimpl },   /* SetExceptionMode */
    { 0x2b, dev_ret0 },      /* GetExceptionMode: xor eax,eax */
    { 0x31, dev_notimpl },   /* OpenSharedResource1 */
    { 0x32, dev_notimpl },   /* OpenSharedResourceByName */
    { 0x38, dev_notimpl },   /* CreateCounterSet */
    { 0x39, dev_notimpl },   /* CreateCounterSample */
    { 0x50, dev_ret0 },      /* PlaceSwapChainView: ret */
    { 0x51, dev_ret0 },      /* SetDebugFlags: xor eax,eax */
};

/* CreateTexture2D (slot 5) do FM6: mesmo SampleDesc do console que o FM5 (2x -> {2, 2}; game+0x334f94 cor
   R11G11B10 1920x1080, misc 0x40000) -> E_INVALIDARG no D3D12. O device do FM6 e o do Azimuth sem o proxy do FM5
   (dev_CreateTexture2D), entao a troca do Quality pelo padrao fica aqui. */
static tex2d_fn orig_fm6_create_tex2d;
static long fm6_CreateTexture2D(void *dev, const UINT *desc, const void *init, void **out)
{
    UINT d[16];
    long hr;
    if (!desc || desc[5] <= 1 || desc[6] == 0 || desc[6] == 0xffffffff)
        return orig_fm6_create_tex2d(dev, desc, init, out);
    memcpy(d, desc, 12 * sizeof(UINT));
    d[6] = 0xffffffff;
    hr = orig_fm6_create_tex2d(dev, d, init, out);
    wlog("d3d: CreateTexture2D(FM6 %ux%u fmt=%u samples=%u quality %#x -> padrao, bind=%#x misc=%#x) hr=%#lx tex=%p",
         d[0], d[1], d[4], d[5], desc[6], d[8], d[10], hr, out ? *out : NULL);
    return hr;
}

/* CreateShaderResourceView (slot 7) do FM6: R8_UINT sobre a cor 2x (game+0x216dc7) = leitura da FMASK, que o
   Azimuth so emula em 4x/8x -> device removido; mesma FMASK emulada do FM5 (srv_fmask, PASSO 8 cont. 20). */
static srv_fn orig_fm6_create_srv;
static long fm6_CreateShaderResourceView(void *dev, void *res, const UINT *desc, void **out)
{
    return srv_fmask(dev, res, desc, out, orig_fm6_create_tex2d, orig_fm6_create_srv);
}

/* Create*Shader / CreateInputLayout do FM6: o bytecode passa por fm6_shader_rewrite (shader_fm6.c: max3 do console
   -> max) antes do Azimuth, que copia o bytecode (vetor proprio) -> o convertido e liberado na volta. O input
   layout recebe o mesmo bytecode convertido (o Azimuth casa shaders por hash). */
uint8_t *fm6_shader_rewrite(const void *code, size_t size, size_t *out_size);
#define FM6_SHADER_FIRST 0x0c                          /* VS 0xc, GS 0xd, GS+SO 0xe, PS 0xf, HS 0x10, DS 0x11, CS 0x12 */
static void *orig_fm6_shader[7], *orig_fm6_input_layout;
typedef long (*create_shader_fn)(void *dev, const void *code, SIZE_T len, void *linkage, void **out);
typedef long (*create_gsso_fn)(void *dev, const void *code, SIZE_T len, const void *decl, UINT n, const UINT *strides,
                               UINT nstrides, UINT rast, void *linkage, void **out);
typedef long (*create_il_fn)(void *dev, const void *descs, UINT n, const void *code, SIZE_T len, void **out);
#define FM6_SHADER(s)                                                                                           \
    static long fm6_create_shader_##s(void *dev, const void *code, SIZE_T len, void *lk, void **out)          \
    {                                                                                                           \
        size_t n = 0;                                                                                           \
        uint8_t *p = fm6_shader_rewrite(code, len, &n);                                                         \
        long hr = ((create_shader_fn)orig_fm6_shader[s - FM6_SHADER_FIRST])(dev, p ? p : code, p ? n : len, lk, out); \
        free(p);                                                                                                \
        return hr;                                                                                              \
    }
FM6_SHADER(0x0c) FM6_SHADER(0x0d) FM6_SHADER(0x0f) FM6_SHADER(0x10) FM6_SHADER(0x11) FM6_SHADER(0x12)
static long fm6_create_shader_0x0e(void *dev, const void *code, SIZE_T len, const void *decl, UINT nd,
                                   const UINT *strides, UINT ns, UINT rast, void *lk, void **out)
{
    size_t n = 0;
    uint8_t *p = fm6_shader_rewrite(code, len, &n);
    long hr = ((create_gsso_fn)orig_fm6_shader[2])(dev, p ? p : code, p ? n : len, decl, nd, strides, ns, rast, lk, out);
    free(p);
    return hr;
}
static long fm6_CreateInputLayout(void *dev, const void *descs, UINT nd, const void *code, SIZE_T len, void **out)
{
    size_t n = 0;
    uint8_t *p = fm6_shader_rewrite(code, len, &n);
    long hr = ((create_il_fn)orig_fm6_input_layout)(dev, descs, nd, p ? p : code, p ? n : len, out);
    free(p);
    return hr;
}
static void *const fm6_shader_hooks[7] = {
    (void *)fm6_create_shader_0x0c, (void *)fm6_create_shader_0x0d, (void *)fm6_create_shader_0x0e,
    (void *)fm6_create_shader_0x0f, (void *)fm6_create_shader_0x10, (void *)fm6_create_shader_0x11,
    (void *)fm6_create_shader_0x12,
};

/* CreatePlacementTexture2D (slot 0x40) do FM6: o Azimuth (0x140121c40) so aceita (a) alvo aliasado: Usage 0, CPU 0,
   misc 0/0x20000/0x40000, 1 mip, 1 fatia, bind SRV|RT, SRV|DS ou SRV|RT|UAV; (b) textura pelo layout do XG: bind SRV,
   CPU 0, 1 amostra, Usage <= 1, 1..15 mips, 1..0x800 fatias. O resto para ("GPU-write/CPU-map/unknown placement
   semantics", game+0x214d79) -> registra a descricao das recusadas antes.
   O FM6 (game+0x21d6f0) cria texturas SO UAV (bind 0x80): cadeia 64x64 de 7 mips R32G32B32A32_UINT e duas 1x1 na
   memoria dos ultimos mips dela (aliasing). O Azimuth nao faz aliasing de memoria (os alvos do caso (a) tambem
   viram recursos committed proprios) e a CPU nao le textura so de UAV -> CreateTexture2D committed comum
   (o endereco fica so no log). PROVISORIO: as 1x1 nao enxergam os mips da cadeia. */
static int az_placement_tex2d_ok(const UINT *d)
{
    if (d[7] == 0 && d[9] == 0 && (d[10] == 0 || d[10] == 0x20000 || d[10] == 0x40000) && d[2] == 1 && d[3] == 1 &&
        (d[8] == 0x28 || d[8] == 0x48 || d[8] == 0xa8))
        return 1;
    return d[8] == 8 && d[9] == 0 && d[5] == 1 && d[2] && d[2] <= 0xf && d[3] && d[3] <= 0x800 && d[7] <= 1;
}

/* so UAV, sem CPU, Usage DEFAULT, 1 amostra, sem misc */
static int fm6_placement_uav_only(const UINT *d)
{
    return d[8] == 0x80 && d[9] == 0 && d[7] == 0 && d[5] == 1 && d[10] == 0;
}

/* chamador de fora do wrapper do jogo (game+0x214cf0): o jogo nao tem unwind registrado -> 2o valor da pilha que cai
   na imagem do jogo (o 1o e o retorno game+0x214d79 do proprio wrapper), como offset */
extern uint8_t *g_fm6_game;
static __attribute__((noinline)) uintptr_t fm6_game_caller(void)
{
    uint8_t **sp = __builtin_frame_address(0), **top = ((NT_TIB *)NtCurrentTeb())->StackBase;
    int k = 0;
    for (; g_fm6_game && sp < top && sp < (uint8_t **)__builtin_frame_address(0) + 0x200; sp++)
        if (*sp > g_fm6_game && *sp < g_fm6_game + 0x1800000 && ++k == 2) return (uintptr_t)(*sp - g_fm6_game);
    return 0;
}

static ptex2d_fn orig_fm6_placement_tex2d;
static long fm6_CreatePlacementTexture2D(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out)
{
    if (desc && !az_placement_tex2d_ok(desc) && fm6_placement_uav_only(desc)) {
        static volatile LONG n;
        LONG k = InterlockedIncrement(&n);
        long hr = orig_fm6_create_tex2d(dev, desc, NULL, out);
        if (k <= 16 || (k & (k - 1)) == 0)
            wlog("d3d: CreatePlacementTexture2D(FM6) so UAV #%ld %ux%u mips=%u fmt=%u addr=%p -> textura committed "
                 "hr=%#lx tex=%p, chamador game+%#zx", k, desc[0], desc[1], desc[2], desc[4], addr, hr, out ? *out : NULL,
                 fm6_game_caller());
        return hr;
    }
    if (desc && !az_placement_tex2d_ok(desc))
        wlog("d3d: CreatePlacementTexture2D(FM6) recusada pelo Azimuth: %ux%u mips=%u arr=%u fmt=%u samples=%u/%#x "
             "usage=%u bind=%#x cpu=%#x misc=%#x tile=%u pitch=%u addr=%p, chamador game+%#zx", desc[0], desc[1], desc[2],
             desc[3], desc[4], desc[5], desc[6], desc[7], desc[8], desc[9], desc[10], tile, pitch, addr,
             fm6_game_caller());
    return orig_fm6_placement_tex2d(dev, desc, tile, pitch, addr, out);
}

static int fm6_device_fixups(void **vt)
{
    size_t i;
    int n = 0;
    if (vt[5] != (void *)fm6_CreateTexture2D) {
        orig_fm6_create_tex2d = (tex2d_fn)vt[5];
        vt[5] = (void *)fm6_CreateTexture2D;
        orig_fm6_create_srv = (srv_fn)vt[7];
        vt[7] = (void *)fm6_CreateShaderResourceView;
        orig_fm6_placement_tex2d = (ptex2d_fn)vt[0x40];
        vt[0x40] = (void *)fm6_CreatePlacementTexture2D;
        orig_fm6_input_layout = vt[0xb];
        vt[0xb] = (void *)fm6_CreateInputLayout;
        for (i = 0; i < 7; i++) {
            orig_fm6_shader[i] = vt[FM6_SHADER_FIRST + i];
            vt[FM6_SHADER_FIRST + i] = fm6_shader_hooks[i];
        }
    }
    for (i = 0; i < sizeof fm6_dev_trivial / sizeof *fm6_dev_trivial; i++) {
        int s = fm6_dev_trivial[i].slot;
        if (vt[s] != g_base + RVA_STUB_SLOT(s)) continue;
        vt[s] = (void *)fm6_dev_trivial[i].fn;
        n++;
    }
    return n;
}

/* Contexto do FM6: o umd.dll do OS "era" copia para ctx+0x3d0 uma tabela de 0x100 slots (imediato .data 0xe2bf0,
   diferido 0xe3760; la [ctx] = ctx+0x3d0) = a do driver do FH2 (.data 0x117b60, 0xfd slots) com 3 metodos a mais
   (tools\umd_vtdiff.py): 8 (novo, xor eax,eax), 0x8e PIXBeginEventEx e 0x91 PIXSetMarkerEx (cabecalho: BeginEvent,
   BeginEventEx, EndEvent, SetMarker, SetMarkerEx). FM6 s -> Azimuth: s < 8 = s; 9..0x8d = s-1; 0x8f/0x90 = s-2;
   >= 0x92 = s-3. Ex.: game+0x3a732f [ctx+0xa08](1) = FM6 0xc7 = CSEnableAutomaticGpuFlush (Azimuth 0xc4), nao Resume.
   O Azimuth faz [ctx] = tabela global do tipo e memcpy(ctx+0x3d0, tabela, 0x870) em 0x586e1 (imediato, dentro do
   D3D11XCreateDeviceX) e 0x11b440 (diferido); o jogo le a copia embutida -> as duas chamadas ao memcpy passam a
   montar a copia no layout do FM6. [ctx] continua com a tabela do Azimuth (slots 0..7 iguais nos dois). */
#define RVA_CTX_COPY_IMM  0x586e1
#define RVA_CTX_COPY_DEF  0x11b440
#define RVA_MEMCPY        0x24b6f0
#define FM6_CTX_SLOTS     0x100
#define FM6_NEW           0xff

extern void write_mem(void *dst, const void *src, size_t len);
extern uint8_t *alloc_near(uint8_t *mod, size_t size);
extern int bytes_match(const uint8_t *p, const char *hex, size_t len);
extern void abs_jmp(uint8_t *at, const void *to, size_t len);

static int fm6_ctx_slot(int s)
{
    if (s < 8) return s;
    if (s == 8 || s == 0x8e || s == 0x91) return -1;
    return s < 0x8e ? s - 1 : s < 0x91 ? s - 2 : s - 3;
}

static long ctx_ret0(void) { return 0; }
static long ctx_ret1(void) { return 1; }
static long ctx_retm1(void) { return -1; }
static long ctx_notimpl(void) { return E_NOTIMPL; }
static long ctx_fail(void) { return E_FAIL; }

/* metodos triviais no driver (tools\umd_trivial.py ... fm6): imediato / diferido; NULL = nao trivial ali */
static const struct { uint8_t slot; long (*imm)(void), (*def)(void); } fm6_ctx_trivial[] = {
    { 0x08, ctx_ret0, ctx_ret0 },          /* novo */
    { 0x1e, ctx_notimpl, NULL },
    { 0x38, ctx_ret0, NULL },
    { 0x70, ctx_ret0, NULL },
    { 0x71, ctx_ret1, ctx_ret0 },
    { 0x72, NULL, ctx_ret0 },
    { 0x73, NULL, ctx_notimpl },
    { 0x76, ctx_ret0, ctx_ret0 }, { 0x77, ctx_ret0, ctx_ret0 },
    { 0x84, ctx_ret0, NULL },
    { 0x86, ctx_ret0, ctx_ret0 },
    { 0x8d, ctx_retm1, ctx_retm1 },        /* PIXBeginEvent */
    { 0x8e, ctx_retm1, ctx_retm1 },        /* PIXBeginEventEx (novo) */
    { 0x8f, ctx_retm1, ctx_retm1 },        /* PIXEndEvent */
    { 0x90, ctx_ret0, ctx_ret0 },          /* PIXSetMarker */
    { 0x91, ctx_ret0, ctx_ret0 },          /* PIXSetMarkerEx (novo) */
    { 0x92, ctx_ret0, ctx_ret0 },
    { 0x93, ctx_notimpl, ctx_notimpl }, { 0x94, ctx_notimpl, ctx_notimpl }, { 0x95, ctx_notimpl, ctx_notimpl },
    { 0x96, ctx_ret0, ctx_ret0 }, { 0x97, ctx_ret0, ctx_ret0 }, { 0x98, ctx_ret0, ctx_ret0 },
    { 0x99, ctx_notimpl, ctx_notimpl },
    { 0x9d, ctx_ret0, NULL }, { 0x9e, ctx_ret0, NULL },
    { 0x9f, NULL, ctx_ret0 }, { 0xa0, NULL, ctx_ret0 }, { 0xa1, NULL, ctx_ret0 }, { 0xa2, NULL, ctx_ret0 },
    { 0xc9, ctx_fail, NULL }, { 0xca, ctx_fail, NULL },
    { 0xcb, ctx_ret0, NULL }, { 0xcc, ctx_ret0, NULL },
    /* SetGraphicsShaderLimits (Azimuth 0xca, stub fatal; game+0x440823): no driver so copia 0x3c bytes de limites
       de onda/CU por estagio p/ o estado do hardware (umd 0x77c60), sem retorno -> sem efeito no PC */
    { 0xcd, ctx_ret0, ctx_ret0 },
    { 0xdc, ctx_ret0, ctx_ret0 },
    { 0xde, ctx_fail, NULL }, { 0xdf, ctx_ret0, NULL },
    { 0xe2, ctx_ret0, NULL },
};

/* XXSetShaderResources do FM6 (PS 0xa, VS 0x1a, GS 0x20, HS 0x3c, DS 0x40, CS 0x44): no driver do OS "era" (umd
   0x11bb0.. 0x12c40, 279 bytes; no do FH2 sao jmp p/ a forma padrao) os argumentos sao (ppViews, StartSlot, packed):
   packed = offset do registrador por estagio (bits 0..15; VS 0x1d80 + 32 * slot) | 7 << 16 | (NumViews - 1) << 19
   (game+0x1905e6 VS, 0x19060f PS, 0x190633 CS). O caminho lento do driver chama a forma padrao com
   (ctx, start, (packed >> 19) + 1, ppViews) -> aqui o mesmo, no metodo do Azimuth da tabela em [ctx] (imediata ou
   diferida). */
typedef void (*set_srvs_fn)(void *ctx, UINT start, UINT n, void *const *views);
#define FM6_SET_SRVS(s)                                                                                 static void fm6_set_srvs_##s(void *ctx, void *const *views, UINT start, UINT packed)                {                                                                                                       ((set_srvs_fn)(*(void ***)ctx)[fm6_ctx_slot(s)])(ctx, start, (packed >> 19) + 1, views);        }
FM6_SET_SRVS(0x0a) FM6_SET_SRVS(0x1a) FM6_SET_SRVS(0x20) FM6_SET_SRVS(0x3c) FM6_SET_SRVS(0x40) FM6_SET_SRVS(0x44)
static const struct { uint8_t slot; void *fn; } fm6_ctx_srvs[] = {
    { 0x0a, (void *)fm6_set_srvs_0x0a }, { 0x1a, (void *)fm6_set_srvs_0x1a }, { 0x20, (void *)fm6_set_srvs_0x20 },
    { 0x3c, (void *)fm6_set_srvs_0x3c }, { 0x40, (void *)fm6_set_srvs_0x40 }, { 0x44, (void *)fm6_set_srvs_0x44 },
};

/* X SetFast* (FM6 0xa3..0xb6 = Azimuth 0xa0..0xb3, stubs fatais): no umd do FM6 tem os mesmos tamanhos do FH2
   (umd_vtdiff "equ"; IASetFastIndexBuffer 0x1c010 = (hwfmt, buffer), como o 0x18c90 do FH2) -> os wrappers do FM5
   sobre os Set* padrao do Azimuth em [ctx], na mesma ordem (game+0x131a2d: IASetFastIndexBuffer(1, ib)). */
#define FM6_FAST_FIRST 0xa3
static void *const fm6_ctx_fast[] = {
    (void *)ctx_PSSetFastConstantBuffer, (void *)ctx_PSSetFastShaderResource, (void *)ctx_PSSetFastSampler,
    (void *)ctx_VSSetFastConstantBuffer, (void *)ctx_VSSetFastShaderResource, (void *)ctx_VSSetFastSampler,
    (void *)ctx_GSSetFastConstantBuffer, (void *)ctx_GSSetFastShaderResource, (void *)ctx_GSSetFastSampler,
    (void *)ctx_CSSetFastConstantBuffer, (void *)ctx_CSSetFastShaderResource, (void *)ctx_CSSetFastSampler,
    (void *)ctx_HSSetFastConstantBuffer, (void *)ctx_HSSetFastShaderResource, (void *)ctx_HSSetFastSampler,
    (void *)ctx_DSSetFastConstantBuffer, (void *)ctx_DSSetFastShaderResource, (void *)ctx_DSSetFastSampler,
    (void *)ctx_IASetFastVertexBuffer, (void *)ctx_IASetFastIndexBuffer,
};

/* UpdateSubresource (FM6 0x31 = Azimuth 0x30) de um placement buffer num contexto diferido (game+0x207247): o
   Azimuth para ("... on a deferred context", validador 0x1401a6fc0). Placement buffer do Azimuth (CreatePlacementBuffer
   0x140121870, so constantes): +0x38 tipo 5, +0x70 ByteWidth, +0x88 placement, +0x168 memoria do jogo (0 = liberada);
   o conteudo e a memoria do jogo, copiada por lista diferida no ExecuteCommandList (0x14011bd40). No console o
   driver escreveria essa memoria; aqui a copia vai para a memoria na gravacao (= o jogo escrevendo o buffer).
   Diferenca: varias atualizacoes do mesmo buffer na mesma lista ficam so com a ultima (log das primeiras). */
#define AZ_CTX_DEFERRED   0xc48
#define AZ_RES_TYPE       0x38
#define AZ_BUF_WIDTH      0x70
#define AZ_BUF_PLACEMENT  0x88
#define AZ_BUF_MEMORY     0x168
typedef void (*update_sub_fn)(void *ctx, void *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp);
static void fm6_UpdateSubresource(void *ctx, uint8_t *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp)
{
    if (res && src && sub == 0 && ((uint8_t *)ctx)[AZ_CTX_DEFERRED] && *(int *)(res + AZ_RES_TYPE) == 5 &&
        res[AZ_BUF_PLACEMENT] && *(uint8_t **)(res + AZ_BUF_MEMORY)) {
        UINT w = *(UINT *)(res + AZ_BUF_WIDTH), lo = box ? box[0] : 0, hi = box ? box[3] : w;
        if (lo < hi && hi <= w) {
            static volatile LONG n;
            LONG k = InterlockedIncrement(&n);
            memcpy(*(uint8_t **)(res + AZ_BUF_MEMORY) + lo, src, hi - lo);
            if (k <= 8 || (k & (k - 1)) == 0)
                wlog("d3d: UpdateSubresource #%ld de placement buffer no diferido %p: buf %p (%u bytes) [%u, %u) "
                     "-> memoria %p, chamador %p", k, ctx, (void *)res, w, lo, hi,
                     *(void **)(res + AZ_BUF_MEMORY), __builtin_return_address(0));   /* g_game e so do FM5 */
            return;
        }
    }
    ((update_sub_fn)(*(void ***)ctx)[fm6_ctx_slot(0x31)])(ctx, res, sub, box, src, rp, dp);
}

/* SOSetTargets (FM6 0x26 = Azimuth 0x25, stub fatal; o Azimuth nao tem stream out e os PSOs nao tem SO): o unico
   chamador pelo contexto (game+0x21c61d) desliga o stream out com (0, NULL, NULL) -> sem efeito no PC. Algum
   buffer de verdade -> stub do Azimuth (para com os argumentos). */
#define FM6_SLOT_SO_SET 0x26
typedef void (*so_set_fn)(void *ctx, UINT n, void *const *bufs, const UINT *offsets);
static void fm6_SOSetTargets(void *ctx, UINT n, void *const *bufs, const UINT *offsets)
{
    UINT i;
    for (i = 0; bufs && i < n; i++)
        if (bufs[i]) {
            ((so_set_fn)(*(void ***)ctx)[fm6_ctx_slot(FM6_SLOT_SO_SET)])(ctx, n, bufs, offsets);
            return;
        }
}

/* Batches de recursos do FM6 (SetFastResourcesFromBatch, FM6 0xd7 = Azimuth 0xd4, stub fatal; game+0xda9ad1). No
   console o jogo monta o batch sozinho: game+0xda9b94 (CB) e game+0xda9cac (SRV) apontam [ctx+0x40]/[ctx+0x48] do
   contexto para a memoria do batch (o BeginResourceBatch inline) e gravam pacotes da CE prontos: cabecalho
   (n - 1) << 16 | offset na CE RAM + o descritor da GPU copiado do objeto (V# de 16 bytes em [buf+0x20], T# de 32
   em [srv+0x30]); o registro i fica em batch + i * 20 (CB) ou i * 36 (SRV). O FromBatch so copia os pacotes para
   o stream. Estagio = {5, 1, 2, 3}[i / 14] (CB, slot i % 14) ou [i >> 7] (SRV, slot i & 0x7f), na numeracao do
   driver: 0 VS, 1 HS, 2 DS, 3 GS, 4 PS, 5 CS (bases da CE RAM 0x1c40, 0x2fb0, 0x4320, 0x5690, 0x6a00, 0 dos
   XXSetFast* do umd). O Azimuth nao tem como voltar do descritor ao objeto -> os dois montadores sao trocados por
   versoes que gravam no registro (mesmo tamanho) o estagio, o slot e o PONTEIRO do objeto, sem tocar no contexto
   do Azimuth, e o FromBatch aplica cada registro com o Set* padrao (1 elemento). */
#define FM6_BATCH_CB       0xda9b94
#define FM6_BATCH_SRV      0xda9cac
#define FM6_BATCH_MAGIC    0xf6ba0000u
#define FM6_BATCH_SRV_FLAG 0x8000u
#define FM6_BATCH_CB_SIZE  20
#define FM6_BATCH_SRV_SIZE 36
#define FM6_BATCH_PM4_NOP  0x80000000u   /* o jogo completa o batch ate 16 bytes com NOPs tipo 2 */
typedef struct { uint8_t *cb, *srv; uint32_t cb_size, srv_size; } fm6_batch;
static const uint8_t fm6_batch_stage[4] = { 5, 1, 2, 3 };
/* Azimuth (layout com o slot 7) por estagio do driver: CB e SRV */
static const uint8_t fm6_stage_cb[6] = { 0x08, 0x3e, 0x42, 0x17, 0x11, 0x47 };
static const uint8_t fm6_stage_srv[6] = { 0x19, 0x3b, 0x3f, 0x1f, 0x09, 0x43 };

static void fm6_batch_put(uint8_t *rec, uint32_t hdr, void *obj)
{
    memcpy(rec, &hdr, 4);
    memcpy(rec + 4, &obj, 8);
}

/* game+0xda9b94(batch, buffer, i) */
static void fm6_batch_add_cb(fm6_batch *b, void *buf, uint32_t i)
{
    if (i * FM6_BATCH_CB_SIZE >= b->cb_size || i / 14 >= 4) return;
    fm6_batch_put(b->cb + i * FM6_BATCH_CB_SIZE, FM6_BATCH_MAGIC | fm6_batch_stage[i / 14] << 8 | i % 14, buf);
}

/* game+0xda9cac(batch, srv, i) */
static void fm6_batch_add_srv(fm6_batch *b, void *srv, uint32_t i)
{
    if (i * FM6_BATCH_SRV_SIZE >= b->srv_size || i >> 7 >= 4) return;
    fm6_batch_put(b->srv + i * FM6_BATCH_SRV_SIZE,
                  FM6_BATCH_MAGIC | FM6_BATCH_SRV_FLAG | fm6_batch_stage[i >> 7] << 8 | (i & 0x7f), srv);
}

static void fm6_SetFastResourcesFromBatch(void *ctx, const uint8_t *p, UINT size)
{
    const uint8_t *end = p + size;
    while (p + 4 <= end) {
        uint32_t hdr, stage, slot, n;
        void *obj;
        memcpy(&hdr, p, 4);
        if (hdr == FM6_BATCH_PM4_NOP) { p += 4; continue; }
        n = hdr & FM6_BATCH_SRV_FLAG ? FM6_BATCH_SRV_SIZE : FM6_BATCH_CB_SIZE;
        stage = hdr >> 8 & 0x7f;
        slot = hdr & 0xff;
        if ((hdr & 0xffff0000u) != FM6_BATCH_MAGIC || stage >= 6 || p + n > end) {
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 8)
                wlog("d3d: SetFastResourcesFromBatch do FM6: registro desconhecido %#x em +%#x de %u bytes; resto "
                     "ignorado", hdr, (UINT)(size - (end - p)), size);
            return;
        }
        memcpy(&obj, p + 4, 8);
        {
            static volatile LONG logged;
            LONG k = InterlockedIncrement(&logged);
            if (k <= 16) {
                uint8_t *vt = obj ? *(uint8_t **)obj : NULL, *tabs = g_base + RVA_VTBL_TABLES;
                wlog("d3d: batch do FM6 #%ld: %s estagio %u slot %u obj %p (tipo do Azimuth %d)", k,
                     hdr & FM6_BATCH_SRV_FLAG ? "SRV" : "CB", stage, slot, obj,
                     vt >= tabs && vt < tabs + NTYPES * SLOTS * 8 ? (int)((vt - tabs) / (SLOTS * 8)) : -1);
            }
        }
        ((set_fn)(*(void ***)ctx)[hdr & FM6_BATCH_SRV_FLAG ? fm6_stage_srv[stage] : fm6_stage_cb[stage]])
            (ctx, slot, 1, &obj);
        p += n;
    }
}

/* Listas aninhadas do FM6: game+0xf973d junta as listas dos workers num diferido (ExecuteCommandList(cl, FALSE),
   FM6 0x3b = Azimuth 0x3a) e o Azimuth para em "ExecuteCommandList on a deferred context" (0x14011c100: o diferido
   grava direto numa command list do D3D12, que nao executa outra). Aqui o diferido fecha o que gravou ate ali
   (FinishCommandList(TRUE), o estado fica) e guarda [parte, pecas de cl, cl] como pendencias; o FinishCommandList
   do jogo (FM6 0x73) pendura as pendencias na lista que devolve e o ExecuteCommandList do imediato executa as pecas
   em ordem antes dela. As pecas tem uma referencia nossa, solta quando o Release da lista chega a 0 (slot 2 da
   vtable da lista trocado na primeira lista com pecas). Bundles (ctx+0xc49) seguem no Azimuth. */
#define AZ_CTX_BUNDLE     0xc49
#define AZ_SLOT_EXECUTE   0x3a
#define AZ_SLOT_CLEAR     0x6e
#define AZ_SLOT_FINISH    0x72
typedef void (*execute_fn)(void *ctx, void *cl, BOOL restore);
typedef HRESULT (*finish_fn)(void *ctx, BOOL restore, void **out);
typedef ULONG (*release_fn)(void *obj);
typedef struct { void *key, **items; int n, cap; } fm6_chain;
typedef struct { fm6_chain *v; int n, cap; } fm6_chains;
static SRWLOCK fm6_cl_lock = SRWLOCK_INIT;
static fm6_chains fm6_pending, fm6_lists;          /* chave: contexto diferido / lista devolvida ao jogo */
static struct { void **vt; release_fn orig; } fm6_cl_vt[4];

static fm6_chain *fm6_chain_find(fm6_chains *s, void *key)
{
    int i;
    for (i = 0; i < s->n; i++)
        if (s->v[i].key == key) return &s->v[i];
    return NULL;
}

static fm6_chain *fm6_chain_get(fm6_chains *s, void *key)
{
    fm6_chain *c = fm6_chain_find(s, key);
    if (c) return c;
    if (s->n == s->cap) {
        fm6_chain *v = realloc(s->v, (s->cap ? s->cap * 2 : 16) * sizeof *v);
        if (!v) return NULL;
        s->v = v;
        s->cap = s->cap ? s->cap * 2 : 16;
    }
    c = &s->v[s->n++];
    memset(c, 0, sizeof *c);
    c->key = key;
    return c;
}

/* tira a cadeia de key do conjunto (devolve as pecas para soltar fora do lock) */
static fm6_chain fm6_chain_take(fm6_chains *s, void *key)
{
    fm6_chain *c = fm6_chain_find(s, key), r = { 0 };
    if (!c) return r;
    r = *c;
    *c = s->v[--s->n];
    return r;
}

static int fm6_chain_push(fm6_chain *c, void *item)
{
    if (c->n == c->cap) {
        void **v = realloc(c->items, (c->cap ? c->cap * 2 : 8) * sizeof *v);
        if (!v) return 0;
        c->items = v;
        c->cap = c->cap ? c->cap * 2 : 8;
    }
    c->items[c->n++] = item;
    return 1;
}

static void fm6_obj_addref(void *obj) { ((release_fn)(*(void ***)obj)[1])(obj); }
static void fm6_obj_release(void *obj) { ((release_fn)(*(void ***)obj)[2])(obj); }

static void fm6_chain_free(fm6_chain *c)
{
    int i;
    for (i = 0; i < c->n; i++) fm6_obj_release(c->items[i]);
    free(c->items);
}

static ULONG fm6_cl_Release(void *cl)
{
    void **vt = *(void ***)cl;
    release_fn orig = NULL;
    ULONG r;
    size_t i;
    for (i = 0; i < sizeof fm6_cl_vt / sizeof *fm6_cl_vt; i++)
        if (fm6_cl_vt[i].vt == vt) orig = fm6_cl_vt[i].orig;
    if (!orig) return 0;                                   /* nao acontece: so vtables trocadas chegam aqui */
    if ((r = orig(cl)) == 0) {
        fm6_chain c;
        AcquireSRWLockExclusive(&fm6_cl_lock);
        c = fm6_chain_take(&fm6_lists, cl);
        ReleaseSRWLockExclusive(&fm6_cl_lock);
        fm6_chain_free(&c);
    }
    return r;
}

/* com o lock: troca o Release da vtable da lista (uma vez por vtable) */
static int fm6_cl_hook(void *cl)
{
    void **vt = *(void ***)cl, *fn = (void *)fm6_cl_Release;
    size_t i;
    for (i = 0; i < sizeof fm6_cl_vt / sizeof *fm6_cl_vt; i++) {
        if (fm6_cl_vt[i].vt == vt) return 1;
        if (fm6_cl_vt[i].vt) continue;
        fm6_cl_vt[i].orig = (release_fn)vt[2];
        fm6_cl_vt[i].vt = vt;
        write_mem(&vt[2], &fn, sizeof fn);
        return 1;
    }
    return 0;
}

static void fm6_ExecuteCommandList(void *ctx, void *cl, BOOL restore)
{
    void **az = *(void ***)ctx, **items = NULL;
    uint8_t *c = ctx;
    int i, n = 0;
    if (!cl || (c[AZ_CTX_DEFERRED] && c[AZ_CTX_BUNDLE])) { ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, cl, restore); return; }
    if (c[AZ_CTX_DEFERRED]) {
        static volatile LONG logged;
        void *part = NULL;
        fm6_chain *p, *e;
        HRESULT hr = ((finish_fn)az[AZ_SLOT_FINISH])(ctx, TRUE, &part);
        if (hr < 0 || !part) {
            wlog("d3d: ExecuteCommandList aninhado no diferido %p: FinishCommandList(TRUE) hr=%#lx", ctx, hr);
            ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, cl, restore);
            return;
        }
        AcquireSRWLockExclusive(&fm6_cl_lock);
        p = fm6_chain_get(&fm6_pending, ctx);
        if (p && fm6_chain_push(p, part)) {
            if ((e = fm6_chain_find(&fm6_lists, cl)) != NULL)
                for (i = 0; i < e->n; i++)
                    if (fm6_chain_push(p, e->items[i])) fm6_obj_addref(e->items[i]);
            if (fm6_chain_push(p, cl)) fm6_obj_addref(cl);
            n = p->n;
        } else
            part = NULL;
        ReleaseSRWLockExclusive(&fm6_cl_lock);
        if (!part) wlog("d3d: ExecuteCommandList aninhado no diferido %p: sem memoria; lista %p perdida", ctx, cl);
        if (!restore) ((void (*)(void *))az[AZ_SLOT_CLEAR])(ctx);
        if (InterlockedIncrement(&logged) <= 8)
            wlog("d3d: ExecuteCommandList aninhado no diferido %p: lista %p (restore %d) -> parte %p, %d pendentes",
                 ctx, cl, restore, part, n);
        return;
    }
    AcquireSRWLockShared(&fm6_cl_lock);
    {
        fm6_chain *e = fm6_chain_find(&fm6_lists, cl);
        if (e && (items = malloc(e->n * sizeof *items)) != NULL) {
            n = e->n;
            memcpy(items, e->items, n * sizeof *items);
            for (i = 0; i < n; i++) fm6_obj_addref(items[i]);
        }
    }
    ReleaseSRWLockShared(&fm6_cl_lock);
    {   /* a mesma lista aninhada mais de uma vez -> mesma command list do D3D12 de novo antes da fence (erro 553 da
           debug layer antes do 0x887a0007, 17_fm6.txt PASSO 33): so conta */
        static volatile LONG reps;
        int j, dup = 0;
        for (i = 0; i < n && !dup; i++)
            for (j = i + 1; j < n; j++)
                if (items[i] == items[j]) { dup = 1; break; }
        if (dup) {
            LONG k = InterlockedIncrement(&reps);
            if ((k & (k - 1)) == 0) wlog("d3d: ExecuteCommandList %p com lista aninhada repetida (%d pecas) (#%ld)", cl, n, k);
        }
    }
    for (i = 0; i < n; i++) {
        ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, items[i], restore);
        fm6_obj_release(items[i]);
    }
    free(items);
    ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, cl, restore);
}

static HRESULT fm6_FinishCommandList(void *ctx, BOOL restore, void **out)
{
    HRESULT hr = ((finish_fn)(*(void ***)ctx)[AZ_SLOT_FINISH])(ctx, restore, out);
    fm6_chain stale = { 0 }, p;
    if (hr < 0 || !out || !*out) return hr;
    AcquireSRWLockExclusive(&fm6_cl_lock);
    p = fm6_chain_take(&fm6_pending, ctx);
    stale = fm6_chain_take(&fm6_lists, *out);              /* endereco reaproveitado sem passar pelo Release */
    if (p.n) {
        fm6_chain *e = fm6_cl_hook(*out) ? fm6_chain_get(&fm6_lists, *out) : NULL;
        if (e) {
            *e = p;
            e->key = *out;
            p.n = 0;
            p.items = NULL;
        }
    }
    ReleaseSRWLockExclusive(&fm6_cl_lock);
    if (p.n) wlog("d3d: FinishCommandList do diferido %p: %d pecas aninhadas perdidas", ctx, p.n);
    fm6_chain_free(&p);
    fm6_chain_free(&stale);
    return hr;
}

int fm6_batch_install(uint8_t *game)
{
    static const struct { uint32_t rva; const char *orig; void *fn; } site[2] = {
        { FM6_BATCH_CB, "48895c240848896c2410488974241857" "4883ec30438d3480418bd8488bfac1e602488be93b7110",
          (void *)fm6_batch_add_cb },
        { FM6_BATCH_SRV, "48895c240848896c2410488974241857" "4883ec30418bf0488bda488be98d3cf6c1e7023b7914",
          (void *)fm6_batch_add_srv },
    };
    int i;
    for (i = 0; i < 2; i++)
        if (!bytes_match(game + site[i].rva, site[i].orig, strlen(site[i].orig) / 2)) {
            wlog("d3d: montador de batch do FM6 em game+%#x nao confere; batches sem conversao", site[i].rva);
            return 0;
        }
    for (i = 0; i < 2; i++) abs_jmp(game + site[i].rva, site[i].fn, 14);
    wlog("d3d: montadores de batch do FM6 (game+%#x CB, game+%#x SRV) -> registros com o objeto", FM6_BATCH_CB,
         FM6_BATCH_SRV);
    return 1;
}

/* monta em dst (0x870 bytes, ja copiados) a tabela do FM6 a partir da tabela do Azimuth src; devolve quantos slots
   triviais do driver entraram no lugar do stub fatal (ou de um slot novo) */
static int fm6_ctx_table(void **dst, void *const *src, int deferred)
{
    int s, n = 0;
    size_t i;
    for (s = 0; s < FM6_CTX_SLOTS; s++) {
        int a = fm6_ctx_slot(s);
        dst[s] = a < 0 ? NULL : src[a];
    }
    for (i = 0; i < sizeof fm6_ctx_trivial / sizeof *fm6_ctx_trivial; i++) {
        long (*fn)(void) = deferred ? fm6_ctx_trivial[i].def : fm6_ctx_trivial[i].imm;
        int s = fm6_ctx_trivial[i].slot, a = fm6_ctx_slot(s);
        if (!fn || (a >= 0 && src[a] != g_base + RVA_STUB_SLOT(a))) continue;
        dst[s] = (void *)fn;
        n++;
    }
    for (i = 0; i < sizeof fm6_ctx_srvs / sizeof *fm6_ctx_srvs; i++) dst[fm6_ctx_srvs[i].slot] = fm6_ctx_srvs[i].fn;
    for (i = 0; i < sizeof fm6_ctx_fast / sizeof *fm6_ctx_fast; i++) {
        int s = FM6_FAST_FIRST + (int)i, a = fm6_ctx_slot(s);
        if (src[a] == g_base + RVA_STUB_SLOT(a)) dst[s] = fm6_ctx_fast[i];
    }
    if (deferred) dst[0x31] = (void *)fm6_UpdateSubresource;
    if (src[fm6_ctx_slot(FM6_SLOT_SO_SET)] == g_base + RVA_STUB_SLOT(fm6_ctx_slot(FM6_SLOT_SO_SET)))
        dst[FM6_SLOT_SO_SET] = (void *)fm6_SOSetTargets;
    if (src[AZ_SLOT_EXECUTE] != g_base + RVA_STUB_SLOT(AZ_SLOT_EXECUTE))
        dst[0x3b] = (void *)fm6_ExecuteCommandList;
    if (deferred && src[AZ_SLOT_FINISH] != g_base + RVA_STUB_SLOT(AZ_SLOT_FINISH))
        dst[0x73] = (void *)fm6_FinishCommandList;
    if (src[fm6_ctx_slot(0xd7)] == g_base + RVA_STUB_SLOT(fm6_ctx_slot(0xd7)))
        dst[0xd7] = (void *)fm6_SetFastResourcesFromBatch;
    for (s = 0; s < FM6_CTX_SLOTS; s++)
        if (!dst[s]) dst[s] = g_base + RVA_STUB_SLOT(s);       /* novo sem comportamento: nao acontece hoje */
    return n;
}

static void *fm6_ctx_copy(void *dst, const void *src, size_t n, int deferred)
{
    static volatile LONG logged[2];
    int t;
    memcpy(dst, src, n);
    if (n != SLOTS * 8) return dst;
    t = fm6_ctx_table(dst, src, deferred);
    if (InterlockedIncrement(&logged[deferred]) == 1)
        wlog("d3d: contexto %s do FM6 %p: tabela embutida no layout do FM6 (0x%x slots, %d triviais do driver)",
             deferred ? "diferido" : "imediato", (uint8_t *)dst - 0x3d0, FM6_CTX_SLOTS, t);
    return dst;
}

void *fm6_ctx_copy_imm(void *dst, const void *src, size_t n) { return fm6_ctx_copy(dst, src, n, 0); }
void *fm6_ctx_copy_def(void *dst, const void *src, size_t n) { return fm6_ctx_copy(dst, src, n, 1); }

/* troca o call memcpy (e8 rel32) dos dois sites por call num stub perto do azimuth.exe (jmp abs p/ fm6_ctx_copy_*) */
static int fm6_ctx_install(void)
{
    static int done;
    static const struct { uint32_t rva; void *fn; } site[2] = {
        { RVA_CTX_COPY_IMM, (void *)fm6_ctx_copy_imm }, { RVA_CTX_COPY_DEF, (void *)fm6_ctx_copy_def },
    };
    uint8_t *stubs;
    int i;
    if (done) return 1;
    for (i = 0; i < 2; i++) {
        uint8_t *at = g_base + site[i].rva;
        if (at[0] != 0xe8 || at + 5 + *(int32_t *)(at + 1) != g_base + RVA_MEMCPY ||
            memcmp(at - 6, "\x41\xb8\x70\x08\x00\x00", 6) != 0) {
            wlog("d3d: copia da tabela do contexto em %#x nao confere; contexto do FM6 sem conversao", site[i].rva);
            return 0;
        }
    }
    if (!(stubs = alloc_near(g_base, 32))) { wlog("d3d: sem memoria perto do azimuth.exe para o contexto do FM6"); return 0; }
    for (i = 0; i < 2; i++) {
        uint8_t *at = g_base + site[i].rva, *st = stubs + i * 16, call[5] = { 0xe8 };
        int32_t rel = (int32_t)(st - (at + 5));
        memcpy(st, "\xff\x25\0\0\0\0", 6);
        memcpy(st + 6, &site[i].fn, 8);
        memcpy(call + 1, &rel, 4);
        write_mem(at, call, sizeof call);
    }
    FlushInstructionCache(GetCurrentProcess(), stubs, 32);
    done = 1;
    wlog("d3d: contexto do FM6: copias da tabela embutida (%#x, %#x) -> layout do FM6", RVA_CTX_COPY_IMM, RVA_CTX_COPY_DEF);
    return 1;
}

/* Objetos do FM6 (umd.dll do OS "era" contra o do FH2): ID3D11DeviceChild ganha um metodo no slot 8 (xor eax,eax;
   ret, logo apos o SetPrivateDataInterfaceGraphics; o mesmo do contexto) -> recurso FM6 9 GetType, 0xc GetDescriptor,
   0xd GetDesc; e IDXGIObject NAO tem o SetPrivateDataInterfaceGraphics (Set 3, SetI 4, Get 5, GetParent 6) ->
   game+0x53d476 IDXGIDevice[7](&adapter) = GetAdapter, IDXGIAdapter[6] = GetParent, IDXGIFactory[0x10] =
   CreateSwapChainForCoreWindow (Azimuth 8, 7, 0x11). As tabelas por tipo mudam no lugar, como no FM5. */
static void fm6_layout_table(void **t, int kind)
{
    if (kind == K_DXGI) drop_slot(t, 5);
    else if (kind == K_CHILD || kind == K_RES) {
        memmove(&t[9], &t[8], (SLOTS - 9) * sizeof *t);
        t[8] = (void *)ctx_ret0;
    }
}

/* CreateSwapChainForCoreWindow do Azimuth (0x14004f730 -> 0x14004eed0) so aceita Flags & ~2 == 0 (ou o perfil
   0x200000/R10G10B10A2) e para em "swap chain unsupported presentation profile". O FM6 (game+0x53d43f) pede
   Flags 0x100000, flag do XDK "era" que o do FH2 nao conhece; o Azimuth monta o descritor do host sozinho
   (Flags 0x40/0x800, 3 buffers) e nao usa as flags do jogo -> 0x100000 sai de uma copia do descritor. */
#define FM6_SC_FLAG_ERA 0x100000
typedef long (*create_sc_cw_fn)(void *factory, void *device, void *window, const uint32_t *desc,
                                void *restrict_output, void **out);
static create_sc_cw_fn orig_fm6_create_sc_cw;

static long fm6_CreateSwapChainForCoreWindow(void *factory, void *device, void *window, const uint32_t *desc,
                                             void *restrict_output, void **out)
{
    uint32_t d[12];   /* DXGI_SWAP_CHAIN_DESC1: w, h, fmt, stereo, sample {n, q}, usage, buffers, scaling,
                         swap effect, alpha, flags (+0x2c) */
    long hr;
    if (!desc) return orig_fm6_create_sc_cw(factory, device, window, desc, restrict_output, out);
    memcpy(d, desc, sizeof d);
    d[11] &= ~FM6_SC_FLAG_ERA;
    hr = orig_fm6_create_sc_cw(factory, device, window, d, restrict_output, out);
    wlog("d3d: CreateSwapChainForCoreWindow(FM6 %ux%u fmt=%#x buffers=%u flags=%#x -> %#x) hr=%#lx sc=%p",
         d[0], d[1], d[2], d[7], desc[11], d[11], hr, out ? *out : NULL);
    return hr;
}

static void fm6_object_layouts(void)
{
    typedef void **(*get_fn)(int);
    get_fn get = (get_fn)(g_base + RVA_VTBL_GET);
    static int done;
    void **factory;
    int t;
    if (done) return;
    done = 1;
    for (t = 0; t < NTYPES; t++) fm6_layout_table(get(t), type_kind[t]);
    factory = get(0xe);
    orig_fm6_create_sc_cw = (create_sc_cw_fn)factory[0x10];
    factory[0x10] = (void *)fm6_CreateSwapChainForCoreWindow;
    wlog("d3d: vtables dos objetos no layout do FM6 (DeviceChild +slot 8, DXGI sem o slot 5)");
}

/* FM6 (OS "era", XDK posterior ao do FH2): D3D11CreateDevice(0, 1, 0, 0x4000000, {0xb100}, 1, 0x20011, ...) em
   game+0x3a66f6, depois QI p/ ID3D11DeviceX e CreateDmaEngineContext no slot 0x3b (= layout do FH2). Device e
   contexto do Azimuth entregues sem proxy; so a tabela embutida do contexto muda de layout (fm6_ctx_install). */
static long WINAPI fm6_D3D11CreateDevice(void *adapter, UINT type, HMODULE sw, UINT flags,
                                         const UINT *levels, UINT nlevels, UINT sdk,
                                         void **out_dev, UINT *out_level, void **out_ctx)
{
    uint32_t desc[16] = { 0x1000b, 0x4000000, 0, 0, 0, 0, 0x3f };
    void *dev = NULL, *ctx = NULL;
    long hr;

    wlog("d3d: D3D11CreateDevice(adapter=%p type=%u flags=%#x levels=%u:%#x sdk=%#x)",
         adapter, type, flags, nlevels, levels && nlevels ? levels[0] : 0, sdk);
    (void)sw;
    if (out_dev) *out_dev = NULL;
    if (out_ctx) *out_ctx = NULL;
    fm6_ctx_install();
    hr = ((createx_fn)(g_base + RVA_CREATE_DEVICEX))(desc, &dev, &ctx);
    wlog("d3d: D3D11XCreateDeviceX hr=%#lx device=%p contexto=%p (FM6, device no layout do FH2)", hr, dev, ctx);
    if (hr < 0) return hr;
    wlog("d3d: device do FM6: %d slots triviais do driver do console no lugar do stub fatal",
         fm6_device_fixups(*(void ***)dev));
    fm6_object_layouts();
    if (out_level) *out_level = 0xb100;
    if (out_dev) *out_dev = dev;
    if (out_ctx) *out_ctx = ctx;
    return hr;
}

/* ------------------------------------------------------------------------ */
/* XAudio2Create                                                              */
/* ------------------------------------------------------------------------ */
/* O FM5 importa XAudio2_9.dll ordinal 1 = XAudio2Create(ppXAudio2, Flags, XAudio2Processor). O FH2 nao importa:
 * faz LoadLibraryExW("xaudio2_9.dll") + GetProcAddress("CreateXAudio2Object") (HLE 0x14009a0c0 / 0x14009a450)
 * -> 0x140181b10(pp, flags, processor, x): exige flags & ~0x400 == 0, processor == -1 e x == 0 ("alternate
 * profile"); ignora o processador (XAudio2Create do desktop com (0, 0)) e embrulha no Engine do Azimuth. */
#define RVA_CREATE_XAUDIO2 0x181b10
#define RVA_XA2_GUARD      0x38741c  /* guard do static "HMODULE do xaudio2_9.dll" (_Init_thread_header) */
#define RVA_XA2_HMOD       0x387420
typedef long (*cxa2_fn)(void **pp, UINT flags, UINT processor, uintptr_t x);

/* O static do 0x140181b10 so e inicializado se a epoca da thread ([TLS[0]+0x10], o exe assume _tls_index 0)
 * for MENOR que o guard (0 = nao inicializado; epocas comecam em INT_MIN). Na thread do jogo TLS[0] e o bloco
 * TLS do FM5 (VC11, sem statics thread-safe): +0x10 e variavel do jogo (>= 0) -> o init era pulado, HMODULE nulo
 * -> "CreateXAudio2Object requires the installed desktop xaudio2_9.dll". Aqui o static e preenchido e o guard
 * marcado como inicializado (INT_MIN: nenhuma epoca e menor). */
static long WINAPI fm5_XAudio2Create(void **pp, UINT flags, UINT processor)
{
    volatile LONG *guard = (volatile LONG *)(g_base + RVA_XA2_GUARD);
    HMODULE *hmod = (HMODULE *)(g_base + RVA_XA2_HMOD);
    long hr;
    if (*guard == 0) {
        HMODULE m = LoadLibraryExW(L"xaudio2_9.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m && InterlockedCompareExchangePointer((void **)hmod, m, NULL) == NULL)
            InterlockedCompareExchange(guard, (LONG)0x80000000, 0);
        wlog("xaudio2: xaudio2_9.dll = %p (epoca da thread %d)", m,
             *(int *)(*(uint8_t **)__readgsqword(0x58) + 0x10));
    }
    hr = ((cxa2_fn)(g_base + RVA_CREATE_XAUDIO2))(pp, flags & 0x400, 0xffffffff, 0);
    wlog("xaudio2: XAudio2Create(flags=%#x processor=%#x) hr=%#lx engine=%p", flags, processor, hr, pp ? *pp : NULL);
    return hr;
}

/* ------------------------------------------------------------------------ */
/* DXGIXPresentArray                                                          */
/* ------------------------------------------------------------------------ */
/* HLE 0x140050f00(SyncInterval, Threshold, Flags, Count, ppSwapChains, pParams) valida 13 dwords de
 * DXGIX_PRESENTARRAY_PARAMETERS (XDK do FH2): [0] Disable, [1] UsePreviousBuffer, [2..5] SourceRect,
 * [6..7] destino, [8..9] escala (0 ou 1.0f), [0xc] == 0. O XDK do FM5 tem so 12 dwords (0x30): FM5 0x714731
 * monta {0,0, 0,0,w,h, 0,0, 1.0f,1.0f, 0,0} em rsp+0x30 e rsp+0x60 ([0xc]) e lixo de pilha -> "composition
 * parameters=". Aqui os 12 dwords sao copiados e [0xc] = 0. */
#define RVA_PRESENT_ARRAY 0x50f00
typedef void (*present_array_fn)(UINT sync, UINT threshold, UINT flags, UINT count, void **swapchains,
                                 const UINT *params);

/* Captura com RenderDoc (injetado por renderdoccmd capture): FM5_RDOC_FRAMES="1500,1800" -> TriggerCapture antes
 * desses presents (API in-app 1.6.0: indice 11 SetCaptureFilePathTemplate, 15 TriggerCapture). */
static void rdoc_maybe_capture(LONG n)
{
    static int init;
    static void **api;
    static LONG frames[16];
    static int nframes;
    int i;
    if (!init) {
        char v[256] = "", *t, *ctx = NULL;
        HMODULE m;
        init = 1;
        GetEnvironmentVariableA("FM5_RDOC_FRAMES", v, sizeof v);
        for (t = strtok_s(v, ",", &ctx); t && nframes < 16; t = strtok_s(NULL, ",", &ctx)) frames[nframes++] = atol(t);
        m = GetModuleHandleA("renderdoc.dll");
        if (m && nframes) {
            typedef int (*getapi_fn)(int ver, void **out);
            getapi_fn ga = (getapi_fn)GetProcAddress(m, "RENDERDOC_GetAPI");
            if (!ga || !ga(10600, (void **)&api)) api = NULL;
        }
        wlog("rdoc: renderdoc.dll %p api %p, %d frames", m, api, nframes);
    }
    if (!api) return;
    for (i = 0; i < nframes; i++)
        if (frames[i] == n) {
            ((void (*)(void))api[15])();
            wlog("rdoc: TriggerCapture no present #%ld", n);
        }
}

static void fm5_DXGIXPresentArray(UINT sync, UINT threshold, UINT flags, UINT count, void **swapchains,
                                  const UINT *params)
{
    UINT p[16] = {0};
    LONG n = InterlockedIncrement(&present_count);
    size_t k;
    rdoc_maybe_capture(n);
    {   /* fps medido (1 s), logado quando muda: confere o modo de present (PASSO 10) */
        static ULONGLONG t0;
        static LONG n0, last_fps;
        ULONGLONG t = GetTickCount64();
        if (!t0) { t0 = t; n0 = n; }
        else if (t - t0 >= 1000) {
            LONG fps = (LONG)(((n - n0) * 1000 + (LONG)(t - t0) / 2) / (LONG)(t - t0));
            if (fps < last_fps - 3 || fps > last_fps + 3) {
                wlog("d3d: fps %ld (present #%ld, sync=%u)", fps, n, sync);
                last_fps = fps;
            }
            t0 = t; n0 = n;
        }
    }
    LONG on = 0;
    for (k = 0; k < sizeof trace_frames / sizeof trace_frames[0]; k++) if ((uint32_t)n == trace_frames[k]) on = 1;
    if (on || frame_trace_on)
        wlog("T PRESENT #%ld sync=%u flags=%#x count=%u sc=%p", n, sync, flags, count, swapchains ? swapchains[0] : 0);
    frame_trace_on = on;
    ctx_trace_on = on || trace_bundles > 0;
    {   /* teste: FM5_DEBUG_CLEAR=1 pinta o backbuffer de vermelho antes do Present */
        int dbg = dbg_clear;
        typedef void (*clear_fn)(void *ctx, void *rtv, const float *c);
        static const float red[4] = { 1, 0, 0, 1 };
        if (dbg < 0) { char v[8] = ""; GetEnvironmentVariableA("FM5_DEBUG_CLEAR", v, sizeof v); dbg = dbg_clear = atoi(v);
                       wlog("d3d: present na thread %lu, FM5_DEBUG_CLEAR=%d rtv=%p", GetCurrentThreadId(), dbg, sc_rtv); }
        if (dbg == 1 && sc_rtv && nproxies)
            ((clear_fn)(*(void ***)proxies[0].real)[0x32])(proxies[0].real, sc_rtv, red);
    }
    if (params) memcpy(p, params, 12 * sizeof(UINT));
    {   /* tempo dentro do Present do host e entre Presents (diagnostico do limite de fps, PASSO 10 cont.) */
        static LARGE_INTEGER f, prev;
        static LONG64 in_sum, out_sum;
        static LONG cnt;
        LARGE_INTEGER a, b;
        if (!f.QuadPart) QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        if (prev.QuadPart) out_sum += a.QuadPart - prev.QuadPart;
        ((present_array_fn)(g_base + RVA_PRESENT_ARRAY))(sync, threshold, flags, count, swapchains, params ? p : NULL);
        QueryPerformanceCounter(&b);
        prev = b; in_sum += b.QuadPart - a.QuadPart;
        if (++cnt == 600) {
            wlog("d3d: present: media %.2f ms no host, %.2f ms no jogo (600 frames)", in_sum * 1000.0 / f.QuadPart / cnt,
                 out_sum * 1000.0 / f.QuadPart / cnt);
            cnt = 0; in_sum = out_sum = 0;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Perfil de esperas (diagnostico, FM5_WAITPROF=1): Sleep/Wait* importados do kernelx pelo FM5, agregados por   */
/* endereco de retorno (contagem, tempo total, ultimo timeout); log a cada 4 s (8 vezes). PASSO 10 (cont.)     */
/* ------------------------------------------------------------------------ */
#define WP_N 512
static struct { volatile LONG64 ret; volatile LONG n; volatile LONG64 ticks; volatile LONG tmo; } wp[WP_N];
static FARPROC wp_orig[6];
static LARGE_INTEGER wp_freq;

static void wp_add(uintptr_t ret, LONG64 t, DWORD tmo)
{
    unsigned i = (unsigned)((ret >> 2) * 2654435761u) % WP_N, k;
    for (k = 0; k < WP_N; k++, i = (i + 1) % WP_N) {
        LONG64 cur = wp[i].ret;
        if (cur == 0) cur = InterlockedCompareExchange64(&wp[i].ret, (LONG64)ret, 0), cur = cur ? cur : (LONG64)ret;
        if (cur == (LONG64)ret) {
            InterlockedIncrement(&wp[i].n); InterlockedAdd64(&wp[i].ticks, t); wp[i].tmo = (LONG)tmo; return;
        }
    }
}
#define WP_BEGIN LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
#define WP_END(tmo) QueryPerformanceCounter(&b); wp_add((uintptr_t)__builtin_return_address(0), b.QuadPart - a.QuadPart, tmo);
static void WINAPI wp_Sleep(DWORD ms)
{ WP_BEGIN ((void (WINAPI *)(DWORD))wp_orig[0])(ms); WP_END(ms) }
static DWORD WINAPI wp_SleepEx(DWORD ms, BOOL al)
{ DWORD r; WP_BEGIN r = ((DWORD (WINAPI *)(DWORD, BOOL))wp_orig[1])(ms, al); WP_END(ms) return r; }
static DWORD WINAPI wp_WFSO(HANDLE h, DWORD ms)
{ DWORD r; WP_BEGIN r = ((DWORD (WINAPI *)(HANDLE, DWORD))wp_orig[2])(h, ms); WP_END(ms) return r; }
static DWORD WINAPI wp_WFSOEx(HANDLE h, DWORD ms, BOOL al)
{ DWORD r; WP_BEGIN r = ((DWORD (WINAPI *)(HANDLE, DWORD, BOOL))wp_orig[3])(h, ms, al); WP_END(ms) return r; }
static DWORD WINAPI wp_WFMO(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{ DWORD r; WP_BEGIN r = ((DWORD (WINAPI *)(DWORD, const HANDLE *, BOOL, DWORD))wp_orig[4])(n, h, all, ms); WP_END(ms) return r; }
static DWORD WINAPI wp_WFMOEx(DWORD n, const HANDLE *h, BOOL all, DWORD ms, BOOL al)
{ DWORD r; WP_BEGIN r = ((DWORD (WINAPI *)(DWORD, const HANDLE *, BOOL, DWORD, BOOL))wp_orig[5])(n, h, all, ms, al); WP_END(ms) return r; }

static DWORD WINAPI wp_thread(void *arg)
{
    static LONG pn[WP_N]; static LONG64 pt[WP_N];
    int r, i, j;
    (void)arg;
    for (r = 0; r < 8; r++) {
        int idx[WP_N], m = 0;
        LONG64 d[WP_N];
        Sleep(4000);
        for (i = 0; i < WP_N; i++) {
            d[i] = wp[i].ret ? wp[i].ticks - pt[i] : 0;
            if (d[i] > 0) idx[m++] = i;
        }
        for (i = 0; i < m; i++) for (j = i + 1; j < m; j++) if (d[idx[j]] > d[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
        wlog("waitprof ---- %d s", (r + 1) * 4);
        for (i = 0; i < m && i < 16; i++) {
            int k = idx[i];
            wlog("waitprof  game%+lld  n=%ld  %.1f ms  tmo=%ld", (long long)(wp[k].ret - (LONG64)(uintptr_t)g_game),
                 wp[k].n - pn[k], d[k] * 1000.0 / wp_freq.QuadPart, wp[k].tmo);
        }
        for (i = 0; i < WP_N; i++) { pn[i] = wp[i].n; pt[i] = wp[i].ticks; }
    }
    return 0;
}

static FARPROC wp_hook(const uint8_t *entry, FARPROC p)
{
    static const char *const names[6] = { "Sleep", "SleepEx", "WaitForSingleObject", "WaitForSingleObjectEx",
                                          "WaitForMultipleObjects", "WaitForMultipleObjectsEx" };
    static void *const hooks[6] = { (void *)wp_Sleep, (void *)wp_SleepEx, (void *)wp_WFSO, (void *)wp_WFSOEx,
                                    (void *)wp_WFMO, (void *)wp_WFMOEx };
    static int on = -1;
    int i;
    if (on < 0) {
        char v[8] = "";
        GetEnvironmentVariableA("FM5_WAITPROF", v, sizeof v);
        on = atoi(v) > 0;
        if (on) { QueryPerformanceFrequency(&wp_freq); CloseHandle(CreateThread(NULL, 0, wp_thread, NULL, 0, NULL)); }
    }
    if (!on || !p || !str_is(entry, "kernelx.dll")) return p;
    for (i = 0; i < 6; i++)
        if (str_is(entry + 0x20, names[i])) { wp_orig[i] = p; wlog("waitprof: kernelx!%s", names[i]); return (FARPROC)hooks[i]; }
    return p;
}

/* ------------------------------------------------------------------------ */
/* Resolvedor de imports                                                      */
/* ------------------------------------------------------------------------ */
typedef FARPROC (*resolver_fn)(const uint8_t *entry);
static resolver_fn orig_resolver;

/* imports sem entrada no HLE do Azimuth (o FH2 nao usa), com a mesma assinatura do Win32 -> DLL real:
   GlobalMemoryStatusEx = thread de liberacao de memoria do FM5 (game+0x683b30), ullAvailPhys;
   GetThreadId (game+0x563ce5) e DuplicateHandle = FM6; StringFromGUID2/GetLocalTime = Xbox.Services.dll do FM6. */
static const struct { const char *dll, *name, *real; } import_fwd[] = {
    { "kernelx.dll", "GlobalMemoryStatusEx", "kernel32.dll" },
    { "kernelx.dll", "GetThreadId", "kernel32.dll" },
    { "kernelx.dll", "DuplicateHandle", "kernel32.dll" },
    { "kernelx.dll", "GetLocalTime", "kernel32.dll" },
    { "combase.dll", "StringFromGUID2", "combase.dll" },
};

static FARPROC import_fallback(const uint8_t *entry)
{
    size_t i;
    for (i = 0; i < sizeof import_fwd / sizeof *import_fwd; i++)
        if (str_is(entry, import_fwd[i].dll) && str_is(entry + 0x20, import_fwd[i].name)) {
            HMODULE m = LoadLibraryA(import_fwd[i].real);
            wlog("%s!%s -> %s", import_fwd[i].dll, import_fwd[i].name, import_fwd[i].real);
            return m ? GetProcAddress(m, import_fwd[i].name) : NULL;
        }
    return NULL;
}

/* DXGIXSetVLineNotification(contador, linha, evento): no console o evento e sinalizado quando o scanout
   passa pela linha. Aqui: uma thread sinaliza os eventos registrados a 60 Hz (prazo por QPC). */
#define VLINE_N 2
static HANDLE vline_event[VLINE_N];
static volatile LONG vline_started;

static DWORD WINAPI vline_thread(void *arg)
{
    LARGE_INTEGER f, next, now, due;
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    int i;
    (void)arg;
    if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&next);
    for (;;) {
        next.QuadPart += f.QuadPart / 60;
        QueryPerformanceCounter(&now);
        if (next.QuadPart < now.QuadPart) next = now;
        due.QuadPart = -(LONGLONG)((next.QuadPart - now.QuadPart) * 10000000 / f.QuadPart);
        if (due.QuadPart && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        for (i = 0; i < VLINE_N; i++) {
            HANDLE e = vline_event[i];
            if (e) SetEvent(e);
        }
    }
    return 0;
}

static HRESULT fm6_DXGIXSetVLineNotification(UINT counter, UINT line, HANDLE event)
{
    if (counter >= VLINE_N) return E_INVALIDARG;
    vline_event[counter] = event;
    wlog("d3d: DXGIXSetVLineNotification(%u, linha %u, evento %p) -> 60 Hz", counter, line, event);
    if (event && !InterlockedExchange(&vline_started, 1)) CloseHandle(CreateThread(NULL, 0, vline_thread, NULL, 0, NULL));
    return S_OK;
}

/* XSAPI do FM6 (ctor do rastreador de chamadas, xsapi+0x5bde3) apaga d:\callHistoryJson.tmp e ignora o
   retorno. d: = armazenamento local persistente, que o manifesto do FM6 nao declara -> no console falha;
   o mapeador do Azimuth (0x1400b86f0: G:, T:, GAME:, letras >= H:) lanca "Unmapped host/device path". */
static BOOL (WINAPI *az_DeleteFileW)(const wchar_t *path);

static int is_d_drive(const wchar_t *p)
{
    return p && (p[0] | 0x20) == L'd' && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
}

static BOOL WINAPI fm6_DeleteFileW(const wchar_t *path)
{
    if (is_d_drive(path)) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return az_DeleteFileW(path);
}

/* FM6 (game+0x6531cc, XAudio2CreateWithVersionInfo do XDK "era") faz GetProcAddress("CreateXAudio2Object") =
   0x140181b10 e chama com (pp, 0x400, processador 0x10, 0) (game+0x5174ca); o Azimuth exige processador -1
   ("alternate profile") e depois ignora o processador -> mesma ponte do FM5 (fm5_XAudio2Create). */
static FARPROC (WINAPI *az_GetProcAddress)(HMODULE m, const char *name);

static FARPROC WINAPI fm6_GetProcAddress(HMODULE m, const char *name)
{
    FARPROC p = az_GetProcAddress(m, name);
    if ((uint8_t *)p == g_base + RVA_CREATE_XAUDIO2) {
        wlog("xaudio2: GetProcAddress(CreateXAudio2Object) -> ponte com processador -1 (FM6)");
        p = (FARPROC)fm5_XAudio2Create;
    }
    return p;
}

static FARPROC hook_resolver(const uint8_t *entry)
{
    FARPROC p = orig_resolver(entry);
    game_tls_apply();   /* 1o import: azimuth-tls.dll ja carregado, jogo ainda nao rodou */
    if (is_fm5()) {
        if (p && str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "DXGIXPresentArray")) {
            wlog("d3d: d3d11_x.dll!DXGIXPresentArray -> parametros de 12 dwords (XDK do FM5)");
            return (FARPROC)fm5_DXGIXPresentArray;
        }
        if (p) return wp_hook(entry, p);
        if (str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "D3D11CreateDevice")) {
            wlog("d3d: d3d11_x.dll!D3D11CreateDevice -> ponte do FM5");
            p = (FARPROC)fm5_D3D11CreateDevice;
        } else if (str_is(entry, "xaudio2_9.dll") && str_is(entry + 0x20, "#1")) {
            wlog("xaudio2: xaudio2_9.dll!#1 (XAudio2Create) -> CreateXAudio2Object do Azimuth");
            p = (FARPROC)fm5_XAudio2Create;
        }
    } else if (!is_fm6()) {
        return p;   /* FH2: exatamente o Azimuth */
    } else if (!p && str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "DXGIXSetVLineNotification")) {
        wlog("d3d: d3d11_x.dll!DXGIXSetVLineNotification -> evento a 60 Hz");
        p = (FARPROC)fm6_DXGIXSetVLineNotification;
    } else if (!p && str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "D3D11CreateDevice")) {
        wlog("d3d: d3d11_x.dll!D3D11CreateDevice -> D3D11XCreateDeviceX do Azimuth (FM6)");
        p = (FARPROC)fm6_D3D11CreateDevice;
    } else if (p && str_is(entry, "kernelx.dll") && str_is(entry + 0x20, "DeleteFileW")) {
        if (!az_DeleteFileW) wlog("kernelx.dll!DeleteFileW -> d:\\ falha com ERROR_PATH_NOT_FOUND (sem PLS)");
        az_DeleteFileW = (BOOL (WINAPI *)(const wchar_t *))p;
        p = (FARPROC)fm6_DeleteFileW;
    } else if (p && str_is(entry, "kernelx.dll") && str_is(entry + 0x20, "GetProcAddress")) {
        az_GetProcAddress = (FARPROC (WINAPI *)(HMODULE, const char *))p;
        p = (FARPROC)fm6_GetProcAddress;
    }
    if (!p) p = import_fallback(entry);
    if (!p) {
        size_t nd, nn;
        const char *d = msvc_str(entry, &nd), *n = msvc_str(entry + 0x20, &nn);
        wlog("import sem resolucao (thunk fatal se chamado): %.*s!%.*s", (int)nd, d, (int)nn, n);
    }
    return p;
}

extern int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp);

/* ------------------------------------------------------------------------ */
/* Diagnostico: draws pulados (0x14012b780) e busca de pipeline (0x1401338e0)   */
/* ------------------------------------------------------------------------ */
#define RVA_DRAW_SKIP   0x12b780   /* draw descartado: 0x14012a6d0 nao achou pipeline */
#define RVA_PSO_LOOKUP  0x1338e0   /* (ctx, esperar) -> pipeline ou 0 */
static void (*orig_draw_skip)(uint8_t *ctx);
static void *(*orig_pso_lookup)(uint8_t *ctx, int wait);
volatile LONG skip_counts[64], pso_null_counts[64];

static void hook_draw_skip(uint8_t *ctx)
{
    int i = proxy_index(ctx);
    InterlockedIncrement(&skip_counts[i < 0 ? 63 : i]);
    orig_draw_skip(ctx);
}

static void *hook_pso_lookup(uint8_t *ctx, int wait)
{
    void *p = orig_pso_lookup(ctx, wait);
    if (!p) {
        int i = proxy_index(ctx);
        LONG n = InterlockedIncrement(&pso_null_counts[i < 0 ? 63 : i]);
        if (n <= 5 || (n & (n - 1)) == 0)
            wlog("d3d: pipeline nulo #%ld no contexto c%d (esperar=%d) vs=%p ps=%p", n, i, wait,
                 *(void **)(ctx + 0x40d0), *(void **)(ctx + 0x40f0));
    }
    return p;
}

void trace_skips(void)
{
    char line[512];
    int i, len = 0;
    for (i = 0; i < 64; i++)
        if (skip_counts[i] || pso_null_counts[i])
            len += _snprintf(line + len, sizeof line - len, " c%d: pulados=%ld nulos=%ld", i == 63 ? -1 : i,
                             skip_counts[i], pso_null_counts[i]);
    if (len) wlog("trace draws pulados (acumulado):%s", line);
}

int d3d_install(void)
{
    if (!hook_function(RVA_DRAW_SKIP, "80b9480c000000488b91400c0000", 14, (void *)hook_draw_skip,
                       (void **)&orig_draw_skip)) return 0;
    if (!hook_function(RVA_PSO_LOOKUP, "48895c241848897c24205541564157", 15, (void *)hook_pso_lookup,
                       (void **)&orig_pso_lookup)) return 0;
    if (!hook_function(RVA_RELEASE, RELEASE_HEX, RELEASE_STOLEN, (void *)pd_release_hook,
                       (void **)&orig_release)) return 0;
    return hook_function(RVA_RESOLVER, "48895c2410488974241848897c2420", RESOLVER_STOLEN,
                         (void *)hook_resolver, (void **)&orig_resolver);
}
