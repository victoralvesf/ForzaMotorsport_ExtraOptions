// Interface of a supported game. Each game lives in its own library (src/games/<id>/) and exposes only its Module.
#pragma once

#include "patch.hpp"

#include <cstdint>
#include <span>

namespace game {

struct Module {
    const char *tag;            // "FM5", also the ini section ([FM5])
    const wchar_t *ini_file;    // next to azimuth.exe, e.g. L"azimuth_fm5.ini"
    // identification of the exe Azimuth mapped (PE header at the base)
    uint32_t time_date_stamp;
    uint32_t size_of_image;
    // options of the ini (off when the key is missing), applied all or nothing
    std::span<const core::patch::Option> options;
    // fixes that need code (hooks) and are always on; null if the game has none
    void (*apply_fixes)(uint8_t *base);
};

} // namespace game
