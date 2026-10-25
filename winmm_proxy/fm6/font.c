/*
 * Fontes dos paineis digitais do FM6 (docs\contexto\fm6\07_opcoes_graficas.txt PASSO 119).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"
#include "fm6.h"

/* As 5 fontes dos paineis (DG1..DG5 .vfont) so tem espaco " , - . / 0-9 : D N P R; o tempo em pt-BR e
   "{1}'{2}''" (stringtables\br UnitStrings) -> cada apostrofo vira o glifo 0xfffd (quadrado). Antes de medir
   (0x10bf78: fonte, wchar*, n) e de montar a malha (0x2b7ef0: fonte, vector<u32> de codigos) o texto de uma fonte
   SEM o apostrofo e reescrito no lugar, mesmo tamanho: "''" -> '"' + espaco, "'" -> ':' (00:12,977"). Fonte =
   dados do VectorFontData: tabela codigo -> glifo em +0x150 (busca 0x10c270: indice da entrada ou -1, valores em
   [+0x160]). */
#define FM6_FONT_MEASURE  0x10bf78
#define FM6_FONT_GEOMETRY 0x2b7ef0
#define FM6_FONT_FIND     0x10c270

static int (*font_find)(void *map, uint32_t cp);
void *fm6_font_measure_tramp, *fm6_font_geometry_tramp;

static int font_has(uint8_t *font, uint32_t cp)
{
    int i = font_find(font + 0x150, cp);
    return i != -1 && (*(int32_t **)(font + 0x160))[i] != -1;
}

static int font_needs_fix(uint8_t *font)
{
    return font && !font_has(font, '\'') && font_has(font, ':') && font_has(font, '"') && font_has(font, ' ');
}

static int writable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (!(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)mi.BaseAddress + mi.RegionSize;
}

static void font_note(uint8_t *font, const char *where)
{
    static volatile LONG n;
    if (InterlockedIncrement(&n) <= 4)
        wlog("FM6: dash font %p: apostrophes in the text %s -> ':' / '\"'", (void *)font, where);
}

/* "''" -> '"' + ' ' e "'" -> ':' em n elementos de w bytes (2 = wchar, 4 = codigo) */
static uint32_t text_at(const void *s, size_t i, size_t w)
{
    return w == 2 ? ((const uint16_t *)s)[i] : ((const uint32_t *)s)[i];
}

static void text_set(void *s, size_t i, size_t w, uint32_t v)
{
    if (w == 2) ((uint16_t *)s)[i] = (uint16_t)v;
    else ((uint32_t *)s)[i] = v;
}

static int fix_text(void *s, size_t n, size_t w)
{
    size_t i;
    int changed = 0;
    for (i = 0; i < n; i++) {
        if (text_at(s, i, w) != '\'') continue;
        if (i + 1 < n && text_at(s, i + 1, w) == '\'') { text_set(s, i, w, '"'); text_set(s, i + 1, w, ' '); i++; }
        else text_set(s, i, w, ':');
        changed = 1;
    }
    return changed;
}

static int has_apostrophe(const void *s, size_t n, size_t w)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (text_at(s, i, w) == '\'') return 1;
    return 0;
}

void fm6_font_measure_fix(uint8_t *font, uint16_t *s, uint32_t n)
{
    if (!s || !n || n > 4096 || !has_apostrophe(s, n, 2) || !font_needs_fix(font) || !writable(s, n * 2)) return;
    if (fix_text(s, n, 2)) font_note(font, "measured");
}

void fm6_font_geometry_fix(uint8_t *font, uint32_t **vec)
{
    uint32_t *b = vec ? vec[0] : NULL, *e = vec ? vec[1] : NULL;
    size_t n = b && e > b ? (size_t)(e - b) : 0;
    if (!n || n > 4096 || !has_apostrophe(b, n, 4) || !font_needs_fix(font) || !writable(b, n * 4)) return;
    if (fix_text(b, n, 4)) font_note(font, "built");
}

/* rcx = fonte, rdx = texto: guarda os argumentos (inteiros e xmm0-3; pode haver mais na pilha), corrige e segue
   para o original pelo trampolim com a pilha da entrada */
void fm6_font_measure_stub(void);
void fm6_font_geometry_stub(void);
#define FONT_STUB(name, fix, tramp)                                   \
    __asm__(".globl " name "\n" name ":\n"                            \
            "  push %rcx\n  push %rdx\n  push %r8\n  push %r9\n"        \
            "  sub $0x68, %rsp\n"                                     \
            "  movups %xmm0, 0x20(%rsp)\n  movups %xmm1, 0x30(%rsp)\n"  \
            "  movups %xmm2, 0x40(%rsp)\n  movups %xmm3, 0x50(%rsp)\n"  \
            "  call " fix "\n"                                        \
            "  movups 0x20(%rsp), %xmm0\n  movups 0x30(%rsp), %xmm1\n"  \
            "  movups 0x40(%rsp), %xmm2\n  movups 0x50(%rsp), %xmm3\n"  \
            "  add $0x68, %rsp\n"                                     \
            "  pop %r9\n  pop %r8\n  pop %rdx\n  pop %rcx\n"          \
            "  jmp *" tramp "(%rip)\n")
FONT_STUB("fm6_font_measure_stub", "fm6_font_measure_fix", "fm6_font_measure_tramp");
FONT_STUB("fm6_font_geometry_stub", "fm6_font_geometry_fix", "fm6_font_geometry_tramp");

void fm6_dash_font_fix(uint8_t *game)
{
    void *const stub = (void *)fm6_font_measure_stub;
    if (game[FM6_FONT_MEASURE] == 0xff && game[FM6_FONT_MEASURE + 1] == 0x25 &&
        !memcmp(game + FM6_FONT_MEASURE + 6, &stub, 8)) return;   /* apply_patches de novo: ja instalado */
    if (!bytes_match(game + FM6_FONT_FIND, "448b51044c8b5908448bc24533c9", 14)) {
        wlog("FM6: dash fonts: glyph lookup site does not match, ignored");
        return;
    }
    font_find = (int (*)(void *, uint32_t))(game + FM6_FONT_FIND);
    if (!hook_at(game + FM6_FONT_MEASURE, "488bc4488958084889681048897018", 15, (void *)fm6_font_measure_stub,
                 &fm6_font_measure_tramp) ||
        !hook_at(game + FM6_FONT_GEOMETRY, "488bc44c8948204c89401848894808", 15, (void *)fm6_font_geometry_stub,
                 &fm6_font_geometry_tramp)) {
        wlog("FM6: dash fonts: text hooks not installed");
        return;
    }
    wlog("FM6: dash fonts: apostrophes in text of fonts without them -> ':' / '\"' (pt-BR lap times)");
}
