/*
 * Shaders do FM6: o compilador do XDK "era" emite instrucoes DXBC que so o console conhece. O D3D12 do PC lanca uma
 * excecao C++ no parser (CShaderCodeParser::ParseInstruction) ao criar o PSO na thread de compilacao do Azimuth
 * (azimuth+0x1364c1) e o processo termina (terminate -> 0xc0000409, sem STOPPED nem .dmp do Azimuth).
 * Unica extensao vista (tools\dxbc_scan.py sobre o dump: ~7000 shaders, 1072 usos): opcode 0x11a, dst + 3 fontes
 * = max3 float (v_max3_f32 do GCN; ex. max3(|ddx.x|, |ddx.y|, |ddx.z|) seguido de max com o mesmo dos ddy).
 * fm6_shader_rewrite troca cada max3 por dois max (0x34) e refaz o container (tamanhos, offsets dos chunks, hash).
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void wlog(const char *fmt, ...);

#define OP_MAX          0x34
#define OP_CUSTOMDATA   0x35
#define OP_DCL_TEMPS    0x68
#define OP_LAST_PC      0xd5            /* ultimo opcode que o parser do PC aceita */
#define OP_XB_MAX3      0x11a
#define OPTOK_SAT       (1u << 13)
#define OPTOK_EXT       0x80000000u
#define OPND_TEMP       0
#define OPND_IMM32      4
#define OPND_IMM64      5

/* ---- hash do container: MD5 dos bytes depois do campo do hash, com o bloco final proprio do DXBC ---- */
static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t md5_r[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };

static void md5_block(uint32_t st[4], const uint8_t *p)
{
    uint32_t x[16], a = st[0], b = st[1], c = st[2], d = st[3];
    int i;
    memcpy(x, p, 64);
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;
        if (i < 16) f = (b & c) | (~b & d), g = i;
        else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
        else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) & 15;
        else f = c ^ (b | ~d), g = (7 * i) & 15;
        f += a + md5_k[i] + x[g];
        t = d; d = c; c = b;
        b += f << md5_r[(i >> 4) * 4 + (i & 3)] | f >> (32 - md5_r[(i >> 4) * 4 + (i & 3)]);
        a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

static void dxbc_checksum(const uint8_t *dxbc, size_t size, uint8_t out[16])
{
    uint32_t st[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    const uint8_t *m = dxbc + 20;
    size_t n = size - 20, full = n & ~(size_t)63, rest = n - full;
    uint32_t bits = (uint32_t)(n * 8), last = (bits >> 2) | 1;
    uint8_t blk[64];
    size_t i;
    for (i = 0; i < full; i += 64) md5_block(st, m + i);
    memset(blk, 0, 64);
    if (rest >= 56) {
        memcpy(blk, m + full, rest);
        blk[rest] = 0x80;
        md5_block(st, blk);
        memset(blk, 0, 64);
    } else {
        memcpy(blk + 4, m + full, rest);
        blk[4 + rest] = 0x80;
    }
    memcpy(blk, &bits, 4);
    memcpy(blk + 60, &last, 4);
    md5_block(st, blk);
    memcpy(out, st, 16);
}

/* ---- operandos ---- */
/* tamanho em tokens do operando em t (n disponiveis); 0 = ilegivel */
static size_t operand_len(const uint32_t *t, size_t n)
{
    uint32_t tok, e;
    size_t i = 1;
    unsigned d, dims;
    if (!n) return 0;
    tok = e = t[0];
    while (e & OPTOK_EXT) {                                     /* tokens estendidos (modificadores) */
        if (i >= n) return 0;
        e = t[i++];
    }
    if ((tok >> 12 & 0xff) == OPND_IMM32) i += (tok & 3) == 1 ? 1 : (tok & 3) == 2 ? 4 : 0;
    else if ((tok >> 12 & 0xff) == OPND_IMM64) i += (tok & 3) == 1 ? 2 : (tok & 3) == 2 ? 8 : 0;
    dims = tok >> 20 & 3;
    for (d = 0; d < dims; d++) {
        unsigned rep = tok >> (22 + 3 * d) & 7;
        if (rep == 0 || rep == 3) i += 1;                       /* imm32 (+ relativo) */
        else if (rep == 1 || rep == 4) i += 2;                  /* imm64 (+ relativo) */
        if (rep >= 2) {
            size_t k = i < n ? operand_len(t + i, n - i) : 0;
            if (!k) return 0;
            i += k;
        }
    }
    return i <= n ? i : 0;
}

/* registrador temporario rN com indice imediato: N; senao -1 */
static long temp_index(const uint32_t *t)
{
    if ((t[0] & OPTOK_EXT) || (t[0] >> 12 & 0xff) != OPND_TEMP || (t[0] >> 20 & 3) != 1 || (t[0] >> 22 & 7) != 0)
        return -1;
    return (long)t[1];
}

/* componentes que a fonte le para a mascara de escrita dmask */
static unsigned src_reads(uint32_t tok, unsigned dmask)
{
    unsigned r = 0, c;
    if ((tok & 3) != 2) return 1;
    switch (tok >> 2 & 3) {
    case 0: return tok >> 4 & 0xf;
    case 1:
        for (c = 0; c < 4; c++)
            if (dmask & 1u << c) r |= 1u << (tok >> (4 + 2 * c) & 3);
        return r;
    default: return 1u << (tok >> 4 & 3);
    }
}

/* fonte sobrepoe o que o dst rN.mask escreve? (fonte com registrador nao imediato conta como sim) */
static int src_overlaps(const uint32_t *src, long dreg, unsigned dmask)
{
    unsigned type = src[0] >> 12 & 0xff;
    size_t e = 1;
    if (type != OPND_TEMP) return type == 3;                    /* x#[] (indexavel): conservador */
    while (src[e - 1] & OPTOK_EXT) e++;
    if ((src[0] >> 20 & 3) != 1 || (src[0] >> 22 & 7) != 0) return 1;
    return (long)src[e] == dreg && (src_reads(src[0], dmask) & dmask);
}

static uint32_t *emit(uint32_t *o, uint32_t ctl, const uint32_t *d, size_t ld, const uint32_t *a, size_t la,
                      const uint32_t *b, size_t lb)
{
    *o++ = OP_MAX | ctl | (uint32_t)(1 + ld + la + lb) << 24;
    memcpy(o, d, ld * 4); o += ld;
    memcpy(o, a, la * 4); o += la;
    memcpy(o, b, lb * 4); o += lb;
    return o;
}

/* max3 em t (12 tokens tipicos) -> dois max em o; devolve o fim ou NULL se nao der. tmp = rN livre (-1 = nenhum),
   *used_tmp marcado quando ele e usado */
static uint32_t *expand_max3(uint32_t *o, const uint32_t *t, size_t len, long tmp, int *used_tmp)
{
    const uint32_t *op[4];
    size_t ol[4], pos = 1;
    uint32_t ctl = t[0] & 0x00fff800u, rsrc[2], rdst[2];
    unsigned dmask;
    long dreg;
    int k;
    if (t[0] & OPTOK_EXT) return NULL;
    for (k = 0; k < 4; k++) {
        op[k] = t + pos;
        ol[k] = operand_len(op[k], len - pos);
        if (!ol[k]) return NULL;
        pos += ol[k];
    }
    if (pos != len || (op[0][0] & 3) != 2 || (op[0][0] >> 2 & 3) != 0) return NULL;
    dmask = op[0][0] >> 4 & 0xf;
    dreg = temp_index(op[0]);
    if (dreg >= 0) {
        /* no lugar: max dst, (duas fontes); max dst, dst, (a que sobra) - a que sobra nao pode ler o que o 1o max
           escreveu */
        for (k = 3; k >= 1; k--) {
            int x = k, p = k == 1 ? 2 : 1, q = k == 3 ? 2 : 3;
            if (src_overlaps(op[x], dreg, dmask)) continue;
            rsrc[0] = (op[0][0] & 0x7ffff000u) | 2 | 1 << 2 | 0xe4 << 4;     /* rN.xyzw */
            rsrc[1] = (uint32_t)dreg;
            o = emit(o, ctl & ~OPTOK_SAT, op[0], ol[0], op[p], ol[p], op[q], ol[q]);
            return emit(o, ctl, op[0], ol[0], rsrc, 2, op[x], ol[x]);
        }
    }
    if (tmp < 0) return NULL;
    /* com temporario novo: max rT.mask, a, b; max dst, rT.xyzw, c */
    rdst[0] = 0x00100002u | dmask << 4; rdst[1] = (uint32_t)tmp;
    rsrc[0] = 0x00100e46u; rsrc[1] = (uint32_t)tmp;
    *used_tmp = 1;
    o = emit(o, ctl & ~OPTOK_SAT, rdst, 2, op[1], ol[1], op[2], ol[2]);
    return emit(o, ctl, op[0], ol[0], rsrc, 2, op[3], ol[3]);
}

static void log_unknown(unsigned op, unsigned len)
{
    static volatile LONG seen[8];
    int i;
    for (i = 0; i < 8; i++) {
        LONG v = InterlockedCompareExchange(&seen[i], (LONG)op, 0);
        if (v == (LONG)op) return;
        if (v == 0) {
            wlog("FM6 shader: console opcode %#x (%u tokens) not converted (PC D3D12 does not accept it)", op, len);
            return;
        }
    }
}

/* devolve um container novo (malloc) com os max3 convertidos, ou NULL se nao ha o que mudar / nao der */
uint8_t *fm6_shader_rewrite(const void *code, size_t size, size_t *out_size)
{
    const uint8_t *d = code;
    uint32_t total, nchunks, i, shex = 0, csize, ndw, *src, *dst, *o, *temps = NULL;
    size_t pos, nmax3 = 0, ntemps = 0;
    long tmp = -1;
    int used_tmp = 0;
    uint8_t *out;
    if (!d || size < 0x20 || memcmp(d, "DXBC", 4)) return NULL;
    memcpy(&total, d + 0x18, 4);
    memcpy(&nchunks, d + 0x1c, 4);
    if (total != size || nchunks > 64 || 0x20 + nchunks * 4 > size) return NULL;
    for (i = 0; i < nchunks; i++) {
        uint32_t off;
        memcpy(&off, d + 0x20 + i * 4, 4);
        if (off + 8 > size) return NULL;
        if (!memcmp(d + off, "SHEX", 4) || !memcmp(d + off, "SHDR", 4)) shex = off;
    }
    if (!shex) return NULL;
    memcpy(&csize, d + shex + 4, 4);
    if (shex + 8 + (size_t)csize > size || csize < 8) return NULL;
    src = (uint32_t *)(d + shex + 8);        /* alinhado: o container e o chunk sao multiplos de 4 */
    ndw = src[1];
    if ((size_t)ndw * 4 != csize) return NULL;
    for (pos = 2; pos < ndw; ) {
        unsigned op = src[pos] & 0x7ff;
        size_t len = op == OP_CUSTOMDATA ? (pos + 1 < ndw ? src[pos + 1] : 0) : src[pos] >> 24 & 0x7f;
        if (!len || pos + len > ndw) return NULL;
        if (op == OP_XB_MAX3) nmax3++;
        else if (op == OP_DCL_TEMPS && len == 2) temps = &src[pos + 1], ntemps++;
        else if (op > OP_LAST_PC) log_unknown(op, (unsigned)len);
        pos += len;
    }
    if (!nmax3) return NULL;
    if (ntemps == 1) tmp = (long)*temps;                        /* hull shader (fases) fica sem temporario novo */
    /* cada max3 (ate 127 tokens) vira no maximo 2 * len + 8 tokens */
    out = malloc(size + nmax3 * (127 + 8) * 4);
    if (!out) return NULL;
    memcpy(out, d, shex + 8);
    dst = (uint32_t *)(out + shex + 8);
    dst[0] = src[0];
    o = dst + 2;
    for (pos = 2; pos < ndw; ) {
        unsigned op = src[pos] & 0x7ff;
        size_t len = op == OP_CUSTOMDATA ? src[pos + 1] : src[pos] >> 24 & 0x7f;
        uint32_t *e = op == OP_XB_MAX3 ? expand_max3(o, src + pos, len, tmp, &used_tmp) : NULL;
        if (op == OP_XB_MAX3 && !e) {
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 4)
                wlog("FM6 shader: max3 (0x11a) with unsupported operands, shader not converted");
            free(out);
            return NULL;
        }
        if (!e) {
            memcpy(o, src + pos, len * 4);
            e = o + len;
        }
        o = e;
        pos += len;
    }
    if (used_tmp) dst[temps - src] = (uint32_t)tmp + 1;     /* dcl_temps N -> N + 1 (declaracoes vem antes) */
    {
        uint32_t nd = (uint32_t)(o - dst), delta = (nd - ndw) * 4, c2 = nd * 4, t2 = total + delta;
        size_t tail = size - (shex + 8 + csize);
        dst[1] = nd;
        memcpy(out + shex + 4, &c2, 4);
        memcpy(out + shex + 8 + c2, d + shex + 8 + csize, tail);
        for (i = 0; i < nchunks; i++) {
            uint32_t off;
            memcpy(&off, out + 0x20 + i * 4, 4);
            if (off > shex) off += delta;
            memcpy(out + 0x20 + i * 4, &off, 4);
        }
        memcpy(out + 0x18, &t2, 4);
        dxbc_checksum(out, t2, out + 4);
        *out_size = t2;
    }
    {
        static volatile LONG n;
        LONG k = InterlockedIncrement(&n);
        if (k <= 4 || (k & (k - 1)) == 0)
            wlog("FM6 shader #%ld: %u console max3 -> max (%u -> %u bytes%s)", k, (unsigned)nmax3,
                 (unsigned)size, (unsigned)*out_size, used_tmp ? ", new temporary" : "");
    }
    return out;
}
