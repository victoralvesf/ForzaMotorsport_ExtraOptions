/*
 * Testes de livery_fm6.c: diagnostico da plotagem do FM6 (debug.ini LiveryLog; 16_opcoes_graficas.txt PASSO 12).
 */
#include "../livery_fm6.c"
#include "test.h"

static int hooked_to(const uint8_t *p, const void *fn)
{
    return p[0] == 0xff && p[1] == 0x25 && !memcmp(p + 2, "\0\0\0\0", 4) && *(void *const *)(p + 6) == fn;
}

static void *fake_pso;
static uint64_t fake_sections(void *mgr, uint64_t lowq, uint64_t a3, uint64_t a4)
{
    static const UINT msaa[12] = { 2048, 1024, 1, 1, 28, 8, 0, 0, 0x20, 0, 0, 0 };
    (void)mgr; (void)lowq; (void)a3; (void)a4;
    CHECK(liv_in_window());
    liv_note_pso(&fake_pso, 0, 0);
    liv_note_pso(NULL, 0, 0);
    liv_note_skip(0);
    liv_note_texture(msaa, 0);
    liv_note_resolve(0, 2048, 1024, 28, 8, 2048, 1024, 28, 1, 28);
    liv_note_dispatch(64, 64, 1);
    return 7;
}

static const char *rt_name;
static uint64_t rt_samples;
static uint64_t fake_create_rt(void *out, uint64_t w, uint64_t h, uint64_t fmt, const char *name, uint64_t samples,
                               uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10)
{
    (void)out; (void)w; (void)h; (void)fmt; (void)a7; (void)a8; (void)a9;
    rt_name = name; rt_samples = samples;
    return a10;
}

TEST(livery_window_counts_and_logs)
{
    liv_log = 1;
    orig_sections = fake_sections;
    log_capture();
    CHECK(!liv_in_window());
    CHECK_EQ(hook_sections((void *)0x1234, 0, 0, 0), 7);
    CHECK(!liv_in_window());
    CHECK(log_has("livery: null pipeline on c0 (wait=0)"));
    CHECK(log_has("livery: draw skipped on c0"));
    CHECK(log_has("livery: CreateTexture2D 2048x1024 mips 1 fmt 28 samples 8"));
    CHECK(log_has("livery: ResolveSubresource c0 2048x1024 fmt 28 samples 8 -> 2048x1024 fmt 28 samples 1"));
    CHECK(log_has("full quality) end in"));
    CHECK(log_has("pipelines 2 (null 1), draws skipped 1, resolves 1, dispatches 1, textures 1"));
    /* fora da janela nada conta; so as BC6H/BC7 grandes vao para o log */
    log_capture();
    liv_note_pso(NULL, 0, 0);
    liv_note_skip(0);
    {
        static const UINT bc7[12] = { 2048, 2048, 12, 1, 98, 1, 0, 0, 8, 0, 0, 0 };
        static const UINT small[12] = { 256, 256, 1, 1, 98, 1, 0, 0, 8, 0, 0, 0 };
        liv_note_texture(small, 0);
        CHECK(!log_has("livery:"));
        liv_note_texture(bc7, 0);
        CHECK(log_has("livery: BC texture 2048x2048 mips 12 fmt 98"));
    }
    liv_log = 0;
}

TEST(livery_render_target_names)
{
    orig_create_rt = fake_create_rt;
    log_capture();
    CHECK_EQ(hook_create_rt(NULL, 4096, 2048, 0x14, "LivMSAATarget-1-2", 8, 1, 0xff, 1, 99), 99);
    CHECK(rt_name && !strcmp(rt_name, "LivMSAATarget-1-2") && rt_samples == 8);
    CHECK(log_has("livery: render target \"LivMSAATarget-1-2\" 4096x2048 engine fmt 0x14 samples 8 quality 0"));
    log_capture();
    hook_create_rt(NULL, 1920, 1080, 0x14, "SceneColor", 1, 1, 0xff, 1, 0);
    CHECK(!log_has("livery:"));
}

TEST(livery_log_install_on_fm6_exe)
{
    uint8_t *g = pe_map(FM6_EXE, NULL);
    if (!g) { test_skip(FM6_EXE " ausente"); return; }
    CHECK_HEX(g + FM6_LIV_RENDER_SECTIONS, "488bc45556574154415541564157");
    CHECK_HEX(g + FM6_CREATE_RT, "4c8bdc534881ecc000000049c74398feffffff");
    CHECK_EQ(g[FM6_LIV_SAMPLES], 8);
    log_capture();
    fm6_livery_log_install(g);
    CHECK(!log_has("do not match"));
    CHECK(log_has("FM6: LiveryLog=1") && log_has("MSAA samples byte 8"));
    CHECK(hooked_to(g + FM6_LIV_RENDER_SECTIONS, (void *)hook_sections));
    CHECK(hooked_to(g + FM6_CREATE_RT, (void *)hook_create_rt));
    /* os trampolins executam o prologo original e voltam logo depois dele */
    CHECK_HEX((uint8_t *)orig_sections, "488bc45556574154415541564157");
    CHECK(*(uint8_t **)((uint8_t *)orig_sections + 14 + 6) == g + FM6_LIV_RENDER_SECTIONS + 14);
    CHECK(*(uint8_t **)((uint8_t *)orig_create_rt + 19 + 6) == g + FM6_CREATE_RT + 19);
    liv_log = 0;
    pe_unmap(g);
}

TEST(livery_placement_textures_logged)
{
    static const UINT big[12] = { 4096, 2048, 13, 1, 98, 1, 0, 0, 8, 0, 0, 0 };
    static const UINT small[12] = { 1024, 1024, 11, 1, 98, 1, 0, 0, 8, 0, 0, 0 };
    log_capture();
    liv_note_placement(big, 13, 4096, (void *)0x1000, 0x1234);
    CHECK(!log_has("livery:"));                   /* so com LiveryLog */
    liv_log = 1;
    liv_note_placement(small, 13, 1024, (void *)0x1000, 0x1234);
    CHECK(!log_has("livery:"));
    liv_note_placement(big, 13, 4096, (void *)0x1000, 0x1234);
    CHECK(log_has("livery: placement texture #1 4096x2048 mips 13 arr 1 fmt 98 bind 0x8 misc 0 tile 13 pitch 4096"));
    CHECK(log_has("caller game+0x1234"));
    liv_log = 0;
}
