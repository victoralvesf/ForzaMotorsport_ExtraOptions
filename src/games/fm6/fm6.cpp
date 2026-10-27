#include "fm6.hpp"

#include "font.hpp"

namespace fm6 {

namespace {

using core::patch::Option;
using core::patch::Site;

// "disablesplashintro" (byte [options+0x30d7]): the three checks fall through as if it were on (je -> nop2) and
// the logo parade check always skips (jne -> jmp).
constexpr Site k_skip_intro[] = {
    { 0xa06b33, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa06c4d, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa07e0c, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa08eb6, "44 38 b8 d7 30 00 00 75", 7, "eb" },
};

// CareerRaceIntros.IntroVideo stays empty: the move-assign of the video name is skipped (call -> nop5) and the
// check of its result falls through (jne -> nop2). Covers IE_Blockbuster and the expansion intro videos.
constexpr Site k_skip_presentation_video[] = {
    { 0xce1c55, "48 8d 4b 28 e8 32 d8 46 ff", 4, "0f 1f 44 00 00" },
    { 0xce1eb4, "85 c0 75 19", 2, "66 90" },
};

// Same design as FM5: OfferOwned -> true, and the IsPurchased lambda takes true in its original form or after
// Azimuth's content package patch.
constexpr Site k_unlock_dlc[] = {
    { 0xa8ad80, "48 89 5c 24 10 56 57 41 56 48 83 ec 20 48 8d b9", 0, "b8 01 00 00 00 c3" }, // mov eax,1; ret
    { 0xa40f50,
      "48 8b 0d d9 dd 39 01 48 83 c2 08 e8 20 9e 04 00"
      "|48 89 d9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90"
      "|48 89 f9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90",
      0, "b0 01 0f 1f 84 00 00 00 00 00 66 0f 1f 44 00 00" }, // mov al,1; nop8; nop6
};

constexpr Option k_options[] = {
    { "SkipIntro", k_skip_intro },
    { "SkipPresentationVideo", k_skip_presentation_video },
    { "UnlockDLC", k_unlock_dlc },
};

} // namespace

const game::Module module = {
    .tag = "FM6",
    .ini_file = L"azimuth_fm6.ini",
    .time_date_stamp = 0x58afb1cd,
    .size_of_image = 0x1fc8000,
    .options = k_options,
    .apply_fixes = dash_font::install,
};

} // namespace fm6
