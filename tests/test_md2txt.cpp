#include "doctest.h"

#include "md2txt.hpp"

#include <cstdio>
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

TEST_CASE("md2txt: headings, inline markup, quotes and code fences")
{
    std::string md = "# Title `x`\n"
                     "\n"
                     "## Sub **bold**\n"
                     "Use **this** and `that`.\n"
                     "> quoted `a`\n"
                     ">\n"
                     "```\n"
                     "keep **as is**\n"
                     "```\n"
                     "\n\n";
    CHECK(md2txt::convert(md) == "Title x\r\n"
                                 "=======\r\n"
                                 "\r\n"
                                 "Sub bold\r\n"
                                 "--------\r\n"
                                 "Use this and that.\r\n"
                                 "quoted a\r\n"
                                 "\r\n"
                                 "keep **as is**\r\n");
}

TEST_CASE("md2txt: CRLF input gives the same output")
{
    CHECK(md2txt::convert("## A\r\ntext\r\n") == md2txt::convert("## A\ntext\n"));
}

TEST_CASE("md2txt: a table becomes aligned columns")
{
    std::string md = "| Option | Default | What |\n"
                     "|---|---|---|\n"
                     "| `SkipIntro` | 1 | Skips it. |\n"
                     "| `UnlockDLC` | 0 | **All** DLC. |\n";
    CHECK(md2txt::convert(md) == "Option     Default  What\r\n"
                                 "---------  -------  ----\r\n"
                                 "SkipIntro  1        Skips it.\r\n"
                                 "UnlockDLC  0        All DLC.\r\n");
}

TEST_CASE("md2txt: the last table column wraps under itself")
{
    std::string words;
    for (int i = 0; i < 30; i++) words += "word ";
    std::string txt = md2txt::convert("| K | V |\n|---|---|\n| a | " + words + "|\n");
    // "a" + 2 spaces, then the text wrapped at k_width with a 3-column hanging indent
    size_t first = txt.find("a  word");
    REQUIRE(first != std::string::npos);
    size_t eol = txt.find("\r\n", first);
    CHECK(eol - first <= md2txt::k_width);
    CHECK(txt.compare(eol + 2, 7, "   word") == 0);
    CHECK(txt.find("\r\n\r\n") == std::string::npos);
}

TEST_CASE("md2txt: the README converts without markdown left")
{
    std::string md = read_all(TEST_SOURCE_DIR "/README.md");
    REQUIRE(!md.empty());
    std::string txt = md2txt::convert(md);
    CHECK(txt.find("**") == std::string::npos);
    CHECK(txt.find('`') == std::string::npos);
    CHECK(txt.find("|---") == std::string::npos);
    CHECK(txt.find("\n#") == std::string::npos);
    CHECK(txt.find("\n> ") == std::string::npos);
    CHECK(txt.find("winmm.dll") != std::string::npos);
    for (size_t p = 0; (p = txt.find('\n', p)) != std::string::npos; p++) CHECK((p > 0 && txt[p - 1] == '\r'));
}
