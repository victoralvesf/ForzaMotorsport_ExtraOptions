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

/* trecho de GPU: desde o Signal ou desde a conclusao anterior da mesma fila, o que vier depois */
TEST(stutter_gpu_segment)
{
    CHECK_EQ(st_gpu_segment(100, 50, 180), 80);    /* fila ociosa: conta do Signal */
    CHECK_EQ(st_gpu_segment(100, 150, 180), 30);   /* fila ocupada: conta da conclusao anterior */
    CHECK_EQ(st_gpu_segment(100, 200, 180), 0);
}

/* recursos por tipo: so tipos com chamadas, a mais lenta com descricao */
TEST(stutter_resource_kinds_format)
{
    D3D12_RESOURCE_DESC d;
    char buf[1024];
    memset(&d, 0, sizeof d);
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = 2048; d.Height = 1024; d.DepthOrArraySize = 1; d.MipLevels = 12;
    d.Format = DXGI_FORMAT_BC1_UNORM;
    rk_reset();
    buf[0] = 0;
    CHECK_EQ(rk_format(buf, sizeof buf), 0);
    rk_note(RK_PLACED, st_freq.QuadPart / 1000, &d, 0, 0);          /* 1 ms */
    rk_note(RK_PLACED, st_freq.QuadPart / 100, &d, 0, 0);           /* 10 ms: a maior */
    rk_note(RK_HEAP, st_freq.QuadPart / 500, NULL, 4 << 20, 0x4);
    rk_format(buf, sizeof buf);
    CHECK(strstr(buf, " | placed 2 11.0 ms (max 10.0 ms tex2d 2048x1024x1 m12 f71 fl0 8192 KB tid") != NULL);
    CHECK(strstr(buf, ", heap 1 2.0 ms (max 2.0 ms 4096 KB fl0x4 tid") != NULL);
    CHECK(strstr(buf, "committed") == NULL);
    rk_reset();
}

/* Release das vtables de recurso: repassa e devolve o valor da original; vtable ja trocada nao entra de novo */
static ULONG STDMETHODCALLTYPE fake_release(IUnknown *u) { (void)u; return 41; }
TEST(stutter_release_hook)
{
    static void *vt[3] = { NULL, NULL, (void *)fake_release };
    struct { void **lpVtbl; } obj = { vt };
    LONG n0 = rk_nvt;
    rk_hook_release(&obj);
    CHECK(vt[2] == (void *)rk_release && rk_nvt == n0 + 1);
    rk_hook_release(&obj);
    CHECK_EQ(rk_nvt, n0 + 1);
    CHECK_EQ(((IUnknown *)&obj)->lpVtbl->Release((IUnknown *)&obj), 41);
}

/* DeferRelease: a ultima referencia do reaper vai para a fila (objeto vivo); as outras passam direto */
typedef struct { void **lpVtbl; LONG refs; } fake_obj;
static ULONG STDMETHODCALLTYPE fo_addref(IUnknown *u) { return (ULONG)InterlockedIncrement(&((fake_obj *)u)->refs); }
static ULONG STDMETHODCALLTYPE fo_release(IUnknown *u) { return (ULONG)InterlockedDecrement(&((fake_obj *)u)->refs); }
TEST(stutter_defer_release)
{
    static void *vt[3] = { NULL, (void *)fo_addref, (void *)fo_release };
    fake_obj o = { vt, 2 };
    LONG d0 = dr_deferred;
    rk_hook_release(&o);
    dr_on = 1;
    dr_is_reaper = 1;
    CHECK_EQ(((IUnknown *)&o)->lpVtbl->Release((IUnknown *)&o), 1);   /* nao era a ultima: solta direto */
    CHECK_EQ(o.refs, 1);
    CHECK_EQ(((IUnknown *)&o)->lpVtbl->Release((IUnknown *)&o), 0);   /* ultima: adiada */
    CHECK_EQ(o.refs, 1);
    CHECK_EQ(dr_deferred, d0 + 1);
    CHECK(dr_q[(dr_head - 1) % DR_QUEUE] == (IUnknown *)&o);
    rk_call_orig((IUnknown *)&o);                                     /* o que a thread de drenagem faz */
    CHECK_EQ(o.refs, 0);
    dr_tail = dr_head;
    dr_is_reaper = 0;
    CHECK_EQ(((IUnknown *)&o)->lpVtbl->Release((IUnknown *)&o), (ULONG)-1);   /* fora do reaper: direto */
    dr_on = 0;
    dr_is_reaper = -1;
}

/* AllocatorMaxAgeMs: troca o imediato de 4000 ms so com os bytes originais; 0 nao mexe; -1 = 0x7fffffff */
TEST(stutter_cmdalloc_age_patch)
{
    static const uint8_t orig[6] = { 0x48, 0x3d, 0xa0, 0x0f, 0x00, 0x00 };
    uint8_t *az = fake_azimuth(0);
    log_capture();
    CHECK(!cmdalloc_age_patch(60000));                         /* bytes zerados: nao confere */
    CHECK(log_has("idade dos command allocators (azimuth+0xdeadd) nao confere"));
    memcpy(az + 0xdeadd, orig, sizeof orig);
    CHECK(!cmdalloc_age_patch(0));
    CHECK(!memcmp(az + 0xdeadd, orig, sizeof orig));
    CHECK(cmdalloc_age_patch(60000));
    CHECK(!memcmp(az + 0xdeadd, "\x48\x3d\x60\xea\x00\x00", 6));
    memcpy(az + 0xdeadd, orig, sizeof orig);
    CHECK(cmdalloc_age_patch(-1));
    CHECK(!memcmp(az + 0xdeadd, "\x48\x3d\xff\xff\xff\x7f", 6));
}

/* perfil das UpdateTileMappings (PASSO 68): contagens, recursos distintos, chamadores */
TEST(stutter_tile_profile)
{
    D3D12_TILE_REGION_SIZE sz = { 4, FALSE, 0, 0, 0 };
    D3D12_TILE_RANGE_FLAGS rf[2] = { D3D12_TILE_RANGE_FLAG_NONE, D3D12_TILE_RANGE_FLAG_NULL };
    UINT rc[2] = { 3, 5 };
    ID3D12Heap *heap = (ID3D12Heap *)(uintptr_t)0x1000;
    void *r1 = (void *)(uintptr_t)0x10000, *r2 = (void *)(uintptr_t)0x20000;
    void *c1 = g_base + 0xe3d00, *c2 = g_base + 0xe3e00;
    char buf[512];
    tl_reset();
    CHECK_EQ(tl_format(buf, sizeof buf), 0);
    tl_note(r1, 1, &sz, heap, 0, NULL, NULL, D3D12_TILE_MAPPING_FLAG_NONE, c1);
    tl_note(r1, 1, &sz, NULL, 2, rf, rc, D3D12_TILE_MAPPING_FLAG_NONE, c1);
    tl_note(r2, 2, NULL, heap, 0, NULL, NULL, D3D12_TILE_MAPPING_FLAG_NO_HAZARD, c2);
    tl_note(r2, 1, &sz, NULL, 0, NULL, NULL, D3D12_TILE_MAPPING_FLAG_NONE, c2);
    tl_format(buf, sizeof buf);
    CHECK(strstr(buf, "stutter: tiles 10 s: 4 chamadas, 5 regioes, 18 tiles, 2 recursos distintos, 1 com flags, 2 sem heap, "
                      "1 faixas NULL; chamadores: azimuth+0xe3d00 x2 azimuth+0xe3e00 x2") != NULL);
    tl_reset();
}

/* NO_HAZARD (PASSO 69): so com os bytes originais; jmp para o stub que poe 1 nas flags com heap e volta aos 2 caminhos */
TEST(stutter_tile_nohazard_patch)
{
    static const uint8_t site[9] = { 0x89, 0x6c, 0x24, 0x50, 0x48, 0x85, 0xc9, 0x74, 0x23 };
    static const uint8_t map[4] = { 0x48, 0x8b, 0x43, 0x58 };
    static const uint8_t stub_head[0x17] = { 0x89, 0x6c, 0x24, 0x50, 0x48, 0x85, 0xc9, 0x74, 0x16,
                                             0xc7, 0x44, 0x24, 0x50, 0x01, 0x00, 0x00, 0x00,
                                             0xff, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint8_t *az = fake_azimuth(0), *stub;
    log_capture();
    CHECK(!tile_nohazard_patch(1));                                    /* bytes zerados: nao confere */
    CHECK(log_has("UpdateTileMappings do Azimuth (azimuth+0xe017d) nao confere"));
    memcpy(az + 0xe017d, site, sizeof site);
    memcpy(az + 0xe0186, map, sizeof map);
    CHECK(!tile_nohazard_patch(0));
    CHECK(!memcmp(az + 0xe017d, site, sizeof site));
    CHECK(tile_nohazard_patch(1));
    CHECK_HEX(az + 0xe017d + 5, "90909090");
    CHECK(az[0xe017d] == 0xe9);
    stub = az + 0xe017d + 5 + *(int32_t *)(az + 0xe017d + 1);
    CHECK(!memcmp(stub, stub_head, sizeof stub_head));
    CHECK(*(uint8_t **)(stub + 0x17) == az + 0xe0186);                  /* mapeamento: segue no Azimuth */
    CHECK_HEX(stub + 0x1f, "ff2500000000");
    CHECK(*(uint8_t **)(stub + 0x25) == az + 0xe01a9);                  /* desmapeamento: flags 0 */
    CHECK(stub + 9 + 0x16 == stub + 0x1f);                              /* je cai no 2o jmp */
    memset(az + 0xe017d, 0, sizeof site);
}

/* DeferRelease (PASSO 70): padrao no FM6, desligado nos outros; o debug.ini sobrescreve nos dois sentidos */
TEST(stutter_defer_release_default)
{
    dbgini_set(NULL);
    fake_azimuth(2);
    CHECK(dr_wanted());
    dbgini_set("[Debug]\nDeferRelease=0\n");
    CHECK(!dr_wanted());
    fake_azimuth(0);
    dbgini_set(NULL);
    CHECK(!dr_wanted());
    dbgini_set("[Debug]\nDeferRelease=1\n");
    CHECK(dr_wanted());
    dbgini_set(NULL);
}
