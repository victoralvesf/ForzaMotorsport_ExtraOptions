/*
 * Resolucao e fps do launcher aplicados ao jogo (FM5 e FM6): o mecanismo e o mesmo nos dois, os sites vem de cada
 * jogo (fm5/fm5.c, fm6/fm6.c; ResSites em common/ui.h).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../proxy.h"
#include "ui.h"

/* Resolucao do launcher (PatchOptions +4 altura, +8 largura; 0 = nao mexe), como 0x14003e720 faz no FH2:
   o imediato 1080 vira a altura; com largura != 0 o vcvttss2si (largura = altura * 16/9) vira mov r32, largura.
   Aspecto != 16:9: a UI (sites +0x130..+0x1c8 do FH2) ainda nao e ajustada. Sites por jogo em ResSites. */
/* Viewport da UI (cont. 3): o renderizador da UI le o tamanho da UI (1920x1080, fixo abaixo), monta a projecao
   ortografica com ele e usa o MESMO tamanho no viewport ([rdi+0x18] = contexto, rax = objeto: call [rax+off] =
   ponteiro de RSSetViewports guardado NO objeto (tabela rapida do XDK, nao a vtable)) -> a UI ficava num quadrado
   1920x1080 no canto. A chamada vira call [rip -> slot no padding cc] = fm5_ui_vcall, que escala o viewport.
   FM5: 0xb60ca0, call [rax+0x188] em 0xb60db8, {0,0,w,h,0,1} em rsp+0x30 (o call [rax+0x130] seguinte e
   OMSetRenderTargets). FM6: 0x127d90, call [rax+0x538] em 0x127ea5, viewport no membro [rdi+0x130]
   (OMSetRenderTargets = [rax+0x4e0]). */
static uint8_t *ui_game;
static const ResSites *ui_sites;
static float ui_sx = 1, ui_sy = 1;
static uint32_t ui_w, ui_h;                      /* tamanho do render (launcher) */

/* Tamanho do RT de cada RTV (CreateRenderTargetView do FM5/FM6 -> ui_rtv_note): o renderizador da UI tambem desenha
   paginas em texturas proprias (painel digital do cockpit, FM6 2026-10-25) com o mesmo viewport 1920x1080; so o
   alvo do tamanho do render e escalado. Tabela aberta por ponteiro; view reaproveitada = entrada sobrescrita. */
#define UI_RTV_N 4096
static struct { const void *rtv; uint32_t w, h; } ui_rtv[UI_RTV_N];
static SRWLOCK ui_rtv_lock = SRWLOCK_INIT;
static unsigned ui_rtv_home(const void *rtv) { return (unsigned)(((uintptr_t)rtv >> 4) * 0x9e3779b1u) % UI_RTV_N; }

void ui_rtv_note(const void *rtv, const void *res)
{
    const uint8_t *r = res;
    unsigned i, k, h;
    if (!rtv) return;
    h = ui_rtv_home(rtv);
    AcquireSRWLockExclusive(&ui_rtv_lock);
    for (k = 0, i = h; k < 16; k++, i = (i + 1) % UI_RTV_N)
        if (ui_rtv[i].rtv == rtv || !ui_rtv[i].rtv) break;
    if (k == 16) i = h;
    ui_rtv[i].rtv = rtv;
    ui_rtv[i].w = r && *(const uint32_t *)(r + 0x38) == 0xc ? *(const uint32_t *)(r + 0x70) : 0;   /* Texture2D */
    ui_rtv[i].h = r && *(const uint32_t *)(r + 0x38) == 0xc ? *(const uint32_t *)(r + 0x74) : 0;
    ReleaseSRWLockExclusive(&ui_rtv_lock);
}

static int ui_rtv_size(const void *rtv, uint32_t *w, uint32_t *h)
{
    unsigned i, k;
    int found = 0;
    if (!rtv) return 0;
    AcquireSRWLockShared(&ui_rtv_lock);
    for (k = 0, i = ui_rtv_home(rtv); k < 16 && ui_rtv[i].rtv; k++, i = (i + 1) % UI_RTV_N)
        if (ui_rtv[i].rtv == rtv) { *w = ui_rtv[i].w; *h = ui_rtv[i].h; found = ui_rtv[i].w != 0; break; }
    ReleaseSRWLockShared(&ui_rtv_lock);
    return found;
}

/* renderizador da UI (rdi, preservado) no 4o argumento: o RSSetViewports so tem 3 */
void fm5_ui_vcall_stub(void);
__asm__(".globl fm5_ui_vcall_stub\n"
        "fm5_ui_vcall_stub:\n"
        "  mov %rdi, %r9\n"
        "  jmp fm5_ui_vcall\n");

void fm5_ui_vcall(void *obj, uint32_t n, void *p, uintptr_t renderer)
{
    typedef void (*vc_fn)(void *, uint32_t, void *, uintptr_t);
    static volatile LONG logged_vp, logged_unknown, nkept;
    static struct { uint32_t w, h; } kept[8];
    const ResSites *rs = ui_sites;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0) - (uintptr_t)ui_game;
    void **tab = (void **)obj;                   /* call [rax+off] com rax = objeto: ponteiro NO objeto (tabela rapida do XDK) */
    if (ret == rs->vp_call + 6 && p) {
        float v[6];
        void *rtv = renderer ? *(void **)(renderer + rs->vp_rtv) : NULL;   /* RT que ele liga logo depois */
        uint32_t w = 0, h = 0;
        if (ui_rtv_size(rtv, &w, &h) && (w != ui_w || h != ui_h)) {
            LONG i, k = nkept < 8 ? nkept : 8;
            for (i = 0; i < k && (kept[i].w != w || kept[i].h != h); i++) ;
            if (i == k && k < 8) {
                kept[k].w = w; kept[k].h = h; nkept = k + 1;
                wlog("%s: UI viewport %.0f,%.0f %.0fx%.0f on a %ux%u target (not the screen): kept", rs->tag,
                     ((float *)p)[0], ((float *)p)[1], ((float *)p)[2], ((float *)p)[3], w, h);
            }
            ((vc_fn)tab[rs->vp_off / 8])(obj, n, p, 0);
            return;
        }
        if (!w && rtv && InterlockedIncrement(&logged_unknown) <= 2)
            wlog("%s: UI viewport: target size unknown (rtv %p), scaled", rs->tag, rtv);
        memcpy(v, p, sizeof v);
        v[0] *= ui_sx; v[2] *= ui_sx; v[1] *= ui_sy; v[3] *= ui_sy;
        if (InterlockedIncrement(&logged_vp) <= 2)
            wlog("%s: UI viewport %.0f,%.0f %.0fx%.0f -> %.0f,%.0f %.0fx%.0f (target game+%#llx)", rs->tag,
                 ((float *)p)[0], ((float *)p)[1], ((float *)p)[2], ((float *)p)[3], v[0], v[1], v[2], v[3],
                 (unsigned long long)((uintptr_t)tab[rs->vp_off / 8] - (uintptr_t)ui_game));
        ((vc_fn)tab[rs->vp_off / 8])(obj, n, v, 0);
        return;
    }
    wlog("%s: fm5_ui_vcall called from unexpected game+%#llx", rs->tag, (unsigned long long)ret);
}

static void fm5_ui_viewport(uint8_t *game, const ResSites *rs, uint32_t w, uint32_t h)
{
    static const uint8_t pad[8] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
    void *fn = (void *)fm5_ui_vcall_stub;
    uint8_t code[6] = { 0xff, 0x15 };
    int32_t rel;
    if (!bytes_match(game + rs->vp_call, rs->vp_call_orig, 6) || memcmp(game + rs->vp_slot, pad, 8)) {
        wlog("%s: resolution: UI viewport sites do not match, ignored", rs->tag);
        return;
    }
    ui_game = game; ui_sites = rs; ui_sx = w / 1920.0f; ui_sy = h / 1080.0f; ui_w = w; ui_h = h;
    write_mem(game + rs->vp_slot, &fn, 8);
    rel = (int32_t)(rs->vp_slot - (rs->vp_call + 6));   memcpy(code + 2, &rel, 4); write_mem(game + rs->vp_call, code, 6);
    wlog("%s: resolution: UI viewport scaled to %ux%u", rs->tag, w, h);
}

/* Graficos da UI (FM6 PASSO 107 cont. 1, FM5 PASSO 9 cont. 4; relacao de marchas da tunagem): o desenhista do
   grafico (FM6 0xcff4e8, FM5 0x521685) calcula a posicao/tamanho no espaco da UI (1920x1080) e chama o SetViewport
   do proprio renderizador (FM6 [rdi+0x1a0] slot 0x30, FM5 [rdi+0x198] slot 0x28; rdx = {int x, y, w, h; float minz,
   maxz}) -> fora da correcao do viewport da UI, ficava no lugar de 1080p. No site (mov rax,[rcx] ; 2 stores do
   retangulo ; call [rax+off]) os stores ficam e o call vira call [rip -> slot no padding] = ui_chart_vcall, que
   escala o retangulo como fm5_ui_vcall. */
static uintptr_t ui_chart_vcall(void *obj, const int32_t *r)
{
    typedef uintptr_t (*vc_fn)(void *, const void *);
    static volatile LONG logged;
    int32_t v[6];
    memcpy(v, r, sizeof v);
    v[0] = (int32_t)(r[0] * ui_sx + 0.5f); v[2] = (int32_t)((r[0] + r[2]) * ui_sx + 0.5f) - v[0];
    v[1] = (int32_t)(r[1] * ui_sy + 0.5f); v[3] = (int32_t)((r[1] + r[3]) * ui_sy + 0.5f) - v[1];
    if (InterlockedIncrement(&logged) <= 2)
        wlog("%s: UI graphic %d,%d %dx%d -> %d,%d %dx%d", ui_sites->tag, r[0], r[1], r[2], r[3], v[0], v[1], v[2], v[3]);
    return ((vc_fn)(*(void ***)obj)[ui_sites->chart_off / 8])(obj, v);
}

static void ui_chart_viewport(uint8_t *game, const ResSites *rs)
{
    static const uint8_t pad[8] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
    void *fn = (void *)ui_chart_vcall;
    uint8_t code[18] = { 0xc5, 0xf8, 0x29, 0x44, 0x24, 0x30,      /* vmovaps [rsp+0x30], xmm0 */
                         0xc5, 0xfb, 0x11, 0x4c, 0x24, 0x40,      /* vmovsd [rsp+0x40], xmm1 */
                         0xff, 0x15 };                            /* call [rip -> slot] */
    char orig[37];
    int32_t rel;
    snprintf(orig, sizeof orig, "488b01c5f829442430c5fb114c2440ff50%02x", rs->chart_off);
    if (!bytes_match(game + rs->chart_vp, orig, 18) || memcmp(game + rs->chart_slot, pad, 8)) {
        wlog("%s: resolution: UI graphics viewport site does not match, ignored", rs->tag);
        return;
    }
    write_mem(game + rs->chart_slot, &fn, 8);
    rel = (int32_t)(rs->chart_slot - (rs->chart_vp + 18));   memcpy(code + 14, &rel, 4);
    write_mem(game + rs->chart_vp, code, sizeof code);
    wlog("%s: resolution: UI graphics viewport scaled", rs->tag);
}

void game_resolution(uint8_t *game, const ResSites *rs, uint32_t height, uint32_t width)
{
    if (!height && !width) return;
    if (height && width && (uint64_t)width * 9 != (uint64_t)height * 16)
        wlog("%s: resolution: aspect %ux%u != 16:9, UI not adjusted (scene only)", rs->tag, width, height);
    if (height) {
        if (!bytes_match(game + rs->height, rs->height_orig, 10))
            wlog("%s: resolution: height site %#x does not match, ignored", rs->tag, rs->height);
        else {
            write_mem(game + rs->height + 6, &height, 4);
            wlog("%s: resolution: height %u", rs->tag, height);
        }
    }
    if (width) {
        uint8_t code[5] = { rs->width_mov };
        if (!bytes_match(game + rs->width, rs->width_orig, 21))
            wlog("%s: resolution: width site %#x does not match, ignored", rs->tag, rs->width);
        else {
            memcpy(code + 1, &width, 4);
            write_mem(game + rs->width + 0x10, code, sizeof code);
            wlog("%s: resolution: width %u", rs->tag, width);
        }
    }
    /* = "UI size from the display" do FH2: o layout da UI e feito para 1920x1080; com o tamanho do render a UI
       ficava no canto superior esquerdo (cont. 2). Forca 1920.0/1080.0 nos dois floats passados ao setter da UI. */
    if (!bytes_match(game + rs->ui_size, rs->ui_size_orig, rs->ui_size_len))
        wlog("%s: resolution: UI size site %#x does not match, ignored", rs->tag, rs->ui_size);
    else {
        write_mem(game + rs->ui_size, rs->ui_size_code, rs->ui_size_len);
        wlog("%s: resolution: UI fixed at 1920x1080", rs->tag);
        fm5_ui_viewport(game, rs, width ? width : (uint32_t)(((uint64_t)(height ? height : 1080) * 16 + 4) / 9),
                        height ? height : 1080);
        if (ui_game == game && ui_sites == rs) ui_chart_viewport(game, rs);
    }
}


/* FPS (PASSO 10): o Present do FM5 (Frame11 slot 0x138 = 0x7146e0) le [frame+0xa0]: 0 -> SyncInterval 1 (60 fps),
   1 -> 2 (30 fps), outro -> 0. O campo e escrito so pelo setter virtual slot 0x180 (0x711620 mov [rcx+0xa0],edx)
   e lido pelo getter slot 0x188 (0x711630). Nos menus/transicoes o jogo pede 1 (30 fps): RenderThreadBaseDeferred
   slot 0x58 (envio do frame, modo em r9d; sites 0x5bcd16 e 0x6017a4) -> slot 0x80 0x59d070 enfileira {0x535ec0,..}
   -> executor 0x682860 -> Frame slot 0x180. Com frame_rate >= 60 no launcher o wrapper troca 1 por 0. */
uint8_t *fps_game;
int fps_force;
const char *fps_tag = "FM5";
uintptr_t fps_queue_ret = 0x6828d0;   /* retorno da chamada do executor da fila (FM5; FM6 = 0, nao mapeado) */

/* stub: rbx (preservado) = no da fila do executor 0x682860 ({fn, rcx, rdx, ?, next}) quando chamado de la */
void fm5_frame_set_sync(uint8_t *frame, uint32_t mode, uintptr_t rbx, uintptr_t ret);
void fm5_frame_set_sync_stub(void);
__asm__(".globl fm5_frame_set_sync_stub\n"
        "fm5_frame_set_sync_stub:\n"
        "  mov %rbx, %r8\n"
        "  mov (%rsp), %r9\n"
        "  jmp fm5_frame_set_sync\n");

void fm5_frame_set_sync(uint8_t *frame, uint32_t mode, uintptr_t rbx, uintptr_t ret)
{
    static volatile LONG last = -1;
    uint32_t v = (fps_force && mode == 1) ? 0 : mode;
    if (InterlockedExchange(&last, (LONG)mode) != (LONG)mode) {
        uintptr_t g = (uintptr_t)fps_game, fn = 0;
        MEMORY_BASIC_INFORMATION mi;
        if (fps_queue_ret && ret - g == fps_queue_ret && VirtualQuery((void *)rbx, &mi, sizeof mi) &&
            mi.State == MEM_COMMIT)
            fn = *(uintptr_t *)rbx - g;
        if (fps_queue_ret)
            wlog("%s: fps: present mode %u -> %u (caller game+%#llx, queue fn game+%#llx, limiter [+0x500]=%u)",
                 fps_tag, mode, v, (unsigned long long)(ret - g), (unsigned long long)fn, frame[0x500]);
        else
            wlog("%s: fps: present mode %u -> %u (caller game+%#llx)", fps_tag, mode, v,
                 (unsigned long long)(ret - g));
    }
    *(uint32_t *)(frame + 0xa0) = v;
}

/* nivel das opcoes graficas do launcher (PatchOptions+0x14: 0 low, 1 default, 2 high, 3 ultra) */
const char *gfx_level_name(int l)
{
    static const char *n[] = { "low", "default", "high", "ultra" };
    return l >= 0 && l < 4 ? n[l] : "?";
}
