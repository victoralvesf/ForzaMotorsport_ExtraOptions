// FM5 StockWheels option (core::stock_rims): where the rim SQL fragments and the rim loader are in FM5.
#pragma once

#include "stock_rims.hpp"

#include <cstddef>
#include <cstdint>

namespace fm5::stock_wheels {

// Sites of the option (in fm5.cpp, checked before install).
constexpr uint32_t k_view_lea = 0x1d4cfb;  // lea rdx, rims of the car's parts view (aftermarket, priced)
constexpr uint32_t k_cats_lea = 0x49bc50;  // lea r8, "FROM" part of the rim categories query
constexpr uint32_t k_names_lea = 0x49ce81; // lea r8, columns (maker, name, logo, icon) of the rims of a category
constexpr uint32_t k_joins_lea = 0x49ceda; // lea r8, joins (annotations, categories) of the rims of a category
constexpr uint32_t k_load = 0x6cedf0;      // rim loader: (this, const std::string &media_name) -> bool
constexpr const char *k_load_entry = "48 8b c4 55 41 56 41 57 48 8d a8 98 fd ff ff 48 81 ec 50 03 00 00";
constexpr uint32_t k_fs_read = 0x6cf055;   // mov rcx, [file system] inside the loader
// inside the loader: lea r8d, [r15 + length of the rims folder] (the model's prefix is a counted assign), lea rdx,
// rims folder (model, AO of LF, AO of RF), lea r8, "\" between name and file (model LF, RF) and lea r8, LF model file
constexpr uint32_t k_prefix_len_lea = 0x6cee57;
constexpr uint32_t k_prefix_leas[] = { 0x6cee5b, 0x6cf0af, 0x6cf1b9 };
constexpr uint32_t k_sep_leas[] = { 0x6cee80, 0x6cef7d };
constexpr uint32_t k_file_leas[] = { 0x6ceea7 };

// Installs the option (Option::install); the sites already matched.
bool install(uint8_t *base, size_t image_size);

} // namespace fm5::stock_wheels
