// FM6 StockWheels: the SQL fragments it builds from the game's, and the rim loader's stubs (on the fake image and,
// when game_binaries\ is here, on the real exe).
#include "doctest.h"

#include "expected.hpp"
#include "image.hpp"

#include "options.hpp"
#include "registry.hpp"
#include "stock_wheels.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace sw = fm6::stock_wheels;

extern "C" uint64_t test_call_rdx_stub(const void *stub); // calls stub, returns the rdx it left
extern "C" uint64_t test_call_r8_stub(const void *stub);  // same with r8
asm(".text\n"
    ".globl test_call_rdx_stub\ntest_call_rdx_stub:\n  sub $40, %rsp\n  call *%rcx\n  mov %rdx, %rax\n"
    "  add $40, %rsp\n  ret\n"
    ".globl test_call_r8_stub\ntest_call_r8_stub:\n  sub $40, %rsp\n  call *%rcx\n  mov %r8, %rax\n"
    "  add $40, %rsp\n  ret\n");

namespace {

std::string build(bool (*f)(const char *, char *, size_t), const char *in, bool *ok = nullptr)
{
    static char out[8192];
    bool r = f(in, out, sizeof out);
    if (ok) *ok = r;
    return r ? out : "";
}

bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

const game::Module &fm6_module()
{
    for (const game::Module *g : games::all())
        if (strcmp(g->tag, "FM6") == 0) return *g;
    FAIL("no FM6 module");
    throw 0;
}

std::wstring ini_stock_wheels()
{
    std::string path = TEST_OUT_DIR "/test_stock_wheels.ini";
    FILE *f = fopen(path.c_str(), "w");
    REQUIRE(f);
    fputs("[FM6]\nStockWheels=1\n", f);
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
void check_installed(const uint8_t *base)
{
    std::string cats = reinterpret_cast<const char *>(rip_target(base + sw::k_cats_lea, 7));
    CHECK(has(cats, "'Stock Rims (All Cars)'"));
    CHECK(has(cats, ") AS WheelCategories "));
    std::string joins = reinterpret_cast<const char *>(rip_target(base + sw::k_joins_lea, 7));
    CHECK(has(joins, "LEFT JOIN Data_Car AS StockCar"));
    CHECK(has(joins, ") AS WheelAnnotations ON"));
    CHECK(has(joins, ") AS WheelCategories ON"));
    std::string names = reinterpret_cast<const char *>(rip_target(base + sw::k_names_lea, 7));
    CHECK(has(names, "THEN StockMake.DisplayName ELSE List_PartManufacturer.PartManufacturer"));
    std::string view = reinterpret_cast<const char *>(rip_target(base + sw::k_view_lea, 7));
    CHECK(has(view, " OR List_Wheels.ID IN (SELECT StockWheelID FROM Data_Car"));

    const uint8_t *entry = base + sw::k_load;
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
        *tid = GetCurrentThreadId();
        CHECK(strcmp(reinterpret_cast<const char *>(call()), redirected) == 0);
        *tid = 0;
    };
    for (uint32_t r : sw::k_prefix_leas)
        check_stub(r, false, "GAME:\\Media\\Cars\\_library\\scene\\wheels\\", "GAME:\\Media\\Cars\\");
    for (uint32_t r : sw::k_sep_leas) check_stub(r, true, "\\", "\\scene\\_library\\Scene\\Wheels\\");
}

void put_fake(test_image::Fake &img)
{
    // the dash font fix (always on) reads its sites too
    img.put(expected::k_fm6_measure, expected::hex(expected::k_fm6_measure_before));
    img.put(expected::k_fm6_find, expected::hex(expected::k_fm6_find_before));
    for (const expected::Change &c : expected::k_fm6)
        if (strcmp(c.option, "StockWheels") == 0) img.put(c.rva, expected::hex(c.before));
    for (const expected::Text &t : expected::k_fm6_stock_wheels_texts)
        img.put(t.rva, std::span(reinterpret_cast<const uint8_t *>(t.text), strlen(t.text) + 1));
}

} // namespace

TEST_CASE("fm6 stock wheels: SQL fragments")
{
    // the car's parts view also lists the stock rim of every car (named as its car)
    CHECK(build(sw::view_sql, "SELECT r FROM List_Wheels WHERE p != 0") ==
          "SELECT r FROM List_Wheels WHERE p != 0 OR List_Wheels.ID IN (SELECT StockWheelID FROM Data_Car WHERE "
          "Data_Car.MediaName = List_Wheels.MediaName)");

    // categories: the game's table plus one row after them
    std::string c = build(sw::categories_sql, "' END AS s FROM WheelCategories LEFT JOIN m ON m.Id = ");
    CHECK(c.rfind("' END AS s FROM (SELECT * FROM WheelCategories UNION ALL SELECT MAX(Id) + 1, 0, ", 0) == 0);
    CHECK(has(c, ") AS WheelCategories LEFT JOIN m ON m.Id = "));

    // rim columns: the stock rim's make and car before the aftermarket maker and name
    std::string n = build(sw::names_sql, expected::k_fm6_stock_wheels_texts[2].text);
    CHECK(n == "CASE WHEN a THEN b WHEN StockMake.ID IS NOT NULL THEN StockMake.DisplayName ELSE "
               "List_PartManufacturer.PartManufacturer END AS M, CASE WHEN a THEN b WHEN StockCar.ID IS NOT NULL "
               "THEN StockCar.DisplayName ELSE List_Wheels.DisplayName END AS N, CASE WHEN StockMake.ID IS NOT NULL "
               "THEN StockMake.IconPath WHEN List_PartManufacturer.LogoPath IS NULL THEN c END AS L, CASE WHEN "
               "PartTable.IsStock = 1 THEN List_CarMake.IconPath WHEN StockMake.ID IS NOT NULL THEN "
               "StockMake.IconPathLarge ELSE d END AS I,");

    // joins: stock car and make, then the annotations and categories with the new rows
    std::string j = build(sw::joins_sql, expected::k_fm6_stock_wheels_texts[3].text);
    CHECK(j.rfind("v AS t ON x LEFT JOIN List_Wheels ON y LEFT JOIN Data_Car AS StockCar ON List_Wheels.IsStock = 1 "
                  "AND StockCar.StockWheelID = List_Wheels.ID LEFT JOIN List_CarMake AS StockMake ON StockMake.ID = "
                  "StockCar.MakeID JOIN (SELECT WheelID, CategoryID, DisplayOrder FROM WheelAnnotations UNION ALL ",
                  0) == 0);
    CHECK(has(j, "lower(List_CarMake.IconPathBase) || '|' || lower(Data_Car.MediaName)"));
    CHECK(has(j, ") AS WheelAnnotations ON z JOIN (SELECT * FROM WheelCategories UNION ALL "));
    CHECK(has(j, ") AS WheelCategories ON w AND "));
}

TEST_CASE("fm6 stock wheels: a missing or repeated token leaves the fragment unbuilt")
{
    bool ok = true;
    build(sw::categories_sql, "FROM Categories ", &ok);
    CHECK_FALSE(ok);
    build(sw::joins_sql, "JOIN WheelAnnotations ON a JOIN WheelAnnotations ON b JOIN WheelCategories ON c", &ok);
    CHECK_FALSE(ok);
    build(sw::view_sql, "SELECT 1", &ok);
    CHECK_FALSE(ok);
    build(sw::names_sql, "ELSE List_Wheels.DisplayName", &ok);
    CHECK_FALSE(ok);
    char small[16];
    CHECK_FALSE(sw::view_sql("SELECT r FROM List_Wheels WHERE p", small, sizeof small));
}

TEST_CASE("fm6 stock wheels: on the fake image")
{
    const game::Module &g = fm6_module();
    test_image::Fake img(g.size_of_image);
    put_fake(img);
    REQUIRE(core::options::apply(g, img.base, ini_stock_wheels().c_str()) == 1);
    check_installed(img.base);
}

TEST_CASE("fm6 stock wheels: off when the rim loader strings are not the expected ones")
{
    const game::Module &g = fm6_module();
    test_image::Fake img(g.size_of_image);
    put_fake(img);
    const char other[] = "GAME:\\Media\\Cars\\_library\\scene\\rims\\";
    img.put(0x15cdd10, std::span(reinterpret_cast<const uint8_t *>(other), sizeof other));
    CHECK(core::options::apply(g, img.base, ini_stock_wheels().c_str()) == 0);
    for (const expected::Change &c : expected::k_fm6)
        if (strcmp(c.option, "StockWheels") == 0) {
            CAPTURE(c.rva);
            std::vector<uint8_t> b = expected::hex(c.before);
            CHECK(memcmp(img.base + c.rva, b.data(), b.size()) == 0);
        }
}

TEST_CASE("fm6 stock wheels: on the real exe")
{
    test_image::Mapped img(test_image::game_exe("FM6"));
    if (!img.base) {
        MESSAGE("game_binaries\\FM6 missing: test skipped");
        return;
    }
    REQUIRE(core::options::apply(fm6_module(), img.base, ini_stock_wheels().c_str()) == 1);
    check_installed(img.base);
}
