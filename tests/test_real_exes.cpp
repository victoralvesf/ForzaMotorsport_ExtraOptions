// The options on the real exes of FM5 1.0.0.20 and FM6 1.0.0.44 (game_binaries\, local only), mapped as Azimuth
// maps them. Without the files these tests only print a message (CI).
#include "doctest.h"

#include "expected.hpp"
#include "image.hpp"

#include "options.hpp"
#include "registry.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using expected::Change;
using expected::hex;

namespace {

std::span<const Change> changes(const char *tag)
{
    if (strcmp(tag, "FM5") == 0) return expected::k_fm5;
    return expected::k_fm6;
}

uint32_t lambda_of(const char *tag) { return strcmp(tag, "FM5") == 0 ? expected::k_fm5_lambda : expected::k_fm6_lambda; }

// an ini with every option of g on
std::wstring ini_all_on(const game::Module &g)
{
    std::string path = TEST_OUT_DIR "/test_real_" + std::string(g.tag) + ".ini";
    FILE *f = fopen(path.c_str(), "w");
    REQUIRE(f);
    fprintf(f, "[%s]\n", g.tag);
    for (const core::patch::Option &o : g.options) fprintf(f, "%s=1\n", o.key);
    fclose(f);
    return std::wstring(path.begin(), path.end());
}

bool same(const uint8_t *p, const char *want)
{
    std::vector<uint8_t> w = hex(want);
    return memcmp(p, w.data(), w.size()) == 0;
}

// Maps the game's exe; null (with a message) when game_binaries\ is not here.
std::unique_ptr<test_image::Mapped> map(const char *tag)
{
    auto img = std::make_unique<test_image::Mapped>(test_image::game_exe(tag));
    if (img->base) return img;
    MESSAGE("game_binaries\\" << std::string(tag) << " missing: test skipped");
    return nullptr;
}

} // namespace

TEST_CASE("real exes: identified by the PE header, with the expected bytes at every site")
{
    for (const char *tag : { "FM5", "FM6" }) {
        CAPTURE(tag);
        auto img = map(tag);
        if (!img) continue;
        const game::Module *g = games::find(img->nt->FileHeader.TimeDateStamp, img->nt->OptionalHeader.SizeOfImage);
        REQUIRE(g);
        CHECK(strcmp(g->tag, tag) == 0);
        for (const Change &c : changes(tag)) {
            CAPTURE(c.rva);
            CHECK(same(img->base + c.rva, c.before));
        }
        if (strcmp(tag, "FM6") == 0) {
            CHECK(same(img->base + expected::k_fm6_measure, expected::k_fm6_measure_before));
            CHECK(same(img->base + expected::k_fm6_find, expected::k_fm6_find_before));
        }
    }
}

TEST_CASE("real exes: every option applies, on the original and after Azimuth's DLC patch")
{
    for (const char *tag : { "FM5", "FM6" }) {
        for (bool azimuth : { false, true }) {
            CAPTURE(tag);
            CAPTURE(azimuth);
            auto img = map(tag);
            if (!img) break;
            const game::Module *g = games::find(img->nt->FileHeader.TimeDateStamp, img->nt->OptionalHeader.SizeOfImage);
            REQUIRE(g);
            if (azimuth) {
                std::vector<uint8_t> a = hex(expected::k_lambda_azimuth);
                REQUIRE(core::patch::write_code(img->base + lambda_of(tag), a.data(), a.size()));
            }
            CHECK(core::options::apply(*g, img->base, ini_all_on(*g).c_str()) == static_cast<int>(g->options.size()));
            for (const Change &c : changes(tag)) {
                CAPTURE(c.rva);
                CHECK(same(img->base + c.rva, c.after));
            }
            if (strcmp(tag, "FM6") == 0) CHECK(same(img->base + expected::k_fm6_measure, "ff 25 00 00 00 00"));
            }
    }
}

TEST_CASE("real exes: one game's options do not match the other game's exe")
{
    for (const char *tag : { "FM5", "FM6" }) {
        CAPTURE(tag);
        auto img = map(tag);
        if (!img) continue;
        for (const game::Module *other : games::all()) {
            if (strcmp(other->tag, tag) == 0) continue;
            for (const core::patch::Option &o : other->options) {
                CAPTURE(o.key);
                CHECK(core::patch::apply(img->base, img->nt->OptionalHeader.SizeOfImage, o, other->tag) ==
                      core::patch::Result::mismatch);
            }
        }
    }
}
