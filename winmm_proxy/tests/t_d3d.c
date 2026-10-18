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
    CHECK(log_has("double free evitado"));
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
    memcpy(az + RVA_VTBL_GET, "ÿ%    ", 6);
    *(void **)(az + RVA_VTBL_GET + 6) = (void *)fake_vtbl_get;
    for (i = 0; i < 0x10e; i++) fm6_type_tab[0xe][i] = fm6_type_tab[5][i] = (void *)(uintptr_t)(0x100 + i);
    fm6_type_tab[0xe][0x11] = (void *)fake_create_sc_cw;                     /* CreateSwapChainForCoreWindow */
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
    CHECK(imm[9] == (void *)0x1008 && imm[0x8d] == (void *)0x108c);
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
        CHECK(site_imm(imm2, src2, 0x870) == imm2);
        CHECK(imm2[0xa3] == (void *)0x10a0);                    /* implementado no Azimuth: intacto */
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
