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
    CHECK_EQ(*(uint64_t *)(real_ctx + CTX_TOPO_PACKET), topo_packets[0]);
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
