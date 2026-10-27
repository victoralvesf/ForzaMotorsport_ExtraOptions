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
    const char *after;  // == before: the site is only checked
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
    { "UnlockMenuFrameRate", 0x7146f7, "8b 89 a0 00 00 00 48 89 5c 24 78 85 c9 74 0f ff c9 74 04 33 db eb 0c bb 02 00 00 00",
      "8b 89 a0 00 00 00 48 89 5c 24 78 85 c9 74 0f ff c9 74 04 33 db eb 0c bb 01 00 00 00" },
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
    { "UnlockMenuFrameRate", 0x207883, "8b 89 a0 00 00 00 85 c9 0f 84 cc 00 00 00 ff c9 0f 85 ce 00 00 00 8d 59 02",
      "8b 89 a0 00 00 00 85 c9 0f 84 cc 00 00 00 ff c9 0f 85 ce 00 00 00 8d 59 01" },
};
constexpr uint32_t k_fm6_lambda = 0xa40f50;

// FM6 dash font fix (always on): the text measure gets "jmp [rip+0]; <stub>; int3", the glyph lookup is only read.
constexpr uint32_t k_fm6_measure = 0x10bf78;
constexpr const char *k_fm6_measure_before = "48 8b c4 48 89 58 08 48 89 68 10 48 89 70 18";
constexpr uint32_t k_fm6_find = 0x10c270;
constexpr const char *k_fm6_find_before = "44 8b 51 04 4c 8b 59 08 44 8b c2 45 33 c9";

} // namespace expected
