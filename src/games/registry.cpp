#include "registry.hpp"

#include "fm5.hpp"
#include "fm6.hpp"

namespace games {

namespace {
const game::Module *const k_games[] = { &fm5::module, &fm6::module };
} // namespace

std::span<const game::Module *const> all() { return k_games; }

const game::Module *find(uint32_t time_date_stamp, uint32_t size_of_image)
{
    for (const game::Module *g : k_games)
        if (g->time_date_stamp == time_date_stamp && g->size_of_image == size_of_image) return g;
    return nullptr;
}

} // namespace games
