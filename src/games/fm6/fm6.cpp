#include "fm6.hpp"

#include "log.hpp"

namespace fm6 {

namespace {

// The options (SkipIntro, SkipPresentationVideo, UnlockDLC) and the dash font fix come with the patch engine.
void apply(uint8_t *base)
{
    core::log::write("FM6 at %p: no extra options yet", static_cast<void *>(base));
}

} // namespace

const game::Module module = {
    .tag = "FM6",
    .ini_file = L"azimuth_fm6.ini",
    .time_date_stamp = 0x58afb1cd,
    .size_of_image = 0x1fc8000,
    .apply = apply,
};

} // namespace fm6
