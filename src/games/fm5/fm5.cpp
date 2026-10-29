#include "fm5.hpp"

#include "wheels.hpp"

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

// Present mode of Frame11/Frame ([frame+0xa0]): the game asks for mode 1 (sync interval 2, half the frame rate) in
// menus and transitions and mode 0 (interval 1) in races, and also reads the mode back to pace its frames. The
// setter (used only by the Frame11 and Frame vtables) stores 0 when it is asked for 1, in its own padding:
// cmp edx,1; jne +2; xor edx,edx; mov [rcx+0xa0],edx; ret.
constexpr Site k_unlock_menu_frame_rate[] = {
    { 0x711620, "89 91 a0 00 00 00 c3 cc cc cc cc cc cc cc cc cc", 0, "83 fa 01 75 02 31 d2 89 91 a0 00 00 00 c3" },
};

// A fifth rim category with the stock rims of every car (core::stock_rims builds the SQL from the game's fragments
// and hooks the rim loader). Sites only checked here: the leas of the SQL fragments (and the length of the view's), the loader's entry, its read of
// the file system, the length of its first rims folder string and its leas of the rims folder, of the "\"
// separator and of the LF model file.
constexpr Site k_stock_wheels[] = {
    { stock_wheels::k_view_len_mov, "41 b8 a2 00 00 00", 0, "" },
    { stock_wheels::k_view_lea, "48 8d 15 1e ea a9 01", 0, "" },
    { stock_wheels::k_cats_lea, "4c 8d 05 39 2c 7f 01", 0, "" },
    { stock_wheels::k_names_lea, "4c 8d 05 08 27 7f 01", 0, "" },
    { stock_wheels::k_joins_lea, "4c 8d 05 df 24 7f 01", 0, "" },
    { stock_wheels::k_load, stock_wheels::k_load_entry, 0, "" },
    { stock_wheels::k_fs_read, "48 8b 0d bc 19 da 01 48 8b 01", 0, "" },
    { stock_wheels::k_prefix_len_lea, "45 8d 47 27", 0, "" },
    { stock_wheels::k_prefix_leas[0], "48 8d 15 2e c6 5e 01", 0, "" },
    { stock_wheels::k_prefix_leas[1], "48 8d 15 da c3 5e 01", 0, "" },
    { stock_wheels::k_prefix_leas[2], "48 8d 15 d0 c2 5e 01", 0, "" },
    { stock_wheels::k_sep_leas[0], "4c 8d 05 ad 5b a0 00", 0, "" },
    { stock_wheels::k_sep_leas[1], "4c 8d 05 b0 5a a0 00", 0, "" },
    { stock_wheels::k_file_leas[0], "4c 8d 05 72 c5 5e 01", 0, "" },
};

constexpr Option k_options[] = {
    { "SkipIntro", k_skip_intro, "Skip intro" },
    { "SkipPresentationVideo", k_skip_presentation_video, "Skip presentation video" },
    { "UnlockDLC", k_unlock_dlc, "Unlock DLC" },
    { "EnableMarketplace", k_enable_marketplace, "Enable Marketplace" },
    { "UnlockMenuFrameRate", k_unlock_menu_frame_rate, "Unlock menu frame rate" },
    { "StockWheels", k_stock_wheels, "Stock rims of every car", stock_wheels::install },
};

} // namespace

const game::Module module = {
    .tag = "FM5",
    .title_id = "7b35d917",
    .time_date_stamp = 0x53d8d1f2,
    .size_of_image = 0x279d000,
    .options = k_options,
    .apply_fixes = nullptr,
};

} // namespace fm5
