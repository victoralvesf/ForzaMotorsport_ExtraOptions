/*
 * Testes de fm5/gfx.c, fm6/gfx.c e fm6/font.c (apostrofos nas fontes dos paineis): valores que os getters com hook devolvem em cada nivel do launcher (PASSO 9).
 * Os sites/getters no exe real sao conferidos em t_patch.c (fm5_patches_*).
 */
#include "../fm5/gfx.c"
#include "../fm6/gfx.c"
#include "../fm6/font.c"
#include "test.h"

static int32_t fake_i4[4];
static float fake_f4[4];
static int fake_int;
static float fake_float;
static void *fake_get_i4(void *mgr, void *out, int idx) { (void)mgr; (void)idx; memcpy(out, fake_i4, 16); return out; }
static void *fake_get_f4(void *mgr, void *out, int idx) { (void)mgr; (void)idx; memcpy(out, fake_f4, 16); return out; }
static int fake_get_int(void *mgr, int idx) { (void)mgr; (void)idx; return fake_int; }
static float fake_get_float(void *mgr, int idx) { (void)mgr; (void)idx; return fake_float; }
static uint8_t fake_get_bool(void *mgr, int idx) { (void)mgr; (void)idx; return 1; }

TEST(car_lod_thresholds_per_level)
{
    int32_t out[4];
    static const int32_t xml[4] = { 2, 4, 6, 25 };   /* Global.xml */
    orig_thresholds = fake_get_i4;
    memcpy(fake_i4, xml, 16);
    car_level = 0; get_thresholds(NULL, out, 0);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 25);
    car_level = 2; get_thresholds(NULL, out, 0);
    CHECK(out[0] == 4 && out[1] == 8 && out[2] == 12 && out[3] == 25);
    car_level = 3; get_thresholds(NULL, out, 0);
    CHECK(out[0] == 64 && out[1] == 64 && out[2] == 64 && out[3] == 99);   /* todos na classe 0 */
}

TEST(car_lod_distances_and_focus)
{
    float out[4];
    int32_t mm[2];
    static const float xml[4] = { 15, 25, 50, 75 };
    orig_transdist = (getter_fn)fake_get_f4;
    memcpy(fake_f4, xml, 16);
    car_level = 0; get_transdist(NULL, out, 0);
    CHECK(out[0] == 15 * 0.6f && out[3] == 75 * 0.6f);
    car_level = 3; get_transdist(NULL, out, 0);
    CHECK(out[0] == 60 && out[3] == 300);
    orig_focus = orig_nonfocus = fake_get_i4;
    fake_i4[0] = 1; fake_i4[1] = 5;
    get_focus(NULL, mm, 0);
    CHECK(mm[0] == 0 && mm[1] == 5);              /* LOD0 fora do cockpit */
    fake_i4[0] = -1;
    get_nonfocus(NULL, mm, 0);
    CHECK(mm[0] == -1);                           /* LOD S (-1) fica */
}

TEST(shadow_getters_per_level)
{
    orig_shadow_res = fake_get_int; orig_hidef = fake_get_int;
    shadow_level = 0; fake_int = 512;  CHECK_EQ(get_shadow_res(NULL, 0), 256);
    fake_int = 256;  CHECK_EQ(get_shadow_res(NULL, 0), 256);           /* minimo 256 */
    shadow_level = 2; fake_int = 512;  CHECK_EQ(get_shadow_res(NULL, 0), 1024);
    fake_int = 4096; CHECK_EQ(get_shadow_res(NULL, 0), 4096);
    shadow_level = 3; fake_int = 2048; CHECK_EQ(get_shadow_res(NULL, 0), 4096);   /* maximo 4096 */
    shadow_level = 2; fake_int = 0;  CHECK_EQ(get_hidef(NULL, 0), 16);
    shadow_level = 3; fake_int = 16; CHECK_EQ(get_hidef(NULL, 0), 64);
    fake_int = 100; CHECK_EQ(get_hidef(NULL, 0), 100);
}

TEST(reflection_getters_per_level)
{
    orig_windshield = fake_get_int; orig_envmap = fake_get_float; orig_half_rate = fake_get_bool;
    refl_level = 0; fake_int = 512;  CHECK_EQ(get_windshield(NULL, 0), 256);
    fake_int = 200;  CHECK_EQ(get_windshield(NULL, 0), 128);
    refl_level = 3; fake_int = 512;  CHECK_EQ(get_windshield(NULL, 0), 2048);
    fake_int = 1024; CHECK_EQ(get_windshield(NULL, 0), 2048);
    refl_level = 2; fake_float = 0.5f; CHECK(get_envmap(NULL, 0) == 1.0f);
    refl_level = 3; CHECK(get_envmap(NULL, 0) == 3.0f);
    fake_float = 0.0f; CHECK(get_envmap(NULL, 0) == 0.0f);              /* cubo estatico continua estatico */
    CHECK_EQ(get_half_rate(NULL, 0), 0);
}

TEST(level_names_and_defaults)
{
    uint8_t dummy[16] = { 0 };
    CHECK_STR(gfx_level_name(0), "low"); CHECK_STR(gfx_level_name(3), "ultra"); CHECK_STR(gfx_level_name(7), "?");
    /* default (1) e fora da faixa: nada e lido nem escrito */
    log_capture();
    fm5_car_detail(dummy, 1); fm5_shadows(dummy, 1); fm5_reflections(dummy, -1);
    CHECK(log_has("original LODs") && log_has("original shadows") && log_has("original reflections"));
}

/* ------------------------------------------------------------------------ */
/* exe real do FM5: sites e getters de cada nivel                            */
/* ------------------------------------------------------------------------ */
static int hooked_to(const uint8_t *p, const void *fn)
{
    return p[0] == 0xff && p[1] == 0x25 && !memcmp(p + 2, "\0\0\0\0", 4) && *(void *const *)(p + 6) == fn;
}

static uint32_t imm32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void check_imm_sites(const uint8_t *g, uint32_t num, uint32_t den)
{
    size_t i;
    for (i = 0; i < sizeof cube_sites / sizeof *cube_sites; i++)
        if (imm32(g + cube_sites[i].rva + cube_sites[i].off) != cube_sites[i].base * num / den)
            test_fail(__FILE__, __LINE__, "cubemap %#x = %u", cube_sites[i].rva, imm32(g + cube_sites[i].rva + cube_sites[i].off));
}

TEST(gfx_sites_match_fm5_exe)
{
    uint8_t *g = pe_map(FM5_EXE, NULL);
    size_t i;
    if (!g) { test_skip(FM5_EXE " ausente"); return; }
    /* todos os sites conferem (tambem os que so os niveis high/ultra usam) */
    for (i = 0; i < sizeof cube_sites / sizeof *cube_sites; i++) CHECK_HEX(g + cube_sites[i].rva, cube_sites[i].orig);
    for (i = 0; i < sizeof mirror_sites / sizeof *mirror_sites; i++) CHECK_HEX(g + mirror_sites[i].rva, mirror_sites[i].orig);
    for (i = 0; i < sizeof cube_sites / sizeof *cube_sites; i++)
        CHECK_EQ(imm32(g + cube_sites[i].rva + cube_sites[i].off), cube_sites[i].base);   /* offset do imediato */
    for (i = 0; i < sizeof mirror_sites / sizeof *mirror_sites; i++)
        CHECK_EQ(imm32(g + mirror_sites[i].rva + mirror_sites[i].off), mirror_sites[i].base);
    CHECK_HEX(g + FM5_GET_CAR_LOD_THRESHOLDS, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_CAR_LOD_TRANSDIST, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_CAR_FOCUS_LOD, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_CAR_NONFOCUS_LOD, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_SHADOW_MAP_RES, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_MAX_HIDEF_SHADOWS, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_WINDSHIELD_RES, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_HALF_RATE_MIRROR, GETTER_PROLOGUE);
    CHECK_HEX(g + FM5_GET_ENVMAP_FREQ, ENVMAP_PROLOGUE);
    CHECK_HEX(g + FM5_CAR_UNFOCUSED_INC, "4138ad1c0300007508fec3");
    pe_unmap(g);
}

TEST(gfx_levels_on_fm5_exe)
{
    static const uint32_t cube[] = { 256, 512, 1024, 2048 }, mnum[] = { 1, 1, 2, 3 }, mden[] = { 2, 1, 1, 1 };
    int level;
    for (level = 0; level < 4; level++) {
        uint8_t *g = pe_map(FM5_EXE, NULL);
        if (!g) { test_skip(FM5_EXE " ausente"); return; }
        log_capture();
        fm5_car_detail(g, level); fm5_shadows(g, level); fm5_reflections(g, level);
        CHECK(!log_has("does not match") && !log_has("do not match"));
        if (level == 1) {
            CHECK_HEX(g + FM5_GET_CAR_LOD_THRESHOLDS, GETTER_PROLOGUE);
            CHECK_HEX(g + FM5_GET_SHADOW_MAP_RES, GETTER_PROLOGUE);
            CHECK_HEX(g + FM5_GET_WINDSHIELD_RES, GETTER_PROLOGUE);
        } else {
            CHECK(hooked_to(g + FM5_GET_CAR_LOD_THRESHOLDS, (void *)get_thresholds));
            CHECK(hooked_to(g + FM5_GET_CAR_LOD_TRANSDIST, (void *)get_transdist));
            CHECK(hooked_to(g + FM5_GET_SHADOW_MAP_RES, (void *)get_shadow_res));
            CHECK(hooked_to(g + FM5_GET_WINDSHIELD_RES, (void *)get_windshield));
            CHECK_EQ(level >= 2, hooked_to(g + FM5_GET_CAR_FOCUS_LOD, (void *)get_focus));
            CHECK_EQ(level >= 2, hooked_to(g + FM5_GET_CAR_NONFOCUS_LOD, (void *)get_nonfocus));
            CHECK_EQ(level >= 2, hooked_to(g + FM5_GET_MAX_HIDEF_SHADOWS, (void *)get_hidef));
            CHECK_EQ(level >= 2, hooked_to(g + FM5_GET_HALF_RATE_MIRROR, (void *)get_half_rate));
            CHECK_EQ(level >= 2, hooked_to(g + FM5_GET_ENVMAP_FREQ, (void *)get_envmap));
            CHECK_EQ(g[FM5_CAR_UNFOCUSED_INC + 9] == 0x90, level == 3);
            /* o trampolim do getter executa o prologo original e volta logo depois dele */
            CHECK_HEX((uint8_t *)orig_shadow_res, GETTER_PROLOGUE);
            CHECK(*(uint8_t **)((uint8_t *)orig_shadow_res + GETTER_STOLEN + 6) == g + FM5_GET_SHADOW_MAP_RES + GETTER_STOLEN);
        }
        check_imm_sites(g, cube[level], 512);
        CHECK_EQ(imm32(g + 0x73adc3 + 1), 768 * mnum[level] / mden[level]);
        CHECK_EQ(imm32(g + 0x73afaa + 4), 256 * mnum[level] / mden[level]);   /* viewport dos espelhos: altura */
        pe_unmap(g);
    }
}

/* ------------------------------------------------------------------------ */
/* FM6: car_detail (faixas por classe, 5 distancias, variantes noite/chuva)  */
/* ------------------------------------------------------------------------ */
static int32_t fake_i6[6];
static float fake_f5[5];
static void *fake_get_i6(void *mgr, void *out, int idx) { (void)mgr; (void)idx; memcpy(out, fake_i6, 24); return out; }
static void *fake_get_f5(void *mgr, void *out, int idx) { (void)mgr; (void)idx; memcpy(out, fake_f5, 20); return out; }

TEST(fm6_car_lod_thresholds_per_level)
{
    int32_t out[6];
    static const int32_t xml[6] = { 0, 3, 4, 4, 7, 25 };   /* Global.xml */
    fm6_orig_thr[0] = fm6_orig_thr[1] = fm6_orig_thr[2] = fake_get_i6;
    memcpy(fake_i6, xml, 24);
    fm6_car_level = 0; fm6_get_thr0(NULL, out, 0);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 3 && out[3] == 4 && out[4] == 11 && out[5] == 25);
    fm6_car_level = 2; fm6_get_thr1(NULL, out, 0);
    CHECK(out[0] == 3 && out[1] == 4 && out[2] == 4 && out[3] == 7 && out[4] == 14 && out[5] == 25);
    fm6_car_level = 3; fm6_get_thr2(NULL, out, 0);
    CHECK(out[0] == 64 && out[1] == 3 && out[2] == 4 && out[3] == 4 && out[4] == 7 && out[5] == 99);
}

TEST(fm6_car_lod_distances_and_focus)
{
    float out[5];
    int32_t mm[2];
    static const float xml[5] = { 15, 35, 60, 100, 300 };
    fm6_orig_dist[0] = fm6_orig_dist[1] = fm6_orig_dist[2] = (getter_fn)fake_get_f5;
    memcpy(fake_f5, xml, 20);
    fm6_car_level = 0; fm6_get_dist0(NULL, out, 0);
    CHECK(out[0] == 15 * 0.6f && out[4] == 300 * 0.6f);
    fm6_car_level = 3; fm6_get_dist2(NULL, out, 0);
    CHECK(out[0] == 60 && out[3] == 400 && out[4] == 1200);
    fm6_orig_focus = fake_get_i4;
    fake_i4[0] = 1; fake_i4[1] = 6;
    fm6_get_focus(NULL, mm, 0);
    CHECK(mm[0] == 0 && mm[1] == 0);              /* carro focado sempre no LOD0 */
    fake_i4[0] = -1; fake_i4[1] = -1;
    fm6_get_focus(NULL, mm, 0);
    CHECK(mm[0] == -1 && mm[1] == -1);            /* LOD S (-1) fica */
}

TEST(fm6_car_detail_on_fm6_exe)
{
    static const uint32_t getters[] = { 0x43fe54, 0xe48e24, 0x20d01c, 0x18ca6c, 0xdb802c, 0x18cbac, 0x445d2c };
    int level;
    size_t i;
    for (level = 0; level < 4; level++) {
        uint8_t *g = pe_map(FM6_EXE, NULL);
        if (!g) { test_skip(FM6_EXE " ausente"); return; }
        for (i = 0; i < 7; i++) CHECK_HEX(g + getters[i], "40574883ec4048c7442420feffffff48895c2458");
        log_capture();
        fm6_car_detail(g, level);
        CHECK(!log_has("does not match") && !log_has("do not match"));
        if (level == 1) {
            CHECK(log_has("FM6: car_detail 1 (default): original LODs"));
            CHECK_HEX(g + 0x43fe54, "40574883ec40");
        } else {
            CHECK(hooked_to(g + 0x43fe54, (void *)fm6_get_thr0));
            CHECK(hooked_to(g + 0xe48e24, (void *)fm6_get_thr1));
            CHECK(hooked_to(g + 0x20d01c, (void *)fm6_get_thr2));
            CHECK(hooked_to(g + 0x18ca6c, (void *)fm6_get_dist0));
            CHECK(hooked_to(g + 0xdb802c, (void *)fm6_get_dist1));
            CHECK(hooked_to(g + 0x18cbac, (void *)fm6_get_dist2));
            CHECK_EQ(level >= 2, hooked_to(g + 0x445d2c, (void *)fm6_get_focus));
            CHECK_HEX((uint8_t *)fm6_orig_thr[0], "40574883ec4048c7442420feffffff48895c2458");
            CHECK(*(uint8_t **)((uint8_t *)fm6_orig_thr[0] + 20 + 6) == g + 0x43fe54 + 20);
        }
        pe_unmap(g);
    }
}

/* ------------------------------------------------------------------------ */
/* FM6: shadows (ShadowMapResolution / MaxHiDefShadows)                      */
/* ------------------------------------------------------------------------ */
TEST(fm6_shadow_values_per_level)
{
    fm6_orig_shadow_res = fake_get_int;
    fm6_orig_hidef = fake_get_int;
    fm6_shadow_level = 0;
    fake_int = 512;  CHECK_EQ(fm6_get_shadow_res(NULL, 0), 256);
    fake_int = 1024; CHECK_EQ(fm6_get_shadow_res(NULL, 0), 512);
    fake_int = 64;   CHECK_EQ(fm6_get_shadow_res(NULL, 0), 64);     /* Hidden fica */
    fm6_shadow_level = 2;
    fake_int = 512;  CHECK_EQ(fm6_get_shadow_res(NULL, 0), 1024);
    fake_int = 2048; CHECK_EQ(fm6_get_shadow_res(NULL, 0), 4096);
    fake_int = 64;   CHECK_EQ(fm6_get_shadow_res(NULL, 0), 64);
    fake_int = 0;    CHECK_EQ(fm6_get_hidef(NULL, 0), 16);
    fake_int = 20;   CHECK_EQ(fm6_get_hidef(NULL, 0), 20);
    fm6_shadow_level = 3;
    fake_int = 512;  CHECK_EQ(fm6_get_shadow_res(NULL, 0), 2048);
    fake_int = 1024; CHECK_EQ(fm6_get_shadow_res(NULL, 0), 4096);
    fake_int = 2048; CHECK_EQ(fm6_get_shadow_res(NULL, 0), 4096);
    fake_int = 4096; CHECK_EQ(fm6_get_shadow_res(NULL, 0), 4096);
    fake_int = 16;   CHECK_EQ(fm6_get_hidef(NULL, 0), 64);
}

TEST(fm6_shadows_on_fm6_exe)
{
    int level;
    for (level = 0; level < 4; level++) {
        uint8_t *g = pe_map(FM6_EXE, NULL);
        if (!g) { test_skip(FM6_EXE " ausente"); return; }
        CHECK_HEX(g + 0x4410e4, "40574883ec4048c7442420feffffff48895c2458");
        CHECK_HEX(g + 0x4582d8, "40574883ec4048c7442420feffffff48895c2458");
        log_capture();
        fm6_shadows(g, level);
        CHECK(!log_has("do not match"));
        if (level == 1) {
            CHECK(log_has("FM6: shadows 1 (default): original shadows"));
            CHECK_HEX(g + 0x4410e4, "40574883ec40");
        } else {
            CHECK(hooked_to(g + 0x4410e4, (void *)fm6_get_shadow_res));
            CHECK_EQ(level >= 2, hooked_to(g + 0x4582d8, (void *)fm6_get_hidef));
            CHECK_HEX((uint8_t *)fm6_orig_shadow_res, "40574883ec4048c7442420feffffff48895c2458");
            CHECK(*(uint8_t **)((uint8_t *)fm6_orig_shadow_res + 20 + 6) == g + 0x4410e4 + 20);
        }
        pe_unmap(g);
    }
}

/* ------------------------------------------------------------------------ */
/* FM6: reflections (cubemap, espelhos, para-brisa)                          */
/* ------------------------------------------------------------------------ */
TEST(fm6_reflection_getters_per_level)
{
    fm6_orig_windshield = fake_get_int; fm6_orig_envmap = fake_get_float; fm6_orig_half_rate = fake_get_bool;
    fm6_refl_level = 0; fake_int = 512;  CHECK_EQ(fm6_get_windshield(NULL, 0), 256);
    fake_int = 200;  CHECK_EQ(fm6_get_windshield(NULL, 0), 128);
    fm6_refl_level = 2; fake_int = 512;  CHECK_EQ(fm6_get_windshield(NULL, 0), 1024);
    fm6_refl_level = 3; fake_int = 512;  CHECK_EQ(fm6_get_windshield(NULL, 0), 2048);
    fake_int = 1024; CHECK_EQ(fm6_get_windshield(NULL, 0), 2048);
    fm6_refl_level = 2; fake_float = 0.5f; CHECK(fm6_get_envmap(NULL, 0) == 1.0f);   /* PhotoMode */
    fm6_refl_level = 3; fake_float = 1.0f; CHECK(fm6_get_envmap(NULL, 0) == 6.0f);   /* Global */
    fake_float = 0.0f; CHECK(fm6_get_envmap(NULL, 0) == 0.0f);                       /* cubo estatico */
    CHECK_EQ(fm6_get_half_rate(NULL, 0), 0);
}

TEST(fm6_reflections_on_fm6_exe)
{
    static const uint32_t cube[] = { 256, 512, 1024, 2048 }, mw[] = { 384, 768, 1536, 2304 }, mh[] = { 128, 256, 512, 768 };
    int level;
    for (level = 0; level < 4; level++) {
        uint8_t *g = pe_map(FM6_EXE, NULL);
        if (!g) { test_skip(FM6_EXE " ausente"); return; }
        /* bytes originais do exe */
        CHECK_HEX(g + 0x36278b, "41bf00020000");
        CHECK_HEX(g + 0x36283c, "b800010000");
        CHECK_HEX(g + 0x362849, "b800020000");
        CHECK_HEX(g + 0x362ac3, "bb00020000");
        CHECK_HEX(g + 0x98f9aa, "ba00020000");
        CHECK_HEX(g + 0x32dc0f, "c7873016000000020000");
        CHECK_HEX(g + 0x3362ea, "be00030000");
        CHECK_HEX(g + 0x3363ed, "c70600030000");
        CHECK_HEX(g + 0x3363f3, "48c7460400010000");
        CHECK_HEX(g + 0x3362e3, "418d5e01448bc3");
        CHECK_HEX(g + 0x7491, "c3cccccccccccccccccccccccccc");
        CHECK_HEX(g + 0x4fcef0, "40574883ec4048c7442420feffffff48895c2458");
        CHECK_HEX(g + 0x3fdf60, "40574883ec4048c7442420feffffff48895c2458");
        CHECK_HEX(g + 0x3e64ac, "40574883ec5048c7442420feffffff48895c2468");
        log_capture();
        fm6_reflections(g, level);
        CHECK(!log_has("does not match"));
        if (level == 1) {
            CHECK(log_has("FM6: reflections 1 (default): original reflections"));
            CHECK_HEX(g + 0x36278b, "41bf00020000");
            CHECK_HEX(g + 0x3362e3, "418d5e01448bc3");
            CHECK_HEX(g + 0x4fcef0, "40574883ec40");
        } else {
            CHECK_EQ(imm32(g + 0x36278b + 2), cube[level]);
            CHECK_EQ(imm32(g + 0x36283c + 1), cube[level] / 2);    /* mips: laco a partir de tamanho/2 */
            CHECK_EQ(imm32(g + 0x362849 + 1), cube[level]);
            CHECK_EQ(imm32(g + 0x362ac3 + 1), cube[level]);
            CHECK_EQ(imm32(g + 0x98f9aa + 1), cube[level]);
            CHECK_EQ(imm32(g + 0x32dc0f + 6), cube[level]);
            CHECK_EQ(imm32(g + 0x3362ea + 1), mw[level]);
            CHECK_EQ(imm32(g + 0x3363ed + 2), mw[level]);
            CHECK_EQ(imm32(g + 0x3363f3 + 4), mh[level]);
            /* altura: call rel32 para 0x7492 + nop de 2 bytes; trecho = mov ebx, h ; mov r8d, ebx ; ret */
            CHECK(g[0x3362e3] == 0xe8 && (int32_t)imm32(g + 0x3362e4) == 0x7492 - (0x3362e3 + 5));
            CHECK(g[0x3362e8] == 0x66 && g[0x3362e9] == 0x90 && g[0x3362ea] == 0xbe);
            CHECK(g[0x7492] == 0xbb && imm32(g + 0x7493) == mh[level]);
            CHECK_HEX(g + 0x7497, "448bc3c3cc");
            CHECK(hooked_to(g + 0x4fcef0, (void *)fm6_get_windshield));
            CHECK_EQ(level >= 2, hooked_to(g + 0x3fdf60, (void *)fm6_get_half_rate));
            CHECK_EQ(level >= 2, hooked_to(g + 0x3e64ac, (void *)fm6_get_envmap));
            CHECK_HEX((uint8_t *)fm6_orig_windshield, "40574883ec4048c7442420feffffff48895c2458");
            CHECK(*(uint8_t **)((uint8_t *)fm6_orig_windshield + 20 + 6) == g + 0x4fcef0 + 20);
        }
        pe_unmap(g);
    }
}

/* ---- fm6/font.c: apostrofos do tempo (pt-BR) nas fontes dos paineis sem o glifo ---- */
static const char *fake_font_cps;       /* codigos que a fonte falsa tem */
static int fake_font_find(void *map, uint32_t cp)
{
    const char *p;
    (void)map;
    for (p = fake_font_cps; *p; p++) if ((uint8_t)*p == cp) return (int)(p - fake_font_cps);
    return -1;
}

static struct { void *rcx, *rdx; uint64_t r8, r9, a5; } font_seen;
static void fake_font_orig(void *rcx, void *rdx, uint64_t r8, uint64_t r9, uint64_t a5)
{
    font_seen.rcx = rcx; font_seen.rdx = rdx; font_seen.r8 = r8; font_seen.r9 = r9; font_seen.a5 = a5;
}

TEST(fm6_dash_font_apostrophes)
{
    uint8_t font[0x170] = { 0 };
    int32_t values[64] = { 0 };
    wchar_t s[16];
    *(int32_t **)(font + 0x160) = values;
    font_find = fake_font_find;
    /* fonte do painel: sem o apostrofo -> formato do ingles no lugar, o resto ate n vira NUL */
    fake_font_cps = " \",-./0123456789:DNPR";
    wcscpy(s, L"00'12,977''");
    fm6_font_measure_fix(font, (uint16_t *)s, 11);
    CHECK(!wcscmp(s, L"00:12,977") && s[9] == 0 && s[10] == 0 && s[11] == 0);
    wcscpy(s, L"1h05'02,1''");
    fm6_font_measure_fix(font, (uint16_t *)s, 11);
    CHECK(!wcscmp(s, L"1h05:02,1"));
    wcscpy(s, L"-00'01,250''");
    fm6_font_measure_fix(font, (uint16_t *)s, 12);
    CHECK(!wcscmp(s, L"-00:01,250"));
    wcscpy(s, L"00:12,977");                          /* sem apostrofo: nada */
    fm6_font_measure_fix(font, (uint16_t *)s, 9);
    CHECK(!wcscmp(s, L"00:12,977"));
    wcscpy(s, L"00'12''xx");                          /* so ate n */
    fm6_font_measure_fix(font, (uint16_t *)s, 5);
    CHECK(!wcscmp(s, L"00:12''xx"));
    memcpy(s, L"0'1''\0'9", 16);                      /* so ate o 1o NUL */
    fm6_font_measure_fix(font, (uint16_t *)s, 8);
    CHECK(!wcscmp(s, L"0:1") && s[3] == 0 && s[4] == 0 && s[6] == '\'' && s[7] == '9');
    /* fonte com o apostrofo (HUD/Planer): nada muda */
    fake_font_cps = " '\",-./0123456789:";
    wcscpy(s, L"00'12,977''");
    fm6_font_measure_fix(font, (uint16_t *)s, 11);
    CHECK(!wcscmp(s, L"00'12,977''"));
    values[1] = -1;                                  /* "'" na tabela mas sem glifo */
    fm6_font_measure_fix(font, (uint16_t *)s, 11);
    CHECK(!wcscmp(s, L"00:12,977"));
    values[1] = 0;
    /* fonte sem ':' para trocar: nada muda */
    fake_font_cps = " ,.0123456789";
    wcscpy(s, L"00'12,977''");
    fm6_font_measure_fix(font, (uint16_t *)s, 11);
    CHECK(!wcscmp(s, L"00'12,977''"));
    /* texto em memoria so de leitura: nada muda (sem queda) */
    {
        uint16_t *ro = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD old;
        fake_font_cps = " \",-./0123456789:";
        memcpy(ro, L"0'1''", 12);
        VirtualProtect(ro, 4096, PAGE_READONLY, &old);
        fm6_font_measure_fix(font, ro, 5);
        CHECK(ro[1] == '\'' && ro[3] == '\'');
        VirtualFree(ro, 0, MEM_RELEASE);
    }
    /* stub: corrige no lugar e repassa rcx/rdx/r8/r9 e o 5o argumento (pilha) ao trampolim */
    {
        typedef void (*stub_fn)(void *, void *, uint64_t, uint64_t, uint64_t);
        void *saved = fm6_font_measure_tramp;
        stub_fn stub = (stub_fn)(uintptr_t)fm6_font_measure_stub;
        fake_font_cps = " \",-./0123456789:";
        wcscpy(s, L"00'12,977''");
        fm6_font_measure_tramp = (void *)fake_font_orig;
        stub(font, s, 11, 0x1234, 0x5678);
        CHECK(!wcscmp(s, L"00:12,977"));
        CHECK(font_seen.rcx == font && font_seen.rdx == s && font_seen.r8 == 11 && font_seen.r9 == 0x1234);
        CHECK(font_seen.a5 == 0x5678);
        fm6_font_measure_tramp = saved;
    }
    /* no exe real: hook com trampolim */
    {
        uint8_t *g = pe_map(FM6_EXE, NULL);
        if (!g) { test_skip(FM6_EXE " ausente"); return; }
        log_capture();
        fm6_dash_font_fix(g);
        CHECK(!log_has("does not match") && !log_has("not installed"));
        CHECK(log_has("FM6: dash fonts: text of fonts without the apostrophe as in English"));
        CHECK(hooked_to(g + 0x10bf78, (void *)fm6_font_measure_stub));
        CHECK_HEX(g + 0x2b7ef0, "488bc44c8948204c89401848894808");   /* malha: sem hook */
        CHECK_HEX((uint8_t *)fm6_font_measure_tramp, "488bc4488958084889681048897018" "ff2500000000");
        CHECK(*(uint8_t **)((uint8_t *)fm6_font_measure_tramp + 15 + 6) == g + 0x10bf78 + 15);
        CHECK(font_find == (void *)(g + 0x10c270));
        log_capture();                               /* apply_patches de novo: nada (ja instalado) */
        fm6_dash_font_fix(g);
        CHECK(!log_has("do not match") && !log_has("not installed") && !log_has("dash fonts"));
        pe_unmap(g);
    }
}
