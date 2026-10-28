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

// Same as FM5 (present mode 1 -> 0 when the game stores it), but the setter (mov [rcx+0xa0],edx; ret) is shared
// with other classes (CPresentationView...). A stub goes in the setter's padding (cmp edx,1; jne setter;
// xor edx,edx; jmp setter) and only the setter slots (+0x1a0) of the Frame11 and Frame vtables point to it: the low
// byte of the pointer 0x...849940 becomes 0x47 (the low 16 bits do not change if the image is relocated).
constexpr Site k_unlock_menu_frame_rate[] = {
    { 0x849940, "89 91 a0 00 00 00 c3 cc cc cc cc cc cc cc cc cc", 7, "83 fa 01 75 f4 31 d2 eb f0" },
    { 0x19f3a88, "40 99 ?? ?? ?? ?? ?? ??", 0, "47" }, // Frame11
    { 0x19f41a8, "40 99 ?? ?? ?? ?? ?? ??", 0, "47" }, // Frame
};

constexpr Option k_options[] = {
    { "SkipIntro", k_skip_intro, "Skip intro" },
    { "SkipPresentationVideo", k_skip_presentation_video, "Skip presentation video" },
    { "UnlockDLC", k_unlock_dlc, "Unlock DLC" },
    { "UnlockMenuFrameRate", k_unlock_menu_frame_rate, "Unlock menu frame rate" },
};

} // namespace

const game::Module module = {
    .tag = "FM6",
    .title_id = "64f9397e",
    .time_date_stamp = 0x58afb1cd,
    .size_of_image = 0x1fc8000,
    .options = k_options,
    .apply_fixes = dash_font::install,
};

} // namespace fm6
