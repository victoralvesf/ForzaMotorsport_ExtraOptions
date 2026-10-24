/*
 * Opcoes graficas do launcher aplicadas ao FM6 (docs\contexto\fm6\07_opcoes_graficas.txt).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"
#include "../common/ui.h"
#include "fm6.h"

typedef void *(*getter_fn)(void *mgr, void *out, int idx);
typedef int (*getter_int_fn)(void *mgr, int idx);

/*
 * car_detail do FM6 (docs\contexto\fm6\07_opcoes_graficas.txt). Mesmos dynamic render settings do FM5, com os bits
 * de presenca testados por byte/dword e variantes de chuva/noite (selecionadas pelo jogo; todas com hook):
 *   CarLODThresholds (id 9 / Night 0xa / Wet 0xb; getters 0x43fe54 / 0xe48e24 / 0x20d01c, selecao 0x20cf54)
 *       {lod1..lod5, count}: NAO e ranking acumulado como no FM5: 0x5eb684 percorre os carros (ordem de distancia)
 *       e da a classe k (LOD minimo k) aos primeiros lodk carros de cada faixa; quem sobra fica com 6 (proxy).
 *       Com >= count carros cada faixa perde 1. Global.xml: 0/3/4/4/7, 25 (nenhum carro na faixa 1).
 *   CarLODTransDist (id 0x11 / 0x12 / 0x13; getters 0x18ca6c / 0xdb802c / 0x18cbac, uso 0x18ccec)
 *       {near, far, veryFar, farthest, lodproxy}: LOD 1 ate near, 2..5 nas faixas, 6 alem de lodproxy;
 *       resultado = max(distancia, classe). Global.xml: 15/35/60/100/300.
 *   CarFocusLODMinMax (id 0xc, getter 0x445d2c, uso 0x18e236): limita o carro focado (0x18defc). Global 1/6.
 *       A distancia nunca da LOD0 (melhor = 1): o LOD0 so aparece no cockpit / foto / pre e pos-corrida.
 *   Mascara de LODs carregados [modelo+0xa0] (bit k = LOD k-1): o jogo usa o primeiro carregado >= o pedido.
 * Niveis: low = faixas uma classe pior e distancias x0.6; high = faixas uma classe melhor, distancias x2 e
 *   carro focado no LOD0 (max 0, como o modo foto); ultra = todos os carros na faixa 1, distancias x4 e LOD0.
 */
#define FM6_GET_CAR_LOD_THRESHOLDS       0x43fe54
#define FM6_GET_CAR_NIGHT_LOD_THRESHOLDS 0xe48e24
#define FM6_GET_CAR_WET_LOD_THRESHOLDS   0x20d01c
#define FM6_GET_CAR_LOD_TRANSDIST        0x18ca6c
#define FM6_GET_CAR_NIGHT_LOD_TRANSDIST  0xdb802c
#define FM6_GET_CAR_WET_LOD_TRANSDIST    0x18cbac
#define FM6_GET_CAR_FOCUS_LOD            0x445d2c
/* push rdi; sub rsp,0x40; mov qword [rsp+0x20],-2; mov [rsp+0x58],rbx (igual nos 7 getters) */
#define FM6_GETTER_PROLOGUE "40574883ec4048c7442420feffffff48895c2458"
#define FM6_GETTER_STOLEN   20

static getter_fn fm6_orig_thr[3], fm6_orig_dist[3], fm6_orig_focus;
static int fm6_car_level;

static void fm6_log_ints(const char *what, int32_t *last, const int32_t *o, const int32_t *v, int n)
{
    if (!memcmp(last, o, n * sizeof *o)) return;
    memcpy(last, o, n * sizeof *o);
    if (n == 6)
        wlog("FM6: car_detail: %s %d/%d/%d/%d/%d/%d -> %d/%d/%d/%d/%d/%d", what, o[0], o[1], o[2], o[3], o[4], o[5],
             v[0], v[1], v[2], v[3], v[4], v[5]);
    else
        wlog("FM6: car_detail: %s %d/%d -> %d/%d", what, o[0], o[1], v[0], v[1]);
}

/* {lod1..lod5, count} -> faixas por nivel; t[0] = faixa 1 */
static void fm6_thresholds(int32_t *t)
{
    int32_t o[6], i;
    memcpy(o, t, sizeof o);
    if (fm6_car_level == 0) {                /* cada carro uma classe pior */
        t[0] = 0;
        for (i = 1; i < 4; i++) t[i] = o[i - 1];
        t[4] = o[3] + o[4];
    } else if (fm6_car_level == 2) {         /* cada carro uma classe melhor, mais carros na ultima faixa */
        t[0] = o[0] + o[1];
        for (i = 1; i < 4; i++) t[i] = o[i + 1];
        t[4] = o[4] * 2;
    } else if (fm6_car_level == 3) {         /* todos na faixa 1; nunca perde pela quantidade de carros */
        t[0] = 64;
        if (t[5] < 99) t[5] = 99;
    }
}

static void *fm6_get_thr(int k, void *mgr, int32_t *out, int idx)
{
    static const char *names[] = { "CarLODThresholds", "CarNightLODThresholds", "CarWetLODThresholds" };
    static int32_t last[3][6] = { { -99 }, { -99 }, { -99 } };
    void *r = fm6_orig_thr[k](mgr, out, idx);
    int32_t o[6];
    memcpy(o, out, sizeof o);
    fm6_thresholds(out);
    fm6_log_ints(names[k], last[k], o, out, 6);
    return r;
}
static void *fm6_get_thr0(void *m, int32_t *o, int i) { return fm6_get_thr(0, m, o, i); }
static void *fm6_get_thr1(void *m, int32_t *o, int i) { return fm6_get_thr(1, m, o, i); }
static void *fm6_get_thr2(void *m, int32_t *o, int i) { return fm6_get_thr(2, m, o, i); }

static void *fm6_get_dist(int k, void *mgr, float *out, int idx)
{
    static const char *names[] = { "CarLODTransDist", "CarNightLODTransDist", "CarWetLODTransDist" };
    static const float scale[] = { 0.6f, 1.0f, 2.0f, 4.0f };
    static float last[3][5] = { { -99 }, { -99 }, { -99 } };
    void *r = fm6_orig_dist[k](mgr, out, idx);
    float o[5];
    int i;
    memcpy(o, out, sizeof o);
    for (i = 0; i < 5; i++) out[i] = o[i] * scale[fm6_car_level];
    if (memcmp(last[k], o, sizeof o)) {
        memcpy(last[k], o, sizeof o);
        wlog("FM6: car_detail: %s %g/%g/%g/%g/%g -> %g/%g/%g/%g/%g", names[k], o[0], o[1], o[2], o[3], o[4],
             out[0], out[1], out[2], out[3], out[4]);
    }
    return r;
}
static void *fm6_get_dist0(void *m, float *o, int i) { return fm6_get_dist(0, m, o, i); }
static void *fm6_get_dist1(void *m, float *o, int i) { return fm6_get_dist(1, m, o, i); }
static void *fm6_get_dist2(void *m, float *o, int i) { return fm6_get_dist(2, m, o, i); }

/* high/ultra: o carro focado pede o LOD0 (o jogo cai no primeiro LOD carregado se nao houver) */
static void *fm6_get_focus(void *mgr, int32_t *out, int idx)
{
    static int32_t last[2] = { -99 };
    void *r = fm6_orig_focus(mgr, out, idx);
    int32_t o[2] = { out[0], out[1] };
    if (out[0] > 0) out[0] = 0;
    if (out[1] > 0) out[1] = 0;
    fm6_log_ints("CarFocusLODMinMax", last, o, out, 2);
    return r;
}

void fm6_car_detail(uint8_t *game, int level)
{
    static const uint32_t thr[] = { FM6_GET_CAR_LOD_THRESHOLDS, FM6_GET_CAR_NIGHT_LOD_THRESHOLDS,
                                    FM6_GET_CAR_WET_LOD_THRESHOLDS };
    static const uint32_t dist[] = { FM6_GET_CAR_LOD_TRANSDIST, FM6_GET_CAR_NIGHT_LOD_TRANSDIST,
                                     FM6_GET_CAR_WET_LOD_TRANSDIST };
    static void *const thr_hooks[] = { (void *)fm6_get_thr0, (void *)fm6_get_thr1, (void *)fm6_get_thr2 };
    static void *const dist_hooks[] = { (void *)fm6_get_dist0, (void *)fm6_get_dist1, (void *)fm6_get_dist2 };
    int i;
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM6: car_detail %d (%s): original LODs", level, gfx_level_name(level));
        return;
    }
    for (i = 0; i < 7; i++) {
        uint32_t rva = i < 3 ? thr[i] : i < 6 ? dist[i - 3] : FM6_GET_CAR_FOCUS_LOD;
        if (!bytes_match(game + rva, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN)) {
            wlog("FM6: car_detail: getter %#x does not match, ignored", rva);
            return;
        }
    }
    fm6_car_level = level;
    for (i = 0; i < 3; i++) {
        hook_at(game + thr[i], FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, thr_hooks[i], (void **)&fm6_orig_thr[i]);
        hook_at(game + dist[i], FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, dist_hooks[i], (void **)&fm6_orig_dist[i]);
    }
    if (level >= 2)
        hook_at(game + FM6_GET_CAR_FOCUS_LOD, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_focus,
                (void **)&fm6_orig_focus);
    wlog("FM6: car_detail %d (%s): car LOD getters hooked", level, gfx_level_name(level));
}

/*
 * shadows do FM6 (docs\contexto\fm6\07_opcoes_graficas.txt, PASSO 110). Mesmos controles do FM5 (getters (mgr, idx)
 * -> int, prologo FM6_GETTER_PROLOGUE):
 *   ShadowMapResolution (id 0x3a, getter 0x4410e4, 1 chamador 0xe42219): compara com o cache [x+0x188] e chama
 *       SetResolution(w = h = res) do gerador de sombras ([game+0x1dfa5d8] slot 1 -> 0x36008c), que recria os
 *       shadow maps. Global/Race 512, FrontEnd 1024, CarSelect/321 2048, Hidden 64 (fica).
 *   MaxHiDefShadows (id 0x3b, getter 0x4582d8, chamador 0x204fe4): orcamento de objetos com sombra hi-def nas
 *       cascatas. Global/FrontEnd 16; 1/8/12/16/24-Car 0 (corridas).
 * Niveis (como no FM5): low = resolucao /2 (min 256); high = x2 (max 4096) e hi-def >= 16; ultra = x4 (max 4096)
 *   e hi-def >= 64. Resolucoes <= 256 (cenario Hidden) nao mudam.
 */
#define FM6_GET_SHADOW_MAP_RES    0x4410e4
#define FM6_GET_MAX_HIDEF_SHADOWS 0x4582d8

static getter_int_fn fm6_orig_shadow_res, fm6_orig_hidef;
static int fm6_shadow_level;

static int fm6_get_shadow_res(void *mgr, int idx)
{
    static int last = -99;
    int o = fm6_orig_shadow_res(mgr, idx), v = o;
    if (o > 256) {
        if (fm6_shadow_level == 0) v = o / 2 < 256 ? 256 : o / 2;
        else if (o < 4096) { v = o * (fm6_shadow_level == 2 ? 2 : 4); if (v > 4096) v = 4096; }
    }
    if (o != last) { last = o; wlog("FM6: shadows: ShadowMapResolution %d -> %d", o, v); }
    return v;
}

static int fm6_get_hidef(void *mgr, int idx)
{
    static int last = -99;
    int o = fm6_orig_hidef(mgr, idx), min = fm6_shadow_level == 2 ? 16 : 64, v = o < min ? min : o;
    if (o != last) { last = o; wlog("FM6: shadows: MaxHiDefShadows %d -> %d", o, v); }
    return v;
}

void fm6_shadows(uint8_t *game, int level)
{
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM6: shadows %d (%s): original shadows", level, gfx_level_name(level));
        return;
    }
    if (!bytes_match(game + FM6_GET_SHADOW_MAP_RES, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN) ||
        (level >= 2 && !bytes_match(game + FM6_GET_MAX_HIDEF_SHADOWS, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN))) {
        wlog("FM6: shadows: getters do not match, ignored");
        return;
    }
    fm6_shadow_level = level;
    hook_at(game + FM6_GET_SHADOW_MAP_RES, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_shadow_res,
            (void **)&fm6_orig_shadow_res);
    if (level >= 2)
        hook_at(game + FM6_GET_MAX_HIDEF_SHADOWS, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_hidef,
                (void **)&fm6_orig_hidef);
    wlog("FM6: shadows %d (%s): shadow getters hooked", level, gfx_level_name(level));
}
