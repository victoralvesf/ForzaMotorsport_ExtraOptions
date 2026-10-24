/*
 * Testes de d3d_fm5.c: proxy do contexto (thunks gerados + wrappers), vtable do device, Draw* empacotados,
 * IASetIndexBuffer, topologia, SetFast*, scissor/samplers guardados, ResolveSubresource, Quality do MSAA,
 * aliasing da ESRAM e private data (incl. a protecao contra double free do bytecode, cont. 22).
 * O "Azimuth" aqui e uma tabela falsa: cada slot grava (slot, argumentos) e devolve rec_ret.
 */
#include "../d3d_fm5.c"
#include "test.h"
#include <stdlib.h>

extern const uint8_t ctx_thunk_kind[SLOTS];      /* d3d_thunks.S: 0 direto, 1 wrapper C, 2 sem equivalente */

/* ---- tabela falsa do Azimuth ---- */
typedef struct { unsigned slot; void *self; uintptr_t a[8]; void *deref3; } Call;
static Call calls[64];
static int ncalls;
static volatile unsigned rec_slot;
static uintptr_t rec_ret;
static int rec_deref3;                            /* a3 e ponteiro p/ ponteiro (Set* com 1 elemento) */

static uintptr_t rec(void *self, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
                     uintptr_t a6, uintptr_t a7, uintptr_t a8)
{
    Call *c = &calls[ncalls < 64 ? ncalls++ : 63];
    c->slot = rec_slot; c->self = self;
    c->a[0] = a1; c->a[1] = a2; c->a[2] = a3; c->a[3] = a4; c->a[4] = a5; c->a[5] = a6; c->a[6] = a7; c->a[7] = a8;
    c->deref3 = rec_deref3 && a3 ? *(void **)a3 : NULL;
    return rec_ret;
}

/* SLOTS stubs: mov rax,&rec_slot ; mov dword [rax],i ; mov rax,rec ; jmp rax */
static void **fake_vtbl(void)
{
    void **t = calloc(SLOTS, sizeof *t);
    uint8_t *code = t_rwx(SLOTS * 32);
    void *slot_addr = (void *)&rec_slot, *fn = (void *)rec;
    int i;
    for (i = 0; i < SLOTS; i++) {
        uint8_t *p = code + i * 32;
        p[0] = 0x48; p[1] = 0xb8; memcpy(p + 2, &slot_addr, 8);
        p[10] = 0xc7; p[11] = 0x00; memcpy(p + 12, &i, 4);
        p[16] = 0x48; p[17] = 0xb8; memcpy(p + 18, &fn, 8);
        p[26] = 0xff; p[27] = 0xe0;
        t[i] = p;
    }
    return t;
}

static void reset_calls(void) { ncalls = 0; rec_ret = 0; rec_deref3 = 0; memset(calls, 0, sizeof calls); }

/* contexto "real" do Azimuth (o proxy le +8..+0x28 e +0x38; topologia escreve +0x168/+0x170) */
static uint8_t *real_ctx;
static void **ctx_tbl;
static uint8_t *ctx_proxy(void)
{
    if (!real_ctx) {
        real_ctx = calloc(1, 0x1000);
        ctx_tbl = fake_vtbl();
        *(void ***)real_ctx = ctx_tbl;
        *(uint32_t *)(real_ctx + 0x38) = 1;
    }
    return proxy_of(real_ctx);
}

/* chama o slot s do FM5 pelo proxy (como o jogo: [proxy] ou a tabela embutida em +0x28) */
#define FM5_CALL(proxy, s, type) ((type)(*(void ***)(proxy))[s])

/* ------------------------------------------------------------------------ */
TEST(ctx_thunks_map_every_slot)
{
    uint8_t *px;
    int s, direct = 0;
    log_capture();
    px = ctx_proxy();
    REQUIRE(px != real_ctx);
    CHECK(*(void **)px == (void *)ctx_proxy_vtbl);
    CHECK(!memcmp(px + 0x28, ctx_proxy_vtbl, SLOTS * 8));   /* tabela embutida do FM5 */
    CHECK(*(void **)(px + CTX_REAL_OFF) == real_ctx);
    CHECK(proxy_of(real_ctx) == px);                         /* um proxy por contexto */
    for (s = 0; s < SLOTS; s++) {
        int fh2 = ctx_fh2_slot(s);
        CHECK_EQ(ctx_thunk_kind[s] == 2, ctx_thunk_kind[s] != 1 && fh2 >= SLOTS);   /* gerador == ctx_fh2_slot */
        if (ctx_thunk_kind[s] != 0) continue;
        reset_calls();
        {
            LONG before = ctx_counts[s];
            /* pela vtable e pela tabela embutida */
            FM5_CALL(px, s, uintptr_t (*)(void *, uintptr_t, uintptr_t, uintptr_t))(px, 0x11, 0x22, 0x33);
            ((uintptr_t (*)(void *, uintptr_t, uintptr_t, uintptr_t))((void **)(px + 0x28))[s])(px, 0x11, 0x22, 0x33);
            CHECK_EQ(ctx_counts[s] - before, 2);
        }
        if (ncalls != 2 || calls[0].slot != (unsigned)fh2 || calls[1].slot != (unsigned)fh2 ||
            calls[0].self != real_ctx || calls[0].a[0] != 0x11 || calls[0].a[2] != 0x33)
            test_fail(__FILE__, __LINE__, "slot FM5 %#x -> Azimuth %#x esperado, chegou %#x (n=%d)", s, fh2,
                      calls[0].slot, ncalls);
        direct++;
    }
    CHECK(direct > 200);
    /* pontos da regra (PASSO 8): 0..6 iguais, 7..0x17 +1, 0x19..0x85 iguais, X >= 0x86 +6 */
    CHECK_EQ(ctx_fh2_slot(6), 6); CHECK_EQ(ctx_fh2_slot(7), 8); CHECK_EQ(ctx_fh2_slot(0x17), 0x18);
    CHECK_EQ(ctx_fh2_slot(0x19), 0x19); CHECK_EQ(ctx_fh2_slot(0x85), 0x85); CHECK_EQ(ctx_fh2_slot(0x86), 0x8c);
}

TEST(ctx_draws_pack_arguments)
{
    uint8_t *px = ctx_proxy();
    reset_calls();
    /* DrawIndexed(IndexCount, StartIndex, Base) -> Azimuth 0xd (u64 {lo Start, hi Count}, Base) */
    FM5_CALL(px, 0x0c, void (*)(void *, UINT, UINT, INT))(px, 36, 6, -2);
    CHECK_EQ(calls[0].slot, 0x0d);
    CHECK_EQ(calls[0].a[0], 36ull << 32 | 6);
    CHECK_EQ((int)calls[0].a[1], -2);
    /* DrawIndexedInstanced(Count, Instances, Start, Base, StartInstance) -> 0x15 ({Start,Count}, {Base,SI}, Inst) */
    FM5_CALL(px, 0x14, void (*)(void *, UINT, UINT, UINT, INT, UINT))(px, 300, 4, 9, -5, 2);
    CHECK_EQ(calls[1].slot, 0x15);
    CHECK_EQ(calls[1].a[0], 300ull << 32 | 9);
    CHECK_EQ(calls[1].a[1], 2ull << 32 | 0xfffffffbull);
    CHECK_EQ((UINT)calls[1].a[2], 4);
    /* DrawInstanced(VertexCount, Instances, StartVertex, StartInstance) -> 0x16 (VC, {SV,SI}, Inst) */
    FM5_CALL(px, 0x15, void (*)(void *, UINT, UINT, UINT, UINT))(px, 3, 7, 100, 1);
    CHECK_EQ(calls[2].slot, 0x16);
    CHECK_EQ((UINT)calls[2].a[0], 3);
    CHECK_EQ(calls[2].a[1], 1ull << 32 | 100);
    CHECK_EQ((UINT)calls[2].a[2], 7);
    /* Draw(VertexCount, StartVertex) e igual nos dois XDKs (FM5 0xd -> Azimuth 0xe) */
    FM5_CALL(px, 0x0d, void (*)(void *, UINT, UINT))(px, 3, 0);
    CHECK_EQ(calls[3].slot, 0x0e);
}

TEST(ctx_index_buffer_hardware_format)
{
    uint8_t *px = ctx_proxy();
    void *ib = (void *)0xabc;
    reset_calls();
    FM5_CALL(px, 0x13, void (*)(void *, void *, UINT, UINT))(px, ib, 42 /* R32_UINT */, 8);
    FM5_CALL(px, 0x13, void (*)(void *, void *, UINT, UINT))(px, ib, 57 /* R16_UINT */, 0);
    FM5_CALL(px, 0x13, void (*)(void *, void *, UINT, UINT))(px, NULL, 0 /* UNKNOWN (desligar) */, 0);
    CHECK(calls[0].slot == 0x14 && calls[0].a[0] == 1 && calls[0].a[1] == (uintptr_t)ib && (UINT)calls[0].a[2] == 8);
    CHECK(calls[1].slot == 0x14 && calls[1].a[0] == 0);
    CHECK(calls[2].slot == 0x14 && calls[2].a[0] == 0 && calls[2].a[1] == 0);
    /* IASetFastIndexBuffer(HardwareIndexFormat, buf) -> offset 0; IASetFastVertexBuffer(slot, vb, stride) */
    FM5_CALL(px, 0x9a + 19, void (*)(void *, UINT, void *))(px, 1, ib);
    CHECK(calls[3].slot == 0x14 && calls[3].a[0] == 1 && calls[3].a[1] == (uintptr_t)ib && (UINT)calls[3].a[2] == 0);
    rec_deref3 = 1;
    FM5_CALL(px, 0x9a + 18, void (*)(void *, UINT, void *, UINT))(px, 2, ib, 16);
    CHECK(calls[4].slot == 0x13 && calls[4].a[0] == 2 && calls[4].a[1] == 1 && calls[4].deref3 == ib);
}

TEST(ctx_topology_written_inline)
{
    uint8_t *px = ctx_proxy();
    reset_calls();
    *(uint32_t *)(real_ctx + CTX_DIRTY) = 0x100;
    FM5_CALL(px, 0x18, void (*)(void *, UINT))(px, 4 /* TRIANGLELIST */);
    CHECK_EQ(ncalls, 0);                          /* nada no Azimuth: escrito no contexto */
    CHECK_EQ(*(uint64_t *)(real_ctx + CTX_TOPO_PACKET), 0x000009ffc0009e00ull);
    CHECK_EQ(*(uint32_t *)(real_ctx + CTX_DIRTY), 0x100 | CTX_DIRTY_TOPO);
    FM5_CALL(px, 0x18, void (*)(void *, UINT))(px, 999);
    CHECK_EQ(*(uint64_t *)(real_ctx + CTX_TOPO_PACKET), 0x000001ffc0009e00ull);
}

TEST(ctx_topology_packet_kinds)
{
    CHECK_EQ(topo_packet(0), 0x000001ffc0009e00ull);              /* UNDEFINED */
    CHECK_EQ(topo_packet(1), 0x000003ffc0009e00ull);              /* POINTLIST */
    CHECK_EQ(topo_packet(5), 0x00000dffc0009e00ull);              /* TRIANGLESTRIP */
    CHECK_EQ(topo_packet(12), 0x0000197fc0009e00ull);             /* TRIANGLELIST_ADJ */
    CHECK_EQ(topo_packet(35), 0x00301354c0009e00ull);             /* 3_CONTROL_POINT_PATCHLIST */
    CHECK_EQ(topo_packet(64), 0x02001307c0009e00ull);             /* 32_CONTROL_POINT_PATCHLIST */
    CHECK_EQ(topo_packet(65), topo_packet(0));
}

static const GUID guid_any = { 1, 2, 3, { 4 } };
static long fake_qi(void *self, const GUID *iid, void **out) { (void)iid; *out = self; return 0; }

TEST(ctx_query_interface_returns_proxy)
{
    uint8_t *px = ctx_proxy();
    void *out = NULL, *saved = ctx_tbl[0];
    ctx_tbl[0] = (void *)fake_qi;
    CHECK_EQ(FM5_CALL(px, 0, long (*)(void *, const GUID *, void **))(px, &guid_any, &out), 0);
    CHECK(out == px);                             /* nunca devolve o contexto real ao jogo */
    ctx_tbl[0] = saved;
}

TEST(ctx_set_fast_become_set_with_one_element)
{
    static const struct { unsigned fm5, az; } map[] = {
        { 0x9a, 0x11 }, { 0x9b, 0x09 }, { 0x9c, 0x0b }, { 0x9d, 0x08 }, { 0x9e, 0x19 }, { 0x9f, 0x1a },
        { 0xa0, 0x17 }, { 0xa1, 0x1f }, { 0xa2, 0x20 }, { 0xa3, 0x47 }, { 0xa4, 0x43 }, { 0xa5, 0x46 },
        { 0xa6, 0x3e }, { 0xa7, 0x3b }, { 0xa8, 0x3d }, { 0xa9, 0x42 }, { 0xaa, 0x3f }, { 0xab, 0x41 } };
    uint8_t *px = ctx_proxy();
    size_t i;
    reset_calls();
    rec_deref3 = 1;
    for (i = 0; i < sizeof map / sizeof *map; i++) {
        void *obj = (void *)(uintptr_t)(0x5000 + i);
        FM5_CALL(px, map[i].fm5, void (*)(void *, UINT, void *))(px, 3, obj);
        if (calls[i].slot != map[i].az || calls[i].a[0] != 3 || calls[i].a[1] != 1 || calls[i].deref3 != obj)
            test_fail(__FILE__, __LINE__, "SetFast FM5 %#x -> Azimuth %#x (chegou %#x, start %llu n %llu)", map[i].fm5,
                      map[i].az, calls[i].slot, (unsigned long long)calls[i].a[0], (unsigned long long)calls[i].a[1]);
    }
}

/* objeto COM falso: conta AddRef/Release */
typedef struct { void **vtbl; LONG refs; } FakeObj;
static ULONG fo_addref(FakeObj *o) { return ++o->refs; }
static ULONG fo_release(FakeObj *o) { return --o->refs; }
static void *fo_vtbl[3] = { NULL, (void *)fo_addref, (void *)fo_release };
static FakeObj *fake_obj(void) { FakeObj *o = calloc(1, sizeof *o); o->vtbl = fo_vtbl; o->refs = 1; return o; }

TEST(ctx_ps_samplers_saved_for_get)
{
    uint8_t *px = ctx_proxy();
    FakeObj *s0 = fake_obj(), *s1 = fake_obj();
    void *set[2] = { s0, s1 }, *got[3] = { (void *)1, (void *)1, (void *)1 };
    reset_calls();
    FM5_CALL(px, 0x0a, void (*)(void *, UINT, UINT, void *const *))(px, 2, 2, set);   /* PSSetSamplers */
    CHECK(calls[0].slot == 0x0b && calls[0].a[0] == 2 && calls[0].a[1] == 2);
    FM5_CALL(px, 0x9c, void (*)(void *, UINT, void *))(px, 5, s0);                    /* PSSetFastSampler */
    FM5_CALL(px, 0x4b, void (*)(void *, UINT, UINT, void **))(px, 1, 3, got);         /* PSGetSamplers */
    CHECK(got[0] == NULL && got[1] == s0 && got[2] == s1);
    CHECK_EQ(s0->refs, 2); CHECK_EQ(s1->refs, 2);                                      /* Get* com AddRef */
    CHECK_EQ(ncalls, 2);                                                              /* Get nao vai ao Azimuth */
    FM5_CALL(px, 0x6e, void (*)(void *))(px);                                          /* ClearState */
    FM5_CALL(px, 0x4b, void (*)(void *, UINT, UINT, void **))(px, 5, 1, got);
    CHECK(got[0] == NULL);
}

TEST(ctx_scissor_rects_saved_for_get)
{
    uint8_t *px = ctx_proxy();
    rect_t r[2] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } }, out[4];
    UINT n;
    reset_calls();
    FM5_CALL(px, 0x2d, void (*)(void *, UINT, const rect_t *))(px, 2, r);
    CHECK(calls[0].slot == 0x2d && calls[0].a[0] == 2);
    n = 4; memset(out, 0xee, sizeof out);
    FM5_CALL(px, 0x60, void (*)(void *, UINT *, rect_t *))(px, &n, out);
    CHECK_EQ(n, 2);
    CHECK(!memcmp(out, r, sizeof r) && out[2].left == 0 && out[3].bottom == 0);
    n = 0;
    FM5_CALL(px, 0x60, void (*)(void *, UINT *, rect_t *))(px, &n, NULL);              /* consulta da contagem */
    CHECK_EQ(n, 2);
    FM5_CALL(px, 0x72, long (*)(void *, BOOL, void **))(px, FALSE, (void **)&out);     /* FinishCommandList(FALSE) */
    n = 4;
    FM5_CALL(px, 0x60, void (*)(void *, UINT *, rect_t *))(px, &n, out);
    CHECK_EQ(n, 0);
    FM5_CALL(px, 0x2d, void (*)(void *, UINT, const rect_t *))(px, 1, r);
    FM5_CALL(px, 0x6e, void (*)(void *))(px);                                          /* ClearState zera */
    n = 4;
    FM5_CALL(px, 0x60, void (*)(void *, UINT *, rect_t *))(px, &n, out);
    CHECK_EQ(n, 0);
}

/* objeto de textura do Azimuth: tipo +0x38, W/H +0x70/+0x74, fmt +0x80, samples +0x84 */
static uint8_t *az_tex(UINT samples)
{
    uint8_t *t = calloc(1, 0x100);
    *(UINT *)(t + 0x38) = 0xc; *(UINT *)(t + 0x70) = 64; *(UINT *)(t + 0x74) = 64; *(UINT *)(t + 0x80) = 26;
    *(UINT *)(t + 0x84) = samples;
    return t;
}

TEST(ctx_resolve_single_sample_becomes_copy)
{
    uint8_t *px = ctx_proxy(), *src1 = az_tex(1), *src2 = az_tex(2), *dst = az_tex(1);
    typedef void (*resolve_fn)(void *, void *, UINT, void *, UINT, UINT);
    reset_calls();
    FM5_CALL(px, 0x39, resolve_fn)(px, dst, 0, src1, 3, 26);
    CHECK_EQ(calls[0].slot, 0x2e);                /* CopySubresourceRegion(dst, 0, 0,0,0, src, 3, NULL) */
    CHECK(calls[0].a[0] == (uintptr_t)dst && calls[0].a[5] == (uintptr_t)src1 && (UINT)calls[0].a[6] == 3 &&
          calls[0].a[7] == 0);
    FM5_CALL(px, 0x39, resolve_fn)(px, dst, 0, src2, 0, 26);
    CHECK(calls[1].slot == 0x39 && calls[1].a[2] == (uintptr_t)src2 && (UINT)calls[1].a[4] == 26);
}

/* ------------------------------------------------------------------------ */
/* device                                                                     */
/* ------------------------------------------------------------------------ */
static uint8_t *fake_dev;
static long fake_get_ctx(void *dev, void **out) { (void)dev; *out = real_ctx; return 0; }
static UINT seen_tex_desc[16];
static long fake_create_tex(void *dev, const UINT *desc, const void *init, void **out)
{
    (void)dev; (void)init;
    memcpy(seen_tex_desc, desc, sizeof seen_tex_desc);
    *out = (void *)(uintptr_t)(0x7000 + desc[6] + desc[0]);
    return 0;
}

static void dev_setup(void)
{
    if (fake_dev) return;
    ctx_proxy();
    fake_dev = calloc(1, 0x100);
    *(void ***)fake_dev = fake_vtbl();
    (*(void ***)fake_dev)[0x29] = (void *)fake_get_ctx;      /* GetImmediateContext */
    (*(void ***)fake_dev)[0x37] = (void *)fake_get_ctx;      /* GetImmediateContextX (FH2) */
    (*(void ***)fake_dev)[5] = (void *)fake_create_tex;
    install_device_vtable(fake_dev);
}

TEST(device_vtable_layout)
{
    static const int wrapped[] = { 3, 5, 7, 9, 0x14, 0x16, 0x3c, 0x1b, 0x29, 0x2c, 0x2d, 0x33, 0x40, 0x37 };
    int s, k;
    dev_setup();
    CHECK(*(void **)fake_dev == (void *)dev_thunk_vtbl);
    for (s = 0; s < SLOTS - DEV_SHIFT; s++) {
        int w = 0, want = s < DEV_X_FIRST ? s : s + DEV_SHIFT;
        for (k = 0; k < (int)(sizeof wrapped / sizeof *wrapped); k++) w |= wrapped[k] == s;
        if (w) continue;
        reset_calls();
        ((void (*)(void *, uintptr_t))(*(void ***)fake_dev)[s])(fake_dev, 0x42);
        if (ncalls != 1 || calls[0].slot != (unsigned)want)
            test_fail(__FILE__, __LINE__, "device FM5 %#x -> Azimuth %#x esperado, chegou %#x", s, want, calls[0].slot);
    }
    CHECK(dev_impl[SLOTS - 1] == (void *)dev_bad_slot);
}

TEST(device_get_context_returns_proxy)
{
    void *out = NULL;
    dev_setup();
    ((long (*)(void *, void **))(*(void ***)fake_dev)[0x29])(fake_dev, &out);
    CHECK(out == proxy_of(real_ctx));
    out = NULL;
    ((long (*)(void *, void **))(*(void ***)fake_dev)[0x33])(fake_dev, &out);   /* X: FM5 0x33 = Azimuth 0x37 */
    CHECK(out == proxy_of(real_ctx));
}

/* Quality do MSAA (cont. 23): D3D12 recebe o padrao; GetDesc chamado pelo JOGO devolve o Quality pedido */
static void fake_getdesc(void *tex, UINT *d) { (void)tex; memset(d, 0, 12 * 4); d[5] = 4; d[6] = 0xffffffff; }

TEST(msaa_quality_standard_pattern_and_getdesc)
{
    UINT desc[12] = { 512, 512, 1, 1, 26, 4, 3 /* Quality */, 0, 0x20, 0, 0, 0 }, d[12];
    void *tex = NULL;
    uint8_t *game = t_rwx(0x10000);
    dev_setup();
    log_capture();
    REQUIRE(((long (*)(void *, const UINT *, const void *, void **))(*(void ***)fake_dev)[5])(fake_dev, desc, NULL, &tex) == 0);
    CHECK_EQ(seen_tex_desc[6], 0xffffffff);       /* D3D12_STANDARD_MULTISAMPLE_PATTERN */
    CHECK_EQ(desc[6], 3);                         /* desc do jogo intacto */
    orig_tex2d_getdesc = fake_getdesc;
    g_game = game;
    tex2d_GetDesc(tex, d);                        /* chamado de fora do jogo (Azimuth) */
    CHECK_EQ(d[6], 0xffffffff);
    ((void (*)(void *, UINT *))t_call_from(game + 0x8000, (void *)tex2d_GetDesc))(tex, d);
    CHECK_EQ(d[6], 3);                            /* chamado do .text do FM5 */
    ((void (*)(void *, UINT *))t_call_from(game + 0x8000, (void *)tex2d_GetDesc))((void *)0x1234, d);
    CHECK_EQ(d[6], 0xffffffff);                   /* textura desconhecida */
}

TEST(msaa_quality_table_wraps)
{
    unsigned i;
    UINT q;
    for (i = 0; i < 300; i++) msaa_q_set((void *)(uintptr_t)(0x100000 + i), i);
    CHECK(msaa_q_get((void *)(uintptr_t)(0x100000 + 299), &q) && q == 299);
    msaa_q_set((void *)(uintptr_t)(0x100000 + 299), 7);     /* mesma textura: atualiza */
    CHECK(msaa_q_get((void *)(uintptr_t)(0x100000 + 299), &q) && q == 7);
    CHECK(!msaa_q_get((void *)(uintptr_t)0xdead0, &q));
}

TEST(host_samples_follows_launcher_msaa)
{
    uint8_t *az = fake_azimuth(0);
    *(UINT *)(az + 0x318ea0) = 4;  CHECK_EQ(host_samples(2), 2);   /* "do console": as do jogo */
    *(UINT *)(az + 0x318ea0) = 0;  CHECK_EQ(host_samples(4), 1);   /* AA desligado */
    *(UINT *)(az + 0x318ea0) = 2;  CHECK_EQ(host_samples(4), 2);
}

/* aliasing da ESRAM (cont. 19/21): cor MSAA, 2 profundidades e a cor 1x do mesmo tamanho */
TEST(alias_track_sequence)
{
    UINT c2[12] = { 1600, 900, 1, 1, 26, 2, 0, 0, 0, 0, 0, 0 }, dp[12] = { 1600, 900, 1, 1, 19, 2 };
    UINT c1[12] = { 1600, 900, 1, 1, 26, 1, 0, 0, 0, 0, 0x20000, 0 }, mir[12] = { 2304, 768, 1, 1, 26, 2 };
    FakeObj *a = fake_obj(), *b = fake_obj(), *c = fake_obj(), *d = fake_obj(), *m = fake_obj();
    log_capture();
    alias_test = 1;
    alias_track(mir, m);                          /* 3:1 = espelho, ignorado */
    CHECK(al_color2x != m);
    alias_track(c2, a);
    CHECK(al_color2x == a && al_w == 1600 && al_h == 900); CHECK_EQ(a->refs, 2);
    alias_track(dp, b);
    alias_track(dp, c);
    CHECK(al_depth_a == b && al_depth_b == c);
    alias_track(c1, d);
    CHECK(al_color1x == d);
    alias_track(c2, m);                           /* nova cor MSAA: troca e solta a anterior e a 1x */
    CHECK(al_color2x == m && al_color1x == NULL); CHECK_EQ(a->refs, 1); CHECK_EQ(d->refs, 1);
    alias_test = 0;
}

/* ------------------------------------------------------------------------ */
/* private data (cont. 22)                                                    */
/* ------------------------------------------------------------------------ */
static const GUID guid_bytecode = { 0x0a51ffd4, 0x060b, 0x4bef, { 0xb5, 0x93, 0x86, 0xf1, 0x53, 0x6f, 0x72, 0x18 } };
static const GUID guid_other = { 0x11111111, 0x2222, 0x3333, { 4, 5, 6, 7, 8, 9, 10, 11 } };
static ULONG release_result;
static ULONG fake_release(void *obj) { (void)obj; return release_result; }

static uint8_t *pd_game;
static uint8_t *pd_setup(void)
{
    /* g_game falso so com o GUID do bytecode no RVA (e o exe real, se existir, no outro teste) */
    if (!pd_game) {
        pd_game = t_rwx(0x1479b08 + 0x20);
        memcpy(pd_game + RVA_FM5_GUID_BYTECODE, &guid_bytecode, 16);
    }
    g_game = pd_game;
    orig_release = fake_release;
    return pd_game;
}

TEST(private_data_set_get)
{
    uint8_t obj[0x40] = { 0 };
    uint32_t v = 0x12345678, out = 0;
    UINT size;
    pd_setup();
    CHECK_EQ(pd_SetPrivateData(obj, &guid_other, 4, &v), S_OK);
    size = 0;
    CHECK_EQ(pd_GetPrivateData(obj, &guid_other, &size, NULL), S_OK);
    CHECK_EQ(size, 4);
    size = 2;
    CHECK_EQ((uint32_t)pd_GetPrivateData(obj, &guid_other, &size, &out), (uint32_t)DXGI_ERROR_MORE_DATA_);
    size = 4;
    CHECK_EQ(pd_GetPrivateData(obj, &guid_other, &size, &out), S_OK);
    CHECK_EQ(out, v);
    size = 4;
    CHECK_EQ((uint32_t)pd_GetPrivateData(obj, &guid_bytecode, &size, &out), (uint32_t)DXGI_ERROR_NOT_FOUND_);
    CHECK_EQ(size, 0);
    CHECK_EQ(pd_SetPrivateData(obj, &guid_other, 0, NULL), S_OK);       /* tamanho 0 = apaga */
    size = 4;
    CHECK_EQ((uint32_t)pd_GetPrivateData(obj, &guid_other, &size, &out), (uint32_t)DXGI_ERROR_NOT_FOUND_);
    CHECK_EQ((uint32_t)pd_SetPrivateData(obj, NULL, 4, &v), (uint32_t)E_INVALIDARG);
}

TEST(private_data_interface_refcount)
{
    uint8_t obj[0x40] = { 0 };
    FakeObj *u = fake_obj(), *u2 = fake_obj();
    void *out = NULL;
    UINT size = 8;
    pd_setup();
    CHECK_EQ(pd_SetPrivateDataInterface(obj, &guid_other, (IUnknown *)u), S_OK);
    CHECK_EQ(u->refs, 2);
    CHECK_EQ(pd_GetPrivateData(obj, &guid_other, &size, &out), S_OK);
    CHECK(out == u); CHECK_EQ(u->refs, 3);        /* Get com AddRef */
    CHECK_EQ(pd_SetPrivateDataInterface(obj, &guid_other, (IUnknown *)u2), S_OK);
    CHECK_EQ(u->refs, 2); CHECK_EQ(u2->refs, 2);  /* substituido: Release no antigo */
    release_result = 1;
    pd_release_hook(obj);                          /* ainda vivo: mantem */
    CHECK_EQ(u2->refs, 2);
    release_result = 0;
    pd_release_hook(obj);                          /* morreu: descarta e solta a interface */
    CHECK_EQ(u2->refs, 1);
    size = 8;
    CHECK_EQ((uint32_t)pd_GetPrivateData(obj, &guid_other, &size, &out), (uint32_t)DXGI_ERROR_NOT_FOUND_);
}

/* o destrutor do shader (Get em game+0x6bb07d) so pode levar o ptr do bytecode uma vez */
TEST(private_data_bytecode_double_free_guard)
{
    uint8_t obj[0x40] = { 0 }, *game = pd_setup();
    void *ptr = (void *)0xb17ec0de, *out;
    UINT size;
    long (*dtor_get)(void *, const GUID *, UINT *, void *) =
        (long (*)(void *, const GUID *, UINT *, void *))t_call_from(game + RVA_FM5_DTOR_GET_RET, (void *)pd_GetPrivateData);
    log_capture();
    CHECK_EQ(pd_SetPrivateData(obj, &guid_bytecode, 8, &ptr), S_OK);
    out = NULL; size = 8;
    CHECK_EQ(pd_GetPrivateData(obj, &guid_bytecode, &size, &out), S_OK);   /* reflexao: quantas vezes quiser */
    CHECK(out == ptr);
    out = NULL; size = 8;
    CHECK_EQ(dtor_get(obj, &guid_bytecode, &size, &out), S_OK);            /* destrutor: 1a vez */
    CHECK(out == ptr);
    out = NULL; size = 8;
    CHECK_EQ((uint32_t)dtor_get(obj, &guid_bytecode, &size, &out), (uint32_t)DXGI_ERROR_NOT_FOUND_);
    CHECK(out == NULL);                            /* 2a vez: double free evitado */
    CHECK(log_has("double free avoided"));
    release_result = 0;
    pd_release_hook(obj);
}

/* RVAs do FM5 usados acima conferem com o exe real */
TEST(private_data_rvas_match_fm5_exe)
{
    uint8_t *g = pe_map(FM5_EXE, NULL);
    const uint8_t *r;
    if (!g) { test_skip(FM5_EXE " ausente"); return; }
    CHECK(!memcmp(g + RVA_FM5_GUID_BYTECODE, &guid_bytecode, 16));
    r = g + RVA_FM5_DTOR_GET_RET;
    /* retorno logo apos um call (call [reg+disp8] / call [rip+x] / call rel32) */
    CHECK((r[-3] == 0xff && (r[-2] & 0x38) == 0x10) || (r[-6] == 0xff && r[-5] == 0x15) || r[-5] == 0xe8 ||
          (r[-6] == 0xff && (r[-5] & 0x38) == 0x10));
    pe_unmap(g);
}

/* entrada do resolvedor: dois std::string do MSVC (dll em +0, nome em +0x20) */
static void set_msvc_str(uint8_t *s, const char *v)
{
    size_t n = strlen(v);
    memset(s, 0, 0x20);
    if (n > 15) { *(const char **)s = v; *(size_t *)(s + 0x18) = n; }
    else { memcpy(s, v, n); *(size_t *)(s + 0x18) = 15; }
    *(size_t *)(s + 0x10) = n;
}

/* imports ausentes no Azimuth -> DLL real com o mesmo nome; o resto continua sem resolucao */
TEST(import_fallback_forwards_to_real_dll)
{
    uint8_t e[0x40];
    HMODULE k32 = GetModuleHandleA("kernel32.dll"), cb = LoadLibraryA("combase.dll");
    set_msvc_str(e, "kernelx.dll"); set_msvc_str(e + 0x20, "GetThreadId");
    CHECK(import_fallback(e) == GetProcAddress(k32, "GetThreadId"));
    CHECK(import_fallback(e) != NULL);
    set_msvc_str(e + 0x20, "GlobalMemoryStatusEx");
    CHECK(import_fallback(e) == GetProcAddress(k32, "GlobalMemoryStatusEx"));
    set_msvc_str(e + 0x20, "DuplicateHandle");
    CHECK(import_fallback(e) == GetProcAddress(k32, "DuplicateHandle"));
    set_msvc_str(e + 0x20, "GetLocalTime");
    CHECK(import_fallback(e) == GetProcAddress(k32, "GetLocalTime"));
    set_msvc_str(e + 0x20, "CreateFileA");
    CHECK(import_fallback(e) == NULL);
    set_msvc_str(e, "combase.dll"); set_msvc_str(e + 0x20, "StringFromGUID2");
    CHECK(cb && import_fallback(e) == GetProcAddress(cb, "StringFromGUID2") && import_fallback(e));
    set_msvc_str(e + 0x20, "GetThreadId");
    CHECK(import_fallback(e) == NULL);
}

/* vline: o evento registrado e sinalizado periodicamente (~60 Hz); evento nulo desliga */
TEST(fm6_vline_notification_signals)
{
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    DWORD t0, n = 0;
    CHECK_EQ(fm6_DXGIXSetVLineNotification(VLINE_N, 0, ev), E_INVALIDARG);
    CHECK_EQ(fm6_DXGIXSetVLineNotification(0, 0, ev), S_OK);
    t0 = GetTickCount();
    while (GetTickCount() - t0 < 200) if (WaitForSingleObject(ev, 100) == WAIT_OBJECT_0) n++;
    CHECK(n >= 6 && n <= 16);                        /* 200 ms a 60 Hz ~ 12 */
    fm6_vline_rate(180);                             /* frame_rate 180 (fm6_frame_unlock) */
    t0 = GetTickCount(); n = 0;
    while (GetTickCount() - t0 < 200) if (WaitForSingleObject(ev, 100) == WAIT_OBJECT_0) n++;
    CHECK(n >= 24 && n <= 44);                       /* 200 ms a 180 Hz ~ 36 */
    fm6_vline_rate(60);
    CHECK_EQ(fm6_DXGIXSetVLineNotification(0, 0, NULL), S_OK);
    Sleep(40);
    ResetEvent(ev);
    CHECK_EQ(WaitForSingleObject(ev, 60), WAIT_TIMEOUT);
    CloseHandle(ev);
}

/* FM6: DeleteFileW em d:\ (armazenamento persistente que o FM6 nao declara) falha sem chamar o Azimuth */
static int az_delete_calls;
static BOOL WINAPI fake_az_delete(const wchar_t *path) { (void)path; az_delete_calls++; return TRUE; }

TEST(fm6_delete_file_d_drive_fails)
{
    az_DeleteFileW = fake_az_delete;
    az_delete_calls = 0;
    SetLastError(0);
    CHECK(!fm6_DeleteFileW(L"d:\\callHistoryJson.tmp"));
    CHECK_EQ(GetLastError(), ERROR_PATH_NOT_FOUND);
    CHECK(!fm6_DeleteFileW(L"D:/x.tmp"));
    CHECK_EQ(az_delete_calls, 0);
    CHECK(fm6_DeleteFileW(L"T:\\x.tmp") && fm6_DeleteFileW(L"d:") && fm6_DeleteFileW(L"dx\\y"));
    CHECK_EQ(az_delete_calls, 3);
    az_DeleteFileW = NULL;
}

/* FM6: GetProcAddress("CreateXAudio2Object") devolve a ponte com processador -1 no lugar do 0x140181b10 */
static uint8_t *gpa_base;
static FARPROC WINAPI fake_az_gpa(HMODULE m, const char *name)
{
    (void)m;
    return (FARPROC)(strcmp(name, "CreateXAudio2Object") ? (void *)0x1234 : gpa_base + 0x181b10);
}

TEST(fm6_get_proc_address_xaudio2_bridge)
{
    gpa_base = fake_azimuth(0);
    az_GetProcAddress = fake_az_gpa;
    CHECK(fm6_GetProcAddress(NULL, "CreateXAudio2Object") == (FARPROC)fm5_XAudio2Create);
    CHECK(fm6_GetProcAddress(NULL, "X3DAudioInitialize") == (FARPROC)(void *)0x1234);
    az_GetProcAddress = NULL;
}

/* FM6: D3D11CreateDevice -> D3D11XCreateDeviceX do Azimuth com o descritor do FH2; device/contexto sem conversao */
static uint32_t createx_desc[11];
static void *fm6_dev_vt[0x56], *fm6_dev[8] = { fm6_dev_vt };
static long fake_createx(const void *desc, void **dev, void **ctx)
{
    memcpy(createx_desc, desc, sizeof createx_desc);
    *dev = fm6_dev;
    *ctx = (void *)0x2222;
    return 0;
}

static void *fm6_type_tab[0x12][0x10e];
static void **fake_vtbl_get(int t) { return fm6_type_tab[t]; }

static uint32_t create_tex_desc[12];
static long fake_fm6_create_tex(void *dev, const UINT *desc, const void *init, void **out)
{
    (void)dev; (void)init;
    memcpy(create_tex_desc, desc, sizeof create_tex_desc);
    *out = (void *)0x7e7e;
    return 0;
}

static void *srv_res;
static UINT srv_desc[4];
static long fake_fm6_create_srv(void *dev, void *res, const UINT *desc, void **out)
{
    (void)dev;
    srv_res = res;
    memcpy(srv_desc, desc, sizeof srv_desc);
    *out = (void *)0x5e5e;
    return 0;
}

static void fake_fm6_getdesc(void *tex, UINT *d) { (void)tex; memset(d, 0, 12 * 4); d[5] = 2; d[6] = 0xffffffff; }

static uint32_t create_sc_desc[12];
static long fake_create_sc_cw(void *factory, void *device, void *window, const uint32_t *desc, void *ro, void **out)
{
    (void)factory; (void)device; (void)window; (void)ro;
    memcpy(create_sc_desc, desc, sizeof create_sc_desc);
    *out = (void *)0x5c5c;
    return 0;
}

TEST(fm6_create_device_uses_azimuth_device)
{
    uint8_t *az = fake_azimuth(0);
    static const UINT lv = 0xb100;
    void *dev = (void *)1, *ctx = (void *)1;
    UINT level = 0;
    int i;
    for (i = 0; i < 0x56; i++) fm6_dev_vt[i] = az + 0xf5f30 + i * 0x100;   /* stubs fatais padrao */
    fm6_dev_vt[0x51] = (void *)0x5151;                                        /* implementado: fica */
    fm6_dev_vt[5] = (void *)fake_fm6_create_tex;                              /* CreateTexture2D */
    fm6_dev_vt[7] = (void *)fake_fm6_create_srv;                              /* CreateShaderResourceView */
    memcpy(az + RVA_CREATE_DEVICEX, "\xff\x25\0\0\0\0", 6);
    *(void **)(az + RVA_CREATE_DEVICEX + 6) = (void *)fake_createx;
    memcpy(az + RVA_VTBL_GET, "я%    ", 6);
    *(void **)(az + RVA_VTBL_GET + 6) = (void *)fake_vtbl_get;
    for (i = 0; i < 0x10e; i++) fm6_type_tab[0xe][i] = fm6_type_tab[5][i] = (void *)(uintptr_t)(0x100 + i);
    fm6_type_tab[0xe][0x11] = (void *)fake_create_sc_cw;                     /* CreateSwapChainForCoreWindow */
    fm6_type_tab[0xc][0xc] = (void *)fake_fm6_getdesc;                       /* Texture2D GetDesc (Azimuth 0xc) */
    CHECK_EQ(fm6_D3D11CreateDevice(NULL, 1, NULL, 0x4000000, &lv, 1, 0x20011, &dev, &level, &ctx), 0);
    CHECK_HEX((uint8_t *)createx_desc, "0b000100" "00000004" "00000000" "00000000" "00000000" "00000000"
                                       "3f000000" "00000000" "00000000" "00000000" "00000000");
    CHECK(dev == (void *)fm6_dev && ctx == (void *)0x2222);
    CHECK_EQ(level, 0xb100);
    /* PlaceSwapChainView (game+0x3a676c) = ret no driver do console; E_NOTIMPL onde o driver devolve isso */
    CHECK_EQ(((long (*)(void *, UINT))fm6_dev_vt[0x50])(fm6_dev, 0x14), 0);
    CHECK_EQ(((long (*)(void *))fm6_dev_vt[0x2b])(fm6_dev), 0);
    CHECK_EQ(((long (*)(void *))fm6_dev_vt[0x38])(fm6_dev), (long)0x80004001);
    CHECK_EQ(((long (*)(void *))fm6_dev_vt[0x28])(fm6_dev), (long)0x80004001);
    CHECK(fm6_dev_vt[0x51] == (void *)0x5151);
    {   /* CreateTexture2D: SampleDesc {2, 2} de game+0x334f94 -> Quality padrao do D3D12; {1, 0} passa igual */
        static const UINT tex[12] = { 1920, 1080, 1, 1, 26, 2, 2, 0, 0x28, 0, 0x40000, 0 };
        static const UINT one[12] = { 64, 64, 1, 1, 28, 1, 0, 0, 8, 0, 0, 0 };
        void *t = NULL;
        CHECK_EQ(((long (*)(void *, const UINT *, const void *, void **))fm6_dev_vt[5])(fm6_dev, tex, NULL, &t), 0);
        CHECK(t == (void *)0x7e7e);
        CHECK_HEX((uint8_t *)create_tex_desc, "80070000" "38040000" "01000000" "01000000" "1a000000" "02000000"
                                              "ffffffff" "00000000" "28000000" "00000000" "00000400" "00000000");
        CHECK_EQ(tex[6], 2);
        {   /* GetDesc do Texture2D (FM6 slot 0xd): o jogo (resolve game+0x40b7aa) ve {2, 2}; o Azimuth, o padrao */
            uint8_t *game = t_rwx(0x10000), *saved = g_fm6_game;
            UINT d[12];
            void (*getdesc)(void *, UINT *) = (void (*)(void *, UINT *))fm6_type_tab[0xc][0xd];
            CHECK(getdesc == fm6_tex2d_GetDesc);
            g_fm6_game = game;
            getdesc(t, d);
            CHECK_EQ(d[6], 0xffffffff);
            ((void (*)(void *, UINT *))t_call_from(game + 0x8000, (void *)getdesc))(t, d);
            CHECK_EQ(d[5], 2);
            CHECK_EQ(d[6], 2);
            ((void (*)(void *, UINT *))t_call_from(game + 0x8000, (void *)getdesc))((void *)0x1234, d);
            CHECK_EQ(d[6], 0xffffffff);                                      /* textura desconhecida */
            g_fm6_game = saved;
        }
        CHECK_EQ(((long (*)(void *, const UINT *, const void *, void **))fm6_dev_vt[5])(fm6_dev, one, NULL, &t), 0);
        CHECK_HEX((uint8_t *)create_tex_desc, "40000000" "40000000" "01000000" "01000000" "1c000000" "01000000"
                                              "00000000" "00000000" "08000000" "00000000" "00000000" "00000000");
    }
    {   /* CreateShaderResourceView: R8_UINT/TEXTURE2D sobre a cor 2x (game+0x216dc7) -> FMASK emulada (R8, 1x, SRV) */
        static uint8_t res[0x100];
        static const UINT fmask[4] = { 62, 4, 0, 1 }, plain[4] = { 26, 4, 0, 1 };
        void *v = NULL;
        *(UINT *)(res + 0x38) = 0xc; *(UINT *)(res + 0x70) = 1600; *(UINT *)(res + 0x74) = 900;
        *(UINT *)(res + 0x84) = 2; *(UINT *)(res + 0xe4) = 1;
        CHECK_EQ(((long (*)(void *, void *, const UINT *, void **))fm6_dev_vt[7])(fm6_dev, res, fmask, &v), 0);
        CHECK(v == (void *)0x5e5e && srv_res == (void *)0x7e7e);
        CHECK_HEX((uint8_t *)create_tex_desc, "40060000" "84030000" "01000000" "01000000" "3e000000" "01000000"
                                              "00000000" "00000000" "08000000" "00000000" "00000000" "00000000");
        CHECK_HEX((uint8_t *)srv_desc, "3e000000" "04000000" "00000000" "01000000");
        CHECK_EQ(((long (*)(void *, void *, const UINT *, void **))fm6_dev_vt[7])(fm6_dev, res, plain, &v), 0);
        CHECK(srv_res == (void *)res && srv_desc[0] == 26);
    }
    CHECK(fm6_dev_vt[0x3b] == az + 0xf5f30 + 0x3b * 0x100);                   /* fora da lista: intacto */
    CHECK(fm6_dev_vt[0xb] == (void *)fm6_CreateInputLayout && fm6_dev_vt[0xc] == (void *)fm6_create_shader_0x0c);
    CHECK(fm6_dev_vt[0xe] == (void *)fm6_create_shader_0x0e && fm6_dev_vt[0x12] == (void *)fm6_create_shader_0x12);
    /* tabelas por tipo: IDXGIFactory (0xe) sem o slot 5, recurso (5) com o slot 8 novo */
    CHECK(fm6_type_tab[0xe][4] == (void *)0x104 && fm6_type_tab[0xe][5] == (void *)0x106);
    {   /* CreateSwapChainForCoreWindow (0x11 -> 0x10): descritor de game+0x53d3fb sem a flag 0x100000 */
        static const uint32_t sc[12] = { 2560, 1440, 0x1c, 0, 1, 0, 0x20, 2, 0, 0, 0, 0x100002 };
        void *swap = NULL;
        CHECK_EQ(((long (*)(void *, void *, void *, const uint32_t *, void *, void **))fm6_type_tab[0xe][0x10])(
                     (void *)1, fm6_dev, (void *)2, sc, NULL, &swap), 0);
        CHECK(swap == (void *)0x5c5c);
        CHECK_HEX((uint8_t *)create_sc_desc, "000a0000" "a0050000" "1c000000" "00000000" "01000000" "00000000"
                                             "20000000" "02000000" "00000000" "00000000" "00000000" "02000000");
        CHECK_EQ(sc[11], 0x100002);
    }
    CHECK(fm6_type_tab[5][7] == (void *)0x107 && fm6_type_tab[5][9] == (void *)0x108);
    CHECK(fm6_type_tab[5][0xc] == (void *)0x10b);                            /* GetDescriptor */
    CHECK_EQ(((long (*)(void))fm6_type_tab[5][8])(), 0);
}

/* FM6: as duas copias da tabela embutida do contexto (ctx+0x3d0) montam o layout do driver do FM6 (+3 slots) */
typedef void *(*copy_site_fn)(void *dst, const void *src, size_t n);
static copy_site_fn fm6_copy_site(uint8_t *az, uint32_t rva)
{
    uint8_t *at = az + rva;
    int32_t rel = (int32_t)(az + 0x24b6f0 - (at + 5));
    memcpy(at - 10, "\x48\x83\xec\x08" "\x41\xb8\x70\x08\x00\x00" "\xe8", 11);   /* sub rsp,8; mov r8d,0x870; call */
    memcpy(at + 1, &rel, 4);
    memcpy(at + 5, "\x48\x83\xc4\x08\xc3", 5);                                 /* add rsp,8; ret */
    return (copy_site_fn)(at - 10);
}

static void *srvs_ctx, *const *srvs_views;
static UINT srvs_start, srvs_n, srvs_slot;
static void fake_set_srvs(void *ctx, UINT start, UINT n, void *const *views, UINT slot)
{
    srvs_ctx = ctx; srvs_start = start; srvs_n = n; srvs_views = views; srvs_slot = slot;
}
static void fake_set_srvs_19(void *c, UINT s, UINT n, void *const *v) { fake_set_srvs(c, s, n, v, 0x19); }
static void fake_set_srvs_43(void *c, UINT s, UINT n, void *const *v) { fake_set_srvs(c, s, n, v, 0x43); }
static void fake_set_srvs_09(void *c, UINT s, UINT n, void *const *v) { fake_set_srvs(c, s, n, v, 0x09); }
static void *ib_ctx, *ib_buf;
static UINT ib_fmt, ib_off;
static void fake_set_ib(void *ctx, UINT hwfmt, void *buf, UINT off)
{
    ib_ctx = ctx; ib_fmt = hwfmt; ib_buf = buf; ib_off = off;
}

TEST(fm6_context_table_layout)
{
    uint8_t *az = fake_azimuth(0);
    static void *src[0x10e], *imm[0x10e], *def[0x10e];
    copy_site_fn site_imm, site_def;
    int i;
    CHECK(!fm6_ctx_install());                                  /* bytes nao conferem: nada muda */
    CHECK_HEX(az + 0x586e1, "0000000000");
    site_imm = fm6_copy_site(az, 0x586e1);
    site_def = fm6_copy_site(az, 0x11b440);
    CHECK(fm6_ctx_install());
    CHECK(az[0x586e1] == 0xe8 && az + 0x586e6 + *(int32_t *)(az + 0x586e2) != az + 0x24b6f0);
    for (i = 0; i < 0x10e; i++) src[i] = (void *)(uintptr_t)(0x1000 + i);
    src[0x1d] = az + 0xf5f30 + 0x1d * 0x100;                    /* stubs fatais do Azimuth */
    src[0x70] = az + 0xf5f30 + 0x70 * 0x100;
    src[0xc6] = az + 0xf5f30 + 0xc6 * 0x100;
    src[0xca] = az + 0xf5f30 + 0xca * 0x100;
    CHECK(site_imm(imm, src, 0x870) == imm);
    CHECK(site_def(def, src, 0x870) == def);
    CHECK(imm[0] == (void *)0x1000 && imm[7] == (void *)0x1007);
    CHECK_EQ(((long (*)(void))imm[8])(), 0);                    /* slot novo */
    CHECK(imm[0x8d] == (void *)0x108c);
    CHECK(imm[9] == (void *)0x1008 && imm[0x12] == (void *)0x1011);   /* imediato: CB do Azimuth */
    CHECK(imm[0x18] == (void *)0x1017 && def[9] == (void *)fm6_def_set_cbs_0);
    CHECK_EQ(((long (*)(void))imm[0x8e])(), (long)-1);          /* PIXBeginEventEx */
    CHECK(imm[0x8f] == (void *)0x108d && imm[0x90] == (void *)0x108e);
    CHECK_EQ(((long (*)(void))imm[0x91])(), 0);                 /* PIXSetMarkerEx */
    CHECK(imm[0x92] == (void *)0x108f);
    CHECK(imm[0xc7] == (void *)0x10c4);                         /* game+0x3a732f: CSEnableAutomaticGpuFlush */
    CHECK(imm[0xff] == (void *)0x10fc);
    CHECK(imm[0x100] == (void *)0x1100 && imm[0x10d] == (void *)0x110d);   /* resto do memcpy */
    /* stub fatal no Azimuth + trivial no driver: E_NOTIMPL / 1 / E_FAIL no imediato */
    CHECK_EQ(((long (*)(void))imm[0x1e])(), (long)0x80004001);
    CHECK_EQ(((long (*)(void))imm[0x71])(), 1);
    CHECK_EQ(((long (*)(void))imm[0xc9])(), (long)0x80004005);
    /* diferido: GetData e Suspend nao sao triviais no driver -> stub fatal continua; 0x71 devolve 0 */
    CHECK(def[0x1e] == az + 0xf5f30 + 0x1d * 0x100 && def[0xc9] == az + 0xf5f30 + 0xc6 * 0x100);
    CHECK_EQ(((long (*)(void))def[0x71])(), 0);
    CHECK(def[0xc7] == (void *)0x10c4);
    CHECK_EQ(((long (*)(void))imm[0xcd])(), 0);                 /* SetGraphicsShaderLimits: sem efeito */
    CHECK_EQ(((long (*)(void))def[0xcd])(), 0);
    {   /* XXSetShaderResources do FM6 (ppViews, StartSlot, packed) -> metodo do Azimuth em [ctx] (start, n, ppViews) */
        static void *az_tab[0x10e], *ctx[1] = { az_tab }, *views[2];
        typedef void (*fm6_srvs_fn)(void *, void *const *, UINT, UINT);
        az_tab[0x19] = (void *)fake_set_srvs_19;
        az_tab[0x43] = (void *)fake_set_srvs_43;
        ((fm6_srvs_fn)imm[0x1a])(ctx, views, 3, (0x1d80 + 3 * 32) | 0x70000 | 1 << 19);   /* VS, 2 views */
        CHECK(srvs_ctx == ctx && srvs_views == views && srvs_slot == 0x19);
        CHECK_EQ(srvs_start, 3);
        CHECK_EQ(srvs_n, 2);
        ((fm6_srvs_fn)def[0x44])(ctx, views, 5, (0x140 + 5 * 32) | 0x70000);               /* CS, 1 view */
        CHECK(srvs_slot == 0x43 && srvs_start == 5 && srvs_n == 1);
    }
    {   /* SetFast* (FM6 0xa3.., Azimuth 0xa0.. stub fatal) -> Set* padrao; IASetFastIndexBuffer (game+0x131a2d) */
        static void *az_tab[0x10e], *ctx[1] = { az_tab };
        static void *src2[0x10e], *imm2[0x10e];
        for (i = 0; i < 0x10e; i++) src2[i] = (void *)(uintptr_t)(0x1000 + i);
        src2[0xb3] = az + 0xf5f30 + 0xb3 * 0x100;
        src2[0xa1] = az + 0xf5f30 + 0xa1 * 0x100;
        src2[0xd4] = az + 0xf5f30 + 0xd4 * 0x100;
        CHECK(site_imm(imm2, src2, 0x870) == imm2);
        CHECK(imm2[0xa3] == (void *)0x10a0);                    /* implementado no Azimuth: intacto */
        CHECK(imm2[0xd7] == (void *)fm6_SetFastResourcesFromBatch && imm[0xd7] == (void *)0x10d4);
        az_tab[0x14] = (void *)fake_set_ib;
        ((void (*)(void *, UINT, void *))imm2[0xb6])(ctx, 1, (void *)0x1b1b);
        CHECK(ib_ctx == ctx && ib_buf == (void *)0x1b1b);
        CHECK_EQ(ib_fmt, 1);
        CHECK_EQ(ib_off, 0);
        az_tab[0x09] = (void *)fake_set_srvs_09;
        ((void (*)(void *, UINT, void *))imm2[0xa4])(ctx, 4, (void *)0x5a5a);   /* PSSetFastShaderResource */
        CHECK(srvs_slot == 0x09 && srvs_start == 4 && srvs_n == 1 && srvs_views[0] == (void *)0x5a5a);
    }
}

/* UpdateSubresource de placement buffer no contexto diferido do FM6 -> memoria do jogo; resto -> Azimuth 0x30 */
static int upd_calls;
static void *upd_res;
static void fake_update_sub(void *ctx, void *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp)
{
    (void)ctx; (void)sub; (void)box; (void)src; (void)rp; (void)dp;
    upd_calls++;
    upd_res = res;
}

TEST(fm6_deferred_update_placement_buffer)
{
    static void *az_tab[0x10e], *src_tab[0x10e], *def[0x10e];
    static uint8_t ctx[0xd00], res[0x200], mem[64];
    static const UINT box[6] = { 8, 0, 0, 12, 1, 1 };
    uint8_t data[64];
    int i;
    for (i = 0; i < 64; i++) data[i] = (uint8_t)(i + 1);
    fake_azimuth(0);
    for (i = 0; i < 0x10e; i++) src_tab[i] = (void *)(uintptr_t)(0x1000 + i);
    fm6_ctx_table(def, src_tab, 0);
    CHECK(def[0x31] == (void *)0x1030);                        /* imediato: metodo do Azimuth */
    fm6_ctx_table(def, src_tab, 1);
    CHECK(def[0x31] == (void *)fm6_UpdateSubresource);
    az_tab[0x30] = (void *)fake_update_sub;
    *(void ***)ctx = az_tab;
    ctx[0xc48] = 1;                                            /* diferido */
    *(int *)(res + 0x38) = 5;
    *(UINT *)(res + 0x70) = 64;
    res[0x88] = 1;
    *(uint8_t **)(res + 0x168) = mem;
    fm6_UpdateSubresource(ctx, res, 0, box, data, 0, 0);       /* [8, 12) */
    CHECK_EQ(upd_calls, 0);
    CHECK(mem[7] == 0 && mem[8] == 1 && mem[11] == 4 && mem[12] == 0);
    fm6_UpdateSubresource(ctx, res, 0, NULL, data, 0, 0);      /* sem box: buffer inteiro */
    CHECK_EQ(upd_calls, 0);
    CHECK(mem[0] == 1 && mem[63] == 64);
    upd_calls = 0;
    {   /* fora do placement / fora dos limites / imediato -> Azimuth (que valida e para) */
        static const UINT past[6] = { 60, 0, 0, 68, 1, 1 };
        fm6_UpdateSubresource(ctx, res, 0, past, data, 0, 0);
        CHECK_EQ(upd_calls, 1);
        res[0x88] = 0;
        fm6_UpdateSubresource(ctx, res, 0, box, data, 0, 0);
        CHECK_EQ(upd_calls, 2);
        res[0x88] = 1;
        ctx[0xc48] = 0;
        fm6_UpdateSubresource(ctx, res, 0, box, data, 0, 0);
        CHECK(upd_calls == 3 && upd_res == res);
    }
}

/* juntador registrado (PASSO 51): UpdateSubresource de placement vai para a sombra do diferido (memoria do jogo
   intacta), a copia da heranca leva o conteudo daquele ponto e o imediato o aplica pelo UpdateSubresource do
   Azimuth (0x30, buffer inteiro) antes do bundle; versao igual a ultima aplicada = pulada */
static int shd_ref, shd_upd_n;
static void *shd_upd_ctx, *shd_upd_res;
static const UINT *shd_upd_box;
static uint8_t shd_upd_data[64];
static ULONG shd_addref(void *o) { (void)o; return (ULONG)++shd_ref; }
static ULONG shd_release(void *o) { (void)o; return (ULONG)--shd_ref; }
static void *shd_vt[3] = { NULL, (void *)shd_addref, (void *)shd_release };
static void shd_update(void *ctx, void *res, UINT sub, const UINT *box, const void *src, UINT rp, UINT dp)
{
    (void)sub; (void)rp; (void)dp;
    shd_upd_n++; shd_upd_ctx = ctx; shd_upd_res = res; shd_upd_box = box;
    memcpy(shd_upd_data, src, sizeof shd_upd_data);
}

TEST(fm6_deferred_placement_update_shadow_to_immediate)
{
    static void *az_tab[0x10e];
    static uint8_t jctx[0xd00], ictx[0xd00], res[0x200], mem[64];
    static const UINT box[6] = { 8, 0, 0, 12, 1, 1 };
    uint8_t d1[4] = { 1, 2, 3, 4 }, d2[64];
    fm6_om_item *o1, *o2, *o3;
    fm6_dctx_info *d;
    fm6_ver *v1;
    LONG i = InterlockedIncrement(&fm6_dctx_n) - 1;
    REQUIRE(i < 128);
    fake_azimuth(0);
    memset(&fm6_dctx[i], 0, sizeof fm6_dctx[i]);
    fm6_dctx[i].ctx = jctx;
    fm6_dctx[i].idx = (int)i;
    d = &fm6_dctx[i];
    for (i = 0; i < 64; i++) mem[i] = 0xee, d2[i] = (uint8_t)(0x40 + i);
    *(void ***)jctx = az_tab;
    *(void ***)ictx = az_tab;
    jctx[0xc48] = 1;
    *(void ***)res = shd_vt;
    *(int *)(res + 0x38) = 5;
    *(UINT *)(res + 0x70) = 64;
    res[0x88] = 1;
    *(uint8_t **)(res + 0x168) = mem;
    az_tab[0x30] = (void *)shd_update;
    log_capture();
    fm6_UpdateSubresource(jctx, res, 0, box, d1, 0, 0);       /* [8, 12) */
    CHECK(mem[8] == 0xee && mem[11] == 0xee);                  /* memoria do jogo intacta */
    REQUIRE(d->upd[0].buf == res && d->upd[0].size == 64 && d->upd[0].dirty == 1 && d->upd[0].cur);
    v1 = d->upd[0].cur;
    CHECK(shd_ref == 1 && v1->ref == 1 && v1->size == 64);
    CHECK(v1->data[7] == 0xee && v1->data[8] == 1 && v1->data[11] == 4 && v1->data[12] == 0xee);
    CHECK(log_has("shadow of buf") && log_has("-> shadow"));
    fm6_UpdateSubresource(jctx, res, 0, box, d1, 0, 0);       /* sem versao compartilhada: altera no lugar */
    CHECK(d->upd[0].cur == v1);
    o1 = fm6_om_snapshot(jctx);                                /* so a sombra suja ja gera a copia */
    REQUIRE(o1 != NULL);
    CHECK(o1->nupd == 1 && o1->upd[0].buf == res && o1->upd[0].ver == v1 && v1->ref == 2);   /* sem copia */
    CHECK_EQ(shd_ref, 2);
    fm6_UpdateSubresource(jctx, res, 0, box, d1, 0, 0);       /* mesmo conteudo: mesma versao */
    CHECK(d->upd[0].cur == v1);
    o2 = fm6_om_snapshot(jctx);
    REQUIRE(o2 != NULL);
    CHECK(o2->upd[0].ver == v1 && v1->ref == 3);
    fm6_UpdateSubresource(jctx, res, 0, NULL, d2, 0, 0);      /* buffer inteiro, versao compartilhada: nova */
    CHECK(d->upd[0].cur != v1 && v1->data[8] == 1 && d->upd[0].cur->data[8] == 0x48 && v1->ref == 2);
    o3 = fm6_om_snapshot(jctx);
    REQUIRE(o3 != NULL);
    CHECK(o3->upd[0].ver->data[0] == 0x40 && o3->upd[0].ver->data[63] == 0x7f);
    /* imediato (fm6_om_apply): cada versao vira um UpdateSubresource do buffer inteiro; a repetida e pulada */
    fm6_upd_apply(ictx, o1);
    CHECK(shd_upd_n == 1 && shd_upd_ctx == ictx && shd_upd_res == res && shd_upd_box == NULL);
    CHECK(shd_upd_data[7] == 0xee && shd_upd_data[8] == 1 && shd_upd_data[12] == 0xee);
    fm6_upd_apply(ictx, o2);
    CHECK_EQ(shd_upd_n, 1);
    fm6_upd_apply(ictx, o3);
    CHECK(shd_upd_n == 2 && shd_upd_data[0] == 0x40 && shd_upd_data[8] == 0x48);
    fm6_upd_apply(ictx, o1);                                   /* lista reexecutada: volta a versao dela */
    CHECK(shd_upd_n == 3 && shd_upd_data[8] == 1);
    CHECK(log_has("placement buffer versions applied on the immediate"));
    fm6_om_item_release(o1);
    fm6_om_item_release(o2);
    fm6_om_item_release(o3);
    CHECK_EQ(shd_ref, 1);                                      /* so a sombra segura o buffer */
    CHECK_EQ(v1->ref, 1);                                      /* so a tabela do imediato (ultima aplicada) */
    d->upd[0].dirty = 0;                                       /* FinishCommandList: lista nova */
    CHECK(fm6_om_snapshot(jctx) == NULL);
    d->ctx = NULL;
}

/* criterio do CreatePlacementTexture2D do Azimuth (0x140121c40), espelhado p/ registrar as recusadas do FM6 */
TEST(fm6_placement_tex2d_azimuth_criteria)
{
    /*             W     H    mip arr fmt smp q  usage bind  cpu misc */
    UINT rt[11] = { 1920, 1080, 1, 1, 10, 1, 0, 0, 0x28, 0, 0 };
    UINT xg[11] = { 256, 256, 9, 6, 71, 1, 0, 0, 0x8, 0, 0 };
    CHECK(az_placement_tex2d_ok(rt));
    rt[8] = 0xa8; rt[10] = 0x40000;
    CHECK(az_placement_tex2d_ok(rt));
    rt[2] = 2;                                                 /* alvo com mips: recusado */
    CHECK(!az_placement_tex2d_ok(rt));
    CHECK(az_placement_tex2d_ok(xg));
    xg[9] = 0x10000;                                           /* CPU read */
    CHECK(!az_placement_tex2d_ok(xg));
    xg[9] = 0; xg[8] = 0x88;                                   /* SRV|UAV */
    CHECK(!az_placement_tex2d_ok(xg));
    xg[8] = 8; xg[2] = 16;
    CHECK(!az_placement_tex2d_ok(xg));
}

/* placement texture so UAV do FM6 (game+0x21d6f0) -> CreateTexture2D committed; o resto -> Azimuth 0x40 */
static int pt_tex2d_calls, pt_place_calls;
static long fake_pt_tex2d(void *dev, const UINT *desc, const void *init, void **out)
{
    (void)dev; (void)desc;
    pt_tex2d_calls++;
    CHECK(init == NULL);
    *out = (void *)0x7e7;
    return 0;
}
static long fake_pt_place(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out)
{
    (void)dev; (void)desc; (void)tile; (void)pitch; (void)addr;
    pt_place_calls++;
    *out = (void *)0x91a;
    return 0;
}

TEST(fm6_placement_uav_only_committed)
{
    UINT uav[11] = { 64, 64, 7, 1, 3, 1, 0, 0, 0x80, 0, 0 };
    UINT srv[11] = { 256, 256, 9, 1, 71, 1, 0, 0, 0x8, 0, 0 };
    void *out = NULL;
    orig_fm6_create_tex2d = fake_pt_tex2d;
    orig_fm6_placement_tex2d = fake_pt_place;
    pt_tex2d_calls = pt_place_calls = 0;
    CHECK(fm6_CreatePlacementTexture2D(NULL, uav, 14, 0, (void *)0x1024d0000, &out) == 0 && out == (void *)0x7e7);
    CHECK(pt_tex2d_calls == 1 && pt_place_calls == 0);
    CHECK(fm6_CreatePlacementTexture2D(NULL, srv, 14, 0, (void *)0x1024d0000, &out) == 0 && out == (void *)0x91a);
    CHECK(pt_tex2d_calls == 1 && pt_place_calls == 1);
    uav[9] = 0x20000;                                          /* CPU read: segue p/ o Azimuth (que para) */
    fm6_CreatePlacementTexture2D(NULL, uav, 14, 0, (void *)0x1024d0000, &out);
    CHECK(pt_tex2d_calls == 1 && pt_place_calls == 2);
    orig_fm6_create_tex2d = NULL;
    orig_fm6_placement_tex2d = NULL;
}

/* SOSetTargets do FM6 (0x26 = Azimuth 0x25, stub fatal): desligar (0, NULL, NULL) = no-op; buffer -> Azimuth */
static int so_calls;
static UINT so_n;
static void fake_so_set(void *ctx, UINT n, void *const *bufs, const UINT *offsets)
{
    (void)ctx; (void)bufs; (void)offsets;
    so_calls++;
    so_n = n;
}

TEST(fm6_so_set_targets_unbind)
{
    static void *az_tab[0x10e], *src_tab[0x10e], *tab[0x10e];
    static uint8_t ctx[0x100];
    void *nulls[2] = { NULL, NULL }, *bufs[2] = { NULL, (void *)0x77 };
    uint8_t *az = fake_azimuth(0);
    int i;
    for (i = 0; i < 0x10e; i++) src_tab[i] = (void *)(uintptr_t)(0x1000 + i);
    fm6_ctx_table(tab, src_tab, 0);
    CHECK(tab[0x26] == (void *)0x1025);                        /* Azimuth com metodo proprio: intacto */
    src_tab[0x25] = az + 0xf5f30 + 0x25 * 0x100;
    fm6_ctx_table(tab, src_tab, 0);
    CHECK(tab[0x26] == (void *)fm6_SOSetTargets);
    fm6_ctx_table(tab, src_tab, 1);
    CHECK(tab[0x26] == (void *)fm6_SOSetTargets);
    az_tab[0x25] = (void *)fake_so_set;
    *(void ***)ctx = az_tab;
    so_calls = 0;
    ((void (*)(void *, UINT, void *const *, const UINT *))tab[0x26])(ctx, 0, NULL, NULL);   /* game+0x21c61d */
    ((void (*)(void *, UINT, void *const *, const UINT *))tab[0x26])(ctx, 2, nulls, NULL);
    CHECK_EQ(so_calls, 0);
    ((void (*)(void *, UINT, void *const *, const UINT *))tab[0x26])(ctx, 2, bufs, NULL);
    CHECK(so_calls == 1 && so_n == 2);
}

/* Batches de recursos do FM6: montadores do jogo (game+0xda9b94 CB, 0xda9cac SRV) gravam o objeto no registro e o
   SetFastResourcesFromBatch (FM6 0xd7) aplica com o Set* padrao do Azimuth em [ctx] */
static int batch_calls;
static UINT batch_slot[8], batch_start[8], batch_n[8];
static void *batch_obj[8];
static void fake_batch_set(void *c, UINT s, UINT n, void *const *v, UINT slot)
{
    (void)c;
    if (batch_calls < 8) {
        batch_slot[batch_calls] = slot; batch_start[batch_calls] = s; batch_n[batch_calls] = n;
        batch_obj[batch_calls] = v[0];
    }
    batch_calls++;
}
static void fake_batch_47(void *c, UINT s, UINT n, void *const *v) { fake_batch_set(c, s, n, v, 0x47); }
static void fake_batch_3e(void *c, UINT s, UINT n, void *const *v) { fake_batch_set(c, s, n, v, 0x3e); }
static void fake_batch_43(void *c, UINT s, UINT n, void *const *v) { fake_batch_set(c, s, n, v, 0x43); }
static void fake_batch_1f(void *c, UINT s, UINT n, void *const *v) { fake_batch_set(c, s, n, v, 0x1f); }

TEST(fm6_resource_batch_records_objects)
{
    static uint8_t cb[16 * 20], srv[0x81 * 36];
    static void *az_tab[0x10e], *ctx[1] = { az_tab }, *o[8][1];
    fm6_batch b = { cb, srv, sizeof cb, sizeof srv };
    memset(cb, 0xee, sizeof cb);
    fake_azimuth(0);
    fm6_batch_hold = 0;                                       /* objetos sem vtable */
    fm6_batch_add_cb(&b, o[0], 0);
    fm6_batch_add_cb(&b, o[3], 3);
    fm6_batch_add_cb(&b, o[4], 14);                           /* estagio 1 (HS), slot 0 */
    fm6_batch_add_cb(&b, o[5], 16);                           /* fora do batch: ignorado */
    CHECK_HEX(cb, "0005baf6");                                /* CS (5) slot 0 */
    CHECK(*(void **)(cb + 4) == o[0]);
    CHECK_HEX(cb + 3 * 20, "0305baf6");
    CHECK(*(void **)(cb + 3 * 20 + 4) == o[3]);
    CHECK_HEX(cb + 14 * 20, "0001baf6");
    CHECK(*(void **)(cb + 14 * 20 + 4) == o[4]);
    fm6_batch_add_srv(&b, o[6], 7);
    fm6_batch_add_srv(&b, o[7], 0x80);                        /* estagio 1 (HS), slot 0; 0x80 * 36 < 0x81 * 36 */
    CHECK_HEX(srv + 7 * 36, "0785baf6");
    CHECK(*(void **)(srv + 7 * 36 + 4) == o[6]);
    CHECK_HEX(srv + 0x80 * 36, "0081baf6");
    CHECK(*(void **)(srv + 0x80 * 36 + 4) == o[7]);
    az_tab[0x47] = (void *)fake_batch_47;
    az_tab[0x3e] = (void *)fake_batch_3e;
    az_tab[0x43] = (void *)fake_batch_43;
    az_tab[0x1f] = (void *)fake_batch_1f;
    fm6_SetFastResourcesFromBatch(ctx, cb, 20);
    CHECK_EQ(batch_calls, 1);
    CHECK(batch_slot[0] == 0x47 && batch_start[0] == 0 && batch_n[0] == 1 && batch_obj[0] == o[0]);
    batch_calls = 0;
    fm6_batch_add_cb(&b, o[1], 1);
    fm6_batch_add_cb(&b, o[2], 2);
    memcpy(cb + 4 * 20, "\0\0\0\x80\0\0\0\x80\0\0\0\x80", 12);   /* NOPs tipo 2 ate 16 bytes */
    log_capture();
    fm6_SetFastResourcesFromBatch(ctx, cb, 4 * 20 + 12);
    CHECK_EQ(batch_calls, 4);
    CHECK(!log_has("unknown record"));
    CHECK(batch_start[3] == 3 && batch_obj[3] == o[3] && batch_obj[2] == o[2]);
    batch_calls = 0;
    fm6_SetFastResourcesFromBatch(ctx, srv + 7 * 36, 36);
    CHECK(batch_calls == 1 && batch_slot[0] == 0x43 && batch_start[0] == 7 && batch_obj[0] == o[6]);
    batch_calls = 0;
    fm6_SetFastResourcesFromBatch(ctx, cb + 14 * 20, 20);
    CHECK(batch_calls == 1 && batch_slot[0] == 0x3e && batch_start[0] == 0);
    batch_calls = 0;
    log_capture();
    fm6_SetFastResourcesFromBatch(ctx, cb + 4 * 20, 20);      /* registro nao gravado: nada */
    CHECK_EQ(batch_calls, 0);
    CHECK(log_has("unknown record 0xeeeeeeee"));
}

/* objeto com contagem de referencias (AddRef/Release nos slots 1/2) */
typedef struct { void **vt; LONG refs; } fake_ref_obj;
static ULONG fake_ref_addref(void *o) { return ++((fake_ref_obj *)o)->refs; }
static ULONG fake_ref_release(void *o) { return --((fake_ref_obj *)o)->refs; }
static void *fake_ref_vt[3] = { NULL, (void *)fake_ref_addref, (void *)fake_ref_release };

TEST(fm6_resource_batch_holds_srv_refs)
{
    static uint8_t srv[4 * 36];
    static void *az_tab[0x10e], *ctx[1] = { az_tab };
    fake_ref_obj a = { fake_ref_vt, 1 }, b2 = { fake_ref_vt, 1 };
    fm6_batch b = { NULL, srv, 0, sizeof srv };
    fake_azimuth(0);
    fm6_batch_hold = 1;
    memset(srv, 0, sizeof srv);
    fm6_batch_add_srv(&b, &a, 1);
    CHECK_EQ(a.refs, 2);                                      /* o jogo + o batch */
    fm6_batch_add_srv(&b, &a, 2);
    CHECK_EQ(a.refs, 3);                                      /* dois registros */
    memcpy(srv + 1 * 36, "оооо", 4);               /* lixo no registro velho: nao e lido */
    fm6_batch_add_srv(&b, &b2, 1);                            /* regravado: solta o anterior */
    CHECK_EQ(a.refs, 2);
    CHECK_EQ(b2.refs, 2);
    CHECK_HEX(srv + 36, "0185baf6");
    CHECK(*(void **)(srv + 36 + 4) == &b2);
    fm6_batch_add_srv(&b, NULL, 2);                           /* nulo: solta o anterior, nada a segurar */
    CHECK_EQ(a.refs, 1);
    az_tab[0x43] = (void *)fake_batch_43;
    a.refs = 1;
    fm6_batch_add_srv(&b, &a, 3);
    a.refs--;                                                 /* o jogo solta a view */
    batch_calls = 0;
    log_capture();
    fm6_SetFastResourcesFromBatch(ctx, srv + 3 * 36, 36);
    CHECK(batch_calls == 1 && batch_obj[0] == &a);
    CHECK_EQ(a.refs, 1);                                      /* a sonda devolve o que pegou */
    CHECK(log_has("already released by the game"));
    batch_calls = 0;
    log_capture();
    fm6_SetFastResourcesFromBatch(ctx, srv + 1 * 36, 36);     /* b2 ainda do jogo: sem aviso */
    CHECK(batch_calls == 1 && batch_obj[0] == &b2);
    CHECK(!log_has("already released by the game"));
    fm6_batch_hold = 1;
}

TEST(fm6_immediate_srv_hold)
{
    fake_ref_obj a = { fake_ref_vt, 1 }, b2 = { fake_ref_vt, 1 }, c = { fake_ref_vt, 1 };
    void *ctx = (void *)0x1234, *out[8], *v2[2];
    int k;
    memset(ih_ctx, 0, sizeof ih_ctx);
    ih_head = ih_tail = 0;
    k = ih_bind(ctx, 4, 3, 1, (void *[]){ &a }, 100, 50, out, 8);
    CHECK_EQ(k, 0);
    CHECK_EQ(a.refs, 2);                                      /* o jogo + o estado do imediato */
    CHECK(ih_ctx[0].ctx == ctx && ih_ctx[0].v[4][3] == &a);
    k = ih_bind(ctx, 4, 3, 1, (void *[]){ &a }, 110, 50, out, 8);
    CHECK(k == 0 && a.refs == 2);                             /* mesma view: nada muda */
    a.refs--;                                                 /* o jogo solta a view ainda ligada */
    log_capture();
    v2[0] = &b2; v2[1] = &c;
    k = ih_bind(ctx, 4, 3, 2, v2, 120, 50, out, 8);
    CHECK_EQ(k, 0);
    CHECK_EQ(a.refs, 1);                                      /* trocada: ainda na fila, nao solta */
    CHECK(b2.refs == 2 && c.refs == 2);
    CHECK(log_has("released by the game still bound on immediate"));
    k = ih_bind(ctx, 0, 0, 0, NULL, 160, 50, out, 8);         /* 40 de 50: ainda espera */
    CHECK_EQ(k, 0);
    k = ih_bind(ctx, 0, 0, 0, NULL, 170, 50, out, 8);
    CHECK(k == 1 && out[0] == &a);                            /* o chamador solta fora da trava */
    log_capture();
    k = ih_bind(ctx, 4, 3, 2, NULL, 180, 50, out, 8);         /* desliga: as duas vao para a fila */
    CHECK(k == 0 && b2.refs == 2 && c.refs == 2);
    CHECK(!log_has("still bound on immediate"));                       /* o jogo ainda as tem */
    CHECK(ih_ctx[0].v[4][3] == NULL && ih_ctx[0].v[4][4] == NULL);
    k = ih_bind(ctx, 6, 0, 1, (void *[]){ &a }, 300, 50, out, 8);   /* estagio invalido: so drena */
    CHECK(k == 2 && a.refs == 1);
}

TEST(fm6_resource_batch_builders_patched)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    void *to;
    if (!game) { test_skip(FM6_EXE " ausente"); return; }
    fake_azimuth(0);
    CHECK(fm6_batch_install(game));
    CHECK_HEX(game + 0xda9b94, "ff2500000000");
    memcpy(&to, game + 0xda9b94 + 6, 8);
    CHECK(to == (void *)fm6_batch_add_cb);
    CHECK_HEX(game + 0xda9cac, "ff2500000000");
    memcpy(&to, game + 0xda9cac + 6, 8);
    CHECK(to == (void *)fm6_batch_add_srv);
    CHECK(!fm6_batch_install(game));                          /* ja trocado: bytes nao conferem */
    pe_unmap(game);
}

/* Listas aninhadas do FM6 (game+0xf973d): ExecuteCommandList num diferido fecha a parte gravada e guarda as pecas;
   o FinishCommandList do jogo pendura as pecas na lista devolvida e o imediato executa tudo em ordem */
typedef struct { void **vt; int ref, id; uint8_t pad[0x38]; void *state; } fake_cl;   /* state em +0x48 */
static void *fake_cl_vt[3];
static fake_cl cl_pool[16];
static int cl_used, exec_n, finish_restore[8], finish_n, clear_n;
static void *exec_ctx[16], *exec_cl[16];
static BOOL exec_restore[16];
static ULONG fake_cl_addref(fake_cl *o) { return ++o->ref; }
static ULONG fake_cl_release(fake_cl *o) { return --o->ref; }
static fake_cl *new_cl(void)
{
    fake_cl *o = &cl_pool[cl_used];
    o->vt = fake_cl_vt;
    o->ref = 1;
    o->id = cl_used++;
    o->state = o;                                            /* lista comum: com estado do D3D12 */
    return o;
}
static void fake_exec(void *ctx, void *cl, BOOL restore)
{
    if (exec_n < 16) { exec_ctx[exec_n] = ctx; exec_cl[exec_n] = cl; exec_restore[exec_n] = restore; }
    exec_n++;
}
static HRESULT fake_finish(void *ctx, BOOL restore, void **out)
{
    (void)ctx;
    if (finish_n < 8) finish_restore[finish_n] = restore;
    finish_n++;
    *out = new_cl();
    return 0;
}
static void fake_clear(void *ctx) { (void)ctx; clear_n++; }

TEST(fm6_nested_command_lists)
{
    static void *az_tab[0x10e], *src_tab[0x10e], *imm[0x10e], *def[0x10e];
    static uint8_t dctx[0xa000], d2ctx[0xa000], ictx[0xa000];
    typedef void (*exec_fn)(void *, void *, BOOL);
    typedef HRESULT (*fin_fn)(void *, BOOL, void **);
    fake_cl *w1, *w2, *p1, *p2, *f, *g, *q;
    void *out = NULL;
    int i;
    fake_azimuth(0);
    for (i = 0; i < 0x10e; i++) src_tab[i] = (void *)(uintptr_t)(0x1000 + i);
    fm6_ctx_table(imm, src_tab, 0);
    fm6_ctx_table(def, src_tab, 1);
    CHECK(imm[0x3b] == (void *)fm6_ExecuteCommandList && def[0x3b] == (void *)fm6_ExecuteCommandList);
    CHECK(def[0x73] == (void *)fm6_FinishCommandList && imm[0x73] == (void *)0x1072);
    CHECK(imm[0x3a] == (void *)0x1039 && def[0x74] == (void *)0x1073);
    fake_cl_vt[1] = (void *)fake_cl_addref;
    fake_cl_vt[2] = (void *)fake_cl_release;
    az_tab[0x3a] = (void *)fake_exec;
    az_tab[0x6e] = (void *)fake_clear;
    az_tab[0x72] = (void *)fake_finish;
    *(void ***)dctx = *(void ***)d2ctx = *(void ***)ictx = az_tab;
    dctx[0xc48] = d2ctx[0xc48] = 1;
    w1 = new_cl();
    w2 = new_cl();
    *(void **)(dctx + AZ_CTX_D3D12_LIST) = *(void **)(d2ctx + AZ_CTX_D3D12_LIST) = dctx;   /* algo gravado */
    ((exec_fn)def[0x3b])(dctx, w1, FALSE);                   /* parte p1 + w1 */
    ((exec_fn)def[0x3b])(dctx, w2, TRUE);                    /* parte p2 + w2, estado mantido */
    CHECK_EQ(exec_n, 0);
    CHECK(finish_n == 2 && finish_restore[0] == TRUE && finish_restore[1] == TRUE);
    CHECK_EQ(clear_n, 0);                                    /* restore FALSE no aninhado nao limpa (PASSO 43) */
    CHECK(w1->ref == 2 && w2->ref == 2);
    p1 = &cl_pool[2];
    p2 = &cl_pool[3];
    CHECK(((fin_fn)def[0x73])(dctx, FALSE, &out) == 0);
    f = out;
    CHECK(f == &cl_pool[4] && finish_restore[2] == FALSE);
    CHECK(fake_cl_vt[2] == (void *)fm6_cl_Release);          /* Release da lista trocado */
    ((exec_fn)imm[0x3b])(ictx, f, FALSE);
    CHECK_EQ(exec_n, 5);
    CHECK(exec_cl[0] == p1 && exec_cl[1] == w1 && exec_cl[2] == p2 && exec_cl[3] == w2 && exec_cl[4] == f);
    CHECK(exec_ctx[0] == ictx && exec_ctx[4] == ictx && !exec_restore[0] && !exec_restore[4]);
    CHECK(p1->ref == 1 && w1->ref == 2);                     /* referencias do imediato ja soltas */
    exec_n = 0;
    {   /* f dentro de outro diferido: as pecas de f entram achatadas */
        ((exec_fn)def[0x3b])(d2ctx, f, FALSE);
        CHECK(((fin_fn)def[0x73])(d2ctx, FALSE, &out) == 0);
        q = &cl_pool[5];
        g = out;
        CHECK(g == &cl_pool[6]);
        ((exec_fn)imm[0x3b])(ictx, g, TRUE);
        CHECK_EQ(exec_n, 7);
        CHECK(exec_cl[0] == q && exec_cl[1] == p1 && exec_cl[4] == w2 && exec_cl[5] == f && exec_cl[6] == g);
        CHECK(exec_restore[6] == TRUE);
        CHECK(f->ref == 2 && p1->ref == 2);
    }
    /* Release ate 0 solta as pecas */
    CHECK_EQ(((ULONG (*)(void *))fake_cl_vt[2])(f), 1);      /* jogo solta f; g ainda o segura */
    CHECK(p1->ref == 2 && w1->ref == 3);
    CHECK_EQ(((ULONG (*)(void *))fake_cl_vt[2])(g), 0);      /* g solta as suas pecas, f chega a 0 e solta as dele */
    CHECK(f->ref == 0 && p1->ref == 0 && p2->ref == 0 && q->ref == 0 && w1->ref == 1 && w2->ref == 1);
    exec_n = 0;
    ((exec_fn)imm[0x3b])(ictx, w1, FALSE);                   /* lista comum: so ela */
    CHECK(exec_n == 1 && exec_cl[0] == w1);
    exec_n = 0;
    d2ctx[0xc49] = 1;                                        /* bundle: Azimuth */
    ((exec_fn)def[0x3b])(d2ctx, w1, FALSE);
    CHECK(exec_n == 1 && exec_ctx[0] == d2ctx && w1->ref == 1);
    d2ctx[0xc49] = 0;
    {   /* nada gravado antes do aninhado (sem command list aberta): sem Finish, sem parte */
        int fin = finish_n;
        exec_n = 0;
        *(void **)(d2ctx + AZ_CTX_D3D12_LIST) = NULL;
        ((exec_fn)def[0x3b])(d2ctx, w1, FALSE);
        ((exec_fn)def[0x3b])(d2ctx, w2, FALSE);
        CHECK_EQ(finish_n, fin);
        CHECK(w1->ref == 2 && w2->ref == 2);
        CHECK(((fin_fn)def[0x73])(d2ctx, FALSE, &out) == 0);
        ((exec_fn)imm[0x3b])(ictx, out, FALSE);
        CHECK(exec_n == 3 && exec_cl[0] == w1 && exec_cl[1] == w2 && exec_cl[2] == out);
        CHECK(log_has("with nothing recorded before: no part"));
    }
}

/* heranca do OM no aninhado (17_fm6.txt PASSO 40/41): o bundle do worker aninhado no juntador executa no imediato
   com o RT/DS, viewports e scissors que o juntador tinha ligado; flags/chamador dos diferidos no log */
static uint8_t inh_ctx[3][0xa000];
static int inh_made, inh_om_n, inh_clear_n, inh_vp_n, inh_sc_n;
static UINT inh_om_rts;
static void *inh_om_ctx, *inh_om_rtv0, *inh_om_dsv;
typedef struct { void **vt; int ref; } inh_view;
static ULONG inh_view_addref(inh_view *v) { return ++v->ref; }
static ULONG inh_view_release(inh_view *v) { return --v->ref; }
static void *inh_view_vt[3] = { NULL, (void *)inh_view_addref, (void *)inh_view_release };
static long inh_defctx(void *dev, UINT flags, void **out) { (void)dev; (void)flags; *out = inh_ctx[inh_made++ % 2]; return 0; }
static void inh_om(void *c, UINT n, void *const *r, void *d)
{
    inh_om_n++;
    inh_om_ctx = c;
    inh_om_rts = n;
    inh_om_rtv0 = n && r ? r[0] : NULL;
    inh_om_dsv = d;
}
static void inh_vp(void *c, UINT n, const float *v) { (void)c; (void)v; inh_vp_n += (int)n; }
static void inh_sc(void *c, UINT n, const RECT *r) { (void)c; (void)r; inh_sc_n += (int)n; }
static void inh_clear(void *c) { (void)c; inh_clear_n++; }
static int inh_res_n, inh_remap_n;
static UINT inh_res_slot, inh_res_start, inh_res_cnt;
static void *inh_res_ctx, *inh_res_last;
static void inh_res(void *c, UINT s, UINT n, void *const *v, UINT slot)
{
    inh_res_n++;
    inh_res_ctx = c; inh_res_slot = slot; inh_res_start = s; inh_res_cnt = n; inh_res_last = v[n - 1];
}
static void inh_res_47(void *c, UINT s, UINT n, void *const *v) { inh_res(c, s, n, v, 0x47); }
static void inh_res_43(void *c, UINT s, UINT n, void *const *v) { inh_res(c, s, n, v, 0x43); }
static void inh_res_09(void *c, UINT s, UINT n, void *const *v) { inh_res(c, s, n, v, 0x09); }
static void inh_res_11(void *c, UINT s, UINT n, void *const *v) { inh_res(c, s, n, v, 0x11); }
static const uint8_t *fr_p, *fr_end;
static void *fr_ctx;
static int fr_n;
static void fake_fastres(void *c, const uint8_t *p, const uint8_t *end, void *a4)
{
    (void)a4;
    fr_ctx = c; fr_p = p; fr_end = end; fr_n++;
}
static void inh_remap(void *c, UINT ds, UINT dslot, UINT ss, UINT sslot)
{
    (void)c;
    if (ds == 4 && dslot == 0 && ss == 5 && sslot == 3) inh_remap_n++;
}
static int inh_bs_n, inh_ds_n, inh_rs_n;
static void *inh_bs_ctx, *inh_bs, *inh_ds, *inh_rs;
static UINT inh_ds_ref, inh_bs_mask;
static float inh_bs_f0;
static void inh_bs_fn(void *c, void *s, const float *f, UINT m)
{
    inh_bs_n++; inh_bs_ctx = c; inh_bs = s; inh_bs_mask = m; inh_bs_f0 = f ? f[0] : -1.0f;
}
static void inh_ds_fn(void *c, void *s, UINT r) { (void)c; inh_ds_n++; inh_ds = s; inh_ds_ref = r; }
static void inh_rs_fn(void *c, void *s) { (void)c; inh_rs_n++; inh_rs = s; }
static int inh_om_uav_n;
static void inh_om_uav(void *c, UINT n, void *const *r, void *d, UINT s, UINT nu, void *const *u, const UINT *k)
{
    (void)c; (void)n; (void)r; (void)d; (void)s; (void)nu; (void)u; (void)k;
    inh_om_uav_n++;
}

TEST(fm6_nested_bundle_inherits_om)
{
    static void *dev_vt[0x100], *src_tab[0x10e], *imm[0x10e], *def[0x10e], *az_tab[0x10e];
    typedef long (*mk_fn)(void *, UINT, void **);
    typedef void (*om_fn)(void *, UINT, void *const *, void *);
    typedef void (*omu_fn)(void *, UINT, void *const *, void *, UINT, UINT, void *const *, const UINT *);
    typedef void (*vp_fn)(void *, UINT, const float *);
    typedef void (*sc_fn)(void *, UINT, const RECT *);
    typedef void (*exec_fn)(void *, void *, BOOL);
    typedef HRESULT (*fin_fn)(void *, BOOL, void **);
    static inh_view rtv = { inh_view_vt, 1 }, dsv = { inh_view_vt, 1 }, cbv = { inh_view_vt, 1 }, srvv = { inh_view_vt, 1 };
    static inh_view bsv = { inh_view_vt, 1 }, dssv = { inh_view_vt, 1 }, rsv = { inh_view_vt, 1 };
    static inh_view fcb = { inh_view_vt, 1 };
    static uint8_t bcb[14 * 20], bsrv[4 * 36];
    fm6_batch bt = { bcb, bsrv, sizeof bcb, sizeof bsrv };
    void *j = NULL, *w = NULL, *out = NULL, *rtvs[1] = { &rtv };
    float vp[6] = { 0, 0, 1920, 1080, 0, 1 };
    RECT sc = { 0, 0, 1920, 1080 };
    fake_cl *b, *b2, *part, *f;
    int i;
    fake_azimuth(0);
    fm6_batch_hold = 0;                     /* aqui so a heranca: o batch nao segura referencia */
    for (i = 0; i < 0x100; i++) dev_vt[i] = (void *)(uintptr_t)(0x2000 + i);
    dev_vt[0x1b] = dev_vt[0x2d] = dev_vt[0x44] = (void *)inh_defctx;
    fm6_device_fixups(dev_vt);
    CHECK(dev_vt[0x1b] == (void *)fm6_defctx0 && dev_vt[0x2d] == (void *)fm6_defctx1 && dev_vt[0x44] == (void *)fm6_defctxx);
    for (i = 0; i < 0x10e; i++) src_tab[i] = (void *)(uintptr_t)(0x1000 + i);
    fm6_ctx_table(imm, src_tab, 0);
    fm6_ctx_table(def, src_tab, 1);
    CHECK(def[0x22] == (void *)fm6_def_OMSetRenderTargets && def[0x23] == (void *)fm6_def_OMSetRenderTargetsUAV);
    CHECK(def[0x2d] == (void *)fm6_def_RSSetViewports && def[0x2e] == (void *)fm6_def_RSSetScissorRects);
    CHECK(def[0x6f] == (void *)fm6_def_ClearState);
    CHECK(def[0x24] == (void *)fm6_def_OMSetBlendState && def[0x25] == (void *)fm6_def_OMSetDepthStencilState);
    CHECK(def[0x2c] == (void *)fm6_def_RSSetState);
    CHECK(imm[0x24] == (void *)0x1023 && imm[0x25] == (void *)0x1024 && imm[0x2c] == (void *)0x102b);
    CHECK(def[0x9f] == (void *)fm6_def_RemapCB && def[0xa0] == (void *)fm6_def_RemapSRV);
    CHECK(imm[0x2d] == (void *)0x102c && imm[0x2e] == (void *)0x102d);   /* imediato: Azimuth */
    CHECK(imm[0x22] == (void *)fm6_def_OMSetRenderTargets && imm[0x6f] == (void *)fm6_def_ClearState);   /* aliasing */
    /* o Azimuth em [ctx] (sem registro da copia embutida nos testes) */
    az_tab[0x21] = (void *)inh_om;
    az_tab[0x22] = (void *)inh_om_uav;
    az_tab[0x2c] = (void *)inh_vp;
    az_tab[0x2d] = (void *)inh_sc;
    az_tab[0x23] = (void *)inh_bs_fn;
    az_tab[0x24] = (void *)inh_ds_fn;
    az_tab[0x2b] = (void *)inh_rs_fn;
    az_tab[0x6e] = (void *)inh_clear;
    az_tab[0x47] = (void *)inh_res_47;                                   /* CS CB */
    az_tab[0x43] = (void *)inh_res_43;                                   /* CS SRV */
    az_tab[0x9d] = (void *)inh_remap;
    az_tab[0x3a] = (void *)fake_exec;
    az_tab[0x72] = (void *)fake_finish;
    fake_cl_vt[1] = (void *)fake_cl_addref;
    if (fake_cl_vt[2] != (void *)fm6_cl_Release) fake_cl_vt[2] = (void *)fake_cl_release;
    for (i = 0; i < 3; i++) *(void ***)inh_ctx[i] = az_tab;
    inh_ctx[0][0xc48] = inh_ctx[1][0xc48] = 1;
    log_capture();
    CHECK(((mk_fn)dev_vt[0x1b])(NULL, 0, &j) == 0 && j == inh_ctx[0]);
    CHECK(((mk_fn)dev_vt[0x44])(NULL, 0x60000, &w) == 0 && w == inh_ctx[1]);
    CHECK(log_has("FM6 CreateDeferredContext flags=0 -> deferred d"));
    CHECK(log_has("FM6 CreateDeferredContextX flags=0x60000 -> deferred d"));
    /* juntador liga RT/DS, viewport e scissor (repassados ao Azimuth) */
    ((om_fn)def[0x22])(j, 1, rtvs, &dsv);
    ((vp_fn)def[0x2d])(j, 1, vp);
    ((sc_fn)def[0x2e])(j, 1, &sc);
    ((omu_fn)def[0x23])(j, FM6_KEEP_RTS, NULL, NULL, 0, 0, NULL, NULL);   /* KEEP: mantem */
    CHECK(inh_om_n == 1 && inh_om_ctx == j && inh_vp_n == 1 && inh_sc_n == 1 && inh_om_uav_n == 1);
    CHECK(fm6_dctx_find(j)->om == 1 && fm6_dctx_find(j)->st.nrtv == 1 && fm6_dctx_find(j)->st.dsv == &dsv);
    /* juntador liga depth-stencil (reversed-Z, ref 3), blend (fator nulo = 1) e rasterizer (PASSO 45) */
    {
        static const float bf[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        ((void (*)(void *, void *, const float *, UINT))def[0x24])(j, &bsv, bf, 0xff);
        ((void (*)(void *, void *, const float *, UINT))def[0x24])(j, &bsv, NULL, 0xffffffffu);
        ((void (*)(void *, void *, UINT))def[0x25])(j, &dssv, 3);
        ((void (*)(void *, void *))def[0x2c])(j, &rsv);
        CHECK(inh_bs_n == 2 && inh_bs_ctx == j && inh_ds_n == 1 && inh_ds_ref == 3 && inh_rs_n == 1);
        CHECK(fm6_dctx_find(j)->st.has == 7 && fm6_dctx_find(j)->st.blend_factor[0] == 1.0f);
        inh_bs_n = inh_ds_n = inh_rs_n = 0;
    }
    /* juntador estaciona CB (CS slot 2) e SRV (CS slot 3) pelo batch; o bundle le com Remap (PS 0 <- CS 3) */
    fm6_batch_add_cb(&bt, &cbv, 2);
    fm6_batch_add_srv(&bt, &srvv, 3);
    fm6_SetFastResourcesFromBatch(j, bcb + 2 * 20, 20);
    fm6_SetFastResourcesFromBatch(j, bsrv + 3 * 36, 36);
    CHECK(inh_res_n == 2 && inh_res_ctx == j);
    CHECK(fm6_dctx_find(j)->st.ncb[5] == 3 && fm6_dctx_find(j)->st.nsrv[5] == 4 && fm6_dctx_find(j)->st.srv[5][3] == &srvv);
    ((void (*)(void *, UINT, UINT, UINT, UINT))def[0xa0])(w, 4, 0, 5, 3);
    CHECK_EQ(inh_remap_n, 1);
    CHECK(log_has("RemapShaderResourceInheritance #1 on deferred d"));
    /* SetFastResources do XDK (FM6 0xd4, PASSO 50): CB do PS slot 4 (tipo 1) e sampler (tipo 2) -> anota so o CB e
       repassa (inicio, fim) ao Azimuth 0xd1 */
    {
        static uint8_t pk[2 * 20];
        uint32_t h1 = 0x90040401u, h2 = 0xa0040001u;
        void *fo = &fcb, *smp = (void *)0x5a3;
        memset(pk, 0, sizeof pk);
        memcpy(pk, &h1, 4); memcpy(pk + 4, &fo, 8);
        memcpy(pk + 20, &h2, 4); memcpy(pk + 24, &smp, 8);
        CHECK(def[0xd4] == (void *)fm6_SetFastResources && imm[0xd4] == (void *)fm6_SetFastResources);
        az_tab[0xd1] = (void *)fake_fastres;
        az_tab[0x11] = (void *)inh_res_11;                               /* PS CB */
        ((void (*)(void *, const uint8_t *, const uint8_t *, void *))def[0xd4])(j, pk, pk + sizeof pk, NULL);
        CHECK(fr_n == 1 && fr_ctx == j && fr_p == pk && fr_end == pk + sizeof pk);
        CHECK(fm6_dctx_find(j)->st.cb[4][4] == &fcb && fm6_dctx_find(j)->st.ncb[4] == 5);
        CHECK(fm6_dctx_find(j)->st.cbset[4][0] == 1u << 4 && fm6_dctx_find(j)->st.nsrv[4] == 0);
    }
    /* bundle do worker (sem estado do D3D12) aninhado: parte + copia do estado + bundle */
    b = new_cl();
    b->state = NULL;
    *(void **)(inh_ctx[0] + AZ_CTX_D3D12_LIST) = inh_ctx[0];             /* algo gravado */
    exec_n = 0;
    ((exec_fn)def[0x3b])(j, b, FALSE);
    part = &cl_pool[cl_used - 1];
    CHECK(rtv.ref == 2 && dsv.ref == 2 && cbv.ref == 2 && srvv.ref == 2 && fcb.ref == 2);   /* views seguras */
    CHECK(bsv.ref == 2 && dssv.ref == 2 && rsv.ref == 2);
    CHECK(log_has("<- bundle"));
    CHECK(fm6_dctx_find(j)->st.nrtv == 1 && fm6_dctx_find(j)->st.nsrv[5] == 4);   /* restore FALSE: juntador mantem */
    /* PS SRV pelo Set* padrao (forma empacotada do FM6: slot 2, 1 view) tambem entra */
    {
        void *ps[1] = { &srvv };
        az_tab[0x09] = (void *)inh_res_09;
        ((void (*)(void *, void *const *, UINT, UINT))def[0x0a])(j, ps, 2, 0);
        CHECK(fm6_dctx_find(j)->st.nsrv[4] == 3 && fm6_dctx_find(j)->st.srv[4][2] == &srvv);
    }
    /* outro bundle aninhado em seguida herda o mesmo estado (o FM6 nao religa o OM entre os aninhados) */
    b2 = new_cl();
    b2->state = NULL;
    *(void **)(inh_ctx[0] + AZ_CTX_D3D12_LIST) = NULL;
    ((exec_fn)def[0x3b])(j, b2, FALSE);
    CHECK(rtv.ref == 3 && srvv.ref == 4);
    CHECK(((fin_fn)def[0x73])(j, FALSE, &out) == 0);
    f = out;
    inh_om_n = inh_vp_n = inh_sc_n = inh_res_n = 0;
    ((exec_fn)imm[0x3b])(inh_ctx[2], f, FALSE);
    /* no imediato: so os slots gravados pelo juntador (PS CB 4, CS CB 2, CS SRV 3; o 2o tambem PS SRV 2), em faixas;
       os nao gravados ficam como o imediato tem (PASSO 50) */
    CHECK(inh_res_n == 3 + 4 && inh_res_ctx == inh_ctx[2] && inh_res_slot == 0x43 && inh_res_start == 3);
    CHECK(inh_res_cnt == 1 && inh_res_last == &srvv);
    CHECK(log_has("1 viewports, 1 scissors, 2 CB slots and 1 SRV slots parked"));
    CHECK(log_has("stage 4:----x stage 5:--x"));
    CHECK_EQ(exec_n, 4);
    CHECK(exec_cl[0] == part && exec_cl[1] == b && exec_cl[2] == b2 && exec_cl[3] == f);
    CHECK(inh_om_n == 2 && inh_om_ctx == inh_ctx[2] && inh_om_rts == 1 && inh_om_rtv0 == &rtv && inh_om_dsv == &dsv);
    CHECK(inh_vp_n == 2 && inh_sc_n == 2);
    CHECK(inh_bs_n == 2 && inh_bs_ctx == inh_ctx[2] && inh_bs == &bsv && inh_bs_mask == 0xffffffffu && inh_bs_f0 == 1.0f);
    CHECK(inh_ds_n == 2 && inh_ds == &dssv && inh_ds_ref == 3 && inh_rs_n == 2 && inh_rs == &rsv);
    CHECK(log_has("nested bundle runs with the joiner's OM (1 RTs"));
    CHECK(log_has("CBs of the copy on immediate"));   /* PASSO 49 */
    CHECK(log_has("stage 4 slot 0 <- stage 5 slot 3"));
    CHECK(log_has("depth ") && log_has(" ref 3, rs "));
    CHECK(rtv.ref == 3);                                                 /* f ainda segura as copias */
    CHECK_EQ(((ULONG (*)(void *))fake_cl_vt[2])(f), 0);
    CHECK(rtv.ref == 1 && dsv.ref == 1 && cbv.ref == 1 && srvv.ref == 1 && fcb.ref == 1);
    CHECK(bsv.ref == 1 && dssv.ref == 1 && rsv.ref == 1);
    /* ClearState do jogo no juntador zera o acompanhado */
    ((om_fn)def[0x22])(j, 1, rtvs, NULL);
    ((void (*)(void *))def[0x6f])(j);
    CHECK(inh_clear_n >= 1 && fm6_dctx_find(j)->st.nrtv == 0 && fm6_dctx_find(j)->st.has == 0);
    /* lista comum (com estado do D3D12) aninhada: sem copia */
    ((om_fn)def[0x22])(j, 1, rtvs, NULL);
    b = new_cl();
    exec_n = 0;
    ((exec_fn)def[0x3b])(j, b, TRUE);
    CHECK(rtv.ref == 1);
    ((fin_fn)def[0x73])(j, FALSE, &out);
    fm6_batch_hold = 1;
}

/* aliasing da ESRAM no FM6 (17_fm6.txt PASSO 44): cena em cor 2x + prof a; o compute (W/8, H/8, 1) le a prof b e o
   pos le a cor 1x -> copia no Dispatch e resolve quando o mesmo contexto liga outro RT (ou ClearState) */
static int al6_copy_n, al6_resolve_n, al6_disp_n;
static void *al6_a[4];
static UINT al6_fmt;
static void al6_copy(void *c, void *dst, void *src) { al6_copy_n++; al6_a[0] = c; al6_a[1] = dst; al6_a[2] = src; }
static void al6_resolve(void *c, void *dst, UINT ds, void *src, UINT ss, UINT fmt)
{
    (void)ds; (void)ss;
    al6_resolve_n++; al6_a[0] = c; al6_a[1] = dst; al6_a[2] = src; al6_fmt = fmt;
}
static void al6_dispatch(void *c, UINT x, UINT y, UINT z) { (void)c; (void)x; (void)y; (void)z; al6_disp_n++; }
static void al6_om(void *c, UINT n, void *const *r, void *d) { (void)c; (void)n; (void)r; (void)d; }
static void al6_clear(void *c) { (void)c; }
static long al6_tex(void *dev, const UINT *desc, const void *init, void **out)
{
    (void)dev; (void)desc; (void)init;
    *out = fake_obj();
    return 0;
}
static long al6_rtv(void *dev, void *res, const UINT *desc, void **out) { (void)dev; (void)res; (void)desc; *out = fake_obj(); return 0; }

TEST(fm6_esram_alias_copy_and_resolve)
{
    static void *dev_vt[0x100], *src_tab[0x10e], *imm[0x10e], *az_tab[0x10e];
    static void *ctx[1] = { az_tab }, *ctx2[1] = { az_tab };
    typedef long (*mk_tex_fn)(void *, const UINT *, const void *, void **);
    typedef long (*mk_rtv_fn)(void *, void *, const UINT *, void **);
    typedef void (*om_fn)(void *, UINT, void *const *, void *);
    typedef void (*disp_fn)(void *, UINT, UINT, UINT);
    UINT c2[12] = { 1920, 1080, 1, 1, 26, 2, 2, 0, 0x28, 0, 0x40000, 0 };
    UINT dp[12] = { 1920, 1080, 1, 1, 19, 2, 2, 0, 0x48, 0, 0x60000, 0 };
    UINT c1[12] = { 1920, 1080, 1, 1, 26, 1, 0, 0, 0xa8, 0, 0, 0 };
    void *tc2, *ta, *tb, *tc1, *rs, *ro, *rtvs[1];
    int i;
    fake_azimuth(0);
    for (i = 0; i < 0x100; i++) dev_vt[i] = (void *)(uintptr_t)(0x2000 + i);
    dev_vt[5] = (void *)al6_tex;
    dev_vt[9] = (void *)al6_rtv;
    fm6_device_fixups(dev_vt);
    CHECK(dev_vt[5] == (void *)fm6_CreateTexture2D && dev_vt[9] == (void *)fm6_CreateRenderTargetView);
    for (i = 0; i < 0x10e; i++) src_tab[i] = (void *)(uintptr_t)(0x1000 + i);
    fm6_ctx_table(imm, src_tab, 0);
    CHECK(imm[0x2a] == (void *)fm6_Dispatch && imm[0x23] == (void *)fm6_def_OMSetRenderTargetsUAV);
    az_tab[0x21] = (void *)al6_om;
    az_tab[0x29] = (void *)al6_dispatch;
    az_tab[0x2f] = (void *)al6_copy;
    az_tab[0x39] = (void *)al6_resolve;
    az_tab[0x6e] = (void *)al6_clear;
    log_capture();
    alias_test = 1;
    ((mk_tex_fn)dev_vt[5])(NULL, c2, NULL, &tc2);
    ((mk_tex_fn)dev_vt[5])(NULL, dp, NULL, &ta);
    ((mk_tex_fn)dev_vt[5])(NULL, dp, NULL, &tb);
    ((mk_tex_fn)dev_vt[5])(NULL, c1, NULL, &tc1);                /* 1x sem ESRAM: vale no FM6 */
    CHECK(al_color2x == tc2 && al_depth_a == ta && al_depth_b == tb && al_color1x == tc1);
    ((mk_rtv_fn)dev_vt[9])(NULL, tc2, NULL, &rs);
    ((mk_rtv_fn)dev_vt[9])(NULL, tc1, NULL, &ro);
    CHECK(alias_is_scene_rtv(rs) && !alias_is_scene_rtv(ro));
    /* Dispatch de outro tamanho: nada; 240x135x1: copia b <- a e repassa */
    ((disp_fn)imm[0x2a])(ctx, 22, 1, 1);
    CHECK(al6_copy_n == 0 && al6_disp_n == 1);
    ((disp_fn)imm[0x2a])(ctx, 240, 135, 1);
    CHECK(al6_copy_n == 1 && al6_disp_n == 2 && al6_a[0] == ctx && al6_a[1] == tb && al6_a[2] == ta);
    CHECK(log_has("alias: FM6 Dispatch 240x135x1"));
    /* ainda na cena (RT da cor 2x), KEEP ou outro contexto: sem resolve */
    rtvs[0] = rs;
    ((om_fn)imm[0x22])(ctx, 1, rtvs, NULL);
    ((om_fn)imm[0x22])(ctx, 0, NULL, NULL);
    rtvs[0] = ro;
    ((om_fn)imm[0x22])(ctx2, 1, rtvs, NULL);
    CHECK_EQ(al6_resolve_n, 0);
    /* sai da cena: resolve cor 1x <- cor 2x (R11G11B10) uma vez */
    ((om_fn)imm[0x22])(ctx, 1, rtvs, NULL);
    CHECK(al6_resolve_n == 1 && al6_a[0] == ctx && al6_a[1] == tc1 && al6_a[2] == tc2 && al6_fmt == 26);
    ((om_fn)imm[0x22])(ctx, 1, rtvs, NULL);
    CHECK_EQ(al6_resolve_n, 1);
    /* ClearState tambem fecha a cena */
    ((disp_fn)imm[0x2a])(ctx, 240, 135, 1);
    ((void (*)(void *))imm[0x6f])(ctx);
    CHECK(al6_resolve_n == 2 && log_has("left the scene (ClearState)"));
    alias_test = 0;
}

/* reexecucao de lista com a execucao anterior ainda na GPU: submete o lote se ela nao saiu e espera a fence */
static int rx_submits, rx_waits, rx_orig;
static uint64_t rx_wait_value;
static uint8_t *rx_fence;
static void rx_submit(uint8_t *ctx, int why, int flag) { (void)ctx; CHECK(why == 0x10 && flag == 0); rx_submits++; }
static void rx_wait(uint8_t *fence, uint64_t v) { rx_waits++; rx_wait_value = v; *(uint64_t *)(fence + AZ_FENCE_DONE) = v; }
static void rx_apply(uint8_t *ctx, uint8_t *cl, int restore, void *ret)
{
    (void)ctx; (void)restore; (void)ret;
    rx_orig++;
    *(uint64_t *)(*(uint8_t **)(cl + AZ_LIST_STATE) + AZ_STATE_LAST_FENCE) = *(uint64_t *)(rx_fence + AZ_FENCE_SUBMITTED) + 1;
}

TEST(fm6_reexecuted_list_waits_for_gpu)
{
    static uint8_t dev[0x1000], ctx[0xd00], cl[0x60], st[0x300];
    rx_fence = dev + AZ_DEV_FENCE;
    *(uint8_t **)(ctx + AZ_CTX_DEVICE) = dev;
    *(uint8_t **)(cl + AZ_LIST_STATE) = st;
    az_submit = rx_submit; az_fence_wait = rx_wait; orig_apply_execute = rx_apply;
    *(uint64_t *)(rx_fence + AZ_FENCE_SUBMITTED) = 10;
    *(uint64_t *)(rx_fence + AZ_FENCE_DONE) = 9;
    fm6_apply_execute(ctx, cl, 0, NULL);                     /* 1a execucao: nada a esperar */
    CHECK(rx_orig == 1 && rx_submits == 0 && rx_waits == 0);
    CHECK_EQ(*(uint64_t *)(st + AZ_STATE_LAST_FENCE), 11);
    fm6_apply_execute(ctx, cl, 0, NULL);                     /* mesma lista no mesmo lote: submete e espera 11 */
    CHECK(rx_orig == 2 && rx_submits == 1 && rx_waits == 1 && rx_wait_value == 11);
    *(uint64_t *)(rx_fence + AZ_FENCE_SUBMITTED) = 12;       /* lote saiu, GPU ainda no 11 */
    *(uint64_t *)(rx_fence + AZ_FENCE_DONE) = 11;
    fm6_apply_execute(ctx, cl, 0, NULL);                     /* anterior em 11... */
    *(uint64_t *)(rx_fence + AZ_FENCE_SUBMITTED) = 13;
    fm6_apply_execute(ctx, cl, 0, NULL);                     /* ...agora em 13, ja submetida: so espera */
    CHECK(rx_orig == 4 && rx_submits == 1 && rx_waits == 2 && rx_wait_value == 13);
    CHECK(log_has("re-executed before the GPU finished"));
    *(uint64_t *)(rx_fence + AZ_FENCE_DONE) = 20;
    fm6_apply_execute(ctx, cl, 0, NULL);                     /* GPU ja passou: direto */
    CHECK(rx_orig == 5 && rx_waits == 2);
    orig_apply_execute = NULL; az_submit = NULL; az_fence_wait = NULL;
}

/* UiTrace: cada viewport/scissor distinto (chamador + valores) entra no log uma vez */
TEST(fm6_ui_trace_logs_distinct_rects)
{
    static const float vp[6] = { 0, 0, 1920, 1080, 0, 1 };
    static const LONG sc[4] = { 10, 20, 300, 400 };
    const char *t, *a;
    log_capture();
    fm6_rs_trace(0, 0x1234, 1, vp);
    fm6_rs_trace(0, 0x1234, 1, vp);
    fm6_rs_trace(1, 0x1234, 1, sc);
    fm6_rs_trace(1, 0x1234, 0, sc);              /* n = 0: nada */
    t = log_text();
    a = strstr(t, "d3d: UiTrace: viewport n=1 0,0 1920x1080");
    CHECK(a && !strstr(a + 1, "d3d: UiTrace: viewport"));
    CHECK(strstr(t, "d3d: UiTrace: scissor n=1 10,20 - 300,400") != NULL);
}


/* tabela de samplers: bloco do Azimuth completado ate a classe (4/8/16) com o sampler nulo do device */
static uint64_t sb_seen[16];
static UINT sb_n, sb_calls;
static void *sb_orig(uint8_t *alloc, void *out, void *key, const uint64_t *handles, UINT n, uint64_t fence, uint8_t pin)
{
    UINT i;
    (void)alloc; (void)key; (void)fence; (void)pin;
    sb_n = n; sb_calls++;
    for (i = 0; i < n && i < 16; i++) sb_seen[i] = handles[i];
    return out;
}

TEST(sampler_table_block_padded_with_null_sampler)
{
    static const uint32_t classes[9] = { 4, 0, 64, 8, 64, 128, 16, 192, 48 };
    static uint8_t dev[0x7000];
    uint8_t *az = fake_azimuth(1), *alloc = dev + 0x2420;
    uint64_t h[9] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800, 0x900 };
    int out;
    log_capture();
    memcpy(az + RVA_SAMPLER_CLASSES, classes, sizeof classes);
    *(uint64_t *)(dev + 0x6958) = 0xabc0;            /* sampler nulo do device (handle de CPU) */
    orig_sampler_block = sb_orig;
    sb_calls = 0;
    /* 3 handles -> bloco de 4: o 4o slot e o sampler nulo */
    CHECK(hook_sampler_block(alloc, &out, NULL, h, 3, 7, 0) == &out);
    CHECK(sb_calls == 1 && sb_n == 4);
    CHECK(sb_seen[0] == 0x100 && sb_seen[1] == 0x200 && sb_seen[2] == 0x300 && sb_seen[3] == 0xabc0);
    /* ja no tamanho da classe: repassa */
    hook_sampler_block(alloc, &out, NULL, h, 4, 7, 0);
    CHECK(sb_n == 4 && sb_seen[3] == 0x400);
    /* 5 -> classe de 8 */
    hook_sampler_block(alloc, &out, NULL, h, 5, 7, 0);
    CHECK(sb_n == 8 && sb_seen[4] == 0x500 && sb_seen[5] == 0xabc0 && sb_seen[7] == 0xabc0);
    /* 9 -> 16 */
    hook_sampler_block(alloc, &out, NULL, h, 9, 7, 0);
    CHECK(sb_n == 16 && sb_seen[8] == 0x900 && sb_seen[15] == 0xabc0);
    CHECK(log_has("d3d: sampler table block 3 -> 4 slots"));
    /* sem sampler nulo no device: repassa como esta */
    *(uint64_t *)(dev + 0x6958) = 0;
    hook_sampler_block(alloc, &out, NULL, h, 3, 7, 0);
    CHECK(sb_n == 3);
    orig_sampler_block = NULL;
}
