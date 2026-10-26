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

/*
 * reflections do FM6 (docs\contexto\fm6\07_opcoes_graficas.txt, PASSO 120). Mesmo esquema do FM5 (cont. 6):
 *   Cubemap (CubemapRenderer11@Presentation11, ctor 0x3626b4): cubo 512 com mips (0x3629ac), [+0x2a0] = 512,
 *       contagem de mips (laco a partir de 256), tabela de angulo solido 512 (0x362849) preenchida em 0x362a58 com
 *       512 >> mip; cubo por sonda (trecho frio 0x98f9a2) e o do carro ([+0x1630] = 512 em 0x32dc0f, lido em
 *       0xe6871f; o difuso [+0x162c] = 32 fica). Todos os 512 trocados juntos.
 *   EnvMapFrequencyScale (id 0x32, getter 0x3e64ac -> float; uso 0xda6f2a = min(N * escala, faces)). Global 1,
 *       1-Car 3, PhotoMode 0.5, FrontEnd/CarSelect/Pre/PostRace 0 (cubo estatico, fica 0).
 *   Espelhos (MirrorRenderer11 vtable 0x19f57f8 slot 1 = 0x33627c): 768x256 fixo; largura em esi (0x3362ea), altura
 *       em ebx = lea ebx,[r14+1] com r14 = 0xff (0x3362e3) para as 4 texturas (e as 4 do trecho frio 0x988f2a com
 *       MSAA); viewport {768, 256} em 0x3363ed/0x3363f3. A altura nao e imediato: os 7 bytes de lea ebx / mov r8d,ebx
 *       viram call rel32 para um trecho no padding cc (0x7492): mov ebx, altura ; mov r8d, ebx ; ret.
 *   HalfRateMirror (id 0x35, getter 0x3fdf60 -> bool): 8/12/16/24-Car = 1 (espelho a 30 fps).
 *   WindshieldReflectionResolution (id 0x3d, getter 0x4fcef0 -> int, chamador 0x4fcb08): altura (Global 512),
 *       largura = altura * 16 / 9.
 * Niveis (como no FM5): low = cubo 256, espelho 384x128, para-brisa /2; high = cubo 1024, espelho 1536x512,
 *   para-brisa x2, espelho sempre a 60, frequencia x2; ultra = cubo 2048, espelho 2304x768, para-brisa x4 (max 2048),
 *   espelho a 60, frequencia x6.
 */
#define FM6_GET_ENVMAP_FREQ      0x3e64ac
#define FM6_GET_HALF_RATE_MIRROR 0x3fdf60
#define FM6_GET_WINDSHIELD_RES   0x4fcef0
/* push rdi; sub rsp,0x50; mov qword [rsp+0x20],-2; mov [rsp+0x68],rbx */
#define FM6_ENVMAP_PROLOGUE "40574883ec5048c7442420feffffff48895c2468"
#define FM6_MIRROR_HEIGHT   0x3362e3   /* lea ebx,[r14+1] ; mov r8d,ebx */
#define FM6_MIRROR_CAVE     0x7492     /* 14 bytes cc depois de um ret */

typedef struct { uint32_t rva; uint8_t off; const char *orig; uint8_t len; uint32_t base; } Fm6ImmSite;

/* imediatos de 32 bits: rva da instrucao, offset do imediato, bytes originais, valor base (escalado) */
static const Fm6ImmSite fm6_cube_sites[] = {
    { 0x36278b, 2, "41bf00020000", 6, 512 },             /* ctor: cubo e [+0x2a0] */
    { 0x36283c, 1, "b800010000", 5, 256 },               /* contagem de mips (laco a partir de tamanho/2) */
    { 0x362849, 1, "b800020000", 5, 512 },               /* tabela de angulo solido: largura = altura */
    { 0x362ac3, 1, "bb00020000", 5, 512 },               /* 0x362a58: tamanho do mip 0 da tabela */
    { 0x98f9aa, 1, "ba00020000", 5, 512 },               /* cubo por sonda */
    { 0x32dc0f, 6, "c7873016000000020000", 10, 512 },    /* cubo do carro [+0x1630] */
};
static const Fm6ImmSite fm6_mirror_sites[] = {
    { 0x3362ea, 1, "be00030000", 5, 768 },               /* largura das texturas */
    { 0x3363ed, 2, "c70600030000", 6, 768 },             /* viewport dos espelhos: largura */
    { 0x3363f3, 4, "48c7460400010000", 8, 256 },         /*                         altura  */
};

static int fm6_patch_imm_sites(uint8_t *game, const Fm6ImmSite *s, size_t n, uint32_t num, uint32_t den,
                               const char *what)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!bytes_match(game + s[i].rva, s[i].orig, s[i].len)) {
            wlog("FM6: reflections: %s: site %#x does not match, ignored", what, s[i].rva);
            return 0;
        }
    for (i = 0; i < n; i++) {
        uint32_t v = s[i].base * num / den;
        write_mem(game + s[i].rva + s[i].off, &v, 4);
    }
    return 1;
}

static int fm6_mirror_height_ok(const uint8_t *game)
{
    if (bytes_match(game + FM6_MIRROR_HEIGHT, "418d5e01448bc3", 7) &&
        bytes_match(game + FM6_MIRROR_CAVE - 1, "c3cccccccccccccccccccc", 11))
        return 1;
    wlog("FM6: reflections: mirror height site does not match, ignored");
    return 0;
}

/* altura dos espelhos: call para o trecho no padding (mov ebx, h ; mov r8d, ebx ; ret) */
static void fm6_mirror_height(uint8_t *game, uint32_t h)
{
    uint8_t cave[9] = { 0xbb, 0, 0, 0, 0, 0x44, 0x8b, 0xc3, 0xc3 };
    uint8_t call[7] = { 0xe8, 0, 0, 0, 0, 0x66, 0x90 };
    int32_t rel = (int32_t)(FM6_MIRROR_CAVE - (FM6_MIRROR_HEIGHT + 5));
    memcpy(cave + 1, &h, 4);
    memcpy(call + 1, &rel, 4);
    write_mem(game + FM6_MIRROR_CAVE, cave, sizeof cave);
    write_mem(game + FM6_MIRROR_HEIGHT, call, sizeof call);
}

typedef float (*getter_float_fn)(void *mgr, int idx);
typedef uint8_t (*getter_bool_fn)(void *mgr, int idx);
static getter_float_fn fm6_orig_envmap;
static getter_bool_fn fm6_orig_half_rate;
static getter_int_fn fm6_orig_windshield;
static int fm6_refl_level;

static float fm6_get_envmap(void *mgr, int idx)
{
    static float last = -99.0f;
    float o = fm6_orig_envmap(mgr, idx), v = o > 0.0f ? o * (fm6_refl_level == 2 ? 2.0f : 6.0f) : o;
    if (o != last) { last = o; wlog("FM6: reflections: EnvMapFrequencyScale %g -> %g", o, v); }
    return v;
}

static uint8_t fm6_get_half_rate(void *mgr, int idx)
{
    static int last = -99;
    uint8_t o = fm6_orig_half_rate(mgr, idx);
    if (o != last) { last = o; wlog("FM6: reflections: HalfRateMirror %d -> 0", o); }
    return 0;
}

static int fm6_get_windshield(void *mgr, int idx)
{
    static int last = -99;
    int o = fm6_orig_windshield(mgr, idx), v = o;
    if (fm6_refl_level == 0) v = o / 2 < 128 ? 128 : o / 2;
    else if (o < 2048) { v = o * (fm6_refl_level == 2 ? 2 : 4); if (v > 2048) v = 2048; }
    if (o != last) { last = o; wlog("FM6: reflections: WindshieldReflectionResolution %d -> %d", o, v); }
    return v;
}

void fm6_reflections(uint8_t *game, int level)
{
    static const uint32_t cube[] = { 256, 512, 1024, 2048 };
    static const uint32_t mirror_num[] = { 1, 1, 2, 3 }, mirror_den[] = { 2, 1, 1, 1 };
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM6: reflections %d (%s): original reflections", level, gfx_level_name(level));
        return;
    }
    fm6_refl_level = level;
    if (fm6_patch_imm_sites(game, fm6_cube_sites, sizeof fm6_cube_sites / sizeof *fm6_cube_sites, cube[level], 512,
                            "cubemap"))
        wlog("FM6: reflections: cubemap 512 -> %u", cube[level]);
    /* largura e altura juntas: o site da altura e conferido antes de escrever a largura */
    if (fm6_mirror_height_ok(game) &&
        fm6_patch_imm_sites(game, fm6_mirror_sites, sizeof fm6_mirror_sites / sizeof *fm6_mirror_sites,
                            mirror_num[level], mirror_den[level], "mirrors")) {
        fm6_mirror_height(game, 256 * mirror_num[level] / mirror_den[level]);
        wlog("FM6: reflections: mirrors 768x256 -> %ux%u", 768 * mirror_num[level] / mirror_den[level],
             256 * mirror_num[level] / mirror_den[level]);
    }
    if (bytes_match(game + FM6_GET_WINDSHIELD_RES, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN))
        hook_at(game + FM6_GET_WINDSHIELD_RES, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_windshield,
                (void **)&fm6_orig_windshield);
    else wlog("FM6: reflections: WindshieldReflectionResolution getter does not match, ignored");
    if (level >= 2) {
        if (bytes_match(game + FM6_GET_HALF_RATE_MIRROR, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN))
            hook_at(game + FM6_GET_HALF_RATE_MIRROR, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_half_rate,
                    (void **)&fm6_orig_half_rate);
        else wlog("FM6: reflections: HalfRateMirror getter does not match, ignored");
        if (bytes_match(game + FM6_GET_ENVMAP_FREQ, FM6_ENVMAP_PROLOGUE, FM6_GETTER_STOLEN))
            hook_at(game + FM6_GET_ENVMAP_FREQ, FM6_ENVMAP_PROLOGUE, FM6_GETTER_STOLEN, (void *)fm6_get_envmap,
                    (void **)&fm6_orig_envmap);
        else wlog("FM6: reflections: EnvMapFrequencyScale getter does not match, ignored");
    }
    wlog("FM6: reflections %d (%s): applied", level, gfx_level_name(level));
}
