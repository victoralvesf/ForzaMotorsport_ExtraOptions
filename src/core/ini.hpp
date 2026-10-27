// Reading of the options ini (azimuth_fm5.ini / azimuth_fm6.ini, one section per game).
#pragma once

namespace core::ini {

// Integer value of [section] key in the file at path; def when the file, the section or the key is missing.
int get_int(const wchar_t *path, const char *section, const char *key, int def);

} // namespace core::ini
