// Integration with Azimuth's launcher: an "Extra Options" section in the settings page of a supported game, next to
// Display, Graphics, Audio and Content packages (Azimuth 1.0.2 to 1.0.4). Every piece of the launcher it touches is found by
// a byte signature in azimuth.exe and checked before anything is written; when one is missing (another Azimuth
// version) the launcher is left as it is and the options stay in the ini only.
#pragma once

#include "game.hpp"

#include <windows.h>
#include <cstdint>
#include <span>

namespace launcher {

// Places in azimuth.exe the section needs (null = not found).
struct Sites {
    static constexpr size_t k_layouts = 2; // forms of the settings page view: 1.0.2/1.0.3, 1.0.4

    uint8_t *build_sections;   // SettingsPage's list of sections of the current game (vector<int>, by Settings)
    uint8_t *rows_dispatch;    // view: rows of the section that is not Display/Graphics/Audio (empty vector)
    size_t layout;             // which form of the view (and of the leas below) matched
    uint8_t *names[2];         // lea of the table of section names (index = section id)
    const wchar_t *const *table; // that table: Display, Graphics, Audio, Content packages
    bool current_game;         // Settings + k_current_game is the current game's title id (std::wstring)
};

// The Extra Options section id: the next one after Content packages.
constexpr int32_t k_section = 4;
constexpr size_t k_current_game = 0xa0;

// Finds every site in the image mapped at base; false (with the reason in why) when one is missing.
bool locate(uint8_t *base, Sites &s, const char **why);

// The game of a Settings (azimuth::app::Settings) by its current title id, or null.
const game::Module *game_of(const uint8_t *settings, std::span<const game::Module *const> games);

// Locates the sites in exe and adds the section for games (rows from rows.hpp, saved to ini). false when the
// launcher was left as it is.
bool install(HMODULE exe, std::span<const game::Module *const> games, const wchar_t *ini);

} // namespace launcher
