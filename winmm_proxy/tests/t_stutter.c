/*
 * Testes de stutter_fm6.c: regra de pico, media movel, percentil, maiores esperas de um intervalo, tamanho
 * aproximado de recurso e instalacao (StutterLog no debug.ini).
 */
#include "test.h"
#include "../stutter_fm6.c"

TEST(stutter_spike_rule)
{
    CHECK(!st_is_spike(40, 0));          /* sem historico */
    CHECK(!st_is_spike(24, 8));          /* abaixo de 25 ms */
    CHECK(!st_is_spike(28, 16.7));       /* abaixo de 1,8x a media */
    CHECK(st_is_spike(31, 16.7));
    CHECK(st_is_spike(90, 33.3));
}

/* limiar da parada do amostrador: 30 ms ou 1,5x a media */
TEST(stutter_stall_threshold)
{
    CHECK(st_stall_ms(0) == 30);
    CHECK(st_stall_ms(16.7) == 30);
    CHECK(st_stall_ms(33.3) > 49.9 && st_stall_ms(33.3) < 50.0);
}

/* threads bloqueadas: so as que comecam no jogo (FM6 tem prioridade sobre o FM5) */
TEST(stutter_blocked_only_game_threads)
{
    static uint8_t game[64];
    uint8_t *old5 = g_game, *old6 = g_fm6_game;
    g_game = NULL; g_fm6_game = NULL;
    CHECK(!st_in_game((uintptr_t)game));
    g_fm6_game = game;
    CHECK(st_in_game((uintptr_t)game + 0x10));
    CHECK(!st_in_game((uintptr_t)game - 1));
    CHECK(!st_in_game((uintptr_t)game + 0x8000000));
    g_game = old5; g_fm6_game = old6;
}

TEST(stutter_moving_average)
{
    static st_avg a;
    int i;
    memset(&a, 0, sizeof a);
    CHECK(st_avg_get(&a) == 0);
    for (i = 0; i < ST_AVG_N; i++) st_avg_add(&a, 16);
    CHECK(st_avg_get(&a) > 15.99 && st_avg_get(&a) < 16.01);
    for (i = 0; i < ST_AVG_N; i++) st_avg_add(&a, 32);   /* janela inteira trocada */
    CHECK(st_avg_get(&a) > 31.99 && st_avg_get(&a) < 32.01);
}

TEST(stutter_percentile)
{
    float v[100];
    int i;
    for (i = 0; i < 100; i++) v[i] = (float)(100 - i);   /* 100..1 */
    CHECK(st_percentile(v, 100, 99) == 99);
    CHECK(st_percentile(v, 100, 0) == 1);
    CHECK(st_percentile(v, 0, 99) == 0);
}

TEST(stutter_wait_top)
{
    uint32_t idx[3], n;
    void *f[1] = { 0 };
    st_wait_reset();
    st_wait_add(100, 5000, 1, 0, f, 0);
    st_wait_add(150, 9000, 2, 3, f, 0);
    st_wait_add(200, 7000, 3, 4, f, 0);
    st_wait_add(250, 20000, 4, 0, f, 0);   /* fora do intervalo */
    st_wait_add(120, 6000, 5, 5, f, 0);
    n = st_wait_top(99, 200, idx, 3);
    CHECK_EQ(n, 3);
    CHECK_EQ(waits[idx[0]].tid, 2);
    CHECK_EQ(waits[idx[1]].tid, 3);
    CHECK_EQ(waits[idx[2]].tid, 5);
    n = st_wait_top(200, 300, idx, 3);
    CHECK_EQ(n, 1);
    CHECK_EQ(waits[idx[0]].us, 20000);
    st_wait_reset();
}

TEST(stutter_resource_bytes)
{
    D3D12_RESOURCE_DESC d;
    memset(&d, 0, sizeof d);
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = 65536; d.Height = 1; d.DepthOrArraySize = 1;
    CHECK_EQ(st_res_bytes(&d), 65536);
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = 1024; d.Height = 512; d.DepthOrArraySize = 6;
    CHECK_EQ(st_res_bytes(&d), 1024ull * 512 * 6 * 4);
    CHECK_EQ(st_res_bytes(NULL), 0);
}

TEST(stutter_off_by_default)
{
    dbgini_set(NULL);
    log_capture();
    stutter_install();
    CHECK(!st_on);
    CHECK(!log_has("stutter:"));
}

/* StutterLog=1: so troca os slots da IAT que apontam para a funcao real (os outros ficam como estao) */
TEST(stutter_install_checks_iat)
{
    uint8_t *az = fake_azimuth(0);
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    void *sleep_fn = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Sleep");
    REQUIRE(dxgi && sleep_fn);
    *(void **)(az + 0x26f878) = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    *(void **)(az + 0x26f880) = (void *)GetProcAddress(dxgi, "CreateDXGIFactory2");
    *(void **)(az + 0x26f320) = sleep_fn;                     /* Sleep */
    *(void **)(az + 0x26f2b8) = (void *)0x1234;               /* WaitForSingleObject: outro alvo */
    dbgini_set("[Debug]\nStutterLog=1\n");
    log_capture();
    stutter_install();
    CHECK(st_on);
    CHECK(*(void **)(az + 0x26f878) == (void *)st_factory1 && *(void **)(az + 0x26f880) == (void *)st_factory2);
    CHECK(*(void **)(az + 0x26f320) == (void *)st_Sleep && wait_real[5] == sleep_fn);
    CHECK(*(void **)(az + 0x26f2b8) == (void *)0x1234);
    CHECK(log_has("stutter: IAT 0x26f2b8 (WaitForSingleObject) nao confere"));
    CHECK(log_has("1 de 7 esperas do azimuth.exe medidas"));
    st_on = 0;
    dbgini_set(NULL);
}

/* amostrador: histograma de RIPs (cheio = recusa chave nova) e ordem por contagem */
TEST(stutter_rip_histogram)
{
    st_bin h[3];
    uint32_t used = 0;
    CHECK(st_hist_add(h, 3, &used, 0x100));
    CHECK(st_hist_add(h, 3, &used, 0x200));
    CHECK(st_hist_add(h, 3, &used, 0x200));
    CHECK(st_hist_add(h, 3, &used, 0x300));
    CHECK(st_hist_add(h, 3, &used, 0x300));
    CHECK(st_hist_add(h, 3, &used, 0x300));
    CHECK(!st_hist_add(h, 3, &used, 0x400));   /* cheio */
    CHECK(st_hist_add(h, 3, &used, 0x100));    /* existente ainda conta */
    st_hist_sort(h, used);
    CHECK_EQ(used, 3);
    CHECK_EQ(h[0].key, 0x300); CHECK_EQ(h[0].n, 3);
    CHECK_EQ(h[1].n, 2); CHECK_EQ(h[2].n, 2);
}

/* retornos provaveis: valores da pilha dentro dos intervalos de codigo, na ordem da pilha, ate o limite */
TEST(stutter_stack_returns)
{
    const uintptr_t ranges[2][2] = { { 0x140000000ull, 0x148000000ull }, { 0x7ff600000000ull, 0x7ff601000000ull } };
    const uint64_t stack[6] = { 0x1234, 0x140123456ull, 0x148000000ull, 0x7ff600abcdefull, 0x140000010ull, 0x140000020ull };
    uintptr_t out[3] = { 0 };
    CHECK_EQ(st_scan_returns(stack, 6, ranges, 2, out, 3), 3);
    CHECK_EQ(out[0], 0x140123456ull);
    CHECK_EQ(out[1], 0x7ff600abcdefull);       /* 0x148000000 = fim exclusivo */
    CHECK_EQ(out[2], 0x140000010ull);
    CHECK_EQ(st_scan_returns(stack, 1, ranges, 2, out, 3), 0);
}

/* limitador da fila do FM6: so troca o SwitchToThread do jogo se a chamada em 0x195410 e o slot conferirem */
static BOOL WINAPI fake_switch(void) { return 7; }

TEST(stutter_queue_throttle_install)
{
    static const uint8_t call[6] = { 0xff, 0x15, 0x72, 0x5b, 0xe8, 0x00 };
    uint8_t *az = fake_azimuth(0), *old6 = g_fm6_game;
    uint8_t *game = VirtualAlloc(NULL, 0x1020000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void **slot;
    REQUIRE(game);
    slot = (void **)(game + 0x101af88);
    g_fm6_game = game;
    *slot = az + 0x1000;                                      /* dentro do azimuth.exe, mas bytes da chamada nao */
    log_capture();
    fq_install();
    CHECK(*slot == az + 0x1000);
    CHECK(log_has("stutter: limitador da fila (game+0x195410 / IAT 0x101af88) nao confere"));
    memcpy(game + 0x195410, call, sizeof call);
    *slot = (void *)0x1234;                                   /* fora do azimuth.exe */
    fq_install();
    CHECK(*slot == (void *)0x1234);
    *slot = az + 0x1000;
    log_capture();
    fq_install();
    CHECK(*slot == (void *)st_switch_thunk && fq_real == (void *)(az + 0x1000));
    CHECK(fq_ret == (uintptr_t)game + 0x195416);
    CHECK(log_has("stutter: limitador da fila do FM6 medido"));
    /* chamada que nao vem do limitador: so repassa */
    fq_real = fake_switch;
    CHECK(st_switch_c(0x10, 0x20) == 7 && fq.q == 0);
    /* chamada do limitador: so contadores (fila lida direto); o log sai do amostrador quando o episodio acaba */
    {
        static uint8_t q[0x100];
        *(uint32_t *)(q + 0xc0) = 9;
        *(uint32_t *)(q + 0xc4) = 4;
        CHECK(st_switch_c((uintptr_t)q, fq_ret) == 7);
        CHECK(st_switch_c((uintptr_t)q, fq_ret) == 7);
        CHECK(fq.q == (uintptr_t)q && fq.spins == 2 && fq.pend0 == 9 && fq.lim == 4);
        log_capture();
        fq_poll(fq.last);                                     /* episodio ainda ativo */
        CHECK(!log_has("stutter: fila"));
        fq.t0 = fq.last - st_freq.QuadPart / 10;              /* durou 100 ms */
        fq_poll(fq.last + st_freq.QuadPart);                  /* 1 s sem voltas: acabou */
        CHECK(log_has("girando (2 voltas)") && log_has("pendentes 9 (max 9) limite 4; candidatas: nenhuma"));
        log_capture();
        fq_poll(fq.last + st_freq.QuadPart * 2);              /* ja registrado */
        CHECK(!log_has("stutter: fila"));
        fq.q = 0;
    }
    g_fm6_game = old6;
    VirtualFree(game, 0, MEM_RELEASE);
}
