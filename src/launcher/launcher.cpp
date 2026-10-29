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

// ---- signatures ----

// Start of the function that lists the sections of the current game: { Display, Graphics, Audio } as a
// vector<int>, plus Content packages when the game has some (the same in every supported Azimuth).
constexpr const char *k_build_sections =
    "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54 41 56 41 57 48 83 ec 60 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 50"
    " 48 8b fa 48 8b f1 48 89 4c 24 38 45 33 e4 44 89 64 24 30 44 89 64 24 40 c7 44 24 44 01 00 00 00"
    " c7 44 24 48 02 00 00 00";
// its first whole instructions, moved to the trampoline (nothing relative to RIP)
constexpr const char *k_build_sections_stolen = "48 89 5c 24 18 48 89 6c 24 20 56 57 41 54";

// SettingsPage's view in one Azimuth build, from the start of its body: the list of sections, the id of the current
// one, then the rows: Audio (2), Display (0), Graphics (1) and, for any other id, an empty vector (the "other"
// block, which the mod takes over; the instruction after it is also the Audio branch's jump target). The two leas
// index the table of section names with the section id (the tabs; the title of the current one).
struct Layout {
    const char *version;
    const char *view;
    size_t other, other_size;
    const char *names[2];
    size_t names_disp[2]; // the lea's rel32 (the lea ends 4 bytes after)
};

constexpr Layout k_layouts[] = {
    // Context in r12 (and [rbp-0x58]), the id in [rbp-0x38]; other: lea rcx,[rbp-0x30]; call <empty vector>;
    // mov r14d,8 (its cleanup flag)
    { "1.0.2/1.0.3",
      "4d 8b e0 4c 89 45 a8 48 89 55 28 4c 8b f9 48 89 55 88 45 33 f6 44 89 75 cc 49 8b 58 08 48 89 5d 88"
      " 48 8b d3 48 8d 4d 10 e8 ?? ?? ?? ?? 90 48 8d 4d 10 e8 ?? ?? ?? ?? 49 8b 57 08 48 3b d0 72 07 4d 89 77 08"
      " 41 8b d6 48 8d 4d 10 e8 ?? ?? ?? ?? 8b 00 89 45 c8 83 f8 02 75 2b 41 83 7f 38 00 75 09 e8 ?? ?? ?? ??"
      " 41 89 47 38 45 8b 47 38 49 8b d4 48 8d 8d 28 80 00 00 e8 ?? ?? ?? ?? 41 be 04 00 00 00 eb 48"
      " 85 c0 75 18 49 8b d4 48 8d 8d 88 81 00 00 e8 ?? ?? ?? ?? 90 41 be 01 00 00 00 eb 4f"
      " 83 f8 01 75 18 49 8b d4 48 8d 8d e0 80 00 00 e8 ?? ?? ?? ?? 90 41 be 02 00 00 00 eb 1f"
      " 48 8d 4d d0 e8 ?? ?? ?? ?? 41 be 08 00 00 00",
      0xbe, 15,
      { "48 63 10 48 8d 05 ?? ?? ?? ?? 48 8b 14 d0",      // movsxd rdx,[rax]; lea rax,[table]; mov rdx,[rax+rdx*8]
        "48 63 7d c8 48 8d 05 ?? ?? ?? ?? 48 8b 14 f8" }, // movsxd rdi,[rbp-0x38]; lea rax,[table]; mov rdx,[rax+rdi*8]
      { 6, 7 } },
    // Context in r14 (and [rsp+0x60]), the id in r12d; other: the empty vector built inline at [rbp+0x7e0]
    // (xorps; movdqu; mov qword), lea rax,[rbp+0x7e0]; mov ebx,8 (its cleanup flag)
    { "1.0.4",
      "4d 8b f0 4c 89 44 24 60 48 89 55 e8 48 8b f1 48 89 54 24 50 33 ff 89 7d 80 4d 8b 68 08 4c 89 6c 24 50"
      " 49 8b d5 48 8d 4d d0 e8 ?? ?? ?? ?? 90 4c 8b 66 08 48 8b 45 d8 48 8b 4d d0 48 2b c1 48 c1 f8 02 4c 3b e0"
      " 72 07 48 89 7e 08 44 8b e7 46 8b 24 a1 44 89 64 24 78 41 83 fc 02 75 25 83 7e 38 00 75 08 e8 ?? ?? ?? ??"
      " 89 46 38 44 8b 46 38 49 8b d6 48 8d 4d a0 e8 ?? ?? ?? ?? bb 04 00 00 00 eb 57"
      " 45 85 e4 75 17 49 8b d6 48 8d 8d 40 0b 00 00 e8 ?? ?? ?? ?? 90 bb 01 00 00 00 eb 5f"
      " 41 83 fc 01 75 17 49 8b d6 48 8d 8d 80 0e 00 00 e8 ?? ?? ?? ?? 90 bb 02 00 00 00 eb 30"
      " 0f 57 c0 f3 0f 7f 85 e0 07 00 00 48 89 bd f0 07 00 00 48 8d 85 e0 07 00 00 bb 08 00 00 00",
      0xbb, 30,
      { "48 63 14 b8 48 8d 05 ?? ?? ?? ?? 48 8b 14 d0",   // movsxd rdx,[rax+rdi*4]; lea rax,[table]; mov rdx,[rax+rdx*8]
        "49 63 d4 48 8d 05 ?? ?? ?? ?? 48 8b 14 d0" },    // movsxd rdx,r12d; lea rax,[table]; mov rdx,[rax+rdx*8]
      { 7, 6 } },
};
static_assert(sizeof k_layouts / sizeof *k_layouts == Sites::k_layouts);

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
// Context (ctx[1] = Settings); empty: Azimuth's function that builds it (null where the view builds it inline).
msvc::Vector<Row> *rows_for(msvc::Vector<Row> *out, const uint8_t *const *ctx, int32_t section, EmptyRows empty)
{
    if (empty) empty(out);
    else *out = {};
    if (section != k_section) return out;
    const game::Module *g = game_of(ctx[1], g_games);
    if (!g) return out;
    if (!fill_rows(*out, *g)) core::log::write("launcher: %s Extra Options: out of memory, rows left empty", g->tag);
    return out;
}

void abs_jmp(uint8_t *out, const void *to)
{
    out[0] = 0xff;
    out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &to, 8);
}

// The stub that replaces the view's "other" block: rows_for(&rows, ctx, section, empty), then what the block leaves
// for the code after it (the cleanup flag; in 1.0.4 also rax = &rows) and back.
uint8_t *make_stub(uint8_t *site, size_t layout)
{
    auto *s = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!s) return nullptr;
    const void *fn = reinterpret_cast<const void *>(rows_for);
    const void *back = site + k_layouts[layout].other_size;
    uint8_t *p = s;
    auto put = [&](std::initializer_list<uint8_t> b) {
        for (uint8_t x : b) *p++ = x;
    };
    auto put64 = [&](const void *v) {
        memcpy(p, &v, 8);
        p += 8;
    };
    if (layout == 0) {
        put({ 0x48, 0x8d, 0x4d, 0xd0 });             // lea rcx,[rbp-0x30]
        put({ 0x4c, 0x89, 0xe2 });                   // mov rdx,r12
        put({ 0x44, 0x8b, 0x45, 0xc8 });             // mov r8d,[rbp-0x38]
        put({ 0x49, 0xb9 });                         // mov r9,empty
        put64(core::scan::rel32(site + 5));
    } else {
        put({ 0x48, 0x8d, 0x8d, 0xe0, 0x07, 0, 0 }); // lea rcx,[rbp+0x7e0]
        put({ 0x4c, 0x89, 0xf2 });                   // mov rdx,r14
        put({ 0x45, 0x89, 0xe0 });                   // mov r8d,r12d
        put({ 0x45, 0x31, 0xc9 });                   // xor r9d,r9d (no empty function)
    }
    put({ 0x48, 0xb8 });                             // mov rax,rows_for
    put64(fn);
    put({ 0xff, 0xd0 });                             // call rax
    if (layout == 0) {
        put({ 0x41, 0xbe, 8, 0, 0, 0 });             // mov r14d,8
    } else {
        put({ 0x48, 0x8d, 0x85, 0xe0, 0x07, 0, 0 }); // lea rax,[rbp+0x7e0]
        put({ 0xbb, 8, 0, 0, 0 });                   // mov ebx,8
    }
    abs_jmp(p, back);                                // jmp back
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
    for (size_t l = 0; l < Sites::k_layouts && !s.rows_dispatch; l++)
        if (uint8_t *v = find(k_layouts[l].view)) {
            s.rows_dispatch = v + k_layouts[l].other;
            s.layout = l;
        }
    if (!s.rows_dispatch) return *why = "settings page view not found", false;
    const Layout &lay = k_layouts[s.layout];
    for (int i = 0; i < 2; i++) {
        if (!(s.names[i] = find(lay.names[i]))) return *why = "section names not found", false;
        auto *t = reinterpret_cast<const wchar_t *const *>(core::scan::rel32(s.names[i] + lay.names_disp[i]));
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
    const Layout &lay = k_layouts[s.layout];
    uint8_t *stub = make_stub(s.rows_dispatch, s.layout);
    uint8_t jmp[32];
    static_assert(sizeof jmp >= k_layouts[0].other_size && sizeof jmp >= k_layouts[1].other_size);
    memset(jmp, 0xcc, sizeof jmp);
    if (stub) abs_jmp(jmp, stub);
    if (!stub || !core::patch::write_code(s.rows_dispatch, jmp, lay.other_size)) {
        core::log::write("launcher: settings page view not patched: no Extra Options section");
        return false;
    }
    for (int i = 0; i < 2; i++) {
        uint8_t *disp = s.names[i] + lay.names_disp[i];
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

    core::log::write("launcher: settings page of Azimuth %s", lay.version);
    for (const game::Module *g : games) {
        int n = 0;
        for (const core::patch::Option &o : g->options) n += o.label != nullptr;
        core::log::write("launcher: Extra Options section for %s (title %s): %d options", g->tag, g->title_id, n);
    }
    return true;
}

} // namespace launcher
