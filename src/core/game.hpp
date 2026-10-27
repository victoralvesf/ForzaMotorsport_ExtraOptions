// Interface of a supported game. Each game lives in its own library (src/games/<id>/) and exposes only its Module.
#pragma once

#include <cstdint>

namespace game {

struct Module {
    const char *tag;            // "FM5", also the ini section ([FM5])
    const wchar_t *ini_file;    // next to azimuth.exe, e.g. L"azimuth_fm5.ini"
    // identification of the exe Azimuth mapped (PE header at the base)
    uint32_t time_date_stamp;
    uint32_t size_of_image;
    // Applies the enabled extra options to the mapped image. Called once, after Azimuth's own patches and
    // before the game runs.
    void (*apply)(uint8_t *base);
};

} // namespace game
