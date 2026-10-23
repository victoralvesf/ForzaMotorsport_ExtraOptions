/*
 * Opcoes graficas do launcher aplicadas ao FM5 (PASSO 9; docs\contexto\16_opcoes_graficas.txt).
 *
 * car_detail (PatchOptions+0x14 [2]; 0 low, 1 default, 2 high, 3 ultra): LOD dos carros.
 * O FM5 tira os parametros de LOD dos "dynamic render settings" (media\renderscenarios\*.xml: Global.xml +
 * o cenario atual, ex. Race / PhotoMode / PreRace_321). O gerenciador global [game+0x25f7e00] tem um getter por
 * controle que resolve as 3 camadas de cenario; os hooks abaixo alteram o valor resolvido (vale em todo cenario).
 *   CarLODThresholds   (id 9,   getter 0x859710) {lod1, lod2, lod3, count}: CPresentationView 0x84ca10 ordena
 *       os carros e da a "classe" (LOD minimo) pelo ranking dos nao focados: < lod1 -> 0, < lod2 -> 1,
 *       < lod3 -> 2, senao 3; com >= count carros os limites caem 1. Global.xml: 2/4/6/25 (exe: 4/8/12/25).
 *   CarLODTransDist    (id 0xc, getter 0x6faf60) {near, far, veryFar, farthest} (m): 0x6e4ad0 da o LOD pela
 *       distancia (0..4) e devolve max(LOD da distancia, classe). Global.xml: 15/25/50/75.
 *   CarFocusLODMinMax  (id 0xa, getter 0x8aa8b0) {min, max}: 0x873807 limita o LOD final do carro focado
 *       (fora do cockpit). Global.xml: min 1 -> na corrida o carro do jogador nunca usa o LOD0 (so no cockpit,
 *       modo foto, pre/pos-corrida, que usam min 0). Numeracao interna: -1 = LOD S, 0 = LOD0 ... 5.
 *   CarNonFocusLODMinMax (id 0xb, getter 0x8aaa20) idem para os outros carros (Global.xml: 0/5).
 *   Carro sem foco da camera: +1 no LOD da distancia (0x8739fa); o LOD0 dos adversarios e descarregado na corrida
 *   (0x1ab8f0 -> UnloadLOD0 0x873750 tira o bit do LOD0 da mascara [modelo+0xa0]) - isso NAO e alterado.
 * Niveis: low = ranking/distancias /2 e x0.6; high = ranking x2, distancias x2, focado com min 0 (LOD0 fora do
 *   cockpit); ultra = todos os carros na classe 0, distancias x4, min 0 e sem o +1 dos carros sem foco.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

extern void wlog(const char *fmt, ...);
extern int bytes_match(const uint8_t *p, const char *hex, size_t len);
extern int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp);

#define FM5_GET_CAR_LOD_THRESHOLDS 0x859710
#define FM5_GET_CAR_FOCUS_LOD      0x8aa8b0
#define FM5_GET_CAR_NONFOCUS_LOD   0x8aaa20
#define FM5_GET_CAR_LOD_TRANSDIST  0x6faf60
/* 0x8739f1: cmp byte [car+0x31c],bpl ; jne +8 ; inc bl -> carro sem foco da camera (SetCameraFocusedOnCar,
   impl 0x82f300) usa o LOD da distancia + 1. Ultra: inc bl vira nop. */
#define FM5_CAR_UNFOCUSED_INC      0x8739f1
/* push rdi; push r14; push r15; sub rsp,0x40; mov qword [rsp+0x20],-2 (igual nos 4 getters) */
#define GETTER_PROLOGUE "4057415641574883ec4048c7442420feffffff"
#define GETTER_STOLEN   19

typedef void *(*getter_fn)(void *mgr, void *out, int idx);
static getter_fn orig_thresholds, orig_focus, orig_nonfocus, orig_transdist;
static int car_level;

static const char *level_name(int l)
{
    static const char *n[] = { "low", "default", "high", "ultra" };
    return l >= 0 && l < 4 ? n[l] : "?";
}

/* loga cada combinacao nova de valores originais (os cenarios trocam pouco: menu, corrida, foto...) */
static void log_change(const char *what, int32_t *last, const int32_t *orig, const int32_t *now, int n, int fl)
{
    int i, same = 1;
    for (i = 0; i < n; i++) same &= last[i] == orig[i];
    if (same) return;
    for (i = 0; i < n; i++) last[i] = orig[i];
    if (fl) {
        const float *o = (const float *)orig, *v = (const float *)now;
        wlog("FM5: car_detail: %s %g/%g/%g/%g -> %g/%g/%g/%g", what, o[0], o[1], o[2], o[3], v[0], v[1], v[2], v[3]);
    } else if (n == 4)
        wlog("FM5: car_detail: %s %d/%d/%d/%d -> %d/%d/%d/%d", what, orig[0], orig[1], orig[2], orig[3],
             now[0], now[1], now[2], now[3]);
    else
        wlog("FM5: car_detail: %s %d/%d -> %d/%d", what, orig[0], orig[1], now[0], now[1]);
}

static void *get_thresholds(void *mgr, int32_t *out, int idx)
{
    static int32_t last[4] = { -99 };
    void *r = orig_thresholds(mgr, out, idx);
    int32_t o[4] = { out[0], out[1], out[2], out[3] }, i;
    for (i = 0; i < 3; i++)
        out[i] = car_level == 0 ? o[i] / 2 : car_level == 2 ? o[i] * 2 : 64;
    if (car_level == 3 && out[3] < 99) out[3] = 99;   /* nunca cair um nivel pela quantidade de carros */
    log_change("CarLODThresholds", last, o, out, 4, 0);
    return r;
}

static void *get_transdist(void *mgr, float *out, int idx)
{
    static int32_t last[4] = { -99 };
    static const float scale[] = { 0.6f, 1.0f, 2.0f, 4.0f };
    void *r = orig_transdist(mgr, out, idx);
    float o[4] = { out[0], out[1], out[2], out[3] };
    int i;
    for (i = 0; i < 4; i++) out[i] = o[i] * scale[car_level];
    log_change("CarLODTransDist", last, (const int32_t *)o, (const int32_t *)out, 4, 1);
    return r;
}

/* high/ultra: o carro focado pode usar o LOD0 fora do cockpit, como no modo foto */
static void *get_focus(void *mgr, int32_t *out, int idx)
{
    static int32_t last[2] = { -99 };
    void *r = orig_focus(mgr, out, idx);
    int32_t o[2] = { out[0], out[1] };
    if (out[0] > 0) out[0] = 0;
    log_change("CarFocusLODMinMax", last, o, out, 2, 0);
    return r;
}

static void *get_nonfocus(void *mgr, int32_t *out, int idx)
{
    static int32_t last[2] = { -99 };
    void *r = orig_nonfocus(mgr, out, idx);
    int32_t o[2] = { out[0], out[1] };
    if (out[0] > 0) out[0] = 0;
    log_change("CarNonFocusLODMinMax", last, o, out, 2, 0);
    return r;
}

void fm5_car_detail(uint8_t *game, int level)
{
    static const uint32_t getters[] = { FM5_GET_CAR_LOD_THRESHOLDS, FM5_GET_CAR_LOD_TRANSDIST,
                                        FM5_GET_CAR_FOCUS_LOD, FM5_GET_CAR_NONFOCUS_LOD };
    size_t i, n;
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM5: car_detail %d (%s): LODs originais", level, level_name(level));
        return;
    }
    n = level >= 2 ? 4 : 2;   /* low so reduz ranking/distancias; min/max ficam como no jogo */
    for (i = 0; i < n; i++)
        if (!bytes_match(game + getters[i], GETTER_PROLOGUE, GETTER_STOLEN)) {
            wlog("FM5: car_detail: getter %#x nao confere, ignorado", getters[i]);
            return;
        }
    car_level = level;
    hook_at(game + FM5_GET_CAR_LOD_THRESHOLDS, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_thresholds,
            (void **)&orig_thresholds);
    hook_at(game + FM5_GET_CAR_LOD_TRANSDIST, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_transdist,
            (void **)&orig_transdist);
    if (level >= 2) {
        hook_at(game + FM5_GET_CAR_FOCUS_LOD, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_focus,
                (void **)&orig_focus);
        hook_at(game + FM5_GET_CAR_NONFOCUS_LOD, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_nonfocus,
                (void **)&orig_nonfocus);
    }
    if (level == 3) {
        uint8_t *p = game + FM5_CAR_UNFOCUSED_INC;
        DWORD old;
        if (bytes_match(p, "4138ad1c0300007508fec3", 11)) {
            VirtualProtect(p + 9, 2, PAGE_EXECUTE_READWRITE, &old);
            p[9] = 0x90; p[10] = 0x90;
            VirtualProtect(p + 9, 2, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p + 9, 2);
        } else wlog("FM5: car_detail: site %#x (+1 sem foco) nao confere, ignorado", FM5_CAR_UNFOCUSED_INC);
    }
    wlog("FM5: car_detail %d (%s): getters de LOD dos carros com hook", level, level_name(level));
}

/*
 * shadows (PatchOptions+0x14 [0]): sombras dinamicas (carros/collidables) do ShadowMapGenerator11.
 *   ShadowMapResolution (id 0x2e, getter 0x8433c0 (mgr, idx) -> int): lado dos shadow maps quadrados. Os 5
 *       chamadores (0x833655, 0x833725, 0x8337d5, 0x833ed1, 0x835c13) comparam com o cache [x+0xf4] e chamam
 *       ShadowMapGenerator11 (global [game+0x25e7990], vtable 0x22c3db0) slot 0x40 = 0x7235f0 SetResolution(w,h),
 *       que recria tudo (0x721f10: D32/R16G16 res x res, ESRAM a partir de 24 MB enquanto couber em 32 MB, senao
 *       memoria normal). Cenarios: Global/Race/Replay 512, FrontEnd 1024, foto/pre/pos-corrida/autovista 2048.
 *   MaxHiDefShadows (id 0x2f, getter 0x735550 (mgr, idx) -> int): orcamento de objetos com sombra "hi-def" nas
 *       cascatas 1..3 (0x7244c0; a cascata 0 sempre tem). Global 16; 8/12/16-Car.xml 0.
 *   As cascatas sao ajustadas aos limites dos objetos (0x720df0), nao ha distancias fixas como no FH2.
 * Niveis: low = resolucao /2; high = x2 (max 4096) e hi-def >= 16; ultra = x4 (max 4096) e hi-def >= 64.
 */
#define FM5_GET_SHADOW_MAP_RES    0x8433c0
#define FM5_GET_MAX_HIDEF_SHADOWS 0x735550

typedef int (*getter_int_fn)(void *mgr, int idx);
static getter_int_fn orig_shadow_res, orig_hidef;
static int shadow_level;

static int get_shadow_res(void *mgr, int idx)
{
    static int last = -99;
    int o = orig_shadow_res(mgr, idx), v = o;
    if (shadow_level == 0) v = o / 2 < 256 ? 256 : o / 2;
    else if (o < 4096) { v = o * (shadow_level == 2 ? 2 : 4); if (v > 4096) v = 4096; }
    if (o != last) { last = o; wlog("FM5: shadows: ShadowMapResolution %d -> %d", o, v); }
    return v;
}

static int get_hidef(void *mgr, int idx)
{
    static int last = -99;
    int o = orig_hidef(mgr, idx), min = shadow_level == 2 ? 16 : 64, v = o < min ? min : o;
    if (o != last) { last = o; wlog("FM5: shadows: MaxHiDefShadows %d -> %d", o, v); }
    return v;
}

void fm5_shadows(uint8_t *game, int level)
{
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM5: shadows %d (%s): sombras originais", level, level_name(level));
        return;
    }
    if (!bytes_match(game + FM5_GET_SHADOW_MAP_RES, GETTER_PROLOGUE, GETTER_STOLEN) ||
        (level >= 2 && !bytes_match(game + FM5_GET_MAX_HIDEF_SHADOWS, GETTER_PROLOGUE, GETTER_STOLEN))) {
        wlog("FM5: shadows: getters nao conferem, ignorado");
        return;
    }
    shadow_level = level;
    hook_at(game + FM5_GET_SHADOW_MAP_RES, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_shadow_res,
            (void **)&orig_shadow_res);
    if (level >= 2)
        hook_at(game + FM5_GET_MAX_HIDEF_SHADOWS, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_hidef,
                (void **)&orig_hidef);
    wlog("FM5: shadows %d (%s): getters de sombra com hook", level, level_name(level));
}

/*
 * reflections (PatchOptions+0x14 [1]): cubemap dinamico dos carros, espelhos e reflexo do para-brisa.
 *   Cubemap (CubemapRenderer11, ctor 0x6d68e0): cubo 512 R11G11B10 com mips (0x6d6150/0x6d6220), tabela de
 *       angulo solido 512 R32F com log2(512)+1 mips (0x6d6b77..0x6d6bb4, preenchida em 0x6d75b0 com 512 >> mip) para
 *       o filtro difuso (cubo 32); cubos por sonda (0x6d7180), temporarios (0x6d7c70) e o do carro (0x86fc91 ->
 *       [+0x1550], criado em 0x872610). O filtro/downsample (0x6de6c0, 0x6dd7a0) le o tamanho dos descritores ->
 *       todos os 512 trocados juntos. Imediatos fixos, nenhum controle dos XMLs.
 *   EnvMapFrequencyScale (id 0x27, getter 0x6faad0 -> float): 0x6d78a0 = faces do cubo atualizadas por frame =
 *       clamp(N * escala, 1, 6) (N = 6 ou [obj+0x2b0]); 0 = cubo estatico (front end, pre/pos-corrida).
 *   Espelhos (MirrorRenderer11 0x73ad50): 768x256 fixo (5 texturas + viewport {768,256} dos 4 espelhos).
 *   HalfRateMirror (id 0x29, getter 0x14e720 -> bool): 8/12/16-Car.xml = 1 (espelho a 30 fps).
 *   WindshieldReflectionResolution (id 0x31, getter 0x735910 -> int, 1 chamador 0x7323ae): altura (512);
 *       largura = altura * 16 / 9 (WindshieldReflectionRenderer11 0x732340).
 * Niveis: low = cubo 256, espelho 384x128, para-brisa /2; high = cubo 1024, espelho 1536x512, para-brisa x2,
 *   espelho sempre a 60, frequencia x2; ultra = cubo 2048, espelho 2304x768, para-brisa x4 (max 2048), espelho a
 *   60, cubo inteiro (6 faces) a cada frame.
 */
#define FM5_GET_ENVMAP_FREQ       0x6faad0
#define FM5_GET_HALF_RATE_MIRROR  0x14e720
#define FM5_GET_WINDSHIELD_RES    0x735910
#define ENVMAP_PROLOGUE "488bc4565741564883ec5048c740b8feffffff"

typedef struct { uint32_t rva; uint8_t off; const char *orig; uint8_t len; uint32_t base; } ImmSite;

/* imediatos de 32 bits: rva da instrucao, offset do imediato, bytes originais, valor base (escalado) */
static const ImmSite cube_sites[] = {
    { 0x6d6977, 1, "ba00020000", 5, 512 },               /* ctor: cubo [+0x1f8] */
    { 0x6d6a0c, 1, "ba00020000", 5, 512 },               /* ctor: cubo temporario */
    { 0x6d6b77, 7, "41c7863004000000020000", 11, 512 },  /* ctor: [+0x430] = tamanho */
    { 0x6d6b97, 1, "b800010000", 5, 256 },               /* contagem de mips (laco a partir de tamanho/2) */
    { 0x6d6ba8, 4, "c744245800020000", 8, 512 },         /* tabela de angulo solido: largura */
    { 0x6d6bb0, 4, "c744245c00020000", 8, 512 },         /*                          altura  */
    { 0x6d7660, 1, "bf00020000", 5, 512 },               /* 0x6d75b0: tamanho do mip 0 da tabela */
    { 0x6d7285, 1, "ba00020000", 5, 512 },               /* cubos por sonda (0x6d7180) */
    { 0x6d7cad, 1, "ba00020000", 5, 512 },               /* cubos temporarios (0x6d7c70) */
    { 0x86fc91, 6, "c7875015000000020000", 10, 512 },    /* cubo do carro [+0x1550] */
};
static const ImmSite mirror_sites[] = {
    { 0x73adc3, 1, "ba00030000", 5, 768 }, { 0x73adcc, 2, "41b800010000", 6, 256 },
    { 0x73ae0b, 1, "ba00030000", 5, 768 }, { 0x73ae14, 2, "41b800010000", 6, 256 },
    { 0x73ae76, 1, "ba00030000", 5, 768 }, { 0x73ae7f, 2, "41b800010000", 6, 256 },
    { 0x73aec4, 1, "ba00030000", 5, 768 }, { 0x73aecf, 2, "41b800010000", 6, 256 },
    { 0x73af57, 1, "ba00030000", 5, 768 }, { 0x73af51, 2, "41b800010000", 6, 256 },
    { 0x73afa4, 2, "c70600030000", 6, 768 },             /* viewport dos 4 espelhos: largura */
    { 0x73afaa, 4, "48c7460400010000", 8, 256 },         /*                          altura  */
};

static int patch_imm_sites(uint8_t *game, const ImmSite *s, size_t n, uint32_t num, uint32_t den, const char *what)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!bytes_match(game + s[i].rva, s[i].orig, s[i].len)) {
            wlog("FM5: reflections: %s: site %#x nao confere, ignorado", what, s[i].rva);
            return 0;
        }
    for (i = 0; i < n; i++) {
        uint8_t *p = game + s[i].rva + s[i].off;
        uint32_t v = s[i].base * num / den;
        DWORD old;
        VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old);
        memcpy(p, &v, 4);
        VirtualProtect(p, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 4);
    }
    return 1;
}

typedef float (*getter_float_fn)(void *mgr, int idx);
typedef uint8_t (*getter_bool_fn)(void *mgr, int idx);
static getter_float_fn orig_envmap;
static getter_bool_fn orig_half_rate;
static getter_int_fn orig_windshield;
static int refl_level;

static float get_envmap(void *mgr, int idx)
{
    static float last = -99.0f;
    float o = orig_envmap(mgr, idx), v = o > 0.0f ? o * (refl_level == 2 ? 2.0f : 6.0f) : o;
    if (o != last) { last = o; wlog("FM5: reflections: EnvMapFrequencyScale %g -> %g", o, v); }
    return v;
}

static uint8_t get_half_rate(void *mgr, int idx)
{
    static int last = -99;
    uint8_t o = orig_half_rate(mgr, idx);
    if (o != last) { last = o; wlog("FM5: reflections: HalfRateMirror %d -> 0", o); }
    return 0;
}

static int get_windshield(void *mgr, int idx)
{
    static int last = -99;
    int o = orig_windshield(mgr, idx), v = o;
    if (refl_level == 0) v = o / 2 < 128 ? 128 : o / 2;
    else if (o < 2048) { v = o * (refl_level == 2 ? 2 : 4); if (v > 2048) v = 2048; }
    if (o != last) { last = o; wlog("FM5: reflections: WindshieldReflectionResolution %d -> %d", o, v); }
    return v;
}

void fm5_reflections(uint8_t *game, int level)
{
    static const uint32_t cube[] = { 256, 512, 1024, 2048 };
    static const uint32_t mirror_num[] = { 1, 1, 2, 3 }, mirror_den[] = { 2, 1, 1, 1 };
    if (level == 1 || level < 0 || level > 3) {
        wlog("FM5: reflections %d (%s): reflexos originais", level, level_name(level));
        return;
    }
    refl_level = level;
    if (patch_imm_sites(game, cube_sites, sizeof cube_sites / sizeof *cube_sites, cube[level], 512, "cubemap"))
        wlog("FM5: reflections: cubemap 512 -> %u", cube[level]);
    if (patch_imm_sites(game, mirror_sites, sizeof mirror_sites / sizeof *mirror_sites, mirror_num[level],
                        mirror_den[level], "espelhos"))
        wlog("FM5: reflections: espelhos 768x256 -> %ux%u", 768 * mirror_num[level] / mirror_den[level],
             256 * mirror_num[level] / mirror_den[level]);
    if (bytes_match(game + FM5_GET_WINDSHIELD_RES, GETTER_PROLOGUE, GETTER_STOLEN))
        hook_at(game + FM5_GET_WINDSHIELD_RES, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_windshield,
                (void **)&orig_windshield);
    else wlog("FM5: reflections: getter WindshieldReflectionResolution nao confere, ignorado");
    if (level >= 2) {
        if (bytes_match(game + FM5_GET_HALF_RATE_MIRROR, GETTER_PROLOGUE, GETTER_STOLEN))
            hook_at(game + FM5_GET_HALF_RATE_MIRROR, GETTER_PROLOGUE, GETTER_STOLEN, (void *)get_half_rate,
                    (void **)&orig_half_rate);
        else wlog("FM5: reflections: getter HalfRateMirror nao confere, ignorado");
        if (bytes_match(game + FM5_GET_ENVMAP_FREQ, ENVMAP_PROLOGUE, GETTER_STOLEN))
            hook_at(game + FM5_GET_ENVMAP_FREQ, ENVMAP_PROLOGUE, GETTER_STOLEN, (void *)get_envmap,
                    (void **)&orig_envmap);
        else wlog("FM5: reflections: getter EnvMapFrequencyScale nao confere, ignorado");
    }
    wlog("FM5: reflections %d (%s): aplicado", level, level_name(level));
}

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
        wlog("FM6: car_detail %d (%s): LODs originais", level, level_name(level));
        return;
    }
    for (i = 0; i < 7; i++) {
        uint32_t rva = i < 3 ? thr[i] : i < 6 ? dist[i - 3] : FM6_GET_CAR_FOCUS_LOD;
        if (!bytes_match(game + rva, FM6_GETTER_PROLOGUE, FM6_GETTER_STOLEN)) {
            wlog("FM6: car_detail: getter %#x nao confere, ignorado", rva);
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
    wlog("FM6: car_detail %d (%s): getters de LOD dos carros com hook", level, level_name(level));
}
