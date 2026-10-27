// Applies a game's extra options to its mapped image, once, after Azimuth's own patches and before the game runs.
#pragma once

#include "game.hpp"

namespace core::options {

// ini_path: the game's ini (g.ini_file). Each option is read from [g.tag] (0 when missing) and applied all or
// nothing; then g.apply_fixes. Returns the number of options applied.
int apply(const game::Module &g, uint8_t *base, const wchar_t *ini_path);

} // namespace core::options
