/*
 * Diagnostico da plotagem (livery) do FM6 (docs\contexto\16_opcoes_graficas.txt PASSO 12). So com debug.ini
 * LiveryLog=1; nao muda nada no jogo, so loga.
 *
 * Caminho no FM6 (igual ao FM5): 0xe05c8c(mgr, lowq) percorre as 6 secoes do carro; 0xe00344 cria os alvos da
 * secao pela funcao de render target do motor 0x335ba4 (out, w, h, fmt do motor, nome, word amostras {count,
 * quality}, ...): "LivMSAATarget-%d-%d" (amostras do byte global 0x1ba8bf8, 8 no exe; tamanho da secao x2),
 * "LivNonMSAA-%d-%d", "LivNonMSAAHighlight-%d-%d", "LivDepthStencil-%d-%d"; 0xe0080c cria "LivRenderTexture".
 * Os vinis sao desenhados no alvo MSAA, resolvidos (ResolveSubresource) e copiados para a textura da pintura.
 *
 * Janela: durante 0xe05c8c (na thread que chama) os hooks de d3d_fm5.c contam pipelines buscados/nulos, draws
 * descartados, resolves, dispatches e texturas criadas; no fim uma linha "livery: render ... end" com os totais.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

extern void wlog(const char *fmt, ...);
extern int bytes_match(const uint8_t *p, const char *hex, size_t len);
extern int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp);

#define FM6_LIV_RENDER_SECTIONS 0xe05c8c   /* (mgr, lowq) */
#define FM6_CREATE_RT           0x335ba4   /* render target do motor */
#define FM6_LIV_SAMPLES         0x1ba8bf8  /* byte: amostras do LivMSAATarget/LivDepthStencil */

typedef uint64_t (*sections_fn)(void *, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*create_rt_fn)(void *, uint64_t, uint64_t, uint64_t, const char *, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t);
static sections_fn orig_sections;
static create_rt_fn orig_create_rt;

int liv_log;                               /* LiveryLog do debug.ini */
static __thread int liv_depth;
static __thread long liv_pso, liv_pso_null, liv_skips, liv_resolves, liv_dispatches, liv_textures;
static volatile LONG liv_renders;

int liv_in_window(void) { return liv_depth > 0; }

void liv_note_pso(void *pso, int wait, int ctx)
{
    if (!liv_depth) return;
    liv_pso++;
    if (!pso && ++liv_pso_null <= 8) wlog("livery: null pipeline on c%d (wait=%d) inside the livery render", ctx, wait);
}

void liv_note_skip(int ctx)
{
    if (!liv_depth) return;
    if (++liv_skips <= 8) wlog("livery: draw skipped on c%d inside the livery render", ctx);
}

void liv_note_resolve(int ctx, UINT sw, UINT sh, UINT sfmt, UINT ss, UINT dw, UINT dh, UINT dfmt, UINT ds, UINT fmt)
{
    if (!liv_depth) return;
    if (++liv_resolves <= 12)
        wlog("livery: ResolveSubresource c%d %ux%u fmt %u samples %u -> %ux%u fmt %u samples %u (view fmt %u)", ctx,
             sw, sh, sfmt, ss, dw, dh, dfmt, ds, fmt);
}

void liv_note_dispatch(UINT x, UINT y, UINT z)
{
    if (!liv_depth) return;
    if (++liv_dispatches <= 4) wlog("livery: Dispatch %ux%ux%u inside the livery render", x, y, z);
}

void liv_note_texture(const UINT *d, long hr)
{
    static volatile LONG bc_logged;
    if (!liv_log || !d) return;
    if (liv_depth) {
        if (++liv_textures <= 32)
            wlog("livery: CreateTexture2D %ux%u mips %u fmt %u samples %u quality %#x bind %#x misc %#x hr %#lx",
                 d[0], d[1], d[2], d[4], d[5], d[6], d[8], d[10], hr);
        return;
    }
    /* BC6H (95/96) e BC7 (98/99) grandes fora da janela: candidatas a saida comprimida da pintura */
    if ((d[4] == 95 || d[4] == 96 || d[4] == 98 || d[4] == 99) && d[0] >= 512 && d[1] >= 512 &&
        InterlockedIncrement(&bc_logged) <= 48)
        wlog("livery: BC texture %ux%u mips %u fmt %u bind %#x usage %u misc %#x hr %#lx (thread %lu)", d[0], d[1],
             d[2], d[4], d[8], d[7], d[10], hr, GetCurrentThreadId());
}

static uint64_t hook_sections(void *mgr, uint64_t lowq, uint64_t a3, uint64_t a4)
{
    LARGE_INTEGER f, t0, t1;
    uint64_t r;
    LONG n = InterlockedIncrement(&liv_renders);
    if (!liv_depth++) liv_pso = liv_pso_null = liv_skips = liv_resolves = liv_dispatches = liv_textures = 0;
    QueryPerformanceCounter(&t0);
    r = orig_sections(mgr, lowq, a3, a4);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    if (!--liv_depth)
        wlog("livery: render #%ld (mgr %p, %s quality) end in %.1f ms: pipelines %ld (null %ld), draws skipped %ld, "
             "resolves %ld, dispatches %ld, textures %ld (thread %lu)", n, mgr, (lowq & 0xff) ? "low/preview" : "full",
             (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart, liv_pso, liv_pso_null, liv_skips,
             liv_resolves, liv_dispatches, liv_textures, GetCurrentThreadId());
    return r;
}

static uint64_t hook_create_rt(void *out, uint64_t w, uint64_t h, uint64_t fmt, const char *name, uint64_t samples,
                               uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10)
{
    if (name && name[0] == 'L' && name[1] == 'i' && name[2] == 'v')
        wlog("livery: render target \"%s\" %ux%u engine fmt %#x samples %u quality %u", name, (UINT)w, (UINT)h,
             (UINT)(fmt & 0xff), (UINT)(samples & 0xff), (UINT)((samples >> 8) & 0xff));
    return orig_create_rt(out, w, h, fmt, name, samples, a7, a8, a9, a10);
}

void fm6_livery_log_install(uint8_t *game)
{
    /* mov rax,rsp; push rbp; push rsi; push rdi; push r12; push r13; push r14; push r15 */
    if (!hook_at(game + FM6_LIV_RENDER_SECTIONS, "488bc45556574154415541564157", 14, (void *)hook_sections,
                 (void **)&orig_sections) ||
        /* mov r11,rsp; push rbx; sub rsp,0xc0; mov qword [r11-0x68],-2 */
        !hook_at(game + FM6_CREATE_RT, "4c8bdc534881ecc000000049c74398feffffff", 19, (void *)hook_create_rt,
                 (void **)&orig_create_rt)) {
        wlog("FM6: LiveryLog: sites do not match, ignored");
        return;
    }
    liv_log = 1;
    wlog("FM6: LiveryLog=1 (livery render targets, render window counters, BC6H/BC7 textures); MSAA samples byte %u",
         game[FM6_LIV_SAMPLES]);
}
