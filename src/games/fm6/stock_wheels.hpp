// FM6 StockWheels option (core::stock_rims): where the rim SQL fragments and the rim loader are in FM6.
#pragma once

#include "stock_rims.hpp"

#include <cstddef>
#include <cstdint>

namespace fm6::stock_wheels {

// Sites of the option (in fm6.cpp, checked before install).
constexpr uint32_t k_view_lea = 0x23138b;  // lea rdx, rims of the car's parts view (aftermarket, priced)
constexpr uint32_t k_cats_lea = 0xcb5bc5;  // lea r8, "FROM" part of the rim categories query
constexpr uint32_t k_names_lea = 0xcb626e; // lea r8, columns (maker, name, logo, icon) of the rims of a category
constexpr uint32_t k_joins_lea = 0xcb62c4; // lea r8, joins (annotations, categories) of the rims of a category
constexpr uint32_t k_load = 0xda9468;      // rim loader: (this, const std::string &media_name) -> bool
constexpr const char *k_load_entry = "48 8b c4 55 48 8d a8 68 fe ff ff 48 81 ec 90 02 00 00";
constexpr uint32_t k_fs_read = 0xda95b1;   // mov rcx, [file system] inside the loader
// inside the loader: lea rdx, rims folder (model, AO of LF, AO of RF) and lea r8, "\" between name and file
constexpr uint32_t k_prefix_leas[] = { 0xda94b5, 0xda9603, 0xda96a6 };
constexpr uint32_t k_sep_leas[] = { 0xda94d7, 0xda954d };

// Installs the option (Option::install); the sites already matched.
bool install(uint8_t *base, size_t image_size);

} // namespace fm6::stock_wheels
