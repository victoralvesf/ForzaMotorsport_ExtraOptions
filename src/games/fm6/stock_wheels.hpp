// FM6 StockWheels option: a fifth rim category in the car upgrades with the stock rims of every car, by make
// (alphabetical), each one fitted from its own car's package.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fm6::stock_wheels {

// Sites of the option (in fm6.cpp, checked before install): the SQL fragments that get the mod's version and the
// rim loader whose paths are redirected for stock rims.
constexpr uint32_t k_view_lea = 0x23138b;  // lea rdx, rims of the car's parts view (aftermarket, priced)
constexpr uint32_t k_cats_lea = 0xcb5bc5;  // lea r8, "FROM" part of the rim categories query
constexpr uint32_t k_names_lea = 0xcb626e; // lea r8, columns (maker, name, logo, icon) of the rims of a category
constexpr uint32_t k_joins_lea = 0xcb62c4; // lea r8, joins (annotations, categories) of the rims of a category
constexpr uint32_t k_load = 0xda9468;      // rim loader: (this, const std::string &media_name) -> bool
constexpr uint32_t k_fs_read = 0xda95b1;   // mov rcx, [file system] inside the loader
// inside the loader: lea rdx, rims folder (model, AO of LF, AO of RF) and lea r8, "\" between name and file
constexpr uint32_t k_prefix_leas[] = { 0xda94b5, 0xda9603, 0xda96a6 };
constexpr uint32_t k_sep_leas[] = { 0xda94d7, 0xda954d };

// The mod's version of each SQL fragment, built from the game's own: false when a token is missing or repeated.
bool view_sql(const char *game, char *out, size_t cap);
bool categories_sql(const char *game, char *out, size_t cap);
bool names_sql(const char *game, char *out, size_t cap);
bool joins_sql(const char *game, char *out, size_t cap);

// Installs the option (Option::install); the sites already matched.
bool install(uint8_t *base, size_t image_size);

} // namespace fm6::stock_wheels
