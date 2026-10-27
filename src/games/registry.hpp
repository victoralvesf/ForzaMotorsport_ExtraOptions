// The supported games and their identification by the PE header of the image Azimuth mapped.
#pragma once

#include "game.hpp"

#include <cstdint>
#include <span>

namespace games {

std::span<const game::Module *const> all();

// The game whose exe has this TimeDateStamp and SizeOfImage, or null.
const game::Module *find(uint32_t time_date_stamp, uint32_t size_of_image);

} // namespace games
