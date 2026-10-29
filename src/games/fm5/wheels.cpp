#include "wheels.hpp"

namespace fm5::stock_wheels {

namespace {

// FM5 car packages keep the stock rim model in one of a few folders and file names (the file system ignores
// case); together these cover all but a few cars that reuse another car's rim under its name.
constexpr const char *k_folders[] = {
    "\\scene\\_library\\Scene\\Wheels\\",
    "\\scene\\Wheels\\",
    "\\scene\\Wheel\\",
};
constexpr const char *k_files[] = {
    "_wheelLF.modelbin", "_wheel_LF.modelbin", "_WheeLF.modelbin", "_WheelLR.modelbin", "_001_wheelLF.modelbin",
};

constexpr core::stock_rims::Game k_game = {
    .tag = "FM5",
    .view_lea = k_view_lea,
    .cats_lea = k_cats_lea,
    .names_lea = k_names_lea,
    .joins_lea = k_joins_lea,
    .view_len_mov = k_view_len_mov,
    .load = k_load,
    .load_entry = k_load_entry,
    .fs_read = k_fs_read,
    .exists_slot = 0xc8,
    .prefix_leas = k_prefix_leas,
    .prefix_len_lea = k_prefix_len_lea,
    .sep_leas = k_sep_leas,
    .file_leas = k_file_leas,
    .folders = k_folders,
    .files = k_files,
};

} // namespace

bool install(uint8_t *base, size_t image_size) { return core::stock_rims::install(k_game, base, image_size); }

} // namespace fm5::stock_wheels
