/*
 * Testes de audio_fm6.c: diagnostico do WASAPI do FM6 (vtable do FmodAudioClient conferido antes da troca,
 * render client do jogo com contagem de quadros/amostras).
 */
#include "../audio_fm6.c"
#include "test.h"

uint8_t *fake_azimuth(int game);
void log_capture(void);
int log_has(const char *s);

static void **fake_client_vt(uint8_t *az)
{
    void **vt = (void **)(az + RVA_FMOD_CLIENT_VT);
    /* metodos do Azimuth esperados (relocados para a base falsa), escritos aqui de novo */
    vt[3] = az + 0x82a00; vt[6] = az + 0x83200; vt[10] = az + 0x83340;
    vt[11] = az + 0x83360; vt[13] = az + 0x833a0; vt[14] = az + 0x833c0;
    return vt;
}

TEST(audio_fm6_install_checks_vtable)
{
    uint8_t *az = fake_azimuth(2);
    void **vt = fake_client_vt(az);
    vt[10] = az + 0x83341;                        /* Start diferente: nada trocado */
    log_capture();
    CHECK(!audio_fm6_install());
    CHECK(log_has("slot 10"));
    CHECK(vt[3] == az + 0x82a00);
    vt[10] = az + 0x83340;
    CHECK(audio_fm6_install());
    CHECK(vt[3] == (void *)ac_init && vt[6] == (void *)ac_padding && vt[10] == (void *)ac_start);
    CHECK(vt[11] == (void *)ac_stop && vt[13] == (void *)ac_set_event && vt[14] == (void *)ac_get_service);
    CHECK((uint8_t *)orig_init == az + 0x82a00 && (uint8_t *)orig_get_service == az + 0x833c0);
}

TEST(audio_buffer_peak_formats)
{
    float f[4] = { 0, -0.5f, 0.25f, 0 };
    int16_t s[4] = { 0, 0, -16384, 100 };
    int32_t z[2] = { 0, 0 };
    CHECK(buffer_peak((BYTE *)f, 2, 8, 32, 1) == 0.5f);
    CHECK(buffer_peak((BYTE *)s, 2, 4, 16, 0) == 0.5f);
    CHECK(buffer_peak((BYTE *)z, 1, 8, 32, 0) == 0);
}

/* render client falso: GetBuffer devolve um buffer do teste */
static float rc_buf[8];
static void *fake_rc_vt[5];
static struct { void **vt; } fake_rc = { fake_rc_vt }, other_rc = { fake_rc_vt };
static long fake_get_buffer(void *s, UINT32 n, BYTE **d) { (void)s; (void)n; *d = (BYTE *)rc_buf; return 0; }
static long fake_release_buffer(void *s, UINT32 n, DWORD fl) { (void)s; (void)n; (void)fl; return 0; }
static long fake_get_service(void *s, const GUID *iid, void **out) { (void)s; (void)iid; *out = &fake_rc; return 0; }
static long fake_init(void *s, int m, DWORD fl, int64_t d, int64_t p, const WAVEFORMATEX *f, const GUID *g)
{ (void)s; (void)m; (void)fl; (void)d; (void)p; (void)f; (void)g; return 0; }

TEST(audio_fm6_render_client_counts_game_frames)
{
    WAVEFORMATEX fmt = { 3, 2, 48000, 48000 * 8, 8, 32, 0 };   /* float estereo */
    void *out = NULL;
    BYTE *d;
    fake_azimuth(2);
    log_capture();
    memset(&st, 0, sizeof st);
    memset(game_rc, 0, sizeof game_rc);
    render_vt = NULL;
    fake_rc_vt[RC_GET_BUFFER] = (void *)fake_get_buffer;
    fake_rc_vt[RC_RELEASE_BUFFER] = (void *)fake_release_buffer;
    orig_init = fake_init;
    orig_get_service = fake_get_service;
    ac_init(NULL, 0, 0, 0, 0, &fmt, NULL);
    CHECK(fmt_block == 8 && fmt_bits == 32 && fmt_float);
    CHECK(log_has("48000 Hz 32 bits bloco 8 float"));
    CHECK_EQ(ac_get_service(NULL, &iid_render_client, &out), 0);
    REQUIRE(out == &fake_rc);
    CHECK(fake_rc_vt[RC_GET_BUFFER] == (void *)rc_get_buffer);
    CHECK(fake_rc_vt[RC_RELEASE_BUFFER] == (void *)rc_release_buffer);
    CHECK(log_has("GetBuffer/ReleaseBuffer com contagem"));
    /* 4 quadros de silencio, 4 com som, 4 com flag SILENT */
    memset(rc_buf, 0, sizeof rc_buf);
    rc_get_buffer(&fake_rc, 4, &d); rc_release_buffer(&fake_rc, 4, 0);
    rc_buf[5] = -0.75f;
    rc_get_buffer(&fake_rc, 4, &d); rc_release_buffer(&fake_rc, 4, 0);
    rc_get_buffer(&fake_rc, 4, &d); rc_release_buffer(&fake_rc, 4, 2);
    CHECK_EQ(st.frames, 12);
    CHECK_EQ(st.frames_sound, 4);
    CHECK_EQ(st.frames_silent_flag, 4);
    CHECK(st.peak == 0.75f);
    CHECK(log_has("ReleaseBuffer #3"));
    /* outro cliente com o mesmo vtable (nao devolvido ao jogo): nao conta */
    rc_get_buffer(&other_rc, 4, &d); rc_release_buffer(&other_rc, 4, 0);
    CHECK_EQ(st.release_calls, 3);
    /* fora do FM6: so repassa */
    fake_azimuth(1);
    rc_get_buffer(&fake_rc, 4, &d); rc_release_buffer(&fake_rc, 4, 0);
    CHECK_EQ(st.release_calls, 3);
}
