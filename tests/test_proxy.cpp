#include "doctest.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::string> read_lines(const char *path)
{
    std::vector<std::string> out;
    if (FILE *f = fopen(path, "r")) {
        char buf[256];
        while (fgets(buf, sizeof buf, f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            out.push_back(s);
        }
        fclose(f);
    }
    return out;
}

// names from "X(name)" lines
std::vector<std::string> inc_names()
{
    std::vector<std::string> out;
    for (const std::string &l : read_lines(TEST_SOURCE_DIR "/src/proxy/winmm_exports.inc"))
        if (l.rfind("X(", 0) == 0 && l.back() == ')') out.push_back(l.substr(2, l.size() - 3));
    return out;
}

// names from "  name=fwd_name" lines; every line must forward to its own stub
std::vector<std::string> def_names()
{
    std::vector<std::string> out;
    for (const std::string &l : read_lines(TEST_SOURCE_DIR "/src/proxy/winmm.def")) {
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        size_t b = l.find_first_not_of(' ');
        std::string name = l.substr(b, eq - b);
        CHECK(l.substr(eq + 1) == "fwd_" + name);
        out.push_back(name);
    }
    return out;
}

} // namespace

TEST_CASE("proxy: winmm_exports.inc and winmm.def list the same exports")
{
    std::vector<std::string> inc = inc_names(), def = def_names();
    CHECK(inc.size() == 180);
    CHECK(inc == def);
}

TEST_CASE("proxy: the built winmm.dll forwards to System32")
{
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    REQUIRE(n);
    wcscpy(sys + n, L"\\winmm.dll");
    HMODULE real = LoadLibraryW(sys);
    REQUIRE(real);

    HMODULE proxy = LoadLibraryA(TEST_WINMM_DLL);
    REQUIRE(proxy);
    CHECK(proxy != real);

    // the export is our stub, and calling it gives what the real function gives
    using TimeGetTime = DWORD(WINAPI *)();
    auto ours = reinterpret_cast<TimeGetTime>((void *)GetProcAddress(proxy, "timeGetTime"));
    auto theirs = reinterpret_cast<TimeGetTime>((void *)GetProcAddress(real, "timeGetTime"));
    REQUIRE(ours);
    REQUIRE(theirs);
    CHECK(reinterpret_cast<void *>(ours) != reinterpret_cast<void *>(theirs));
    DWORD a = theirs(), b = ours();
    CHECK(b - a < 1000);

    using GetNumDevs = UINT(WINAPI *)();
    auto ours_n = reinterpret_cast<GetNumDevs>((void *)GetProcAddress(proxy, "waveOutGetNumDevs"));
    auto theirs_n = reinterpret_cast<GetNumDevs>((void *)GetProcAddress(real, "waveOutGetNumDevs"));
    REQUIRE(ours_n);
    REQUIRE(theirs_n);
    CHECK(ours_n() == theirs_n());

    FreeLibrary(proxy);
    FreeLibrary(real);
}
