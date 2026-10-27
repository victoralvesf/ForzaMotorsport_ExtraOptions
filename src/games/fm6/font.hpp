// Dash fonts of FM6 (DG1..DG5 .vfont): they lack the apostrophe, so pt-BR lap times ("{1}'{2}''") show squares.
// The text measured with such a font is rewritten in place in the English format the dashes were drawn for.
// Always on (game bug, Azimuth does not fix it).
#pragma once

#include <cstddef>
#include <cstdint>

namespace fm6::dash_font {

// Hooks the text measure (0x10bf78) of the image at base. Logs and does nothing when a site does not match.
void install(uint8_t *base);

// s[0..n) up to the first NUL without "''" and with "'" -> ':'; what is left up to that NUL becomes NUL. Returns
// the new length.
size_t fix_text(uint16_t *s, size_t n);

} // namespace fm6::dash_font
