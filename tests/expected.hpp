// Bytes every option must leave in the game image, written here by hand (not taken from the option tables):
// "before" = the bytes of FM5 1.0.0.20 / FM6 1.0.0.44 at the site, "after" = the same range once the option is on.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace expected {

struct Change {
    const char *option; // ini key
    uint32_t rva;
    const char *before;
    const char *after;  // == before: the site is only checked; null: rewritten by the option's code (own test)
};

// "48 8b" -> { 0x48, 0x8b }
std::vector<uint8_t> hex(const char *s);

constexpr const char *k_lambda_unlocked = "b0 01 0f 1f 84 00 00 00 00 00 66 0f 1f 44 00 00"; // mov al,1; nop8; nop6

// The IsPurchased lambda after Azimuth's content package patch (as Azimuth 1.0.2 left it in both games, with the
// address it loaded at), and the same with rdi as the argument register.
constexpr const char *k_lambda_azimuth = "48 89 d9 48 b8 a0 01 6f 9a f6 7f 00 00 ff d0 90";
constexpr const char *k_lambda_azimuth_rdi = "48 89 f9 48 b8 a0 01 6f 9a f6 7f 00 00 ff d0 90";

constexpr Change k_fm5[] = {
    { "SkipIntro", 0x13a33e, "80 be 9f 30 00 00 00", "80 be 9f 30 00 00 ff" },
    { "SkipIntro", 0x13a825, "80 be 9f 30 00 00 00", "80 be 9f 30 00 00 ff" },
    { "SkipIntro", 0x13b9ce, "80 be 9f 30 00 00 00", "80 be 9f 30 00 00 ff" },
    { "SkipPresentationVideo", 0x4ab060, "48 83 ec 78 48 c7 44 24 20 fe ff ff ff", "e9 eb 7e 00 00 c7 44 24 20 fe ff ff ff" },
    { "SkipPresentationVideo", 0x4b2f50, "48 83 ec 48 48 8d 15 85 1a c2 00", "48 83 ec 48 48 8d 15 85 1a c2 00" },
    { "UnlockDLC", 0x203050, "48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83",
      "b8 01 00 00 00 c3 89 6c 24 18 56 57 41 56 48 83" },
    { "UnlockDLC", 0x19e6a0, "48 8b 0d c1 59 43 02 48 83 c2 08 e8 a0 49 06 00", k_lambda_unlocked },
    { "EnableMarketplace", 0x180c2a, "c7 83 bc 30 00 00 00 00 00 01", "c7 83 bc 30 00 00 00 00 01 01" },
    { "UnlockMenuFrameRate", 0x711620, "89 91 a0 00 00 00 c3 cc cc cc cc cc cc cc cc cc",
      "83 fa 01 75 02 31 d2 89 91 a0 00 00 00 c3 cc cc" },
};
constexpr uint32_t k_fm5_lambda = 0x19e6a0;

constexpr Change k_fm6[] = {
    { "SkipIntro", 0xa06b33, "41 38 b4 24 d7 30 00 00 74 2e", "41 38 b4 24 d7 30 00 00 66 90" },
    { "SkipIntro", 0xa06c4d, "41 38 b4 24 d7 30 00 00 74 07", "41 38 b4 24 d7 30 00 00 66 90" },
    { "SkipIntro", 0xa07e0c, "41 38 b4 24 d7 30 00 00 74 3f", "41 38 b4 24 d7 30 00 00 66 90" },
    { "SkipIntro", 0xa08eb6, "44 38 b8 d7 30 00 00 75 2f", "44 38 b8 d7 30 00 00 eb 2f" },
    { "SkipPresentationVideo", 0xce1c55, "48 8d 4b 28 e8 32 d8 46 ff", "48 8d 4b 28 0f 1f 44 00 00" },
    { "SkipPresentationVideo", 0xce1eb4, "85 c0 75 19", "85 c0 66 90" },
    { "UnlockDLC", 0xa8ad80, "48 89 5c 24 10 56 57 41 56 48 83 ec 20 48 8d b9",
      "b8 01 00 00 00 c3 57 41 56 48 83 ec 20 48 8d b9" },
    { "UnlockDLC", 0xa40f50, "48 8b 0d d9 dd 39 01 48 83 c2 08 e8 20 9e 04 00", k_lambda_unlocked },
    { "UnlockMenuFrameRate", 0x849940, "89 91 a0 00 00 00 c3 cc cc cc cc cc cc cc cc cc",
      "89 91 a0 00 00 00 c3 83 fa 01 75 f4 31 d2 eb f0" },
    { "UnlockMenuFrameRate", 0x19f3a88, "40 99 84 40 01 00 00 00", "47 99 84 40 01 00 00 00" },
    { "UnlockMenuFrameRate", 0x19f41a8, "40 99 84 40 01 00 00 00", "47 99 84 40 01 00 00 00" },
    // lea rdx / lea r8 of the SQL fragments: view rims, categories FROM, rim columns, rim joins (test_fm6_stock_wheels)
    { "StockWheels", 0x23138b, "48 8d 15 8e ed 07 01", nullptr },
    { "StockWheels", 0xcb5bc5, "4c 8d 05 b4 16 91 00", nullptr },
    { "StockWheels", 0xcb626e, "4c 8d 05 2b 19 91 00", nullptr },
    { "StockWheels", 0xcb62c4, "4c 8d 05 05 17 91 00", nullptr },
    // rim loader: entry (hooked), read of the file system, lea of the rims folder x3 and of "\" x2 (-> calls)
    { "StockWheels", 0xda9468, "48 8b c4 55 48 8d a8 68 fe ff ff 48 81 ec 90 02 00 00", nullptr },
    { "StockWheels", 0xda95b1, "48 8b 0d 40 fa fc 00 48 8b 01", "48 8b 0d 40 fa fc 00 48 8b 01" },
    { "StockWheels", 0xda94b5, "48 8d 15 54 48 82 00", nullptr },
    { "StockWheels", 0xda9603, "48 8d 15 06 47 82 00", nullptr },
    { "StockWheels", 0xda96a6, "48 8d 15 63 46 82 00", nullptr },
    { "StockWheels", 0xda94d7, "4c 8d 05 4a 1c 33 00", nullptr },
    { "StockWheels", 0xda954d, "4c 8d 05 d4 1b 33 00", nullptr },
};
constexpr uint32_t k_fm6_lambda = 0xa40f50;

// StockWheels on the fake image: the strings its leas point to, written here as small stand-ins holding each token
// the option edits (the real fragments are only read from the real exe).
struct Text {
    uint32_t rva;
    const char *text;
};
constexpr Text k_fm6_stock_wheels_texts[] = {
    { 0x12b0120, "SELECT r FROM List_Wheels WHERE p != 0" },
    { 0x15c7280, "' END AS s FROM WheelCategories LEFT JOIN m ON m.Id = " },
    { 0x15c7ba0, "CASE WHEN a THEN b ELSE List_PartManufacturer.PartManufacturer END AS M, CASE WHEN a THEN b "
                 "ELSE List_Wheels.DisplayName END AS N, CASE WHEN List_PartManufacturer.LogoPath IS NULL THEN c "
                 "END AS L, CASE WHEN PartTable.IsStock = 1 THEN List_CarMake.IconPath ELSE d END AS I," },
    { 0x15c79d0, "v AS t ON x LEFT JOIN List_Wheels ON y JOIN WheelAnnotations ON z JOIN WheelCategories ON w AND " },
    { 0x15cdd10, "GAME:\\Media\\Cars\\_library\\scene\\wheels\\" },
    { 0x10db128, "\\" },
};

// FM6 dash font fix (always on): the text measure gets "jmp [rip+0]; <stub>; int3", the glyph lookup is only read.
constexpr uint32_t k_fm6_measure = 0x10bf78;
constexpr const char *k_fm6_measure_before = "48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18";
constexpr uint32_t k_fm6_find = 0x10c270;
constexpr const char *k_fm6_find_before = "44 8b 51 04 4c 8b 59 08 44 8b c2 45 33 c9";

} // namespace expected
