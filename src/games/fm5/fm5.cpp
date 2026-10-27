#include "fm5.hpp"

namespace fm5 {

namespace {

using core::patch::Option;
using core::patch::Site;

// "disablesplashintro" command line option (byte [rsi+0x309f]): each check compares with 0xff instead of 0, so
// the game sees it on.
constexpr Site k_skip_intro[] = {
    { 0x13a33e, "80 be 9f 30 00 00 00", 6, "ff" },
    { 0x13a825, "80 be 9f 30 00 00 00", 6, "ff" },
    { 0x13b9ce, "80 be 9f 30 00 00 00", 6, "ff" },
};

// Welcome screen: the function that opens the presentation video page (IE_Blockbuster.bk2) jumps straight to the
// callback that runs when the video ends (which does not use the page).
constexpr Site k_skip_presentation_video[] = {
    { 0x4ab060, "48 83 ec 78 48 c7 44 24 20 fe ff ff ff", 0, "e9 eb 7e 00 00" }, // jmp 0x4b2f50
    { 0x4b2f50, "48 83 ec 48 48 8d 15 85 1a c2 00", 0, "" },
};

// OfferOwned (entitlement manager) returns true for every offer (marketplace UI and its other callers), and the
// IsPurchased lambda takes true without asking. Azimuth >= 1.0.2 replaces the lambda's OfferOwned call with its
// own content package check (mov rcx,rbx; mov rax,imm64; call rax; nop), so both forms are accepted.
constexpr Site k_unlock_dlc[] = {
    { 0x203050, "48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83", 0, "b8 01 00 00 00 c3" }, // mov eax,1; ret
    { 0x19e6a0,
      "48 8b 0d c1 59 43 02 48 83 c2 08 e8 a0 49 06 00"
      "|48 89 d9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90"
      "|48 89 f9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90",
      0, "b0 01 0f 1f 84 00 00 00 00 00 66 0f 1f 44 00 00" }, // mov al,1; nop8; nop6
};

// Default of enablemarketplace (normally set only by the server config): 0x01000000 -> 0x01010000.
constexpr Site k_enable_marketplace[] = {
    { 0x180c2a, "c7 83 bc 30 00 00 00 00 00 01", 8, "01" },
};

// Present of Frame11: the game asks for present mode 1 (sync interval 2, half the frame rate) in menus and
// transitions and mode 0 (interval 1) in races. Mode 1 also takes interval 1 (mov ebx,2 -> mov ebx,1), so Azimuth's
// frame rate holds everywhere.
constexpr Site k_unlock_menu_frame_rate[] = {
    { 0x7146f7, "8b 89 a0 00 00 00 48 89 5c 24 78 85 c9 74 0f ff c9 74 04 33 db eb 0c bb 02 00 00 00", 24, "01" },
};

constexpr Option k_options[] = {
    { "SkipIntro", k_skip_intro },
    { "SkipPresentationVideo", k_skip_presentation_video },
    { "UnlockDLC", k_unlock_dlc },
    { "EnableMarketplace", k_enable_marketplace },
    { "UnlockMenuFrameRate", k_unlock_menu_frame_rate },
};

} // namespace

const game::Module module = {
    .tag = "FM5",
    .ini_file = L"azimuth_fm5.ini",
    .time_date_stamp = 0x53d8d1f2,
    .size_of_image = 0x279d000,
    .options = k_options,
    .apply_fixes = nullptr,
};

} // namespace fm5
