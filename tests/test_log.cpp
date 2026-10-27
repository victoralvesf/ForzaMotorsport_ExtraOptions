#include "doctest.h"

#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string read_all(const char *path)
{
    std::string s;
    if (FILE *f = fopen(path, "rb")) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        fclose(f);
    }
    return s;
}

} // namespace

TEST_CASE("log: lines are written and the file is recreated on open")
{
    const char *path = TEST_OUT_DIR "/test_log.log";
    const wchar_t *wpath = L"" TEST_OUT_DIR "/test_log.log";

    REQUIRE(core::log::open(wpath));
    core::log::write("first run %d", 1);
    core::log::close();
    std::string first = read_all(path);
    CHECK(first.find("] first run 1\r\n") != std::string::npos); // text mode: CRLF
    CHECK(first.front() == '[');

    REQUIRE(core::log::open(wpath));
    core::log::write("second run");
    core::log::close();
    std::string second = read_all(path);
    CHECK(second.find("first run") == std::string::npos);
    CHECK(second.find("] second run\r\n") != std::string::npos);

    core::log::write("after close"); // no file: ignored
    CHECK(read_all(path) == second);
    remove(path);
}
