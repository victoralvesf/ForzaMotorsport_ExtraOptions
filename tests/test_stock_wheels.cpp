// StockWheels (FM5 and FM6): the SQL fragments core::stock_rims builds from the game's, and the rim loader's stubs
// (on the fake image and, when game_binaries\ is here, on the real exe).
#include "doctest.h"

#include "expected.hpp"
#include "image.hpp"

#include "options.hpp"
#include "registry.hpp"
#include "stock_rims.hpp"
#include "stock_wheels.hpp"
#include "wheels.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sr = core::stock_rims;

extern "C" uint64_t test_call_rdx_stub(const void *stub); // calls stub, returns the rdx it left
extern "C" uint64_t test_call_r8_stub(const void *stub);  // same with r8
extern "C" uint64_t test_call_stub_r8(const void *stub, uint64_t r8); // r8 = r8 before the call, returns r8 after
asm(".text\n"
    ".globl test_call_rdx_stub\ntest_call_rdx_stub:\n  sub $40, %rsp\n  call *%rcx\n  mov %rdx, %rax\n"
    "  add $40, %rsp\n  ret\n"
    ".globl test_call_r8_stub\ntest_call_r8_stub:\n  sub $40, %rsp\n  call *%rcx\n  mov %r8, %rax\n"
    "  add $40, %rsp\n  ret\n"
    ".globl test_call_stub_r8\ntest_call_stub_r8:\n  sub $40, %rsp\n  mov %rdx, %r8\n  call *%rcx\n"
    "  mov %r8, %rax\n  add $40, %rsp\n  ret\n");

namespace {

// what the option touches in one game
struct Rims {
    const char *tag;
    uint32_t view_lea, cats_lea, names_lea, joins_lea, load;
    std::vector<uint32_t> prefix_leas;
    uint32_t prefix_len_lea; // 0 = none
    std::vector<uint32_t> sep_leas, file_leas;
    std::span<const expected::Change> changes;
    std::span<const expected::Text> texts;
    uint32_t rims_rva; // the game's rims folder string
};

const Rims k_fm6 = {
    "FM6", fm6::stock_wheels::k_view_lea, fm6::stock_wheels::k_cats_lea, fm6::stock_wheels::k_names_lea,
    fm6::stock_wheels::k_joins_lea, fm6::stock_wheels::k_load, { 0xda94b5, 0xda9603, 0xda96a6 }, 0,
    { 0xda94d7, 0xda954d }, {}, expected::k_fm6, expected::k_fm6_stock_wheels_texts, 0x15cdd10,
};

const Rims k_fm5 = {
    "FM5", fm5::stock_wheels::k_view_lea, fm5::stock_wheels::k_cats_lea, fm5::stock_wheels::k_names_lea,
    fm5::stock_wheels::k_joins_lea, fm5::stock_wheels::k_load, { 0x6cee5b, 0x6cf0af, 0x6cf1b9 }, 0x6cee57,
    { 0x6cee80, 0x6cef7d }, { 0x6ceea7 }, expected::k_fm5, expected::k_fm5_stock_wheels_texts, 0x1cbb490,
};

std::string build(bool (*f)(const char *, char *, size_t), const char *in, bool *ok = nullptr)
{
    static char out[8192];
    bool r = f(in, out, sizeof out);
    if (ok) *ok = r;
    return r ? out : "";
}

bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

const game::Module &module(const char *tag)
{
    for (const game::Module *g : games::all())
        if (strcmp(g->tag, tag) == 0) return *g;
    FAIL("no module " << tag);
    throw 0;
}

std::wstring ini_stock_wheels(const char *tag)
{
    std::string path = TEST_OUT_DIR "/test_stock_wheels_" + std::string(tag) + ".ini";
    FILE *f = fopen(path.c_str(), "w");
    REQUIRE(f);
    fprintf(f, "[%s]\nStockWheels=1\n", tag);
    fclose(f);
    return std::wstring(path.begin(), path.end());
}

const uint8_t *rip_target(const uint8_t *insn, size_t len)
{
    int32_t d;
    memcpy(&d, insn + len - 4, 4);
    return insn + len + d;
}

// After the option: the SQL leas point to the mod's text; each loader lea is "call stub; nop2", the stub gives the
// game's string to other threads and the redirected one to the thread in its tid slot.
void check_installed(const Rims &r, const uint8_t *base)
{
    std::string cats = reinterpret_cast<const char *>(rip_target(base + r.cats_lea, 7));
    CHECK(has(cats, "'Stock Rims (All Cars)'"));
    CHECK(has(cats, ") AS WheelCategories "));
    std::string joins = reinterpret_cast<const char *>(rip_target(base + r.joins_lea, 7));
    CHECK(has(joins, "LEFT JOIN Data_Car AS StockCar"));
    CHECK(has(joins, ") AS WheelAnnotations ON"));
    CHECK(has(joins, ") AS WheelCategories ON"));
    std::string names = reinterpret_cast<const char *>(rip_target(base + r.names_lea, 7));
    CHECK(has(names, "THEN StockMake.DisplayName ELSE List_PartManufacturer.PartManufacturer"));
    std::string view = reinterpret_cast<const char *>(rip_target(base + r.view_lea, 7));
    CHECK(has(view, " OR List_Wheels.ID IN (SELECT StockWheelID FROM Data_Car"));

    const uint8_t *entry = base + r.load;
    CHECK(entry[0] == 0xff);
    CHECK(entry[1] == 0x25);

    auto check_stub = [&](uint32_t rva, bool r8, const char *game, const char *redirected) {
        CAPTURE(rva);
        const uint8_t *site = base + rva;
        REQUIRE(site[0] == 0xe8);
        CHECK(site[5] == 0x66);
        CHECK(site[6] == 0x90);
        const uint8_t *stub = rip_target(site, 5);
        // cmp reg, [rip+tid] after "mov reg, gs:[0x48]"
        auto *tid = const_cast<uint64_t *>(reinterpret_cast<const uint64_t *>(rip_target(stub + 9, 7)));
        auto call = [&] { return r8 ? test_call_r8_stub(stub) : test_call_rdx_stub(stub); };
        *tid = 0;
        CHECK(strcmp(reinterpret_cast<const char *>(call()), game) == 0);
        if (rva == r.prefix_leas[0] && r.prefix_len_lea) CHECK(test_call_stub_r8(stub, 0x27) == 0x27);
        *tid = GetCurrentThreadId();
        CHECK(strcmp(reinterpret_cast<const char *>(call()), redirected) == 0);
        // the counted assign of the first rims folder gets the cars folder's length
        if (rva == r.prefix_leas[0] && r.prefix_len_lea) CHECK(test_call_stub_r8(stub, 0x27) == 17);
        *tid = 0;
    };
    for (uint32_t a : r.prefix_leas)
        check_stub(a, false, "GAME:\\Media\\Cars\\_library\\scene\\wheels\\", "GAME:\\Media\\Cars\\");
    // until a stock rim loads, the redirected folder and file are the first candidates
    for (uint32_t a : r.sep_leas) check_stub(a, true, "\\", "\\scene\\_library\\Scene\\Wheels\\");
    for (uint32_t a : r.file_leas) check_stub(a, true, "_wheelLF.modelbin", "_wheelLF.modelbin");
}

void put_fake(const Rims &r, test_image::Fake &img)
{
    if (strcmp(r.tag, "FM6") == 0) { // the dash font fix (always on) reads its sites too
        img.put(expected::k_fm6_measure, expected::hex(expected::k_fm6_measure_before));
        img.put(expected::k_fm6_find, expected::hex(expected::k_fm6_find_before));
    }
    for (const expected::Change &c : r.changes)
        if (strcmp(c.option, "StockWheels") == 0) img.put(c.rva, expected::hex(c.before));
    for (const expected::Text &t : r.texts)
        img.put(t.rva, std::span(reinterpret_cast<const uint8_t *>(t.text), strlen(t.text) + 1));
}

void check_untouched(const Rims &r, const uint8_t *base)
{
    for (const expected::Change &c : r.changes)
        if (strcmp(c.option, "StockWheels") == 0) {
            CAPTURE(c.rva);
            std::vector<uint8_t> b = expected::hex(c.before);
            CHECK(memcmp(base + c.rva, b.data(), b.size()) == 0);
        }
}

} // namespace

TEST_CASE("stock wheels: SQL fragments")
{
    // the car's parts view also lists the stock rim of every car (named as its car)
    CHECK(build(sr::view_sql, "SELECT r FROM List_Wheels WHERE p != 0") ==
          "SELECT r FROM List_Wheels WHERE p != 0 OR List_Wheels.ID IN (SELECT StockWheelID FROM Data_Car WHERE "
          "Data_Car.MediaName = List_Wheels.MediaName)");

    // categories: the game's table plus one row after them
    std::string c = build(sr::categories_sql, "' END AS s FROM WheelCategories LEFT JOIN m ON m.Id = ");
    CHECK(c.rfind("' END AS s FROM (SELECT * FROM WheelCategories UNION ALL SELECT MAX(Id) + 1, 0, ", 0) == 0);
    CHECK(has(c, ") AS WheelCategories LEFT JOIN m ON m.Id = "));

    // rim columns: the stock rim's make and car before the aftermarket maker and name
    std::string n = build(sr::names_sql, expected::k_fm6_stock_wheels_texts[2].text);
    CHECK(n == "CASE WHEN a THEN b WHEN StockMake.ID IS NOT NULL THEN StockMake.DisplayName ELSE "
               "List_PartManufacturer.PartManufacturer END AS M, CASE WHEN a THEN b WHEN StockCar.ID IS NOT NULL "
               "THEN StockCar.DisplayName ELSE List_Wheels.DisplayName END AS N, CASE WHEN StockMake.ID IS NOT NULL "
               "THEN StockMake.IconPath WHEN List_PartManufacturer.LogoPath IS NULL THEN c END AS L, CASE WHEN "
               "PartTable.IsStock = 1 THEN List_CarMake.IconPath WHEN StockMake.ID IS NOT NULL THEN "
               "StockMake.IconPath ELSE d END AS I,");

    // joins: stock car and make, then the annotations and categories with the new rows
    std::string j = build(sr::joins_sql, expected::k_fm6_stock_wheels_texts[3].text);
    CHECK(j.rfind("v AS t ON x LEFT JOIN List_Wheels ON y LEFT JOIN Data_Car AS StockCar ON List_Wheels.IsStock = 1 "
                  "AND StockCar.StockWheelID = List_Wheels.ID LEFT JOIN List_CarMake AS StockMake ON StockMake.ID = "
                  "StockCar.MakeID JOIN (SELECT WheelID, CategoryID, DisplayOrder FROM WheelAnnotations UNION ALL ",
                  0) == 0);
    CHECK(has(j, "lower(List_CarMake.IconPathBase) || '|' || lower(Data_Car.MediaName)"));
    CHECK(has(j, ") AS WheelAnnotations ON z JOIN (SELECT * FROM WheelCategories UNION ALL "));
    CHECK(has(j, ") AS WheelCategories ON w AND "));
}

TEST_CASE("stock wheels: a missing or repeated token leaves the fragment unbuilt")
{
    bool ok = true;
    build(sr::categories_sql, "FROM Categories ", &ok);
    CHECK_FALSE(ok);
    build(sr::joins_sql, "JOIN WheelAnnotations ON a JOIN WheelAnnotations ON b JOIN WheelCategories ON c", &ok);
    CHECK_FALSE(ok);
    build(sr::view_sql, "SELECT 1", &ok);
    CHECK_FALSE(ok);
    build(sr::names_sql, "ELSE List_Wheels.DisplayName", &ok);
    CHECK_FALSE(ok);
    char small[16];
    CHECK_FALSE(sr::view_sql("SELECT r FROM List_Wheels WHERE p", small, sizeof small));
}

TEST_CASE("stock wheels: on the fake image")
{
    for (const Rims *r : { &k_fm6, &k_fm5 }) {
        CAPTURE(r->tag);
        const game::Module &g = module(r->tag);
        test_image::Fake img(g.size_of_image);
        put_fake(*r, img);
        REQUIRE(core::options::apply(g, img.base, ini_stock_wheels(r->tag).c_str()) == 1);
        check_installed(*r, img.base);
    }
}

TEST_CASE("stock wheels: off when the rim loader strings are not the expected ones")
{
    for (const Rims *r : { &k_fm6, &k_fm5 }) {
        CAPTURE(r->tag);
        const game::Module &g = module(r->tag);
        test_image::Fake img(g.size_of_image);
        put_fake(*r, img);
        const char other[] = "GAME:\\Media\\Cars\\_library\\scene\\rims\\";
        img.put(r->rims_rva, std::span(reinterpret_cast<const uint8_t *>(other), sizeof other));
        CHECK(core::options::apply(g, img.base, ini_stock_wheels(r->tag).c_str()) == 0);
        check_untouched(*r, img.base);
    }
}

TEST_CASE("stock wheels: FM5 off when the counted rims folder has another length")
{
    const game::Module &g = module("FM5");
    test_image::Fake img(g.size_of_image);
    put_fake(k_fm5, img);
    // a rims folder one character longer than the 0x27 bytes the loader copies
    const char moved[] = "GAME:\\Media\\Cars\\x_library\\scene\\wheels\\";
    img.put(k_fm5.rims_rva, std::span(reinterpret_cast<const uint8_t *>(moved), sizeof moved));
    CHECK(core::options::apply(g, img.base, ini_stock_wheels("FM5").c_str()) == 0);
    check_untouched(k_fm5, img.base);
}

TEST_CASE("stock wheels: on the real exes")
{
    for (const Rims *r : { &k_fm6, &k_fm5 }) {
        CAPTURE(r->tag);
        test_image::Mapped img(test_image::game_exe(r->tag));
        if (!img.base) {
            MESSAGE("game_binaries\\" << r->tag << " missing: test skipped");
            continue;
        }
        REQUIRE(core::options::apply(module(r->tag), img.base, ini_stock_wheels(r->tag).c_str()) == 1);
        check_installed(*r, img.base);
    }
}
