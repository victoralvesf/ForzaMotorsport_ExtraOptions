#include "launcher.hpp"

#include "hook.hpp"
#include "log.hpp"
#include "patch.hpp"
#include "pe.hpp"
#include "rows.hpp"
#include "scan.hpp"

#include <cstring>
#include <cwchar>

namespace launcher {

namespace {

// ---- signatures (Azimuth 1.0.2) ----

// Start of the function that lists the sections of the current game: { Display, Graphics, Audio } as a
// vector<int>, plus Content packages when the game has some.
constexpr const char *k_build_sections =
    "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 60 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 50"
    " 48 8b fa 48 8b f1 48 89 4c 24 38 45 33 e4 44 89 64 24 30 44 89 64 24 40 c7 44 24 44 01 00 00 00"
    " c7 44 24 48 02 00 00 00";
// its first whole instructions, moved to the trampoline (nothing relative to RIP)
constexpr const char *k_build_sections_stolen = "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54";

// SettingsPage's view, from its start: Context in r12 (and [rbp-0x58]), the list of sections, the id of the current
// one in [rbp-0x38], then the rows: Audio (2), Display (0), Graphics (1) and, for any other id, an empty vector
// built at [rbp-0x30] (lea rcx; call; mov r14d,8 = its cleanup flag). The mod takes over those 15 bytes.
constexpr const char *k_rows_dispatch =
    "4d 8b e0 4c 89 45 a8 48 89 55 28 4c 8b f9 48 89 55 88 45 33 f6 44 89 75 cc 49 8b 58 08 48 89 5d 88"
    " 48 8b d3 48 8d 4d 10 e8 ?? ?? ?? ?? 90 48 8d 4d 10 e8 ?? ?? ?? ?? 49 8b 57 08 48 3b d0 72 07 4d 89 77 08"
    " 41 8b d6 48 8d 4d 10 e8 ?? ?? ?? ?? 8b 00 89 45 c8 83 f8 02 75 2b 41 83 7f 38 00 75 09 e8 ?? ?? ?? ??"
    " 41 89 47 38 45 8b 47 38 49 8b d4 48 8d 8d 28 80 00 00 e8 ?? ?? ?? ?? 41 be 04 00 00 00 eb 48"
    " 85 c0 75 18 49 8b d4 48 8d 8d 88 81 00 00 e8 ?? ?? ?? ?? 90 41 be 01 00 00 00 eb 4f"
    " 83 f8 01 75 18 49 8b d4 48 8d 8d e0 80 00 00 e8 ?? ?? ?? ?? 90 41 be 02 00 00 00 eb 1f"
    " 48 8d 4d d0 e8 ?? ?? ?? ?? 41 be 08 00 00 00";
constexpr size_t k_rows_other = 0xbe;      // the "lea rcx,[rbp-0x30]; call empty; mov r14d,8" of any other id
constexpr size_t k_rows_other_size = 15;   // the next instruction (0xcd) is also the Audio branch's jump target

// name of a section: index the table of names with the section id (the tabs; the title of the current one)
constexpr const char *k_names[2] = {
    "48 63 10 48 8d 05 ?? ?? ?? ?? 48 8b 14 d0",    // movsxd rdx,[rax]; lea rax,[table]; mov rdx,[rax+rdx*8]
    "48 63 7d c8 48 8d 05 ?? ?? ?? ?? 48 8b 14 f8", // movsxd rdi,[rbp-0x38]; lea rax,[table]; mov rdx,[rax+rdi*8]
};
constexpr size_t k_names_disp[2] = { 6, 7 }; // the lea's rel32 (the lea ends 4 bytes after)
constexpr const wchar_t *k_table[] = { L"Display", L"Graphics", L"Audio", L"Content packages" };
static_assert(sizeof k_table / sizeof *k_table == k_section);

// current title of a Settings: cmp qword [rbx+0xb8],7; lea r8,[rbx+0xa0] (a std::wstring)
constexpr const char *k_current_game_sig = "48 83 bb b8 00 00 00 07 4c 8d 83 a0 00 00 00";

constexpr const wchar_t *k_section_name = L"Extra Options";

// ---- state ----

std::span<const game::Module *const> g_games;

using BuildSections = msvc::Vector<int32_t> *(*)(msvc::Vector<int32_t> *out, const uint8_t *settings);
BuildSections g_build_sections; // trampoline to Azimuth's

using EmptyRows = msvc::Vector<Row> *(*)(msvc::Vector<Row> *out);

// p (a pointer read from the image at base) where it is in this mapping: the loader relocates azimuth.exe, a copy
// mapped by the tests is not.
const uint8_t *in_image(const uint8_t *base, const void *p)
{
    const IMAGE_NT_HEADERS64 *nt = core::pe::headers(base);
    auto v = reinterpret_cast<uintptr_t>(p), b = reinterpret_cast<uintptr_t>(base);
    uintptr_t ib = nt->OptionalHeader.ImageBase, size = nt->OptionalHeader.SizeOfImage;
    if (v >= b && v < b + size) return static_cast<const uint8_t *>(p);
    if (v >= ib && v < ib + size) return base + (v - ib);
    return nullptr;
}

bool same_string(const uint8_t *base, const wchar_t *p, const wchar_t *want)
{
    const uint8_t *q = in_image(base, p);
    size_t n = (wcslen(want) + 1) * sizeof(wchar_t);
    return q && core::pe::readable(q, n) && memcmp(q, want, n) == 0;
}

// Appends v to a vector<int> built by Azimuth.
bool append(msvc::Vector<int32_t> &v, int32_t x)
{
    if (v.last != v.end) {
        *v.last++ = x;
        return true;
    }
    msvc::Vector<int32_t> n;
    if (!msvc::reserve(n, v.size() + 1)) return false;
    if (v.first) memcpy(n.first, v.first, v.size() * sizeof(int32_t));
    n.last = n.first + v.size();
    *n.last++ = x;
    msvc::free(v.first);
    v = n;
    return true;
}

msvc::Vector<int32_t> *build_sections(msvc::Vector<int32_t> *out, const uint8_t *settings)
{
    g_build_sections(out, settings);
    if (game_of(settings, g_games)) append(*out, k_section);
    return out;
}

// Called from the stub at the view's "other section" rows, instead of Azimuth's empty vector. ctx: the page's
// Context (ctx[1] = Settings).
msvc::Vector<Row> *rows_for(msvc::Vector<Row> *out, const uint8_t *const *ctx, int32_t section, EmptyRows empty)
{
    if (section != k_section) return empty(out);
    const game::Module *g = game_of(ctx[1], g_games);
    if (!g) return empty(out);
    if (!fill_rows(*out, *g)) {
        core::log::write("launcher: %s Extra Options: out of memory, rows left empty", g->tag);
        return empty(out);
    }
    return out;
}

void abs_jmp(uint8_t *out, const void *to)
{
    out[0] = 0xff;
    out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &to, 8);
}

// The stub that replaces the view's 15 bytes: rows_for(&rows, ctx, section, empty), then the cleanup flag and back.
uint8_t *make_stub(uint8_t *site)
{
    auto *s = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!s) return nullptr;
    const void *empty = core::scan::rel32(site + 5), *fn = reinterpret_cast<const void *>(rows_for);
    const void *back = site + k_rows_other_size;
    uint8_t *p = s;
    auto put = [&](std::initializer_list<uint8_t> b) {
        for (uint8_t x : b) *p++ = x;
    };
    auto put64 = [&](const void *v) {
        memcpy(p, &v, 8);
        p += 8;
    };
    put({ 0x48, 0x8d, 0x4d, 0xd0 });  // lea rcx,[rbp-0x30]
    put({ 0x4c, 0x89, 0xe2 });        // mov rdx,r12
    put({ 0x44, 0x8b, 0x45, 0xc8 });  // mov r8d,[rbp-0x38]
    put({ 0x49, 0xb9 });              // mov r9,empty
    put64(empty);
    put({ 0x48, 0xb8 });              // mov rax,rows_for
    put64(fn);
    put({ 0xff, 0xd0 });              // call rax
    put({ 0x41, 0xbe, 8, 0, 0, 0 });  // mov r14d,8
    abs_jmp(p, back);                 // jmp back
    DWORD old;
    if (!VirtualProtect(s, 64, PAGE_EXECUTE_READ, &old)) return nullptr;
    FlushInstructionCache(GetCurrentProcess(), s, 64);
    return s;
}

} // namespace

bool locate(uint8_t *base, Sites &s, const char **why)
{
    s = {};
    auto find = [&](const char *sig) { return const_cast<uint8_t *>(core::scan::find_unique(base, sig)); };
    if (!(s.build_sections = find(k_build_sections))) return *why = "section list not found", false;
    if (uint8_t *v = find(k_rows_dispatch)) s.rows_dispatch = v + k_rows_other;
    else return *why = "settings page view not found", false;
    for (int i = 0; i < 2; i++) {
        if (!(s.names[i] = find(k_names[i]))) return *why = "section names not found", false;
        auto *t = reinterpret_cast<const wchar_t *const *>(core::scan::rel32(s.names[i] + k_names_disp[i]));
        if (i == 1 && t != s.table) return *why = "section names: two tables", false;
        s.table = t;
    }
    if (!core::pe::readable(s.table, sizeof k_table)) return *why = "section names unreadable", false;
    for (int i = 0; i < k_section; i++)
        if (!same_string(base, s.table[i], k_table[i])) return *why = "section names differ", false;
    s.current_game = find(k_current_game_sig) != nullptr;
    if (!s.current_game) return *why = "current game of the settings not found", false;
    return true;
}

const game::Module *game_of(const uint8_t *settings, std::span<const game::Module *const> games)
{
    if (!settings || !core::pe::readable(settings + k_current_game, sizeof(msvc::WString))) return nullptr;
    const auto *id = reinterpret_cast<const msvc::WString *>(settings + k_current_game);
    if (id->size != 8 || id->cap < 7 || !core::pe::readable(id->data(), 8 * sizeof(wchar_t))) return nullptr;
    for (const game::Module *g : games) {
        if (!g->title_id || strlen(g->title_id) != 8) continue;
        bool same = true;
        for (size_t i = 0; i < 8 && same; i++) {
            wchar_t c = id->data()[i];
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
            same = c == static_cast<unsigned char>(g->title_id[i]);
        }
        if (same) return g;
    }
    return nullptr;
}

bool install(HMODULE exe, std::span<const game::Module *const> games, const wchar_t *ini)
{
    auto *base = reinterpret_cast<uint8_t *>(exe);
    Sites s;
    const char *why = "";
    if (!locate(base, s, &why)) {
        core::log::write("launcher: %s: no Extra Options section (Azimuth's launcher left as it is)", why);
        return false;
    }
    g_games = games;
    set_ini(ini);

    // the table of names with ours at k_section, where both leas reach it
    auto *table = static_cast<const wchar_t **>(core::hook::alloc_near(s.names[0], 0x1000, PAGE_READWRITE));
    if (!table || !core::hook::reachable(s.names[1], table, 0x1000)) {
        core::log::write("launcher: no memory near azimuth.exe for the section names: no Extra Options section");
        return false;
    }
    memcpy(table, s.table, sizeof k_table);
    table[k_section] = k_section_name;
    DWORD old;
    VirtualProtect(table, 0x1000, PAGE_READONLY, &old);

    // rows first (only used for our id), then the names, then the section itself
    uint8_t *stub = make_stub(s.rows_dispatch);
    uint8_t jmp[k_rows_other_size];
    memset(jmp, 0xcc, sizeof jmp);
    if (stub) abs_jmp(jmp, stub);
    if (!stub || !core::patch::write_code(s.rows_dispatch, jmp, sizeof jmp)) {
        core::log::write("launcher: settings page view not patched: no Extra Options section");
        return false;
    }
    for (int i = 0; i < 2; i++) {
        uint8_t *disp = s.names[i] + k_names_disp[i];
        auto rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(table) - reinterpret_cast<intptr_t>(disp + 4));
        if (!core::patch::write_code(disp, &rel, 4)) {
            core::log::write("launcher: section names not patched: no Extra Options section");
            return false;
        }
    }
    void *tramp = nullptr;
    if (!core::hook::install(s.build_sections, k_build_sections_stolen, reinterpret_cast<void *>(build_sections),
                             &tramp)) {
        core::log::write("launcher: section list not hooked: no Extra Options section");
        return false;
    }
    g_build_sections = reinterpret_cast<BuildSections>(tramp);

    for (const game::Module *g : games) {
        int n = 0;
        for (const core::patch::Option &o : g->options) n += o.label != nullptr;
        core::log::write("launcher: Extra Options section for %s (title %s): %d options", g->tag, g->title_id, n);
    }
    return true;
}

} // namespace launcher
