/*
 * Testes de shader_fm6.c: max3 do console (opcode 0x11a) -> dois max, container refeito e hash do DXBC.
 * Hashes esperados calculados com tools\dxbc.py (conferido contra ~7000 shaders reais de um dump do FM6).
 */
#include "../shader_fm6.c"
#include "test.h"

/* container DXBC {SHEX, STAT(4 bytes)} com o hash certo */
static size_t make_dxbc(uint8_t *out, const uint32_t *tok, size_t n)
{
    uint32_t hdr = 0x20 + 2 * 4, shex = hdr, stat = shex + 8 + (uint32_t)n * 4, total = stat + 12, v;
    memset(out, 0, total);
    memcpy(out, "DXBC", 4);
    v = 1; memcpy(out + 0x14, &v, 4);
    memcpy(out + 0x18, &total, 4);
    v = 2; memcpy(out + 0x1c, &v, 4);
    memcpy(out + 0x20, &shex, 4);
    memcpy(out + 0x24, &stat, 4);
    memcpy(out + shex, "SHEX", 4);
    v = (uint32_t)n * 4; memcpy(out + shex + 4, &v, 4);
    memcpy(out + shex + 8, tok, n * 4);
    memcpy(out + stat, "STAT", 4);
    v = 4; memcpy(out + stat + 4, &v, 4);
    v = 0xdeadbeef; memcpy(out + stat + 8, &v, 4);
    dxbc_checksum(out, total, out + 4);
    return total;
}

TEST(dxbc_checksum_matches_reference)
{
    uint8_t d[148], h[16];
    int i;
    for (i = 0; i < 148; i++) d[i] = (uint8_t)(i * 7 + 3);
    dxbc_checksum(d, 144, h);                                   /* resto 60 >= 56: dois blocos finais */
    CHECK_HEX(h, "3ad04d84eaae247bbd2a4ff49d53eb3b");
    dxbc_checksum(d, 148, h);                                   /* resto 0 */
    CHECK_HEX(h, "92f40506414668174c7615900f9394b2");
}

TEST(fm6_shader_max3_expanded)
{
    /* ps_5_0: dcl_temps 2; r1.w = max3(|r0.x|,|r0.y|,|r0.z|); r0.y = max3(|r0.y|,|r0.z|,|r0.w|) (dst e fonte);
       o0.x = max3_sat(|r1.w|, r0.y, |r0.x|) (saida: nao pode ser lida -> temporario r2); ret */
    static const uint32_t in[40] = {
        0x50, 40, 0x02000068, 2,
        0x0c00011a, 0x00100082, 1, 0x8010000a, 0x81, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0,
        0x0c00011a, 0x00100022, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0, 0x8010003a, 0x81, 0,
        0x0b00211a, 0x00102012, 0, 0x8010003a, 0x81, 1, 0x0010001a, 0, 0x8010000a, 0x81, 0,
        0x0100003e,
    };
    static const uint32_t want[55] = {
        0x50, 55, 0x02000068, 3,
        0x09000034, 0x00100082, 1, 0x8010000a, 0x81, 0, 0x8010001a, 0x81, 0,        /* max r1.w, |r0.x|, |r0.y| */
        0x08000034, 0x00100082, 1, 0x00100e46, 1, 0x8010002a, 0x81, 0,              /* max r1.w, r1, |r0.z| */
        0x09000034, 0x00100022, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0,        /* max r0.y, |r0.y|, |r0.z| */
        0x08000034, 0x00100022, 0, 0x00100e46, 0, 0x8010003a, 0x81, 0,              /* max r0.y, r0, |r0.w| */
        0x08000034, 0x00100012, 2, 0x8010003a, 0x81, 1, 0x0010001a, 0,              /* max r2.x, |r1.w|, r0.y */
        0x08002034, 0x00102012, 0, 0x00100e46, 2, 0x8010000a, 0x81, 0,              /* max_sat o0.x, r2, |r0.x| */
        0x0100003e,
    };
    static uint8_t src[512], exp[512];
    size_t n = make_dxbc(src, in, 40), en = make_dxbc(exp, want, 55), on = 0;
    uint8_t *out;
    CHECK_EQ(n, 220);
    CHECK_HEX(src + 4, "78a196928bce85edcfbb26bcd8205573");
    out = fm6_shader_rewrite(src, n, &on);
    REQUIRE(out);
    CHECK_EQ(on, 280);
    CHECK_EQ(en, 280);
    CHECK(!memcmp(out, exp, 280));                              /* tokens, tamanhos, offset do STAT */
    CHECK_HEX(out + 4, "713e5fad23b5fe896d41e683c0d56ca4");
    CHECK_HEX(out + 0x24, "0c010000");                          /* STAT 0xd0 deslocado 60 bytes */
    free(out);
}

TEST(fm6_shader_without_max3_untouched)
{
    static const uint32_t in[6] = { 0x50, 6, 0x02000068, 1, 0x0100003e, 0x0100003e };
    static uint8_t src[256];
    size_t n = make_dxbc(src, in, 6), on = 0;
    CHECK(fm6_shader_rewrite(src, n, &on) == NULL);
    CHECK(fm6_shader_rewrite("DXBX", 4, &on) == NULL);
    src[0x18] ^= 1;                                             /* tamanho total errado */
    CHECK(fm6_shader_rewrite(src, n, &on) == NULL);
}
