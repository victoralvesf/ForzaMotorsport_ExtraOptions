#include "fm5.hpp"

#include "log.hpp"

namespace fm5 {

namespace {

// The options (SkipIntro, SkipPresentationVideo, UnlockDLC, EnableMarketplace) come with the patch engine.
void apply(uint8_t *base)
{
    core::log::write("FM5 at %p: no extra options yet", static_cast<void *>(base));
}

} // namespace

const game::Module module = {
    .tag = "FM5",
    .ini_file = L"azimuth_fm5.ini",
    .time_date_stamp = 0x53d8d1f2,
    .size_of_image = 0x279d000,
    .apply = apply,
};

} // namespace fm5
