#include "stock_wheels.hpp"

namespace fm6::stock_wheels {

namespace {

// Every FM6 car package has its stock rim at scene\_library\Scene\Wheels\<car>_WheelLF.modelbin (the file system
// ignores case), so only the folder changes and the game's file name stays.
constexpr const char *k_folders[] = { "\\scene\\_library\\Scene\\Wheels\\" };
constexpr const char *k_files[] = { "_wheelLF.modelbin" };

constexpr core::stock_rims::Game k_game = {
    .tag = "FM6",
    .view_lea = k_view_lea,
    .cats_lea = k_cats_lea,
    .names_lea = k_names_lea,
    .joins_lea = k_joins_lea,
    .load = k_load,
    .load_entry = k_load_entry,
    .fs_read = k_fs_read,
    .exists_slot = 0xe0,
    .prefix_leas = k_prefix_leas,
    .prefix_len_lea = 0,
    .sep_leas = k_sep_leas,
    .file_leas = {},
    .folders = k_folders,
    .files = k_files,
};

} // namespace

bool install(uint8_t *base, size_t image_size) { return core::stock_rims::install(k_game, base, image_size); }

} // namespace fm6::stock_wheels
