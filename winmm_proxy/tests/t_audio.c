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
    CHECK(log_has("48000 Hz 32 bits block 8 float"));
    CHECK_EQ(ac_get_service(NULL, &iid_render_client, &out), 0);
    REQUIRE(out == &fake_rc);
    CHECK(fake_rc_vt[RC_GET_BUFFER] == (void *)rc_get_buffer);
    CHECK(fake_rc_vt[RC_RELEASE_BUFFER] == (void *)rc_release_buffer);
    CHECK(log_has("GetBuffer/ReleaseBuffer counted"));
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

TEST(audio_fm6_game_hooks_on_real_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    log_capture();
    audio_fm6_game_hooks(game);
    CHECK(log_has("sound init / ACP hooks installed"));
    /* jmp absoluto (ff 25 00000000 + destino) nos 3 sites */
    CHECK_HEX(game + 0x79e0b0, "ff2500000000");
    CHECK(*(void **)(game + 0x79e0b6) == (void *)fm6_sound_init);
    CHECK(*(void **)(game + 0x64d6b6) == (void *)fm6_sound_sub);
    CHECK(*(void **)(game + 0x6553b6) == (void *)fm6_acp_setup);
    /* trampolins com os bytes originais */
    CHECK_HEX(orig_sound_init, "4489442418895424105556574154ff25");
    CHECK_HEX(orig_acp_setup, "40534881ecc0000000488bd933d2ff25");
}

/* mensagem ACP 0x10 do Azimuth: {0x10, 0, tipo do comando, tag u64 em +0xc, contador u32 em +0x14} */
static uint8_t fake_msg[0x18];
static uint64_t fake_pop(void *hal, uint8_t *msg) { (void)hal; memcpy(msg, fake_msg, sizeof fake_msg); return 1; }

static void make_completion(uint32_t type, uint32_t cmd, uint64_t tag, uint32_t counter)
{
    memset(fake_msg, 0, sizeof fake_msg);
    memcpy(fake_msg, &type, 4);
    memcpy(fake_msg + 8, &cmd, 4);
    memcpy(fake_msg + 0xc, &tag, 8);
    memcpy(fake_msg + 0x14, &counter, 4);
}

TEST(audio_fm6_acp_completion_tag_moved_to_fm6_offset)
{
    uint8_t m[0x18];
    uint64_t tag;
    fake_azimuth(2);
    log_capture();
    orig_acp_pop = fake_pop;
    make_completion(0x10, 7, 0x0000000500000003ull, 1);
    CHECK_EQ(acp_pop(NULL, m), 1);
    memcpy(&tag, m + 0x10, 8);
    CHECK(tag == 0x0000000500000003ull);                  /* FM6 le o tag em +0x10 */
    CHECK_HEX(m, "10000000000000000700000003000000");    /* tipo/comando/inicio do tag intactos */
    CHECK(log_has("ACP message 0x10 #"));
    /* outros tipos: intactos */
    make_completion(2, 0, 0x1234, 9);
    acp_pop(NULL, m);
    CHECK(!memcmp(m, fake_msg, sizeof m));
    /* fora do FM6: layout do Azimuth (FH2/FM5) */
    fake_azimuth(1);
    make_completion(0x10, 7, 3, 1);
    acp_pop(NULL, m);
    CHECK(!memcmp(m, fake_msg, sizeof m));
}

TEST(audio_acp_pop_hook_on_real_azimuth)
{
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL);
    if (!az) { test_skip(AZIMUTH_EXE " ausente"); return; }
    g_base = az;
    log_capture();
    REQUIRE(hook_at(az + RVA_ACP_POP_MESSAGE, "48895c24104889742418574883ec20", 15, (void *)acp_pop,
                    (void **)&orig_acp_pop));
    CHECK_HEX(az + 0xb16d0, "ff2500000000");
    CHECK(*(void **)(az + 0xb16d6) == (void *)acp_pop);
    CHECK_HEX(orig_acp_pop, "48895c24104889742418574883ec20ff25");
    pe_unmap(az);
}

/* AudioDump: arquivo lido de volta inteiro (build\audio_fm6_*) */
static uint8_t *read_dump(const char *name, DWORD *size)
{
    static uint8_t buf[8192];
    char path[MAX_PATH];
    FILE *f;
    snprintf(path, sizeof path, "%s\\winmm_proxy\\build\\%s", t_root(), name);
    f = fopen(path, "rb");
    if (!f) return NULL;
    *size = (DWORD)fread(buf, 1, sizeof buf, f);
    fclose(f);
    return buf;
}

static void dump_to_build(int secs)
{
    MultiByteToWideChar(CP_ACP, 0, t_root(), -1, dump_dir, MAX_PATH);
    wcscat(dump_dir, L"\\winmm_proxy\\build\\");
    dump_secs = secs;
    dump_out_done = dump_shape_done = 0;
    dump_seq = 0;
    dump_armed = 1;                                /* F9 */
}

TEST(audio_dump_wav_output_truncated_to_seconds)
{
    float in[6] = { 0.5f, -0.5f, 0.25f, -0.25f, 1.0f, -1.0f };
    uint8_t *w;
    DWORD n = 0;
    fake_azimuth(2);
    log_capture();
    dump_to_build(1);
    fmt_block = 8; fmt_bits = 32; fmt_float = 1; fmt_channels = 2; fmt_rate = 4;   /* 1 s = 4 quadros */
    dump_output((const BYTE *)in, 3, 0);
    dump_output((const BYTE *)in, 3, 2);           /* SILENT: zeros, cortado no limite (1 quadro) */
    CHECK(dump_out_done && !dump_out.f);
    dump_output((const BYTE *)in, 3, 0);           /* depois do limite: nada */
    w = read_dump("audio_fm6_out.wav", &n);
    REQUIRE(w);
    CHECK_EQ(n, 44 + 32);
    CHECK_HEX(w, "52494646" "44000000" "57415645666d7420" "10000000" "0300" "0200" "04000000" "20000000" "0800"
                 "2000" "64617461" "20000000");
    CHECK(!memcmp(w + 44, in, 24));
    CHECK_HEX(w + 68, "0000000000000000");
    CHECK(log_has("audio_fm6_out.wav done"));
    /* sem F9: nada gravado */
    dump_to_build(1);
    dump_armed = 0;
    dump_output((const BYTE *)in, 3, 0);
    CHECK(!dump_out.f && !dump_out_done);
}

TEST(audio_dump_shape_graph_src_and_mix)
{
    static uint8_t proc[0x5300], src[4 * 0x20], xma[4 * 0x40], mixes[2 * 0x200];
    uint64_t entries[3] = { 1, 2 | 3ull << 13 | 1ull << 25, 0x10 | 1ull << 14 | 5ull << 27 };
    uint8_t graph[0x58] = { 0 }, *f, *r;
    uint8_t *az = fake_azimuth(2);
    DWORD n = 0;
    float *m = (float *)(mixes + 0x200);
    int i;
    log_capture();
    dump_to_build(1);
    *(uint32_t *)(az + 0x333790) = 4; *(uint8_t **)(az + 0x3337a8) = src;
    *(uint32_t *)(az + 0x3337a0) = 4; *(uint8_t **)(az + 0x3337c8) = xma;
    *(uint8_t **)(proc + 0x1220) = mixes;
    *(uint16_t *)(proc + 0x1238 + 1 * 2) = 2;                  /* mix id 1 -> bloco 1 */
    *(uint64_t *)(proc + 0x1e8 + 3 * 8) = 0x77;               /* stamp do reset do contexto 3 */
    for (i = 0; i < 128; i++) m[i] = (float)i;
    *(uint64_t **)(graph + 0x48) = entries;
    *(uint64_t **)(graph + 0x50) = entries + 3;
    *(uint32_t *)(graph + 0x40) = 0x42;
    *(uint64_t *)(graph + 0x30) = 0x99;
    memset(src + 3 * 0x20, 0xa1, 0x20);
    memset(xma + 3 * 0x40, 0xb1, 0x40);
    audio_dump_graph_pre(graph);
    memset(xma + 3 * 0x40, 0xb2, 0x40);                        /* o interpretador anda o contexto */
    audio_dump_graph_post(proc, graph, 0);                     /* nao consumido: nada */
    CHECK(!dump_shape.f);
    audio_dump_graph_post(proc, graph, 1);
    REQUIRE(dump_shape.f);
    dump_close(&dump_shape, 0);
    f = read_dump("audio_fm6_shape.bin", &n);
    REQUIRE(f);
    CHECK_EQ(n, 8 + (8 + 24 + 24) + (8 + 208) + (8 + 16 + 512));
    CHECK_HEX(f, "415a534801000000");
    r = f + 8;                                                 /* grafo: seq 0, id 0x42, stamp 0x99, 3 entradas */
    CHECK_HEX(r, "01000000" "30000000" "00000000" "42000000" "9900000000000000" "03000000" "00000000"
                 "0100000000000000");
    r += 8 + 48;                                               /* SRC: ctx 3, reset 0x77, antes/depois */
    CHECK_HEX(r, "02000000" "d0000000" "00000000" "03000000" "7700000000000000" "a1a1a1a1");
    CHECK_EQ(r[24 + 0x20], 0xb1);
    CHECK_EQ(r[24 + 0x60], 0xa1);
    CHECK_EQ(r[24 + 0x80], 0xb2);
    r += 8 + 208;                                              /* mix 1 -> DMA 5 */
    CHECK_HEX(r, "03000000" "10020000" "00000000" "01000000" "05000000" "00000000");
    CHECK(!memcmp(r + 24, m, 0x200));
    dump_secs = 0;
}
