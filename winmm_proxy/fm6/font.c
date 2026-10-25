/*
 * Fontes dos paineis digitais do FM6 (docs\contexto\fm6\07_opcoes_graficas.txt PASSO 119).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"
#include "fm6.h"

/* As 5 fontes dos paineis (DG1..DG5 .vfont) so tem espaco " , - . / 0-9 : D N P R; o tempo em pt-BR e
   "{1}'{2}''" (stringtables\br UnitStrings) -> cada apostrofo vira o glifo 0xfffd (quadrado). Na medida do texto
   (0x10bf78: fonte, wchar*, n) o texto de uma fonte SEM o apostrofo e reescrito no lugar no formato do ingles,
   para o qual os paineis foram desenhados: "''" sai e "'" -> ':' (00:12,977); o resto ate n vira NUL. O desenho
   (0x10d5b0, busca do glifo embutida) le o MESMO texto depois e, como a medida, para no NUL ou no fim. Fonte =
   dados do VectorFontData: tabela codigo -> glifo em +0x150 (busca 0x10c270: indice da entrada ou -1, valores em
   [+0x160]). */
#define FM6_FONT_MEASURE  0x10bf78
#define FM6_FONT_FIND     0x10c270

static int (*font_find)(void *map, uint32_t cp);
void *fm6_font_measure_tramp;

static int font_has(uint8_t *font, uint32_t cp)
{
    int i = font_find(font + 0x150, cp);
    return i != -1 && (*(int32_t **)(font + 0x160))[i] != -1;
}

static int font_needs_fix(uint8_t *font)
{
    return font && !font_has(font, '\'') && font_has(font, ':');
}

static int writable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (!(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
    return (const uint8_t *)p + n <= (const uint8_t *)mi.BaseAddress + mi.RegionSize;
}

/* s[0..n) (ate o 1o NUL) sem "''" e com "'" -> ':'; o que sobra ate o NUL original vira NUL. Devolve o tamanho */
static size_t fix_text(uint16_t *s, size_t n)
{
    size_t i, k = 0;
    for (i = 0; i < n && s[i]; i++) {
        if (s[i] == '\'' && i + 1 < n && s[i + 1] == '\'') { i++; continue; }
        s[k++] = s[i] == '\'' ? ':' : s[i];
    }
    if (k < i) memset(s + k, 0, (i - k) * 2);
    return k;
}

void fm6_font_measure_fix(uint8_t *font, uint16_t *s, uint32_t n)
{
    static volatile LONG logged;
    size_t i;
    if (!s || !n || n > 4096) return;
    for (i = 0; i < n && s[i] && s[i] != '\''; i++) ;
    if (i == n || !s[i] || !font_needs_fix(font) || !writable(s, n * 2)) return;
    fix_text(s, n);
    if (InterlockedIncrement(&logged) <= 4)
        wlog("FM6: dash font %p: text with apostrophes -> as in English (\"''\" removed, \"'\" -> ':')", (void *)font);
}

/* rcx = fonte, rdx = texto, r8 = n: guarda os argumentos (inteiros e xmm0-3; pode haver mais na pilha), corrige e
   segue para o original pelo trampolim com a pilha da entrada */
void fm6_font_measure_stub(void);
__asm__(".globl fm6_font_measure_stub\n"
        "fm6_font_measure_stub:\n"
        "  push %rcx\n  push %rdx\n  push %r8\n  push %r9\n"
        "  sub $0x68, %rsp\n"
        "  movups %xmm0, 0x20(%rsp)\n  movups %xmm1, 0x30(%rsp)\n"
        "  movups %xmm2, 0x40(%rsp)\n  movups %xmm3, 0x50(%rsp)\n"
        "  call fm6_font_measure_fix\n"
        "  movups 0x20(%rsp), %xmm0\n  movups 0x30(%rsp), %xmm1\n"
        "  movups 0x40(%rsp), %xmm2\n  movups 0x50(%rsp), %xmm3\n"
        "  add $0x68, %rsp\n"
        "  pop %r9\n  pop %r8\n  pop %rdx\n  pop %rcx\n"
        "  jmp *fm6_font_measure_tramp(%rip)\n");

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
                 &fm6_font_measure_tramp)) {
        wlog("FM6: dash fonts: text hook not installed");
        return;
    }
    wlog("FM6: dash fonts: text of fonts without the apostrophe as in English (pt-BR lap times)");
}
