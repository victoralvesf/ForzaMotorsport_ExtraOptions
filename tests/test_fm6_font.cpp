#include "doctest.h"

#include "font.hpp"

#include <cstring>
#include <string>

namespace {

size_t fix(const char16_t *in, uint16_t *buf, size_t n)
{
    memset(buf, 0xff, 64 * sizeof *buf);
    memcpy(buf, in, (std::char_traits<char16_t>::length(in) + 1) * sizeof *buf);
    return fm6::dash_font::fix_text(buf, n);
}

} // namespace

TEST_CASE("fm6 dash font: pt-BR lap time text as in English")
{
    uint16_t s[64];
    CHECK(fix(u"1'02,345''", s, 11) == 8);
    CHECK(memcmp(s, u"1:02,345\0\0\0", 11 * 2) == 0);

    CHECK(fix(u"12,9''", s, 7) == 4); // only seconds
    CHECK(memcmp(s, u"12,9\0\0\0", 7 * 2) == 0);

    CHECK(fix(u"1'02", s, 3) == 3);   // n stops before the NUL
    CHECK(memcmp(s, u"1:0", 3 * 2) == 0);
    CHECK(s[3] == '2');

    CHECK(fix(u"0:12", s, 5) == 4);   // nothing to change
    CHECK(memcmp(s, u"0:12", 5 * 2) == 0);
}
