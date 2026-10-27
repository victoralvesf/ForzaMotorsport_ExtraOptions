#include "doctest.h"

#include "hook.hpp"
#include "ini.hpp"
#include "loader_watch.hpp"
#include "patch.hpp"
#include "pe.hpp"
#include "version.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>

using core::patch::Option;
using core::patch::Result;
using core::patch::Site;

namespace {

// a page of fake game code
struct Code {
    uint8_t *p;
    Code() { p = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ)); }
    ~Code() { VirtualFree(p, 0, MEM_RELEASE); }
    void set(size_t at, const uint8_t *b, size_t n) { core::patch::write_code(p + at, b, n); }
};

} // namespace

TEST_CASE("patch: parse hex with spaces and wildcards")
{
    core::patch::Bytes b;
    REQUIRE(core::patch::parse("48 8b??c3", 9, b, true));
    CHECK(b.size == 4);
    CHECK(b.value[0] == 0x48);
    CHECK(b.value[1] == 0x8b);
    CHECK(b.any[2]);
    CHECK(b.value[3] == 0xc3);
    CHECK(!core::patch::parse("48 ??", 5, b, false)); // wildcard not allowed
    CHECK(!core::patch::parse("4", 1, b, true));
    CHECK(!core::patch::parse("4g", 2, b, true));
    CHECK(!core::patch::parse("", 0, b, true));
}

TEST_CASE("patch: site validity")
{
    CHECK(core::patch::valid({ 0, "80 be 9f 30 00 00 00", 6, "ff" }));
    CHECK(core::patch::valid({ 0, "aa bb|cc dd ee", 0, "" }));
    CHECK(!core::patch::valid({ 0, "80 be", 1, "ff ff" }));       // write past the checked bytes
    CHECK(!core::patch::valid({ 0, "aa bb cc|dd", 1, "ff" }));    // ... of one of the forms
    CHECK(!core::patch::valid({ 0, "aa", 0, "??" }));             // wildcard in write
    CHECK(!core::patch::valid({ 0, "aa||bb", 0, "" }));           // empty form
}

TEST_CASE("patch: match picks the first matching form")
{
    const uint8_t code[] = { 0x48, 0x89, 0xd9, 0x48, 0xb8, 1, 2, 3, 4, 5, 6, 7, 8, 0xff, 0xd0, 0x90 };
    size_t size = 0;
    CHECK(core::patch::match(code, sizeof code, "48 8b 0d|48 89 d9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90", &size) == 1);
    CHECK(size == 16);
    CHECK(core::patch::match(code, sizeof code, "48 89", &size) == 0);
    CHECK(size == 2);
    CHECK(core::patch::match(code, sizeof code, "48 8b|49", nullptr) == -1);
    CHECK(core::patch::match(code, 2, "48 89 d9", nullptr) == -1); // not enough bytes
}

TEST_CASE("patch: an option is written only when every site matches")
{
    Code c;
    const uint8_t a[] = { 0x80, 0xbe, 0x9f, 0x30, 0x00, 0x00, 0x00 };
    const uint8_t b[] = { 0x85, 0xc0, 0x75, 0x19 };
    c.set(0x10, a, sizeof a);
    c.set(0x40, b, sizeof b);

    const Site bad[] = { { 0x10, "80 be 9f 30 00 00 00", 6, "ff" }, { 0x40, "85 c0 74 19", 2, "66 90" } };
    CHECK(core::patch::apply(c.p, 0x1000, Option{ "Bad", bad }, "TEST") == Result::mismatch);
    CHECK(c.p[0x16] == 0x00); // first site untouched

    const Site outside[] = { { 0x10, "80 be 9f 30 00 00 00", 6, "ff" }, { 0x2000, "00", 0, "01" } };
    CHECK(core::patch::apply(c.p, 0x1000, Option{ "Outside", outside }, "TEST") == Result::mismatch);
    CHECK(c.p[0x16] == 0x00);

    const Site malformed[] = { { 0x10, "80 be 9f 30 00 00 00", 6, "ff" }, { 0x40, "85 c0", 2, "66 90" } };
    CHECK(core::patch::apply(c.p, 0x1000, Option{ "Malformed", malformed }, "TEST") == Result::invalid);
    CHECK(c.p[0x16] == 0x00);

    const Site good[] = { { 0x10, "80 be 9f 30 00 00 00", 6, "ff" }, { 0x40, "85 c0 75 19", 2, "66 90" },
                          { 0x40, "85 c0", 0, "" } };
    CHECK(core::patch::apply(c.p, 0x1000, Option{ "Good", good }, "TEST") == Result::applied);
    const uint8_t a2[] = { 0x80, 0xbe, 0x9f, 0x30, 0x00, 0x00, 0xff };
    const uint8_t b2[] = { 0x85, 0xc0, 0x66, 0x90 };
    CHECK(memcmp(c.p + 0x10, a2, sizeof a2) == 0);
    CHECK(memcmp(c.p + 0x40, b2, sizeof b2) == 0);

    MEMORY_BASIC_INFORMATION mi;
    VirtualQuery(c.p, &mi, sizeof mi);
    CHECK(mi.Protect == PAGE_EXECUTE_READ); // protection restored
}

TEST_CASE("patch: a site at the end of a section is compared up to the end of its region")
{
    // two pages with different protections (Azimuth protects each section): a short site in the last bytes of
    // the first one is checked and written, a longer form than what is left there does not match
    auto *p = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ));
    REQUIRE(p);
    DWORD old;
    VirtualProtect(p + 0x1000, 0x1000, PAGE_READONLY, &old);
    const uint8_t b[] = { 0x85, 0xc0, 0x75, 0x19 };
    core::patch::write_code(p + 0xffc, b, sizeof b);
    CHECK(core::pe::readable_size(p + 0xffc, 32) == 4);
    CHECK(core::pe::readable_size(p + 0x1000, 32) == 32);
    CHECK(core::pe::readable_size(p + 0x10, 8) == 8);

    const Site longer[] = { { 0xffc, "85 c0 75 19 00", 2, "66 90" } };
    CHECK(core::patch::apply(p, 0x2000, Option{ "Longer", longer }, "TEST") == Result::mismatch);
    const Site end[] = { { 0xffc, "85 c0 75 19", 2, "66 90" } };
    CHECK(core::patch::apply(p, 0x2000, Option{ "End", end }, "TEST") == Result::applied);
    CHECK(p[0xffe] == 0x66);
    CHECK(p[0xfff] == 0x90);
    VirtualFree(p, 0, MEM_RELEASE);

    CHECK(core::pe::readable_size(reinterpret_cast<void *>(0x10), 8) == 0); // not mapped
}

namespace {

int (*g_add_tramp)(int, int);
int add_hooked(int x, int y) { return g_add_tramp(x, y) * 100; }

} // namespace

TEST_CASE("hook: detour with trampoline")
{
    Code c;
    // lea eax,[rcx+rdx] ; nop x12 ; ret  -> 15 stolen bytes = lea + 12 nops
    uint8_t fn[16] = { 0x8d, 0x04, 0x11 };
    memset(fn + 3, 0x90, 12);
    fn[15] = 0xc3;
    c.set(0, fn, sizeof fn);
    auto add = reinterpret_cast<int (*)(int, int)>(c.p);
    REQUIRE(add(2, 3) == 5);

    void *tramp = nullptr;
    CHECK(!core::hook::install(c.p, "8d 04 12 90 90 90 90 90 90 90 90 90 90 90 90", reinterpret_cast<void *>(add_hooked),
                               &tramp)); // bytes differ
    CHECK(!core::hook::install(c.p, "8d 04 11 90 90", reinterpret_cast<void *>(add_hooked), &tramp)); // too short
    CHECK(add(2, 3) == 5);

    REQUIRE(core::hook::install(c.p, "8d 04 11 90 90 90 90 90 90 90 90 90 90 90 90",
                                reinterpret_cast<void *>(add_hooked), &tramp));
    g_add_tramp = reinterpret_cast<int (*)(int, int)>(tramp);
    CHECK(add(2, 3) == 500);
    CHECK(g_add_tramp(4, 4) == 8);
}

TEST_CASE("ini: values of a section")
{
    const char *path = TEST_OUT_DIR "/test_ini.ini";
    const wchar_t *wpath = L"" TEST_OUT_DIR "/test_ini.ini";
    FILE *f = fopen(path, "w");
    REQUIRE(f);
    fputs("; comment\n[FM5]\nSkipIntro=1\nUnlockDLC = 0\n[FM6]\nSkipIntro=2\n", f);
    fclose(f);
    CHECK(core::ini::get_int(wpath, "FM5", "SkipIntro", 0) == 1);
    CHECK(core::ini::get_int(wpath, "FM5", "UnlockDLC", 1) == 0);
    CHECK(core::ini::get_int(wpath, "FM5", "Missing", 7) == 7);
    CHECK(core::ini::get_int(wpath, "FM6", "SkipIntro", 0) == 2);
    CHECK(core::ini::get_int(wpath, "FM7", "SkipIntro", 3) == 3);
    remove(path);
    CHECK(core::ini::get_int(wpath, "FM5", "SkipIntro", 0) == 0); // no file
}

TEST_CASE("pe: headers, IAT slot by name and version")
{
    HMODULE self = GetModuleHandleW(nullptr);
    const IMAGE_NT_HEADERS64 *nt = core::pe::headers(self);
    REQUIRE(nt);
    CHECK(nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    const uint8_t zeros[64] = {};
    CHECK(core::pe::headers(zeros) == nullptr);

    void **slot = core::pe::iat_slot(self, "kernel32.dll", "GetCurrentProcessId");
    REQUIRE(slot);
    CHECK(*slot == reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessId")));
    CHECK(core::pe::iat_slot(self, "kernel32.dll", "NoSuchFunction") == nullptr);
    CHECK(core::pe::iat_slot(self, "nosuch.dll", "GetCurrentProcessId") == nullptr);

    core::version::Version v;
    REQUIRE(core::version::of_module(GetModuleHandleW(L"kernel32.dll"), v));
    CHECK(v.major >= 6);
    CHECK(!core::version::of_module(self, v)); // the test runner has no VERSIONINFO

    CHECK(core::version::at_least({ 1, 0, 2, 0 }, 1, 0, 2));
    CHECK(core::version::at_least({ 1, 1, 0, 0 }, 1, 0, 2));
    CHECK(core::version::at_least({ 2, 0, 0, 0 }, 1, 0, 2));
    CHECK(!core::version::at_least({ 1, 0, 1, 9 }, 1, 0, 2));
    CHECK(!core::version::at_least({ 0, 9, 9, 0 }, 1, 0, 2));
}

namespace {

uint8_t *g_seen;
int g_seen_count;
void on_image(uint8_t *base, const IMAGE_NT_HEADERS64 *) { g_seen = base, g_seen_count++; }

} // namespace

TEST_CASE("loader_watch: whole PE images flushed through the IAT are reported")
{
    // the runner imports FlushInstructionCache (patch::write_code): hook its own IAT
    HMODULE self = GetModuleHandleW(nullptr);
    REQUIRE(core::loader_watch::install(self, on_image));
    const IMAGE_NT_HEADERS64 *nt = core::pe::headers(self);
    HANDLE proc = GetCurrentProcess();
    auto flush = reinterpret_cast<BOOL(WINAPI *)(HANDLE, LPCVOID, SIZE_T)>(
        *core::pe::iat_slot(self, "KERNEL32.dll", "FlushInstructionCache"));

    CHECK(flush(proc, self, nt->OptionalHeader.SizeOfImage));
    CHECK(g_seen_count == 1);
    CHECK(g_seen == reinterpret_cast<uint8_t *>(self));

    flush(proc, self, 0x1000);                                      // not the whole image
    Code c;
    flush(proc, c.p, 0x1000);                                       // no PE at the base
    CHECK(g_seen_count == 1);
}
