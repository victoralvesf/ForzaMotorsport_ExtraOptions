// Every option of FM5 and FM6 on a fake image holding the game's bytes at the sites, through the ini as the mod
// reads it (core::options::apply), against the bytes written in expected.hpp.
#include "doctest.h"

#include "expected.hpp"
#include "image.hpp"

#include "options.hpp"
#include "registry.hpp"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using expected::Change;
using expected::hex;

extern "C" void fm6_font_measure_stub();
extern "C" void *fm6_font_measure_tramp;

namespace {

const game::Module &module(const char *tag)
{
    for (const game::Module *g : games::all())
        if (strcmp(g->tag, tag) == 0) return *g;
    FAIL("no module " << tag);
    throw 0;
}

std::span<const Change> changes(const char *tag)
{
    if (strcmp(tag, "FM5") == 0) return expected::k_fm5;
    return expected::k_fm6;
}

// writes an ini with [tag] key=1 for each key in on
std::wstring ini(const char *tag, const std::set<std::string> &on)
{
    std::string path = TEST_OUT_DIR "/test_options_" + std::string(tag) + ".ini";
    FILE *f = fopen(path.c_str(), "w");
    REQUIRE(f);
    fprintf(f, "[%s]\n", tag);
    for (const game::Module *g : games::all())
        if (strcmp(g->tag, tag) == 0)
            for (const core::patch::Option &o : g->options) fprintf(f, "%s=%d\n", o.key, on.count(o.key) ? 1 : 0);
    fclose(f);
    return std::wstring(path.begin(), path.end());
}

void put_game(test_image::Fake &img, const char *tag)
{
    for (const Change &c : changes(tag)) img.put(c.rva, hex(c.before));
    if (strcmp(tag, "FM6") == 0) {
        img.put(expected::k_fm6_measure, hex(expected::k_fm6_measure_before));
        img.put(expected::k_fm6_find, hex(expected::k_fm6_find_before));
        for (const expected::Text &t : expected::k_fm6_stock_wheels_texts)
            img.put(t.rva, std::span(reinterpret_cast<const uint8_t *>(t.text), strlen(t.text) + 1));
    }
}

// want == null: the option's code rewrote the site (checked in its own test)
void check_bytes(const uint8_t *base, uint32_t rva, const char *want)
{
    if (!want) return;
    std::vector<uint8_t> w = hex(want);
    CAPTURE(rva);
    CAPTURE(want);
    CHECK(memcmp(base + rva, w.data(), w.size()) == 0);
}

// every site holds "after" when its option is in on, "before" otherwise
void check_game(const uint8_t *base, const char *tag, const std::set<std::string> &on)
{
    for (const Change &c : changes(tag)) {
        CAPTURE(c.option);
        check_bytes(base, c.rva, on.count(c.option) ? c.after : c.before);
    }
}

void check_fm6_font_hooked(const uint8_t *base)
{
    const uint8_t *p = base + expected::k_fm6_measure;
    check_bytes(base, expected::k_fm6_measure, "ff 25 00 00 00 00");
    void *to;
    memcpy(&to, p + 6, 8);
    CHECK(to == reinterpret_cast<void *>(fm6_font_measure_stub));
    CHECK(p[14] == 0xcc);
    check_bytes(base, expected::k_fm6_find, expected::k_fm6_find_before);

    // the trampoline runs the 15 original bytes and jumps back after them
    auto *t = static_cast<const uint8_t *>(fm6_font_measure_tramp);
    REQUIRE(t);
    std::vector<uint8_t> stolen = hex(expected::k_fm6_measure_before);
    CHECK(memcmp(t, stolen.data(), stolen.size()) == 0);
    CHECK(memcmp(t + 15, hex("ff 25 00 00 00 00").data(), 6) == 0);
    memcpy(&to, t + 21, 8);
    CHECK(to == p + 15);
}

std::set<std::string> all_keys(const char *tag)
{
    std::set<std::string> keys;
    for (const core::patch::Option &o : module(tag).options) keys.insert(o.key);
    return keys;
}

} // namespace

TEST_CASE("options: the expected changes cover every option of the tables, and nothing more")
{
    for (const char *tag : { "FM5", "FM6" }) {
        CAPTURE(tag);
        std::set<std::string> in_expected;
        for (const Change &c : changes(tag)) in_expected.insert(c.option);
        CHECK(in_expected == all_keys(tag));

        // each changed site of the tables appears in expected.hpp (same rva and option)
        for (const core::patch::Option &o : module(tag).options)
            for (const core::patch::Site &s : o.sites) {
                bool found = false;
                for (const Change &c : changes(tag)) found |= c.rva == s.rva && strcmp(c.option, o.key) == 0;
                CAPTURE(o.key);
                CAPTURE(s.rva);
                CHECK(found);
            }
    }
}

TEST_CASE("options: FM5 jump of SkipPresentationVideo lands on the checked callback")
{
    std::vector<uint8_t> a = hex(expected::k_fm5[3].after);
    REQUIRE(a[0] == 0xe9);
    int32_t rel;
    memcpy(&rel, &a[1], 4);
    CHECK(expected::k_fm5[3].rva + 5 + rel == expected::k_fm5[4].rva);
}

TEST_CASE("options: FM6 UnlockMenuFrameRate stub jumps back to the setter and the vtables point to the stub")
{
    const Change *setter = nullptr;
    for (const Change &c : expected::k_fm6)
        if (strcmp(c.option, "UnlockMenuFrameRate") == 0 && c.rva == 0x849940) setter = &c;
    REQUIRE(setter);
    std::vector<uint8_t> a = hex(setter->after);
    // jne rel8 at +10 and jmp rel8 at +14 both land on the setter at +0
    CHECK(a[10] == 0x75);
    CHECK(12 + static_cast<int8_t>(a[11]) == 0);
    CHECK(a[14] == 0xeb);
    CHECK(16 + static_cast<int8_t>(a[15]) == 0);
    for (const Change &c : expected::k_fm6)
        if (strcmp(c.option, "UnlockMenuFrameRate") == 0 && c.rva != setter->rva) {
            std::vector<uint8_t> v = hex(c.after);
            uint64_t to;
            memcpy(&to, v.data(), 8);
            CHECK(to == 0x140000000 + setter->rva + 7);
        }
}

TEST_CASE("options: each option alone, all on and all off")
{
    for (const char *tag : { "FM5", "FM6" }) {
        const game::Module &g = module(tag);
        std::vector<std::set<std::string>> cases = { {}, all_keys(tag) };
        for (const core::patch::Option &o : g.options) cases.push_back({ o.key });
        for (const auto &on : cases) {
            CAPTURE(tag);
            CAPTURE(on.size());
            test_image::Fake img(g.size_of_image);
            put_game(img, tag);
            CHECK(core::options::apply(g, img.base, ini(tag, on).c_str()) == static_cast<int>(on.size()));
            check_game(img.base, tag, on);
            if (strcmp(tag, "FM6") == 0) check_fm6_font_hooked(img.base);
        }
    }
}

TEST_CASE("options: everything is off without the ini")
{
    for (const char *tag : { "FM5", "FM6" }) {
        CAPTURE(tag);
        const game::Module &g = module(tag);
        test_image::Fake img(g.size_of_image);
        put_game(img, tag);
        CHECK(core::options::apply(g, img.base, L"" TEST_OUT_DIR "/no_such.ini") == 0);
        check_game(img.base, tag, {});
    }
}

TEST_CASE("options: [GLOBAL] DisableLog turns the log off only when set")
{
    CHECK_FALSE(core::options::log_disabled(L"" TEST_OUT_DIR "/no_such.ini"));
    const char *path = TEST_OUT_DIR "/test_options_global.ini";
    const wchar_t *wpath = L"" TEST_OUT_DIR "/test_options_global.ini";
    for (int value : { 0, 1 }) {
        CAPTURE(value);
        FILE *f = fopen(path, "w");
        REQUIRE(f);
        fprintf(f, "[GLOBAL]\nDisableLog=%d\n[FM5]\nSkipIntro=1\n", value);
        fclose(f);
        CHECK(core::options::log_disabled(wpath) == (value == 1));
    }
    FILE *f = fopen(path, "w"); // the key in a game section is not the global one
    REQUIRE(f);
    fprintf(f, "[FM5]\nDisableLog=1\n");
    fclose(f);
    CHECK_FALSE(core::options::log_disabled(wpath));
    remove(path);
}

TEST_CASE("options: UnlockDLC takes the lambda in Azimuth's form, and nothing else")
{
    struct Lambda {
        const char *bytes;
        bool applies;
    };
    const Lambda lambdas[] = {
        { expected::k_lambda_azimuth, true },
        { expected::k_lambda_azimuth_rdi, true },
        { "48 89 c9 48 b8 a0 01 6f 9a f6 7f 00 00 ff d0 90", false }, // other register
        { "48 89 d9 48 b8 a0 01 6f 9a f6 7f 00 00 ff d1 90", false }, // other call
        { expected::k_lambda_unlocked, false },                       // already unlocked
    };
    for (const char *tag : { "FM5", "FM6" }) {
        const game::Module &g = module(tag);
        uint32_t lambda = strcmp(tag, "FM5") == 0 ? expected::k_fm5_lambda : expected::k_fm6_lambda;
        for (const Lambda &l : lambdas) {
            CAPTURE(tag);
            CAPTURE(l.bytes);
            test_image::Fake img(g.size_of_image);
            put_game(img, tag);
            img.put(lambda, hex(l.bytes));
            CHECK(core::options::apply(g, img.base, ini(tag, { "UnlockDLC" }).c_str()) == (l.applies ? 1 : 0));
            for (const Change &c : changes(tag)) {
                bool on = l.applies && strcmp(c.option, "UnlockDLC") == 0;
                if (c.rva == lambda) check_bytes(img.base, c.rva, on ? c.after : l.bytes);
                else check_bytes(img.base, c.rva, on ? c.after : c.before);
            }
        }
    }
}

TEST_CASE("options: one changed site leaves its whole option off and the others on")
{
    for (const char *tag : { "FM5", "FM6" }) {
        const game::Module &g = module(tag);
        for (const Change &broken : changes(tag)) {
            CAPTURE(tag);
            CAPTURE(broken.rva);
            test_image::Fake img(g.size_of_image);
            put_game(img, tag);
            std::vector<uint8_t> b = hex(broken.before);
            b.front() ^= 0x01; // the first byte is checked in every site (the last may be "??")
            img.put(broken.rva, b);
            std::set<std::string> on = all_keys(tag);
            on.erase(broken.option);
            CHECK(core::options::apply(g, img.base, ini(tag, all_keys(tag)).c_str()) ==
                  static_cast<int>(on.size()));
            for (const Change &c : changes(tag)) {
                if (c.rva == broken.rva) {
                    CHECK(memcmp(img.base + c.rva, b.data(), b.size()) == 0);
                    continue;
                }
                CAPTURE(c.option);
                check_bytes(img.base, c.rva, on.count(c.option) ? c.after : c.before);
            }
        }
    }
}

TEST_CASE("options: FM6 dash font fix stays off when its sites do not match")
{
    const game::Module &g = module("FM6");
    for (uint32_t rva : { expected::k_fm6_measure, expected::k_fm6_find }) {
        CAPTURE(rva);
        test_image::Fake img(g.size_of_image);
        put_game(img, "FM6");
        img.put(rva, hex("cc"));
        core::options::apply(g, img.base, ini("FM6", {}).c_str());
        if (rva == expected::k_fm6_measure) check_bytes(img.base, rva, "cc 8b c4 48 89 58 08 48 89 68 10 48 89 70 18");
        else check_bytes(img.base, expected::k_fm6_measure, expected::k_fm6_measure_before);
    }
}
