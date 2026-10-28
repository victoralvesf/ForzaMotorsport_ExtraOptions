// The Extra Options section of Azimuth's launcher: MSVC layouts, the rows, the game of a Settings, and the sites in
// the real azimuth.exe 1.0.2 (game_binaries\AZIMUTH\, local only; skipped without it).
#include "doctest.h"

#include "expected.hpp"
#include "image.hpp"

#include "hook.hpp"
#include "ini.hpp"
#include "launcher.hpp"
#include "msvc.hpp"
#include "registry.hpp"
#include "rows.hpp"
#include "scan.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using launcher::Row;

namespace {

std::wstring text(const msvc::WString &w) { return std::wstring(w.data(), w.size); }

constexpr core::patch::Site k_no_sites[] = { { 0, "90", 0, "" } };
constexpr core::patch::Option k_test_options[] = {
    { "First", k_no_sites, "First option" },
    { "Hidden", k_no_sites },
    { "Third", k_no_sites, "A label longer than seven characters" },
};
const game::Module k_test_game = {
    .tag = "TEST",
    .title_id = "0badf00d",
    .time_date_stamp = 0,
    .size_of_image = 0,
    .options = k_test_options,
    .apply_fixes = nullptr,
};

// _Func_base vtable slots of a row's on_change
using Copy = void *(*)(void *self, void *where);
using DoCall = int32_t (*)(void *self, void *settings, const size_t *choice);
const void *const *vft_of(const Row &r) { return *static_cast<const void *const *const *>(r.on_change.impl); }

// a Settings whose current title id (std::wstring at k_current_game) is id
struct FakeSettings {
    alignas(8) uint8_t bytes[0x100] = {};
    explicit FakeSettings(const wchar_t *id)
    {
        auto *w = reinterpret_cast<msvc::WString *>(bytes + launcher::k_current_game);
        w->cap = 7;
        REQUIRE(msvc::assign(*w, id));
    }
    ~FakeSettings() { msvc::destroy(*reinterpret_cast<msvc::WString *>(bytes + launcher::k_current_game)); }
};

std::string read_file(const char *path)
{
    std::string s;
    FILE *f = fopen(path, "rb");
    if (!f) return s;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    fclose(f);
    return s;
}

} // namespace

TEST_CASE("msvc: wstring inline up to 7 characters, on the heap above")
{
    msvc::WString w{};
    w.cap = 7;
    REQUIRE(msvc::assign(w, "Off"));
    CHECK(w.cap == 7);
    CHECK(w.size == 3);
    CHECK(w.data() == w.buf);
    CHECK(text(w) == L"Off");
    msvc::destroy(w);

    REQUIRE(msvc::assign(w, L"Extra Options"));
    CHECK(w.cap == 15); // 13 | 7
    CHECK(w.size == 13);
    CHECK(w.data() == w.ptr);
    CHECK(w.ptr[13] == 0);
    CHECK(text(w) == L"Extra Options");
    msvc::destroy(w);
    CHECK(w.cap == 7);

    CHECK(msvc::alloc(0) == nullptr);
    CHECK(msvc::alloc(0x1000) == nullptr); // MSVC frees blocks of 4 KB and more another way
    msvc::Vector<Row> v;
    CHECK(!msvc::reserve(v, 0x1000 / sizeof(Row) + 1));
}

TEST_CASE("rows: an Off/On row per labelled option, saved to the ini when changed")
{
    const char *ini = TEST_OUT_DIR "/test_launcher.ini";
    FILE *f = fopen(ini, "w");
    REQUIRE(f);
    fputs("; comment kept\n[TEST]\nFirst=0\nHidden=1\nThird=1\n", f);
    fclose(f);
    std::string s(ini);
    std::wstring wini(s.begin(), s.end());
    launcher::set_ini(wini.c_str());

    msvc::Vector<Row> rows;
    REQUIRE(launcher::fill_rows(rows, k_test_game));
    REQUIRE(rows.size() == 2); // "Hidden" has no label
    const Row &a = rows.first[0], &b = rows.first[1];
    CHECK(strcmp(a.key, "First") == 0);
    CHECK(text(a.label) == L"First option");
    CHECK(a.selected == 0);
    CHECK(strcmp(b.key, "Third") == 0);
    CHECK(text(b.label) == L"A label longer than seven characters");
    CHECK(b.selected == 1);
    for (const Row *r : { &a, &b }) {
        CHECK(r->control == launcher::k_control_toggle);
        CHECK(!r->disabled);
        REQUIRE(r->choices.size() == 2);
        CHECK(text(r->choices.first[0]) == L"Off");
        CHECK(text(r->choices.first[1]) == L"On");
        CHECK(r->on_change.impl == r->on_change.storage); // inline: Azimuth never frees it
    }

    // the launcher calls _Do_call(settings, choice) when the player picks On for "First"
    size_t on = 1;
    auto call = reinterpret_cast<DoCall>(vft_of(a)[2]);
    CHECK(call(a.on_change.impl, nullptr, &on) == launcher::k_no_setting);
    CHECK(launcher::value(k_test_game, 0) == 1);
    CHECK(core::ini::get_int(wini.c_str(), "TEST", "First", -1) == 1);

    // a copy (Azimuth copies rows) calls the same option; Off for "Third"
    msvc::Function copy{};
    copy.impl = reinterpret_cast<Copy>(vft_of(b)[0])(b.on_change.impl, copy.storage);
    CHECK(copy.impl == copy.storage);
    size_t off = 0;
    CHECK(reinterpret_cast<DoCall>(vft_of(b)[2])(copy.impl, nullptr, &off) == launcher::k_no_setting);
    CHECK(core::ini::get_int(wini.c_str(), "TEST", "Third", -1) == 0);
    CHECK(core::ini::get_int(wini.c_str(), "TEST", "Hidden", -1) == 1);
    CHECK(read_file(ini).find("; comment kept") != std::string::npos);

    // rebuilt rows show the new values
    launcher::destroy_rows(rows);
    REQUIRE(launcher::fill_rows(rows, k_test_game));
    CHECK(rows.first[0].selected == 1);
    CHECK(rows.first[1].selected == 0);
    launcher::destroy_rows(rows);
    CHECK(rows.first == nullptr);
}

TEST_CASE("launcher: the game of a Settings is its current title id, any case")
{
    auto all = games::all();
    CHECK(strcmp(launcher::game_of(FakeSettings(L"7b35d917").bytes, all)->tag, "FM5") == 0);
    CHECK(strcmp(launcher::game_of(FakeSettings(L"7B35D917").bytes, all)->tag, "FM5") == 0);
    CHECK(strcmp(launcher::game_of(FakeSettings(L"64f9397e").bytes, all)->tag, "FM6") == 0);
    CHECK(launcher::game_of(FakeSettings(L"1a96545b").bytes, all) == nullptr); // Forza Horizon 2
    CHECK(launcher::game_of(FakeSettings(L"7b35d91").bytes, all) == nullptr);
    CHECK(launcher::game_of(FakeSettings(L"").bytes, all) == nullptr);
    CHECK(launcher::game_of(nullptr, all) == nullptr);
}

TEST_CASE("scan: a signature must match exactly once")
{
    auto *self = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
    CHECK(core::scan::find_unique(self, "cc cc") == nullptr); // everywhere
    CHECK(core::scan::find_unique(self, "zz") == nullptr);
    CHECK(core::scan::find_unique(self, "?? ??") == nullptr);
    uint8_t p[] = { 0x10, 0xfe, 0xff, 0xff, 0xff };
    CHECK(core::scan::rel32(p + 1) == p + 3);
    CHECK(core::scan::rel32(p + 1, 1) == p + 4);
}

TEST_CASE("hook: memory near an address is reachable with a rel32")
{
    auto *self = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
    void *p = core::hook::alloc_near(self, 0x1000, PAGE_READWRITE);
    REQUIRE(p);
    CHECK(core::hook::reachable(self, p, 0x1000));
    VirtualFree(p, 0, MEM_RELEASE);
    CHECK(!core::hook::reachable(self, self + 0x80000000ull, 1));
    CHECK(core::hook::reachable(self, self + 0x7fffff00ull, 0x100));
}

TEST_CASE("real azimuth.exe 1.0.2: every launcher site is found, then patched for the Extra Options section")
{
    test_image::Mapped img(test_image::game_exe("AZIMUTH"));
    if (!img.base) {
        MESSAGE("game_binaries\\AZIMUTH missing: test skipped");
        return;
    }
    launcher::Sites s;
    const char *why = "";
    REQUIRE_MESSAGE(launcher::locate(img.base, s, &why), why);
    // RVAs from the analysis of 1.0.2 (docs, local)
    CHECK(s.build_sections - img.base == 0xfd170);
    CHECK(s.rows_dispatch - img.base == 0x108d18);
    CHECK(s.names[0] - img.base == 0x109ef0);
    CHECK(s.names[1] - img.base == 0x1102da);
    CHECK(reinterpret_cast<const uint8_t *>(s.table) - img.base == 0x7faa00);
    CHECK(s.current_game);
    // lea rcx,[rbp-0x30]; call <empty vector>; mov r14d,8
    std::vector<uint8_t> before = expected::hex("48 8d 4d d0 e8 7f 87 f1 ff 41 be 08 00 00 00");
    CHECK(memcmp(s.rows_dispatch, before.data(), before.size()) == 0);

    REQUIRE(launcher::install(reinterpret_cast<HMODULE>(img.base), games::all(), L"unused.ini"));
    CHECK(memcmp(s.rows_dispatch, "\xff\x25\0\0\0\0", 6) == 0);
    CHECK(memcmp(s.build_sections, "\xff\x25\0\0\0\0", 6) == 0);
    const uint8_t *disp[2] = { s.names[0] + 6, s.names[1] + 7 };
    auto *t0 = reinterpret_cast<const wchar_t *const *>(core::scan::rel32(disp[0]));
    auto *t1 = reinterpret_cast<const wchar_t *const *>(core::scan::rel32(disp[1]));
    CHECK(t0 == t1);
    CHECK(memcmp(t0, s.table, 4 * sizeof(void *)) == 0);
    CHECK(std::wstring(t0[launcher::k_section]) == L"Extra Options");

    // once patched, the view no longer matches: a second install changes nothing
    CHECK(!launcher::locate(img.base, s, &why));
}
