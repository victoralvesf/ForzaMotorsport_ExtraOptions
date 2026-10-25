/*
 * Device/contexto D3D11 do FM6 sobre o D3D11.X do Azimuth (layout do FH2; docs\contexto\fm6\): slots triviais do
 * driver do console, texturas/views/shaders no formato do PC, heranca de estado dos bundles, batches de SetFast*,
 * imports do d3d11_x/kernelx que o Azimuth nao tem. So roda com o FM6 carregado (fm6_game).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../proxy.h"
#include "../common/d3d.h"
#include "fm6.h"

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
    if (!desc || desc[5] <= 1 || desc[6] == 0 || desc[6] == 0xffffffff) {
        hr = orig_fm6_create_tex2d(dev, desc, init, out);
        liv_note_texture(desc, hr);
        if (desc && desc[0] >= 1280 && hr >= 0 && out && *out) {   /* texturas de tela: aliasing da ESRAM */
            wlog("d3d: CreateTexture2D(FM6 %ux%u fmt=%u samples=%u bind=%#x misc=%#x esram[0xb]=%#x) hr=%#lx tex=%p",
                 desc[0], desc[1], desc[4], desc[5], desc[8], desc[10], desc[11], hr, *out);
            alias_track_ex(desc, *out, 0);
        }
        return hr;
    }
    memcpy(d, desc, 12 * sizeof(UINT));
    d[6] = 0xffffffff;
    hr = orig_fm6_create_tex2d(dev, d, init, out);
    liv_note_texture(desc, hr);
    if (hr >= 0 && out && *out) msaa_q_set(*out, desc[6]);
    wlog("d3d: CreateTexture2D(FM6 %ux%u fmt=%u samples=%u quality %#x -> default, bind=%#x misc=%#x) hr=%#lx tex=%p",
         d[0], d[1], d[4], d[5], desc[6], d[8], d[10], hr, out ? *out : NULL);
    if (hr >= 0 && out && *out) alias_track_ex(desc, *out, 0);
    return hr;
}

/* CreateRenderTargetView (slot 9) do FM6: views da cor MSAA da cena (aliasing, alias_rtv_note) */
static rtv_fn orig_fm6_create_rtv;
static long fm6_CreateRenderTargetView(void *dev, void *res, const UINT *desc, void **out)
{
    long hr = orig_fm6_create_rtv(dev, res, desc, out);
    if (hr >= 0 && out && *out) alias_rtv_note(*out, res);
    return hr;
}

/* CreateShaderResourceView (slot 7) do FM6: R8_UINT sobre a cor 2x (game+0x216dc7) = leitura da FMASK, que o
   Azimuth so emula em 4x/8x -> device removido; mesma FMASK emulada do FM5 (srv_fmask, PASSO 8 cont. 20). */
static srv_fn orig_fm6_create_srv;
static long fm6_CreateShaderResourceView(void *dev, void *res, const UINT *desc, void **out)
{
    return srv_fmask(dev, res, desc, out, orig_fm6_create_tex2d, orig_fm6_create_srv);
}

/* Create*Shader / CreateInputLayout do FM6: o bytecode passa por shader_console_rewrite (common/shader.c: max3 do console
   -> max) antes do Azimuth, que copia o bytecode (vetor proprio) -> o convertido e liberado na volta. O input
   layout recebe o mesmo bytecode convertido (o Azimuth casa shaders por hash). */
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
        uint8_t *p = shader_console_rewrite(code, len, &n);                                                         \
        long hr = ((create_shader_fn)orig_fm6_shader[s - FM6_SHADER_FIRST])(dev, p ? p : code, p ? n : len, lk, out); \
        free(p);                                                                                                \
        return hr;                                                                                              \
    }
FM6_SHADER(0x0c) FM6_SHADER(0x0d) FM6_SHADER(0x0f) FM6_SHADER(0x10) FM6_SHADER(0x11) FM6_SHADER(0x12)
static long fm6_create_shader_0x0e(void *dev, const void *code, SIZE_T len, const void *decl, UINT nd,
                                   const UINT *strides, UINT ns, UINT rast, void *lk, void **out)
{
    size_t n = 0;
    uint8_t *p = shader_console_rewrite(code, len, &n);
    long hr = ((create_gsso_fn)orig_fm6_shader[2])(dev, p ? p : code, p ? n : len, decl, nd, strides, ns, rast, lk, out);
    free(p);
    return hr;
}
static long fm6_CreateInputLayout(void *dev, const void *descs, UINT nd, const void *code, SIZE_T len, void **out)
{
    size_t n = 0;
    uint8_t *p = shader_console_rewrite(code, len, &n);
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
static __attribute__((noinline)) uintptr_t fm6_game_caller(void)
{
    uint8_t **sp = __builtin_frame_address(0), **top = ((NT_TIB *)NtCurrentTeb())->StackBase;
    int k = 0;
    for (; g_game && sp < top && sp < (uint8_t **)__builtin_frame_address(0) + 0x200; sp++)
        if (*sp > g_game && *sp < g_game + 0x1800000 && ++k == 2) return (uintptr_t)(*sp - g_game);
    return 0;
}

/* CreateBlendState (slot 0x14) do FM6: com LiveryLog loga os descs distintos com alpha-to-coverage (a plotagem de
   fabrica e decal com A2C; o xadrez do driver e tirado nos PSOs, common/gpuguard.c AlphaToCoverageNoDither). Desc
   D3D11_BLEND_DESC: [0] A2C, [1] IndependentBlendEnable, RT[i] = 8 dwords a partir de [2]. */
static state_fn orig_fm6_create_blend;
static long fm6_CreateBlendState(void *dev, const UINT *d, void **out)
{
    long hr = orig_fm6_create_blend(dev, d, out);
    if (d && d[0] && liv_log) {
        static UINT seen[32][10];
        static volatile LONG lock;
        static int nseen;
        int k, novo = 1;
        while (InterlockedExchange(&lock, 1)) SwitchToThread();
        for (k = 0; k < nseen; k++) if (!memcmp(seen[k], d, sizeof seen[k])) novo = 0;
        if (novo && nseen < 32) memcpy(seen[nseen++], d, sizeof seen[0]);
        InterlockedExchange(&lock, 0);
        if (novo)
            wlog("livery: CreateBlendState with alpha-to-coverage indep=%u rt0 {en=%u src=%u dst=%u op=%u srcA=%u dstA=%u "
                 "opA=%u mask=%#x} -> hr %#lx %p, caller game+%#zx", d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8],
                 d[9], hr, out ? *out : NULL, fm6_game_caller());
    }
    return hr;
}

static ptex2d_fn orig_fm6_placement_tex2d;
static long fm6_CreatePlacementTexture2D(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out)
{
    if (liv_log) liv_note_placement(desc, tile, pitch, addr, fm6_game_caller());
    if (desc && !az_placement_tex2d_ok(desc) && fm6_placement_uav_only(desc)) {
        static volatile LONG n;
        LONG k = InterlockedIncrement(&n);
        long hr = orig_fm6_create_tex2d(dev, desc, NULL, out);
        if (k <= 16 || (k & (k - 1)) == 0)
            wlog("d3d: CreatePlacementTexture2D(FM6) UAV only #%ld %ux%u mips=%u fmt=%u addr=%p -> committed texture "
                 "hr=%#lx tex=%p, caller game+%#zx", k, desc[0], desc[1], desc[2], desc[4], addr, hr, out ? *out : NULL,
                 fm6_game_caller());
        return hr;
    }
    if (desc && !az_placement_tex2d_ok(desc))
        wlog("d3d: CreatePlacementTexture2D(FM6) rejected by Azimuth: %ux%u mips=%u arr=%u fmt=%u samples=%u/%#x "
             "usage=%u bind=%#x cpu=%#x misc=%#x tile=%u pitch=%u addr=%p, caller game+%#zx", desc[0], desc[1], desc[2],
             desc[3], desc[4], desc[5], desc[6], desc[7], desc[8], desc[9], desc[10], tile, pitch, addr,
             fm6_game_caller());
    if (pa_tracking()) {   /* PASSO 99: endereco/chamador/objeto da placed (common/stutter.c pa_live_report) */
        long hr;
        pa_note_begin(addr, fm6_game_caller());
        hr = orig_fm6_placement_tex2d(dev, desc, tile, pitch, addr, out);
        pa_note_end(hr >= 0 && out ? *out : NULL);
        return hr;
    }
    return orig_fm6_placement_tex2d(dev, desc, tile, pitch, addr, out);
}

/* Heranca do OM no aninhado (17_fm6.txt PASSO 40/41): as listas dos workers sao DRAW BUNDLES do Azimuth (lista sem
   estado do D3D12, [cl+0x48] nulo) aninhados no diferido juntador (game+0xf9510). No console o aninhado e um salto
   de command buffer e o bundle desenha no RT/viewport que o juntador ligou; o Azimuth reproduz o bundle com o
   estado do IMEDIATO (operacao 0x26 em 0x140111820), que nao tem o RT do juntador (ele esta na command list do
   D3D12 do juntador) -> 3384 draws sem RT/DS = cena cinza. O proxy acompanha, por diferido, RT/DS, viewports e
   scissors; no aninhado de um bundle guarda uma copia (views com AddRef) na cadeia, antes do bundle, e o imediato
   aplica a copia (OMSetRenderTargets/RSSetViewports/RSSetScissorRects do Azimuth) antes de executar o bundle.
   Tambem registra flags/chamador dos diferidos e o dono das listas aninhadas (log).
   PASSO 45: o bundle tambem herda depth-stencil (+stencil ref), blend (+fator/mascara) e rasterizer do juntador;
   sem eles os PSOs do mundo saiam com o padrao do D3D11 (depth Less) sobre a prof limpa em 0 (reversed-Z). */
#define AZ_SLOT_OM        0x21
#define AZ_SLOT_OM_UAV    0x22
#define AZ_SLOT_BLEND     0x23
#define AZ_SLOT_DEPTH     0x24
#define AZ_SLOT_RS        0x2b
#define AZ_SLOT_VIEWPORTS 0x2c
#define AZ_SLOT_SCISSORS  0x2d
#define AZ_SLOT_CLEARST   0x6e
#define AZ_LIST_STATE     0x48          /* estado do D3D12 da lista; nulo = draw bundle */
#define FM6_KEEP_RTS      0xffffffffu   /* D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL */
/* Azimuth (layout com o slot 7) por estagio do driver (0 VS, 1 HS, 2 DS, 3 GS, 4 PS, 5 CS): CB e SRV */
static const uint8_t fm6_stage_cb[6] = { 0x08, 0x3e, 0x42, 0x17, 0x11, 0x47 };
static const uint8_t fm6_stage_srv[6] = { 0x19, 0x3b, 0x3f, 0x1f, 0x09, 0x43 };
typedef struct {
    UINT nrtv, nvp, nsc;
    void *rtv[8], *dsv;
    float vp[16][6];
    RECT sc[16];
    /* CB/SRV estacionados pelo SetFastResourcesFromBatch (n = ultimo slot ligado + 1, por estagio do driver) */
    uint8_t ncb[6], nsrv[6];
    void *cb[6][14], *srv[6][128];
    uint64_t cbset[6][1], srvset[6][2];   /* slots gravados (objeto ou NULL): so esses vao para o imediato */
    /* estados de pipeline ligados (FM6_HAS_*: NULL ligado = padrao, tambem herdado) */
    UINT has, stencil_ref, sample_mask;
    void *blend, *depth, *rs;
    float blend_factor[4];
} fm6_om;
#define FM6_HAS_BLEND 1u
#define FM6_HAS_DEPTH 2u
#define FM6_HAS_RS    4u
/* sombra, por diferido, de um placement buffer que ele atualiza pelo UpdateSubresource (dirty = na lista atual).
   O conteudo e uma versao imutavel depois de compartilhada (copy-on-write): a copia da heranca e a tabela do
   imediato so seguram uma referencia, e versao igual = mesmo ponteiro */
#define FM6_UPD_MAX   8
#define FM6_UPD_BYTES 0x10000u
typedef struct { volatile LONG ref; UINT size; uint8_t data[]; } fm6_ver;
typedef struct { void *buf; UINT size, dirty; fm6_ver *cur; } fm6_upd;
typedef struct { void *ctx; UINT flags; uintptr_t caller; volatile LONG om, om_empty; int idx; fm6_om st;
                 fm6_upd upd[FM6_UPD_MAX]; } fm6_dctx_info;
extern int st_on;
static fm6_dctx_info fm6_dctx[128];
static volatile LONG fm6_dctx_n;
static defctx_fn orig_fm6_defctx[3];
static struct { void *ctx; void *const *src; } fm6_ctx_src[256];   /* diferido -> tabela do Azimuth de onde veio */
static volatile LONG fm6_ctx_src_n;

/* PASSO 117: chamado em todo Set* dos diferidos; a busca linear passava por ate 128 entradas de ~8 KB (falta de cache
   em cada uma) -> ultima resposta por thread, valida enquanto nenhum diferido novo entrou (o mais novo ganha) */
static fm6_dctx_info *fm6_dctx_find(const void *ctx)
{
    static __thread const void *last_ctx;
    static __thread fm6_dctx_info *last_d;
    static __thread LONG last_n;
    LONG i, n = fm6_dctx_n < 128 ? fm6_dctx_n : 128;
    if (ctx && ctx == last_ctx && n == last_n && last_d->ctx == ctx) return last_d;
    for (i = n - 1; i >= 0; i--)
        if (fm6_dctx[i].ctx == ctx) {
            last_ctx = ctx; last_d = &fm6_dctx[i]; last_n = n;
            return &fm6_dctx[i];
        }
    return NULL;
}

static void fm6_ctx_src_note(void *ctx, void *const *src)
{
    LONG i = InterlockedIncrement(&fm6_ctx_src_n) - 1;
    fm6_ctx_src[i & 255].src = src;
    fm6_ctx_src[i & 255].ctx = ctx;
}

/* metodo a do Azimuth para o diferido ctx: a tabela da qual a copia embutida foi montada (diferido comum e draw
   bundle tem tabelas diferentes); sem registro, a tabela em [ctx] */
static void *fm6_az_fn(void *ctx, int a)
{
    LONG i, n = fm6_ctx_src_n < 256 ? fm6_ctx_src_n : 256;
    for (i = 0; i < n; i++)
        if (fm6_ctx_src[(fm6_ctx_src_n - 1 - i) & 255].ctx == ctx) return fm6_ctx_src[(fm6_ctx_src_n - 1 - i) & 255].src[a];
    return (*(void ***)ctx)[a];
}

static long fm6_defctx(int which, void *dev, UINT flags, void **out)
{
    static const char *const names[3] = { "CreateDeferredContext", "CreateDeferredContext1", "CreateDeferredContextX" };
    uintptr_t caller = fm6_game_caller();
    long hr = orig_fm6_defctx[which](dev, flags, out);
    if (hr >= 0 && out && *out) {
        LONG i = InterlockedIncrement(&fm6_dctx_n) - 1;
        if (i < 128) {
            memset(&fm6_dctx[i].st, 0, sizeof fm6_dctx[i].st);
            fm6_dctx[i].flags = flags;
            fm6_dctx[i].caller = caller;
            fm6_dctx[i].idx = (int)i;
            fm6_dctx[i].ctx = *out;
        }
        wlog("d3d: FM6 %s flags=%#x -> deferred d%ld %p, caller game+%#zx", names[which], flags, i, *out, caller);
    }
    return hr;
}
static long fm6_defctx0(void *dev, UINT flags, void **out) { return fm6_defctx(0, dev, flags, out); }
static long fm6_defctx1(void *dev, UINT flags, void **out) { return fm6_defctx(1, dev, flags, out); }
static long fm6_defctxx(void *dev, UINT flags, void **out) { return fm6_defctx(2, dev, flags, out); }

static void fm6_om_set_rts(void *ctx, UINT n, void *const *rtvs, void *dsv)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    UINT i;
    if (!d) return;
    InterlockedIncrement(&d->om);
    if (!n && !dsv) InterlockedIncrement(&d->om_empty);
    d->st.nrtv = n > 8 ? 8 : n;
    for (i = 0; i < 8; i++) d->st.rtv[i] = i < d->st.nrtv && rtvs ? rtvs[i] : NULL;
    d->st.dsv = dsv;
}

static void fm6_om_reset(void *ctx)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (d) memset(&d->st, 0, sizeof d->st);
}

/* FM6 0x22/0x23/0x2d/0x2e/0x6f do diferido (Azimuth 0x21/0x22/0x2c/0x2d/0x6e): acompanha e repassa. 0x22/0x23/0x6f
   e o Dispatch (FM6 0x2a = Azimuth 0x29) tambem no imediato: aliasing da ESRAM (alias_fm6_*) */
static void fm6_def_OMSetRenderTargets(void *ctx, UINT n, void *const *rtvs, void *dsv)
{
    fm6_om_set_rts(ctx, n, rtvs, dsv);
    ((void (*)(void *, UINT, void *const *, void *))fm6_az_fn(ctx, AZ_SLOT_OM))(ctx, n, rtvs, dsv);
    alias_fm6_leave_scene(ctx, n, rtvs, 0, fm6_az_fn);
}
static void fm6_def_OMSetRenderTargetsUAV(void *ctx, UINT n, void *const *rtvs, void *dsv, UINT s, UINT nu,
                                          void *const *uavs, const UINT *counts)
{
    if (n != FM6_KEEP_RTS) fm6_om_set_rts(ctx, n, rtvs, dsv);
    ((void (*)(void *, UINT, void *const *, void *, UINT, UINT, void *const *, const UINT *))fm6_az_fn(ctx, AZ_SLOT_OM_UAV))(
        ctx, n, rtvs, dsv, s, nu, uavs, counts);
    alias_fm6_leave_scene(ctx, n, rtvs, 0, fm6_az_fn);
}
#define AZ_SLOT_DISPATCH  0x29
static void fm6_Dispatch(void *ctx, UINT x, UINT y, UINT z)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    liv_note_dispatch(x, y, z);
    alias_fm6_dispatch(ctx, x, y, z, d && (d->flags & 0x60000) == 0x60000, fm6_az_fn);
    ((void (*)(void *, UINT, UINT, UINT))fm6_az_fn(ctx, AZ_SLOT_DISPATCH))(ctx, x, y, z);
}
/* ResolveSubresource (FM6 0x3a = Azimuth 0x39): so repassa; dentro da janela da plotagem vai para o log */
#define AZ_SLOT_RESOLVE  0x39
static void fm6_ResolveSubresource(void *ctx, void *dst, UINT dsub, void *src, UINT ssub, UINT fmt)
{
    if (liv_in_window()) {
        const uint8_t *s = src, *d = dst;
        liv_note_resolve(-1, s ? *(const UINT *)(s + 0x70) : 0, s ? *(const UINT *)(s + 0x74) : 0,
                         s ? *(const UINT *)(s + 0x80) : 0, s ? *(const UINT *)(s + 0x84) : 0,
                         d ? *(const UINT *)(d + 0x70) : 0, d ? *(const UINT *)(d + 0x74) : 0,
                         d ? *(const UINT *)(d + 0x80) : 0, d ? *(const UINT *)(d + 0x84) : 0, fmt);
    }
    ((void (*)(void *, void *, UINT, void *, UINT, UINT))fm6_az_fn(ctx, AZ_SLOT_RESOLVE))(ctx, dst, dsub, src, ssub, fmt);
}
/* UiTrace (debug.ini, dev): cada viewport/scissor distinto (chamador no jogo + valores) vai para o log uma vez, para
   achar elementos que ficam fora do lugar com a resolucao do launcher (UI em 1920x1080, render maior) */
int fm6_ui_trace;
static void fm6_rs_trace(int sc, uintptr_t ret, UINT n, const void *p)
{
    static struct { uintptr_t ret; int32_t v[4]; uint8_t sc; } seen[512];
    static volatile LONG nseen, dropped;
    static SRWLOCK lock = SRWLOCK_INIT;
    int32_t v[4] = { 0 };
    LONG i, k;
    if (!p || !n) return;
    for (i = 0; i < 4; i++) v[i] = sc ? ((const LONG *)p)[i] : (int32_t)((const float *)p)[i];
    AcquireSRWLockExclusive(&lock);
    for (k = 0; k < nseen; k++)
        if (seen[k].ret == ret && seen[k].sc == sc && !memcmp(seen[k].v, v, sizeof v)) break;
    if (k == nseen && nseen < 512) {
        seen[nseen].ret = ret; seen[nseen].sc = (uint8_t)sc; memcpy(seen[nseen].v, v, sizeof v); nseen++;
        ReleaseSRWLockExclusive(&lock);
        {   /* pilha: ate 5 valores que caem na imagem do jogo (o jogo nao tem unwind registrado; pode ter lixo) */
            uint8_t **sp = __builtin_frame_address(0), **top = ((NT_TIB *)NtCurrentTeb())->StackBase;
            unsigned long long st[5] = { 0 };
            int j = 0;
            for (; g_game && sp < top && sp < (uint8_t **)__builtin_frame_address(0) + 0x400 && j < 5; sp++)
                if (*sp > g_game && *sp < g_game + 0x1800000) st[j++] = (unsigned long long)(*sp - g_game);
            wlog(sc ? "d3d: UiTrace: scissor n=%u %d,%d - %d,%d (caller game+%#llx; stack %#llx %#llx %#llx %#llx %#llx)"
                    : "d3d: UiTrace: viewport n=%u %d,%d %dx%d (caller game+%#llx; stack %#llx %#llx %#llx %#llx %#llx)",
                 n, v[0], v[1], v[2], v[3],
                 (unsigned long long)(ret - (uintptr_t)g_game), st[0], st[1], st[2], st[3], st[4]);
        }
        return;
    }
    ReleaseSRWLockExclusive(&lock);
    if (k == 512 && InterlockedIncrement(&dropped) == 1) wlog("d3d: UiTrace: 512 entries, the rest is not logged");
}
static void fm6_imm_RSSetViewports(void *ctx, UINT n, const float *vp)
{
    fm6_rs_trace(0, (uintptr_t)__builtin_return_address(0), n, vp);
    ((void (*)(void *, UINT, const float *))fm6_az_fn(ctx, AZ_SLOT_VIEWPORTS))(ctx, n, vp);
}
static void fm6_imm_RSSetScissorRects(void *ctx, UINT n, const RECT *r)
{
    fm6_rs_trace(1, (uintptr_t)__builtin_return_address(0), n, r);
    ((void (*)(void *, UINT, const RECT *))fm6_az_fn(ctx, AZ_SLOT_SCISSORS))(ctx, n, r);
}
static void fm6_def_RSSetViewports(void *ctx, UINT n, const float *vp)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (fm6_ui_trace) fm6_rs_trace(0, (uintptr_t)__builtin_return_address(0), n, vp);
    if (d) {
        d->st.nvp = n > 16 ? 16 : n;
        if (vp) memcpy(d->st.vp, vp, d->st.nvp * sizeof d->st.vp[0]);
    }
    ((void (*)(void *, UINT, const float *))fm6_az_fn(ctx, AZ_SLOT_VIEWPORTS))(ctx, n, vp);
}
static void fm6_def_RSSetScissorRects(void *ctx, UINT n, const RECT *r)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (fm6_ui_trace) fm6_rs_trace(1, (uintptr_t)__builtin_return_address(0), n, r);
    if (d) {
        d->st.nsc = n > 16 ? 16 : n;
        if (r) memcpy(d->st.sc, r, d->st.nsc * sizeof *r);
    }
    ((void (*)(void *, UINT, const RECT *))fm6_az_fn(ctx, AZ_SLOT_SCISSORS))(ctx, n, r);
}
/* FM6 0x24/0x25/0x2c do diferido (Azimuth 0x23/0x24/0x2b): OMSetBlendState, OMSetDepthStencilState, RSSetState */
static void fm6_def_OMSetBlendState(void *ctx, void *bs, const float *factor, UINT mask)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (d) {
        d->st.has |= FM6_HAS_BLEND;
        d->st.blend = bs;
        d->st.sample_mask = mask;
        if (factor) memcpy(d->st.blend_factor, factor, sizeof d->st.blend_factor);
        else d->st.blend_factor[0] = d->st.blend_factor[1] = d->st.blend_factor[2] = d->st.blend_factor[3] = 1.0f;
    }
    ((void (*)(void *, void *, const float *, UINT))fm6_az_fn(ctx, AZ_SLOT_BLEND))(ctx, bs, factor, mask);
}
static void fm6_def_OMSetDepthStencilState(void *ctx, void *ds, UINT ref)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (d) {
        d->st.has |= FM6_HAS_DEPTH;
        d->st.depth = ds;
        d->st.stencil_ref = ref;
    }
    ((void (*)(void *, void *, UINT))fm6_az_fn(ctx, AZ_SLOT_DEPTH))(ctx, ds, ref);
}
static void fm6_def_RSSetState(void *ctx, void *rs)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    if (d) {
        d->st.has |= FM6_HAS_RS;
        d->st.rs = rs;
    }
    ((void (*)(void *, void *))fm6_az_fn(ctx, AZ_SLOT_RS))(ctx, rs);
}
/* RemapConstantBufferInheritance / RemapShaderResourceInheritance (FM6 0x9f/0xa0 = Azimuth 0x9c/0x9d) do diferido:
   so conta os do bundle (estagio/slot de destino <- origem) e repassa */
#define AZ_SLOT_REMAP_CB 0x9c
static void fm6_def_remap(int srv, void *ctx, UINT ds, UINT dslot, UINT ss, UINT sslot)
{
    static volatile LONG n[2];
    LONG k = InterlockedIncrement(&n[srv]);
    if (k <= 8 || (k & (k - 1)) == 0) {
        fm6_dctx_info *d = fm6_dctx_find(ctx);
        wlog("d3d: inheritance: Remap%sInheritance #%ld on deferred d%d (flags %#x): stage %u slot %u <- stage %u "
             "slot %u", srv ? "ShaderResource" : "ConstantBuffer", k, d ? d->idx : -1, d ? d->flags : 0, ds, dslot,
             ss, sslot);
    }
    ((void (*)(void *, UINT, UINT, UINT, UINT))fm6_az_fn(ctx, AZ_SLOT_REMAP_CB + srv))(ctx, ds, dslot, ss, sslot);
}
static void fm6_def_RemapCB(void *ctx, UINT ds, UINT dslot, UINT ss, UINT sslot) { fm6_def_remap(0, ctx, ds, dslot, ss, sslot); }
static void fm6_def_RemapSRV(void *ctx, UINT ds, UINT dslot, UINT ss, UINT sslot) { fm6_def_remap(1, ctx, ds, dslot, ss, sslot); }

static void fm6_def_ClearState(void *ctx)
{
    fm6_om_reset(ctx);
    ((void (*)(void *))fm6_az_fn(ctx, AZ_SLOT_CLEARST))(ctx);
    alias_fm6_leave_scene(ctx, 0, NULL, 1, fm6_az_fn);
}

/* copia do estado do juntador na cadeia: objeto com AddRef/Release (slots 1/2) como as pecas */
typedef struct { void **vt; volatile LONG ref; fm6_om st; UINT nupd; struct { void *buf; fm6_ver *ver; } upd[FM6_UPD_MAX]; } fm6_om_item;
static ULONG fm6_om_item_addref(fm6_om_item *o) { return (ULONG)InterlockedIncrement(&o->ref); }
static ULONG fm6_om_item_release(fm6_om_item *o);
static void *fm6_om_item_vt[3] = { NULL, (void *)fm6_om_item_addref, (void *)fm6_om_item_release };
static void fm6_obj_ref(void *o, int add)
{
    if (o) ((ULONG (*)(void *))(*(void ***)o)[add ? 1 : 2])(o);
}
/* copia so a parte usada (PASSO 117: o estado inteiro tem ~8 KB e e copiado a cada bundle aninhado); quem le a
   copia so passa de nrtv/nvp/nsc/ncb/nsrv por aqui */
static void fm6_om_copy(fm6_om *dst, const fm6_om *src)
{
    int s;
    memcpy(dst, src, offsetof(fm6_om, cb));
    for (s = 0; s < 6; s++) {
        memcpy(dst->cb[s], src->cb[s], src->ncb[s] * sizeof src->cb[s][0]);
        memcpy(dst->srv[s], src->srv[s], src->nsrv[s] * sizeof src->srv[s][0]);
    }
    memcpy(dst->cbset, src->cbset, sizeof *dst - offsetof(fm6_om, cbset));
}

static void fm6_om_views(fm6_om *st, int add)
{
    UINT i, s;
    for (i = 0; i < st->nrtv; i++) fm6_obj_ref(st->rtv[i], add);
    fm6_obj_ref(st->dsv, add);
    fm6_obj_ref(st->blend, add);
    fm6_obj_ref(st->depth, add);
    fm6_obj_ref(st->rs, add);
    for (s = 0; s < 6; s++) {
        for (i = 0; i < st->ncb[s]; i++) fm6_obj_ref(st->cb[s][i], add);
        for (i = 0; i < st->nsrv[s]; i++) fm6_obj_ref(st->srv[s][i], add);
    }
}

/* CB/SRV ligados num diferido (Set* padrao, SetFast*, FromBatch): o bundle aninhado le pelos remaps os do
   juntador (estagios 5/1/2/3 estacionados e o proprio PS, 17_fm6.txt PASSO 42/43) */
static void fm6_res_note(void *ctx, int srv, UINT stage, UINT start, UINT n, void *const *objs)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    UINT i, max = srv ? 128u : 14u;
    if (!d || stage >= 6 || start >= max) return;
    if (n > max - start) n = max - start;
    for (i = 0; i < n; i++) {
        void *o = objs ? objs[i] : NULL;
        UINT k = start + i;
        if (srv) d->st.srv[stage][k] = o, d->st.srvset[stage][k >> 6] |= 1ull << (k & 63);
        else d->st.cb[stage][k] = o, d->st.cbset[stage][0] |= 1ull << k;
    }
    if (srv && start + n > d->st.nsrv[stage]) d->st.nsrv[stage] = (uint8_t)(start + n);
    if (!srv && start + n > d->st.ncb[stage]) d->st.ncb[stage] = (uint8_t)(start + n);
}

/* objeto de recurso do Azimuth (os do buffer tambem no UpdateSubresource de placement, abaixo) */
#define AZ_CTX_DEFERRED   0xc48
#define AZ_RES_TYPE       0x38
#define AZ_BUF_WIDTH      0x70
#define AZ_BUF_PLACEMENT  0x88
#define AZ_BUF_MEMORY     0x168
#define AZ_BUF_VA         0x148
static int fm6_om_has_res(const fm6_om *st)
{
    int s;
    for (s = 0; s < 6; s++)
        if (st->ncb[s] || st->nsrv[s]) return 1;
    return 0;
}
static void fm6_ver_release(fm6_ver *v)
{
    if (v && InterlockedDecrement(&v->ref) == 0) free(v);
}
static ULONG fm6_om_item_release(fm6_om_item *o)
{
    LONG r = InterlockedDecrement(&o->ref);
    if (r == 0) {
        UINT i;
        fm6_om_views(&o->st, 0);
        for (i = 0; i < o->nupd; i++) fm6_obj_ref(o->upd[i].buf, 0), fm6_ver_release(o->upd[i].ver);
        free(o);
    }
    return (ULONG)r;
}

/* lista sem estado do D3D12 = draw bundle do Azimuth */
static int fm6_is_bundle(const void *cl) { return !*(void *const *)((const uint8_t *)cl + AZ_LIST_STATE); }

/* estado do juntador ctx para herdar (NULL se ele nunca ligou nada) */
static fm6_om_item *fm6_om_snapshot(void *ctx)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    fm6_om_item *o;
    UINT i, nupd = 0;
    if (!d) return NULL;
    for (i = 0; i < FM6_UPD_MAX; i++) nupd += d->upd[i].dirty != 0;
    if (!nupd && !d->st.nrtv && !d->st.dsv && !d->st.nvp && !d->st.has && !fm6_om_has_res(&d->st)) return NULL;
    if (!(o = malloc(sizeof *o))) return NULL;
    o->vt = fm6_om_item_vt;
    o->ref = 1;
    fm6_om_copy(&o->st, &d->st);
    fm6_om_views(&o->st, 1);
    /* versao dos placement buffers que o juntador atualizou: o que o bundle le neste ponto da lista */
    o->nupd = 0;
    for (i = 0; i < FM6_UPD_MAX; i++) {
        fm6_upd *u = &d->upd[i];
        if (!u->dirty) continue;
        o->upd[o->nupd].buf = u->buf;
        o->upd[o->nupd].ver = u->cur;
        InterlockedIncrement(&u->cur->ref);
        fm6_obj_ref(u->buf, 1);
        o->nupd++;
    }
    return o;
}

/* versao de cada placement buffer aplicada por ultimo no imediato pela heranca (mesma = nada a fazer) */
static struct { void *buf; fm6_ver *ver; } fm6_upd_imm[16];
static SRWLOCK fm6_upd_imm_lock = SRWLOCK_INIT;
static volatile LONG fm6_upd_applied, fm6_upd_same;

/* UpdateSubresource (Azimuth 0x30) no imediato com o conteudo do juntador: o Azimuth renomeia o placement buffer
   (copia nova na GPU, 0x1400e9a00 -> 0x1400e95c0), entao os draws do bundle seguinte leem esta versao e os de antes
   ficam com a deles. Versao diferente com o mesmo conteudo (d4 x d5) tambem e pulada. */
#define AZ_SLOT_UPDATE_SUB 0x30
static void fm6_upd_apply(void *ctx, const fm6_om_item *o)
{
    typedef void (*upd_fn)(void *, void *, UINT, const UINT *, const void *, UINT, UINT);
    UINT i, k;
    for (i = 0; i < o->nupd; i++) {
        fm6_ver *v = o->upd[i].ver, *old = NULL;
        int same = 0, slot = -1;
        LONG n;
        AcquireSRWLockExclusive(&fm6_upd_imm_lock);
        for (k = 0; k < 16; k++)
            if (fm6_upd_imm[k].buf == o->upd[i].buf || (slot < 0 && !fm6_upd_imm[k].buf)) {
                slot = (int)k;
                if (fm6_upd_imm[k].buf == o->upd[i].buf) break;
            }
        if (slot >= 0 && fm6_upd_imm[slot].buf == o->upd[i].buf) {
            fm6_ver *last = fm6_upd_imm[slot].ver;
            same = last == v || (last->size == v->size && !memcmp(last->data, v->data, v->size));
        }
        if (slot >= 0 && fm6_upd_imm[slot].ver != v) {   /* guarda a mais nova (ponteiro igual da proxima vez) */
            old = fm6_upd_imm[slot].ver;
            InterlockedIncrement(&v->ref);
            fm6_upd_imm[slot].buf = o->upd[i].buf;
            fm6_upd_imm[slot].ver = v;
        }
        ReleaseSRWLockExclusive(&fm6_upd_imm_lock);
        fm6_ver_release(old);
        if (same) {
            InterlockedIncrement(&fm6_upd_same);
            continue;
        }
        ((upd_fn)(*(void ***)ctx)[AZ_SLOT_UPDATE_SUB])(ctx, o->upd[i].buf, 0, NULL, v->data, 0, 0);
        n = InterlockedIncrement(&fm6_upd_applied);
        if ((n & (n - 1)) == 0)
            wlog("d3d: inheritance: %ld placement buffer versions applied on the immediate (%ld identical skipped); last buf "
                 "%p (%u bytes)", n, fm6_upd_same, o->upd[i].buf, v->size);
    }
}

/* Set* (start, n, objs) de cada faixa contigua de slots gravados em mask entre 0 e n-1; devolve quantos */
static UINT fm6_set_runs(void *ctx, set_fn fn, void *const *objs, const uint64_t *mask, UINT n)
{
    UINT i = 0, j, total = 0;
    while (i < n) {
        if (!(mask[i >> 6] >> (i & 63) & 1)) { i++; continue; }
        for (j = i; j < n && mask[j >> 6] >> (j & 63) & 1; j++) {}
        fn(ctx, i, j - i, objs + i);
        total += j - i;
        i = j;
    }
    return total;
}

/* no imediato: aplica o estado herdado antes do bundle seguinte */
static void fm6_om_apply(void *ctx, const fm6_om_item *o)
{
    void **az = *(void ***)ctx;
    static volatile LONG logged, logged_cb;
    UINT s, ncb = 0, nsrv = 0;
    if (o->st.nrtv || o->st.dsv || o->st.nvp)
        ((void (*)(void *, UINT, void *const *, void *))az[AZ_SLOT_OM])(ctx, o->st.nrtv, o->st.rtv, o->st.dsv);
    if (o->st.nvp) ((void (*)(void *, UINT, const float *))az[AZ_SLOT_VIEWPORTS])(ctx, o->st.nvp, &o->st.vp[0][0]);
    if (o->st.nsc) ((void (*)(void *, UINT, const RECT *))az[AZ_SLOT_SCISSORS])(ctx, o->st.nsc, o->st.sc);
    if (o->st.has & FM6_HAS_BLEND)
        ((void (*)(void *, void *, const float *, UINT))az[AZ_SLOT_BLEND])(ctx, o->st.blend, o->st.blend_factor,
                                                                            o->st.sample_mask);
    if (o->st.has & FM6_HAS_DEPTH)
        ((void (*)(void *, void *, UINT))az[AZ_SLOT_DEPTH])(ctx, o->st.depth, o->st.stencil_ref);
    if (o->st.has & FM6_HAS_RS) ((void (*)(void *, void *))az[AZ_SLOT_RS])(ctx, o->st.rs);
    /* os remaps do bundle resolvem a origem no estado do imediato (pacote 0xc, 0x140111820); so os slots que o
       juntador gravou: o resto fica como o imediato tem (PASSO 50: a faixa 0..n-1 gravava NULL no b4) */
    for (s = 0; s < 6; s++) {
        ncb += fm6_set_runs(ctx, (set_fn)az[fm6_stage_cb[s]], o->st.cb[s], o->st.cbset[s], o->st.ncb[s]);
        nsrv += fm6_set_runs(ctx, (set_fn)az[fm6_stage_srv[s]], o->st.srv[s], o->st.srvset[s], o->st.nsrv[s]);
    }
    fm6_upd_apply(ctx, o);
    if (InterlockedIncrement(&logged) <= 4)
        wlog("d3d: inheritance: nested bundle runs with the joiner's OM (%u RTs, DS %p, %u viewports, %u scissors, "
             "%u CB slots and %u SRV slots parked; blend %s%p, depth %s%p ref %u, rs %s%p)", o->st.nrtv, o->st.dsv,
             o->st.nvp, o->st.nsc, ncb, nsrv, o->st.has & FM6_HAS_BLEND ? "" : "(not bound) ", o->st.blend,
             o->st.has & FM6_HAS_DEPTH ? "" : "(not bound) ", o->st.depth, o->st.stencil_ref,
             o->st.has & FM6_HAS_RS ? "" : "(not bound) ", o->st.rs);
    if (InterlockedIncrement(&logged_cb) <= 16) {   /* PASSO 49: quais slots de CB a copia liga */
        char buf[256];
        int len = 0;
        UINT i;
        for (s = 0; s < 6; s++) {
            if (!o->st.ncb[s]) continue;
            len += wsprintfA(buf + len, " stage %u:", s);
            for (i = 0; i < o->st.ncb[s]; i++)
                len += wsprintfA(buf + len, "%c", !(o->st.cbset[s][0] >> i & 1) ? '-' : o->st.cb[s][i] ? 'x' : '.');
        }
        wlog("d3d: inheritance: CBs of the copy on immediate %p (x = object, . = NULL, - = not recorded):%s", ctx,
             len ? buf : " none");
    }
}

/* lista devolvida pelo FinishCommandList -> diferido que a gravou (anel) */
static struct { void *cl, *ctx; } fm6_cl_owner[512];
static volatile LONG fm6_cl_owner_n;

static void fm6_cl_owner_note(void *cl, void *ctx)
{
    LONG i = InterlockedIncrement(&fm6_cl_owner_n) - 1;
    fm6_cl_owner[i & 511].ctx = ctx;
    fm6_cl_owner[i & 511].cl = cl;
}

static void *fm6_cl_owner_find(const void *cl)
{
    LONG i, n = fm6_cl_owner_n < 512 ? fm6_cl_owner_n : 512;
    for (i = 0; i < n; i++)
        if (fm6_cl_owner[(fm6_cl_owner_n - 1 - i) & 511].cl == cl) return fm6_cl_owner[(fm6_cl_owner_n - 1 - i) & 511].ctx;
    return NULL;
}

/* 1a vez de cada par (juntador, dono da lista): flags/chamador e OMSetRenderTargets de cada um */
static void fm6_nest_note(void *ctx, void *cl)
{
    static struct { void *j, *w; } seen[64];
    static volatile LONG nseen, calls;
    void *w;
    fm6_dctx_info *dj, *dw;
    LONG i, n = nseen < 64 ? nseen : 64;
    if (n >= 64 || InterlockedIncrement(&calls) > 4096) return;   /* so diagnostico: os pares aparecem no inicio */
    w = fm6_cl_owner_find(cl);
    dj = fm6_dctx_find(ctx);
    dw = w ? fm6_dctx_find(w) : NULL;
    for (i = 0; i < n; i++)
        if (seen[i].j == ctx && seen[i].w == w) return;
    i = InterlockedIncrement(&nseen) - 1;
    if (i >= 64) return;
    seen[i].j = ctx;
    seen[i].w = w;
    wlog("d3d: inheritance: nested on deferred d%d (flags %#x, game+%#zx, %ld OMSet, %ld empty) <- %s %p from "
         "deferred d%d %p (flags %#x, game+%#zx, %ld OMSet, %ld empty)",
         dj ? dj->idx : -1, dj ? dj->flags : 0, dj ? dj->caller : 0, dj ? dj->om : 0, dj ? dj->om_empty : 0,
         fm6_is_bundle(cl) ? "bundle" : "list", cl, dw ? dw->idx : -1, w, dw ? dw->flags : 0, dw ? dw->caller : 0,
         dw ? dw->om : 0, dw ? dw->om_empty : 0);
}

static int fm6_device_fixups(void **vt)
{
    size_t i;
    int n = 0;
    if (vt[0x1b] != (void *)fm6_defctx0) {
        orig_fm6_defctx[0] = (defctx_fn)vt[0x1b];
        vt[0x1b] = (void *)fm6_defctx0;
        orig_fm6_defctx[1] = (defctx_fn)vt[0x2d];
        vt[0x2d] = (void *)fm6_defctx1;
        orig_fm6_defctx[2] = (defctx_fn)vt[0x44];
        vt[0x44] = (void *)fm6_defctxx;
    }
    if (vt[5] != (void *)fm6_CreateTexture2D) {
        orig_fm6_create_tex2d = (tex2d_fn)vt[5];
        vt[5] = (void *)fm6_CreateTexture2D;
        orig_fm6_create_srv = (srv_fn)vt[7];
        vt[7] = (void *)fm6_CreateShaderResourceView;
        orig_fm6_create_rtv = (rtv_fn)vt[9];
        orig_fm6_create_blend = (state_fn)vt[0x14];
        vt[0x14] = (void *)fm6_CreateBlendState;
        vt[9] = (void *)fm6_CreateRenderTargetView;
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
#define FM6_SET_SRVS(s, stage) \
    static void fm6_set_srvs_##s(void *ctx, void *const *views, UINT start, UINT packed) \
    { \
        fm6_res_note(ctx, 1, stage, start, (packed >> 19) + 1, views); \
        ((set_srvs_fn)(*(void ***)ctx)[fm6_ctx_slot(s)])(ctx, start, (packed >> 19) + 1, views); \
    }
FM6_SET_SRVS(0x0a, 4) FM6_SET_SRVS(0x1a, 0) FM6_SET_SRVS(0x20, 3) FM6_SET_SRVS(0x3c, 1) FM6_SET_SRVS(0x40, 2)
FM6_SET_SRVS(0x44, 5)
static const struct { uint8_t slot; void *fn; } fm6_ctx_srvs[] = {
    { 0x0a, (void *)fm6_set_srvs_0x0a }, { 0x1a, (void *)fm6_set_srvs_0x1a }, { 0x20, (void *)fm6_set_srvs_0x20 },
    { 0x3c, (void *)fm6_set_srvs_0x3c }, { 0x40, (void *)fm6_set_srvs_0x40 }, { 0x44, (void *)fm6_set_srvs_0x44 },
};

/* PSSetFastSampler (Azimuth 0x0b com 1 elemento): o do FM5 tambem guarda os samplers por proxy (so FM5) */
static void fm6_PSSetFastSampler(void *real, UINT slot, void *obj) { ((set_fn)(*(void ***)real)[0x0b])(real, slot, 1, &obj); }

/* X SetFast* (FM6 0xa3..0xb6 = Azimuth 0xa0..0xb3, stubs fatais): no umd do FM6 tem os mesmos tamanhos do FH2
   (umd_vtdiff "equ"; IASetFastIndexBuffer 0x1c010 = (hwfmt, buffer), como o 0x18c90 do FH2) -> os wrappers do FM5
   sobre os Set* padrao do Azimuth em [ctx], na mesma ordem (game+0x131a2d: IASetFastIndexBuffer(1, ib)). */
#define FM6_FAST_FIRST 0xa3
static void *const fm6_ctx_fast[] = {
    (void *)ctx_PSSetFastConstantBuffer, (void *)ctx_PSSetFastShaderResource, (void *)fm6_PSSetFastSampler,
    (void *)ctx_VSSetFastConstantBuffer, (void *)ctx_VSSetFastShaderResource, (void *)ctx_VSSetFastSampler,
    (void *)ctx_GSSetFastConstantBuffer, (void *)ctx_GSSetFastShaderResource, (void *)ctx_GSSetFastSampler,
    (void *)ctx_CSSetFastConstantBuffer, (void *)ctx_CSSetFastShaderResource, (void *)ctx_CSSetFastSampler,
    (void *)ctx_HSSetFastConstantBuffer, (void *)ctx_HSSetFastShaderResource, (void *)ctx_HSSetFastSampler,
    (void *)ctx_DSSetFastConstantBuffer, (void *)ctx_DSSetFastShaderResource, (void *)ctx_DSSetFastSampler,
    (void *)ctx_IASetFastVertexBuffer, (void *)ctx_IASetFastIndexBuffer,
};

/* CB padrao (Azimuth fm6_stage_cb, FM6 = +1) e SetFast* de CB/SRV no diferido: anotam para a heranca e repassam */
#define FM6_DEF_CB(stage) \
    static void fm6_def_set_cbs_##stage(void *ctx, UINT start, UINT n, void *const *cbs) \
    { \
        fm6_res_note(ctx, 0, stage, start, n, cbs); \
        ((set_fn)fm6_az_fn(ctx, fm6_stage_cb[stage]))(ctx, start, n, cbs); \
    }
FM6_DEF_CB(0) FM6_DEF_CB(1) FM6_DEF_CB(2) FM6_DEF_CB(3) FM6_DEF_CB(4) FM6_DEF_CB(5)
static void *const fm6_def_cbs[6] = {
    (void *)fm6_def_set_cbs_0, (void *)fm6_def_set_cbs_1, (void *)fm6_def_set_cbs_2,
    (void *)fm6_def_set_cbs_3, (void *)fm6_def_set_cbs_4, (void *)fm6_def_set_cbs_5,
};
#define FM6_DEF_FAST(name, srv, stage) \
    static void fm6_def_##name(void *ctx, UINT slot, void *obj) \
    { \
        fm6_res_note(ctx, srv, stage, slot, 1, &obj); \
        ctx_##name(ctx, slot, obj); \
    }
FM6_DEF_FAST(PSSetFastConstantBuffer, 0, 4) FM6_DEF_FAST(PSSetFastShaderResource, 1, 4)
FM6_DEF_FAST(VSSetFastConstantBuffer, 0, 0) FM6_DEF_FAST(VSSetFastShaderResource, 1, 0)
FM6_DEF_FAST(GSSetFastConstantBuffer, 0, 3) FM6_DEF_FAST(GSSetFastShaderResource, 1, 3)
FM6_DEF_FAST(CSSetFastConstantBuffer, 0, 5) FM6_DEF_FAST(CSSetFastShaderResource, 1, 5)
FM6_DEF_FAST(HSSetFastConstantBuffer, 0, 1) FM6_DEF_FAST(HSSetFastShaderResource, 1, 1)
FM6_DEF_FAST(DSSetFastConstantBuffer, 0, 2) FM6_DEF_FAST(DSSetFastShaderResource, 1, 2)
/* na ordem do fm6_ctx_fast (CB, SRV, sampler por estagio); NULL = sem anotacao */
static void *const fm6_def_fast[18] = {
    (void *)fm6_def_PSSetFastConstantBuffer, (void *)fm6_def_PSSetFastShaderResource, NULL,
    (void *)fm6_def_VSSetFastConstantBuffer, (void *)fm6_def_VSSetFastShaderResource, NULL,
    (void *)fm6_def_GSSetFastConstantBuffer, (void *)fm6_def_GSSetFastShaderResource, NULL,
    (void *)fm6_def_CSSetFastConstantBuffer, (void *)fm6_def_CSSetFastShaderResource, NULL,
    (void *)fm6_def_HSSetFastConstantBuffer, (void *)fm6_def_HSSetFastShaderResource, NULL,
    (void *)fm6_def_DSSetFastConstantBuffer, (void *)fm6_def_DSSetFastShaderResource, NULL,
};

/* UpdateSubresource (FM6 0x31 = Azimuth 0x30) de um placement buffer num contexto diferido (game+0x207247): o
   Azimuth para ("... on a deferred context", validador 0x1401a6fc0). Placement buffer do Azimuth (CreatePlacementBuffer
   0x140121870, so constantes): +0x38 tipo 5, +0x70 ByteWidth, +0x88 placement, +0x168 memoria do jogo (0 = liberada);
   o conteudo e a memoria do jogo, copiada por lista diferida no ExecuteCommandList (0x14011bd40). No console o
   driver copia os dados na GPU, na ordem dos draws. Os juntadores d4/d5 do FM6 atualizam assim 2 buffers (1712 e
   2048 bytes, constantes por objeto do carro) antes de cada bundle aninhado, ~1000x por quadro e em 2 threads; os
   bundles desenham no imediato, que le o buffer no submit -> todas as pecas com o ultimo valor (rodas atrasadas,
   interior piscando, 17_fm6.txt PASSO 51). Agora: a atualizacao vai para a sombra do diferido (fm6_upd), a copia
   da heranca leva o conteudo e o imediato o aplica antes do bundle (fm6_upd_apply). Sem sombra (diferido sem
   registro, buffer grande, mais de FM6_UPD_MAX): memoria do jogo, como antes. */
typedef void (*update_sub_fn)(void *ctx, void *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp);
static fm6_upd *fm6_upd_get(void *ctx, uint8_t *res, UINT w)
{
    fm6_dctx_info *d = fm6_dctx_find(ctx);
    int i, free_i = -1;
    if (!d) return NULL;
    for (i = 0; i < FM6_UPD_MAX; i++) {
        if (d->upd[i].buf == res && d->upd[i].size == w) return &d->upd[i];
        if (!d->upd[i].buf && free_i < 0) free_i = i;
    }
    if (free_i < 0 || w > FM6_UPD_BYTES || !(d->upd[free_i].cur = malloc(sizeof(fm6_ver) + w))) return NULL;
    d->upd[free_i].cur->ref = 1;
    d->upd[free_i].cur->size = w;
    memcpy(d->upd[free_i].cur->data, *(uint8_t **)(res + AZ_BUF_MEMORY), w);
    fm6_obj_ref(res, 1);                                       /* a sombra vive com o diferido */
    d->upd[free_i].buf = res;
    d->upd[free_i].size = w;
    wlog("d3d: placement buffer UpdateSubresource on deferred d%d %p: shadow of buf %p (%u bytes, memory %p)",
         d->idx, ctx, (void *)res, w, *(void **)(res + AZ_BUF_MEMORY));
    return &d->upd[free_i];
}

static void fm6_UpdateSubresource(void *ctx, uint8_t *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp)
{
    if (res && src && sub == 0 && ((uint8_t *)ctx)[AZ_CTX_DEFERRED] && *(int *)(res + AZ_RES_TYPE) == 5 &&
        res[AZ_BUF_PLACEMENT] && *(uint8_t **)(res + AZ_BUF_MEMORY)) {
        UINT w = *(UINT *)(res + AZ_BUF_WIDTH), lo = box ? box[0] : 0, hi = box ? box[3] : w;
        if (lo < hi && hi <= w) {
            static volatile LONG n;
            LONG k = InterlockedIncrement(&n);
            fm6_upd *u = fm6_upd_get(ctx, res, w);
            if (u) {
                if (memcmp(u->cur->data + lo, src, hi - lo)) {   /* mesmo conteudo (o comum): mesma versao */
                    fm6_ver *v = u->cur;
                    if (v->ref > 1 && (v = malloc(sizeof *v + w)) != NULL) {   /* compartilhada: versao nova */
                        v->ref = 1;
                        v->size = w;
                        memcpy(v->data, u->cur->data, w);
                        fm6_ver_release(u->cur);
                        u->cur = v;
                    }
                    if (v) memcpy(v->data + lo, src, hi - lo);
                }
                u->dirty = 1;
            } else
                memcpy(*(uint8_t **)(res + AZ_BUF_MEMORY) + lo, src, hi - lo);
            if (k <= 8 || (k & (k - 1)) == 0)
                wlog("d3d: placement buffer UpdateSubresource #%ld on deferred %p: buf %p (%u bytes) [%u, %u) "
                     "-> %s %p, caller %p", k, ctx, (void *)res, w, lo, hi, u ? "shadow" : "memory",
                     u ? (void *)u->cur->data : *(void **)(res + AZ_BUF_MEMORY), __builtin_return_address(0));
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

/* PASSO 79: no console o registro guarda a COPIA do T#, entao o jogo solta a view logo depois e o batch continua
   valido; aqui o registro guarda o objeto do Azimuth, que morre na ultima Release (memoria liberada na hora, slots de
   descritor devolvidos pelo reaper e reaproveitados). Com BatchHoldRefs (FM6 padrao 1, debug.ini) o batch segura
   uma referencia ao SRV: tabela lateral endereco do registro -> objeto (o registro velho nao e lido: memoria nova
   do jogo pode ter lixo com o magic); regravar o registro solta o anterior. */
int fm6_batch_hold = 1;
typedef struct { const uint8_t *rec; void *obj; } fm6_hold_ent;
static fm6_hold_ent *fm6_hold_tab;
static size_t fm6_hold_cap, fm6_hold_n;
static SRWLOCK fm6_hold_lock = SRWLOCK_INIT;
static volatile LONG fm6_batch_orphans, fm6_batch_probes;

static size_t fm6_hold_slot(fm6_hold_ent *t, size_t cap, const uint8_t *rec)
{
    size_t h = ((uintptr_t)rec >> 2) * 0x9e3779b97f4a7c15ull & (cap - 1);
    while (t[h].rec && t[h].rec != rec) h = (h + 1) & (cap - 1);
    return h;
}

/* guarda obj para o registro rec; devolve o que ele segurava (NULL se nada); 0 = sem memoria (nao guarda) */
static int fm6_hold_swap(const uint8_t *rec, void *obj, void **old)
{
    size_t h;
    *old = NULL;
    AcquireSRWLockExclusive(&fm6_hold_lock);
    if ((fm6_hold_n + 1) * 2 > fm6_hold_cap) {
        size_t cap = fm6_hold_cap ? fm6_hold_cap * 2 : 4096, k;
        fm6_hold_ent *t = calloc(cap, sizeof *t);
        if (!t) { ReleaseSRWLockExclusive(&fm6_hold_lock); return 0; }
        for (k = 0; k < fm6_hold_cap; k++)
            if (fm6_hold_tab[k].rec) t[fm6_hold_slot(t, cap, fm6_hold_tab[k].rec)] = fm6_hold_tab[k];
        free(fm6_hold_tab);
        fm6_hold_tab = t;
        fm6_hold_cap = cap;
    }
    h = fm6_hold_slot(fm6_hold_tab, fm6_hold_cap, rec);
    if (!fm6_hold_tab[h].rec) { fm6_hold_tab[h].rec = rec; fm6_hold_n++; }
    *old = fm6_hold_tab[h].obj;
    fm6_hold_tab[h].obj = obj;
    ReleaseSRWLockExclusive(&fm6_hold_lock);
    return 1;
}

/* game+0xda9cac(batch, srv, i) */
static void fm6_batch_add_srv(fm6_batch *b, void *srv, uint32_t i)
{
    uint8_t *rec;
    void *old;
    if (i * FM6_BATCH_SRV_SIZE >= b->srv_size || i >> 7 >= 4) return;
    rec = b->srv + i * FM6_BATCH_SRV_SIZE;
    fm6_batch_put(rec, FM6_BATCH_MAGIC | FM6_BATCH_SRV_FLAG | fm6_batch_stage[i >> 7] << 8 | (i & 0x7f), srv);
    if (!fm6_batch_hold) return;
    if (srv) ((ULONG (*)(void *))(*(void ***)srv)[1])(srv);
    if (!fm6_hold_swap(rec, srv, &old)) {
        if (srv) ((ULONG (*)(void *))(*(void ***)srv)[2])(srv);
        return;
    }
    if (old) ((ULONG (*)(void *))(*(void ***)old)[2])(old);
}

/* com BatchHoldRefs: SRV aplicado do batch que so a nossa referencia segura (AddRef devolve 2) = o jogo ja o soltou
   (sem a referencia seria objeto liberado). Limite inferior: referencias de listas gravadas tambem contam. */
static void fm6_batch_probe(const uint8_t *rec, void *obj)
{
    ULONG c = ((ULONG (*)(void *))(*(void ***)obj)[1])(obj);
    LONG k;
    ((ULONG (*)(void *))(*(void ***)obj)[2])(obj);
    if (InterlockedIncrement(&fm6_batch_probes) <= 4)
        wlog("d3d: FM6 batch: SRV %p reference probe = %lu", obj, (unsigned long)c);
    if (c != 2) return;
    k = InterlockedIncrement(&fm6_batch_orphans);
    if (k <= 16 || !(k & (k - 1)))
        wlog("d3d: FM6 batch: SRV %p already released by the game still applied from record %p (%ld times)", obj, rec, k);
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
                wlog("d3d: FM6 SetFastResourcesFromBatch: unknown record %#x at +%#x of %u bytes; rest "
                     "ignored", hdr, (UINT)(size - (end - p)), size);
            return;
        }
        memcpy(&obj, p + 4, 8);
        {
            static volatile LONG logged;
            LONG k = InterlockedIncrement(&logged);
            if (k <= 16) {
                uint8_t *vt = obj ? *(uint8_t **)obj : NULL, *tabs = g_base + RVA_VTBL_TABLES;
                wlog("d3d: FM6 batch #%ld: %s stage %u slot %u obj %p (Azimuth type %d)", k,
                     hdr & FM6_BATCH_SRV_FLAG ? "SRV" : "CB", stage, slot, obj,
                     vt >= tabs && vt < tabs + NTYPES * SLOTS * 8 ? (int)((vt - tabs) / (SLOTS * 8)) : -1);
            }
        }
        if (st_on && fm6_batch_hold && obj && hdr & FM6_BATCH_SRV_FLAG) fm6_batch_probe(p, obj);   /* diagnostico */
        ((set_fn)(*(void ***)ctx)[hdr & FM6_BATCH_SRV_FLAG ? fm6_stage_srv[stage] : fm6_stage_cb[stage]])
            (ctx, slot, 1, &obj);
        fm6_res_note(ctx, !!(hdr & FM6_BATCH_SRV_FLAG), stage, slot, 1, &obj);
        p += n;
    }
}

/* SetFastResources_Debug (FM6 0xd4 = Azimuth 0xd1, implementado no Azimuth: 0x140111cd0 imediato, 0x1401b8110
   diferido): o SetFastResources inline do XDK monta (inicio, fim) com pacotes [cabecalho: bit 31 | tipo << 28 |
   estagio << 16 | slot << 8 | n] + n x {objeto, metadados} de 16 bytes; tipo 0 SRV, 1 CB, 2 sampler (1 com
   0x106xxxx = vertex buffer). O FM6 liga por aqui o b4 de VS/PS (game+0x954dad/0x954de6, 0x996c8c/0x996d08) e o
   Azimuth aplica direto, sem passar pelos Set* -> a heranca nao via (PASSO 50: carros pretos). Anota CB/SRV como os
   Set* e repassa. */
#define FM6_SLOT_FASTRES 0xd4
typedef void (*fastres_fn)(void *ctx, const uint8_t *p, const uint8_t *end, void *a4);
static void fm6_SetFastResources(void *ctx, const uint8_t *p, const uint8_t *end, void *a4)
{
    const uint8_t *q = p;
    while (q && end && q + 4 <= end) {
        uint32_t h, type, stage, slot, n, i;
        void *objs[32];
        memcpy(&h, q, 4);
        n = h & 0xff;
        type = h >> 28 & 7;
        stage = h >> 16 & 0xf;
        slot = h >> 8 & 0xff;
        if (!(h >> 31) || n > 32 || q + 4 + (size_t)n * 16 > end) break;
        if (type <= 1 && stage < 6 && (h & 0xff00000) == 0) {
            for (i = 0; i < n; i++) memcpy(&objs[i], q + 4 + i * 16, 8);
            fm6_res_note(ctx, type == 0, stage, slot, n, objs);
        }
        q += 4 + (size_t)n * 16;
    }
    ((fastres_fn)fm6_az_fn(ctx, fm6_ctx_slot(FM6_SLOT_FASTRES)))(ctx, p, end, a4);
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
#define AZ_CTX_D3D12_LIST 0x9d88   /* command list do D3D12 aberta (so ao gravar: 0x1400df480; Finish zera) */
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

/* diagnostico do vazamento de listas (17_fm6.txt PASSO 34): referencias nossas vivas em cadeias */
static volatile LONG fm6_refs_live, fm6_refs_logged;

static void fm6_refs_note(LONG d, const char *why, int chain)
{
    LONG v = InterlockedExchangeAdd(&fm6_refs_live, d) + d;
    if (v >= 64 && (v & (v - 1)) == 0 && InterlockedExchange(&fm6_refs_logged, v) != v)
        wlog("d3d: %ld references of live nested pieces (%s, chain of %d)", v, why, chain);
}

static void fm6_chain_free(fm6_chain *c)
{
    int i;
    for (i = 0; i < c->n; i++) fm6_obj_release(c->items[i]);
    if (c->n) fm6_refs_note(-c->n, "released", c->n);
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
        static volatile LONG logged, empty;
        void *part = NULL;
        fm6_chain *p, *e;
        int ok = 0;
        /* nada gravado desde a ultima parte (sem command list aberta): o Finish abriria e fecharia uma lista vazia,
           com allocator novo, a cada aninhado seguido (milhares em segundos -> "Kernel memory failure",
           17_fm6.txt PASSO 37) -> so a lista entra nas pendencias */
        if (!*(void **)(c + AZ_CTX_D3D12_LIST)) {
            LONG k = InterlockedIncrement(&empty);
            if ((k & (k - 1)) == 0) wlog("d3d: nested on deferred %p with nothing recorded before: no part (#%ld)", ctx, k);
        } else {
            HRESULT hr = ((finish_fn)az[AZ_SLOT_FINISH])(ctx, TRUE, &part);
            if (hr < 0 || !part) {
                wlog("d3d: nested ExecuteCommandList on deferred %p: FinishCommandList(TRUE) hr=%#lx", ctx, hr);
                ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, cl, restore);
                return;
            }
        }
        AcquireSRWLockExclusive(&fm6_cl_lock);
        p = fm6_chain_get(&fm6_pending, ctx);
        if (p && (!part || fm6_chain_push(p, part))) {
            int before = p->n - (part != NULL), inner = 0;
            ok = 1;
            if ((e = fm6_chain_find(&fm6_lists, cl)) != NULL)
                for (i = 0; i < e->n; i++)
                    if (fm6_chain_push(p, e->items[i])) fm6_obj_addref(e->items[i]);
            if (fm6_is_bundle(cl)) {   /* heranca: o bundle desenha com o OM do juntador */
                fm6_om_item *st = fm6_om_snapshot(ctx);
                if (st && !fm6_chain_push(p, st)) fm6_om_item_release(st);
            }
            if (fm6_chain_push(p, cl)) fm6_obj_addref(cl);
            n = p->n;
            inner = e ? e->n : 0;
            fm6_refs_note(n - before, "nested", n);
            {   /* lista que ja traz cadeia: mostra as primeiras e as cadeias recorde */
                static volatile LONG shown, longest;
                if (inner && (InterlockedIncrement(&shown) <= 8 || n > longest)) {
                    if (n > longest) longest = n;
                    wlog("d3d: nested on deferred %p: list %p already has a chain of %d -> pending %d", ctx, cl, inner, n);
                }
            }
        }
        ReleaseSRWLockExclusive(&fm6_cl_lock);
        if (!ok) wlog("d3d: nested ExecuteCommandList on deferred %p: out of memory; list %p lost", ctx, cl);
        /* restore FALSE no aninhado NAO limpa o juntador: no console o aninhado e um salto de command buffer e os
           registros continuam; o FM6 liga OM/PS uma vez e aninha varios bundles em seguida (17_fm6.txt PASSO 43) */
        fm6_nest_note(ctx, cl);
        if (InterlockedIncrement(&logged) <= 8)
            wlog("d3d: nested ExecuteCommandList on deferred %p: list %p (restore %d) -> part %p, %d pending",
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
            if ((k & (k - 1)) == 0) wlog("d3d: ExecuteCommandList %p with a repeated nested list (%d pieces) (#%ld)", cl, n, k);
        }
    }
    for (i = 0; i < n; i++) {
        if (*(void ***)items[i] == fm6_om_item_vt) fm6_om_apply(ctx, items[i]);
        else ((execute_fn)az[AZ_SLOT_EXECUTE])(ctx, items[i], restore);
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
    fm6_cl_owner_note(*out, ctx);
    if (!restore) fm6_om_reset(ctx);
    {   /* lista nova: o bundle so herda o conteudo que o juntador atualizar nela */
        fm6_dctx_info *d = fm6_dctx_find(ctx);
        int i;
        if (d)
            for (i = 0; i < FM6_UPD_MAX; i++) d->upd[i].dirty = 0;
    }
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
    if (stale.n) {
        static volatile LONG k;
        LONG c = InterlockedIncrement(&k);
        if ((c & (c - 1)) == 0) wlog("d3d: FinishCommandList: list %p reused without Release, old chain of %d (#%ld)", *out, stale.n, c);
    }
    if (p.n) wlog("d3d: FinishCommandList of deferred %p: %d nested pieces lost", ctx, p.n);
    fm6_chain_free(&p);
    fm6_chain_free(&stale);
    return hr;
}

/* Reexecucao de lista ainda na GPU (17_fm6.txt PASSO 35): o laco do FM6 (game+0xf96b1) aninha a lista de cada
   worker; worker sem trabalho novo repete a lista antiga, no mesmo quadro ou no seguinte. O worker do Azimuth
   (op 0x2e, 0x14011bd40) reenvia as mesmas command lists do D3D12 sem esperar a execucao anterior (erro 553 da
   debug layer, depois 0x887a0007). O estado da lista guarda a fence da ultima execucao ([estado+0x298]); se a GPU
   ainda nao passou dela: submete o lote atual se ela ainda nao saiu e espera a fence (o mesmo par submit/espera
   que o Azimuth ja usa nessa funcao para os dados de upload da lista). */
extern void cx_add(uint32_t kind, void *a, void *b, const uint32_t *x, int nx);
#define CX_REEXEC_KIND 8   /* CX_REEXEC em common/stutter.c */
#define RVA_APPLY_EXECUTE   0x11bd40
#define RVA_SUBMIT          0xdf730
#define RVA_FENCE_WAIT      0xf2c10
#define AZ_CTX_DEVICE       0xc40
#define AZ_STATE_LAST_FENCE 0x298
#define AZ_DEV_FENCE        0xcc0
#define AZ_FENCE_SUBMITTED  0x160
#define AZ_FENCE_DONE       0x180
typedef void (*apply_execute_fn)(uint8_t *ctx, uint8_t *cl, int restore, void *ret);
static apply_execute_fn orig_apply_execute;
static void (*az_submit)(uint8_t *ctx, int why, int flag);
static void (*az_fence_wait)(uint8_t *fence, uint64_t value);
static volatile LONG fm6_reexec_waits;

static void fm6_apply_execute(uint8_t *ctx, uint8_t *cl, int restore, void *ret)
{
    uint8_t *st = cl ? *(uint8_t **)(cl + AZ_LIST_STATE) : NULL, *fence = *(uint8_t **)(ctx + AZ_CTX_DEVICE) + AZ_DEV_FENCE;
    uint64_t last = st ? *(volatile uint64_t *)(st + AZ_STATE_LAST_FENCE) : 0;
    if (last && st_on) {   /* caixa-preta (PASSO 78): lista ja executada antes, reenviada agora */
        uint32_t x[3] = { (uint32_t)last, (uint32_t)*(volatile uint64_t *)(fence + AZ_FENCE_DONE),
                          (uint32_t)*(volatile uint64_t *)(fence + AZ_FENCE_SUBMITTED) };
        cx_add(CX_REEXEC_KIND, cl, st, x, 3);
    }
    if (last > *(volatile uint64_t *)(fence + AZ_FENCE_DONE)) {
        LONG k = InterlockedIncrement(&fm6_reexec_waits);
        int unsent = *(volatile uint64_t *)(fence + AZ_FENCE_SUBMITTED) < last;
        if (unsent) az_submit(ctx, 0x10, 0);
        az_fence_wait(fence, last);
        if ((k & (k - 1)) == 0)
            wlog("d3d: list %p re-executed before the GPU finished the previous run (fence %llu%s) -> wait (#%ld)", cl,
                 (unsigned long long)last, unsent ? ", batch submitted before" : "", k);
    }
    orig_apply_execute(ctx, cl, restore, ret);
}

int fm6_reexec_install(void)
{
    if (!bytes_match(g_base + RVA_SUBMIT, "48895c24188954241055565741544155", 16) ||
        !bytes_match(g_base + RVA_FENCE_WAIT, "48895c24185556574881ec00010000", 15)) {
        wlog("d3d: Azimuth fence submit/wait do not match; re-execution without wait");
        return 0;
    }
    az_submit = (void *)(g_base + RVA_SUBMIT);
    az_fence_wait = (void *)(g_base + RVA_FENCE_WAIT);
    if (!hook_function(RVA_APPLY_EXECUTE, "48895c241855565741544155415641574883ec50", 16, (void *)fm6_apply_execute,
                       (void **)&orig_apply_execute)) {
        wlog("d3d: worker list execution (%#x) does not match; re-execution without wait", RVA_APPLY_EXECUTE);
        return 0;
    }
    wlog("d3d: FM6: list re-execution waits for the previous run's fence");
    return 1;
}

/* PASSO 82: SRVs do contexto IMEDIATO sem referencia. *SetShaderResources do Azimuth (0x1401b21d0) no diferido
   (ctx+0xc48) faz AddRef/Release das views; no imediato vai para 0x14010fd30, que so copia os ponteiros para o
   estado e publica a operacao para o 'Azimuth worker' - sem referencia. O worker aplica depois e, no draw, o bind
   (0x140127740) le o handle CPU do objeto da view (+0x40/+0xd0) e copia para o heap da GPU. O jogo (D3D11: o contexto
   segura a referencia; console: o T# ja foi copiado) solta a view ainda ligada -> objeto liberado na hora, slot de
   descritor devolvido pelo reaper em "enviado + 1" e reaproveitado -> o worker copia o slot de OUTRA view (reset no
   fim da corrida; com RetireMargin nos slots, descritor de recurso ja destruido: GPU lenta).
   Correcao: o hook segura uma referencia a cada SRV ligada no imediato (por contexto, estagio e slot) e a view
   trocada so e solta depois de ImmediateSrvHoldMs (debug.ini, FM6 padrao 1000; 0 = sem hook), quando o worker e a
   GPU ja passaram. Diagnostico: na troca, a view que so a nossa referencia segura = o jogo soltou ainda ligada.
   O SetFastResources imediato (0x140111cd0) nao passa por aqui. */
#define RVA_IMM_SET_SRVS 0x10fd30
#define IH_CTX  4
#define IH_RING 65536
typedef void (*imm_srvs_fn)(void *ctx, UINT stage, UINT start, UINT n, void *const *views, UINT64 a6);
static imm_srvs_fn orig_imm_srvs;
int ih_hold_ms;
static LONG64 ih_freq;
static SRWLOCK ih_lock = SRWLOCK_INIT;
static struct { void *ctx; void *v[6][128]; } ih_ctx[IH_CTX];
static struct { void *obj; LONG64 t; } ih_q[IH_RING];
static unsigned ih_head, ih_tail;                 /* fila de views trocadas: [tail, head) */
static volatile LONG ih_dead_bound, ih_overflow;

static ULONG ih_addref(void *o) { return ((ULONG (*)(void *))(*(void ***)o)[1])(o); }
static ULONG ih_release(void *o) { return ((ULONG (*)(void *))(*(void ***)o)[2])(o); }

/* sob ih_lock: anota a troca dos slots, faz AddRef nas novas, enfileira as antigas e devolve em out (ate max) as
   que ja esperaram hold_ms (ou que transbordaram a fila) para soltar fora da trava */
static int ih_bind(void *ctx, UINT stage, UINT start, UINT n, void *const *views, LONG64 now, LONG64 hold,
                   void **out, int max)
{
    int c, k = 0;
    UINT i;
    for (c = 0; c < IH_CTX && ih_ctx[c].ctx && ih_ctx[c].ctx != ctx; c++) {}
    if (c < IH_CTX && !ih_ctx[c].ctx) ih_ctx[c].ctx = ctx;
    while (ih_tail != ih_head && k < max && now - ih_q[ih_tail % IH_RING].t >= hold)
        out[k++] = ih_q[ih_tail++ % IH_RING].obj;
    if (c >= IH_CTX || stage >= 6 || start >= 128) return k;
    if (n > 128 - start) n = 128 - start;
    for (i = 0; i < n; i++) {
        void *nv = views ? views[i] : NULL, **slot = &ih_ctx[c].v[stage][start + i], *old = *slot;
        if (nv == old) continue;
        if (nv) ih_addref(nv);
        *slot = nv;
        if (!old) continue;
        if (st_on) {                              /* diagnostico (StutterLog): sonda = 2 chamadas atomicas por troca */
            if (ih_addref(old) == 2) {            /* so a nossa (+ a sonda): o jogo ja soltou */
                LONG d = InterlockedIncrement(&ih_dead_bound);
                if (d <= 8 || !(d & (d - 1)))
                    wlog("d3d: SRV %p released by the game still bound on immediate %p (stage %u slot %u; %ld times)",
                         old, ctx, stage, start + i, d);
            }
            ih_release(old);
        }
        if (ih_head - ih_tail >= IH_RING) {       /* fila cheia: a mais antiga sai agora */
            if (k < max) out[k++] = ih_q[ih_tail % IH_RING].obj;
            else ih_release(ih_q[ih_tail % IH_RING].obj);
            ih_tail++;
            InterlockedIncrement(&ih_overflow);
        }
        ih_q[ih_head % IH_RING].obj = old;
        ih_q[ih_head++ % IH_RING].t = now;
    }
    return k;
}

static void fm6_imm_set_srvs(void *ctx, UINT stage, UINT start, UINT n, void *const *views, UINT64 a6)
{
    void *out[64];
    LARGE_INTEGER t;
    int k;
    QueryPerformanceCounter(&t);
    AcquireSRWLockExclusive(&ih_lock);
    k = ih_bind(ctx, stage, start, n, views, t.QuadPart, ih_freq * ih_hold_ms / 1000, out, 64);
    ReleaseSRWLockExclusive(&ih_lock);
    orig_imm_srvs(ctx, stage, start, n, views, a6);
    while (k) ih_release(out[--k]);
}

int fm6_imm_srv_hold_install(void)
{
    LARGE_INTEGER f;
    ih_hold_ms = dbg_int(L"ImmediateSrvHoldMs", 1000);
    if (ih_hold_ms <= 0) return 0;
    QueryPerformanceFrequency(&f);
    ih_freq = f.QuadPart;
    if (!hook_function(RVA_IMM_SET_SRVS, "48895c240848896c24104889742418", 15, (void *)fm6_imm_set_srvs,
                       (void **)&orig_imm_srvs)) {
        wlog("d3d: Azimuth immediate SetShaderResources (%#x) does not match; immediate SRVs not referenced",
             RVA_IMM_SET_SRVS);
        return 0;
    }
    wlog("d3d: FM6: immediate context SRVs referenced; a replaced one is released %d ms later", ih_hold_ms);
    return 1;
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
            wlog("d3d: FM6 batch builder at game+%#x does not match; batches not converted", site[i].rva);
            return 0;
        }
    for (i = 0; i < 2; i++) abs_jmp(game + site[i].rva, site[i].fn, 14);
    fm6_batch_hold = dbg_int(L"BatchHoldRefs", 1) > 0;
    fm6_ui_trace = dbg_int(L"UiTrace", 0) > 0;
    if (fm6_ui_trace) wlog("d3d: UiTrace=1 (distinct viewports/scissors in the log)");
    wlog("d3d: FM6 batch builders (game+%#x CB, game+%#x SRV) -> records carry the object%s", FM6_BATCH_CB,
         FM6_BATCH_SRV, fm6_batch_hold ? "; the batch holds a reference to the SRV" : "");
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
        if (src[a] == g_base + RVA_STUB_SLOT(a))
            dst[s] = deferred && i < 18 && fm6_def_fast[i] ? fm6_def_fast[i] : fm6_ctx_fast[i];
    }
    if (deferred)
        for (i = 0; i < 6; i++) dst[fm6_stage_cb[i] + 1] = fm6_def_cbs[i];   /* Azimuth 8..0x8c = FM6 - 1 */
    if (deferred) dst[0x31] = (void *)fm6_UpdateSubresource;
    /* aliasing da ESRAM (imediato e diferido) e heranca do OM no aninhado (diferido) */
    dst[0x22] = (void *)fm6_def_OMSetRenderTargets;
    dst[0x23] = (void *)fm6_def_OMSetRenderTargetsUAV;
    dst[0x2a] = (void *)fm6_Dispatch;
    if (liv_log) dst[0x3a] = (void *)fm6_ResolveSubresource;
    dst[0x6f] = (void *)fm6_def_ClearState;
    if (!deferred && fm6_ui_trace) {
        dst[0x2d] = (void *)fm6_imm_RSSetViewports;
        dst[0x2e] = (void *)fm6_imm_RSSetScissorRects;
    }
    if (deferred) {   /* heranca do OM no aninhado: acompanha RT/DS, viewports e scissors de cada diferido */
        dst[0x2d] = (void *)fm6_def_RSSetViewports;
        dst[0x2e] = (void *)fm6_def_RSSetScissorRects;
        dst[0x24] = (void *)fm6_def_OMSetBlendState;
        dst[0x25] = (void *)fm6_def_OMSetDepthStencilState;
        dst[0x2c] = (void *)fm6_def_RSSetState;
        if (src[AZ_SLOT_REMAP_CB] != g_base + RVA_STUB_SLOT(AZ_SLOT_REMAP_CB)) dst[0x9f] = (void *)fm6_def_RemapCB;
        if (src[AZ_SLOT_REMAP_CB + 1] != g_base + RVA_STUB_SLOT(AZ_SLOT_REMAP_CB + 1)) dst[0xa0] = (void *)fm6_def_RemapSRV;
    }
    if (src[fm6_ctx_slot(FM6_SLOT_SO_SET)] == g_base + RVA_STUB_SLOT(fm6_ctx_slot(FM6_SLOT_SO_SET)))
        dst[FM6_SLOT_SO_SET] = (void *)fm6_SOSetTargets;
    if (src[AZ_SLOT_EXECUTE] != g_base + RVA_STUB_SLOT(AZ_SLOT_EXECUTE))
        dst[0x3b] = (void *)fm6_ExecuteCommandList;
    if (deferred && src[AZ_SLOT_FINISH] != g_base + RVA_STUB_SLOT(AZ_SLOT_FINISH))
        dst[0x73] = (void *)fm6_FinishCommandList;
    if (src[fm6_ctx_slot(0xd7)] == g_base + RVA_STUB_SLOT(fm6_ctx_slot(0xd7)))
        dst[0xd7] = (void *)fm6_SetFastResourcesFromBatch;
    if (src[fm6_ctx_slot(FM6_SLOT_FASTRES)] != g_base + RVA_STUB_SLOT(fm6_ctx_slot(FM6_SLOT_FASTRES)))
        dst[FM6_SLOT_FASTRES] = (void *)fm6_SetFastResources;
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
    if (deferred) fm6_ctx_src_note((uint8_t *)dst - 0x3d0, src);
    if (InterlockedIncrement(&logged[deferred]) == 1)
        wlog("d3d: FM6 %s context %p: embedded table in the FM6 layout (0x%x slots, %d trivial driver slots)",
             deferred ? "deferred" : "immediate", (uint8_t *)dst - 0x3d0, FM6_CTX_SLOTS, t);
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
            wlog("d3d: context table copy at %#x does not match; FM6 context not converted", site[i].rva);
            return 0;
        }
    }
    if (!(stubs = alloc_near(g_base, 32))) { wlog("d3d: no memory near azimuth.exe for the FM6 context"); return 0; }
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
    wlog("d3d: FM6 context: embedded table copies (%#x, %#x) -> FM6 layout", RVA_CTX_COPY_IMM, RVA_CTX_COPY_DEF);
    return 1;
}

/* GetDesc do Texture2D no layout do FM6 (slot 0xd): o resolve MSAA do FM6 (game+0x40b7aa, desc lido por
   game+0x19070c) so desenha com SampleDesc {2, 2} / {4, 3}, como o do FM5 (cont. 23); com o Quality do D3D12
   (0xffffffff) pulava -> thumbnails da garagem (GarageThumbnail bigThumb/smallThumb) pretos. */
#define FM6_IMAGE_SIZE 0x1fc8000
static tex2d_getdesc_fn orig_fm6_tex2d_getdesc;
static void fm6_tex2d_GetDesc(void *tex, UINT *d)
{
    orig_fm6_tex2d_getdesc(tex, d);
    msaa_q_fix(tex, d, __builtin_return_address(0), g_game, FM6_IMAGE_SIZE);
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
    orig_fm6_tex2d_getdesc = (tex2d_getdesc_fn)get(0xc)[0xd];
    get(0xc)[0xd] = (void *)fm6_tex2d_GetDesc;
    factory = get(0xe);
    orig_fm6_create_sc_cw = (create_sc_cw_fn)factory[0x10];
    factory[0x10] = (void *)fm6_CreateSwapChainForCoreWindow;
    wlog("d3d: object vtables in the FM6 layout (DeviceChild +slot 8, DXGI without slot 5, Texture2D GetDesc "
         "with the game's MSAA Quality)");
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
    wlog("d3d: D3D11XCreateDeviceX hr=%#lx device=%p context=%p (FM6, device in the FH2 layout)", hr, dev, ctx);
    if (hr < 0) return hr;
    wlog("d3d: FM6 device: %d trivial console driver slots in place of the fatal stub",
         fm6_device_fixups(*(void ***)dev));
    fm6_object_layouts();
    if (out_level) *out_level = 0xb100;
    if (out_dev) *out_dev = dev;
    if (out_ctx) *out_ctx = ctx;
    return hr;
}


/* DXGIXSetVLineNotification(contador, linha, evento): no console o evento e sinalizado quando o scanout
   passa pela linha. Aqui: uma thread sinaliza os eventos registrados a 60 Hz (prazo por QPC). O FM6 espera esse
   evento a cada quadro (Frame11 slot 0x108, framesperrender 1) -> com frame_rate > 60 a taxa e a do launcher. */
#define VLINE_N 2
static HANDLE vline_event[VLINE_N];
static volatile LONG vline_started, vline_hz = 60;

void fm6_vline_rate(uint32_t hz) { InterlockedExchange(&vline_hz, (LONG)(hz ? hz : 60)); }

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
        next.QuadPart += f.QuadPart / vline_hz;
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
    wlog("d3d: DXGIXSetVLineNotification(%u, line %u, event %p) -> %ld Hz", counter, line, event, vline_hz);
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
   ("alternate profile") e depois ignora o processador -> mesma ponte do FM5 (xaudio2_create_bridge, common/d3d.c). */
static FARPROC (WINAPI *az_GetProcAddress)(HMODULE m, const char *name);

static FARPROC WINAPI fm6_GetProcAddress(HMODULE m, const char *name)
{
    FARPROC p = az_GetProcAddress(m, name);
    if ((uint8_t *)p == g_base + RVA_CREATE_XAUDIO2) {
        wlog("xaudio2: GetProcAddress(CreateXAudio2Object) -> bridge with processor -1 (FM6)");
        p = (FARPROC)xaudio2_create_bridge;
    }
    return p;
}

/* ------------------------------------------------------------------------ */
/* Imports do FM6 (resolvedor do Azimuth, common/d3d.c)                       */
/* ------------------------------------------------------------------------ */
FARPROC fm6_resolve_import(const uint8_t *entry, FARPROC p)
{
    if (!p && str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "DXGIXSetVLineNotification")) {
        wlog("d3d: d3d11_x.dll!DXGIXSetVLineNotification -> 60 Hz event");
        p = (FARPROC)fm6_DXGIXSetVLineNotification;
    } else if (!p && str_is(entry, "d3d11_x.dll") && str_is(entry + 0x20, "D3D11CreateDevice")) {
        wlog("d3d: d3d11_x.dll!D3D11CreateDevice -> Azimuth D3D11XCreateDeviceX (FM6)");
        p = (FARPROC)fm6_D3D11CreateDevice;
    } else if (p && str_is(entry, "kernelx.dll") && str_is(entry + 0x20, "DeleteFileW")) {
        if (!az_DeleteFileW) wlog("kernelx.dll!DeleteFileW -> d:\\ fails with ERROR_PATH_NOT_FOUND (no PLS)");
        az_DeleteFileW = (BOOL (WINAPI *)(const wchar_t *))p;
        p = (FARPROC)fm6_DeleteFileW;
    } else if (p && str_is(entry, "kernelx.dll") && str_is(entry + 0x20, "GetProcAddress")) {
        az_GetProcAddress = (FARPROC (WINAPI *)(HMODULE, const char *))p;
        p = (FARPROC)fm6_GetProcAddress;
    }
    return import_finish(entry, p);
}
