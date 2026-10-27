#include "font.hpp"

#include "hook.hpp"
#include "log.hpp"
#include "patch.hpp"

#include <windows.h>
#include <cstring>

namespace fm6::dash_font {

namespace {

// text measure: rcx font, rdx wchar text, r8 length; the draw (0x10d5b0) reads the same text afterwards
constexpr uint32_t k_measure = 0x10bf78;
constexpr const char *k_measure_stolen = "48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18";
// code -> index lookup in a font's glyph map (map at font+0x150, -1 when missing; glyph per index at [font+0x160])
constexpr uint32_t k_find = 0x10c270;
constexpr const char *k_find_expect = "44 8b 51 04 4c 8b 59 08 44 8b c2 45 33 c9";

using FindFn = int (*)(void *map, uint32_t code);
FindFn g_find;

bool font_has(uint8_t *font, uint32_t code)
{
    int i = g_find(font + 0x150, code);
    return i != -1 && (*reinterpret_cast<int32_t **>(font + 0x160))[i] != -1;
}

bool writable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (!(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return false;
    return static_cast<const uint8_t *>(p) + n <= static_cast<const uint8_t *>(mi.BaseAddress) + mi.RegionSize;
}

} // namespace

size_t fix_text(uint16_t *s, size_t n)
{
    size_t i = 0, k = 0;
    for (; i < n && s[i]; i++) {
        if (s[i] == '\'' && i + 1 < n && s[i + 1] == '\'') {
            i++;
            continue;
        }
        s[k++] = s[i] == '\'' ? ':' : s[i];
    }
    if (k < i) memset(s + k, 0, (i - k) * sizeof *s);
    return k;
}

} // namespace fm6::dash_font

extern "C" {

void *fm6_font_measure_tramp;

void fm6_font_measure_fix(uint8_t *font, uint16_t *s, uint32_t n)
{
    using namespace fm6::dash_font;
    static volatile LONG logged;
    if (!font || !s || !n || n > 4096) return;
    size_t i = 0;
    while (i < n && s[i] && s[i] != '\'') i++;
    if (i == n || !s[i]) return;
    if (font_has(font, '\'') || !font_has(font, ':') || !writable(s, n * sizeof *s)) return;
    fix_text(s, n);
    if (InterlockedIncrement(&logged) <= 4)
        core::log::write("FM6: dash font %p: text with apostrophes as in English (\"''\" removed, \"'\" -> ':')",
                         static_cast<void *>(font));
}

// Saves the arguments (rcx, rdx, r8, r9, xmm0-3; more may be on the stack), fixes the text and continues in the
// original through the trampoline with the stack as it was on entry.
void fm6_font_measure_stub();
asm(".text\n.globl fm6_font_measure_stub\nfm6_font_measure_stub:\n"
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

} // extern "C"

namespace fm6::dash_font {

void install(uint8_t *base)
{
    if (core::patch::match(base + k_find, 64, k_find_expect, nullptr) != 0) {
        core::log::write("FM6: dash fonts: glyph lookup site %#x does not match, fix ignored", k_find);
        return;
    }
    g_find = reinterpret_cast<FindFn>(base + k_find);
    if (!core::hook::install(base + k_measure, k_measure_stolen, reinterpret_cast<void *>(fm6_font_measure_stub),
                             &fm6_font_measure_tramp)) {
        core::log::write("FM6: dash fonts: text measure site %#x does not match, fix ignored", k_measure);
        return;
    }
    core::log::write("FM6: dash fonts: text of fonts without the apostrophe as in English (pt-BR lap times)");
}

} // namespace fm6::dash_font
