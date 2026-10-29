// StockWheels (FM5 and FM6, same engine): a fifth rim category in the car upgrades with the stock rims of every
// car, by make (alphabetical), each one fitted from its own car's package. The game's folder only describes where
// its SQL fragments and its rim loader are; this module builds the SQL and redirects the loader.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace core::stock_rims {

struct Game {
    const char *tag; // log prefix ("FM6")
    // lea rdx/r8 of the SQL fragments: rims of the car's parts view (aftermarket, priced), "FROM" part of the rim
    // categories query, columns (maker, name, logo, icon) and joins (annotations, categories) of a category's rims
    uint32_t view_lea, cats_lea, names_lea, joins_lea;
    uint32_t load;          // rim loader: (this, const std::string &media_name) -> bool
    const char *load_entry; // its first whole instructions (hook, no RIP-relative)
    uint32_t fs_read;       // mov rcx, [file system] inside the loader
    uint32_t exists_slot;   // byte offset of "exists(const std::string &path) -> bool" in the file system's vtable
    // lea rdx, rims folder (model, AO of LF, AO of RF)
    std::span<const uint32_t> prefix_leas;
    // 0, or "lea r8d, [r15 + length of the rims folder]" right before prefix_leas[0] (a counted assign)
    uint32_t prefix_len_lea;
    std::span<const uint32_t> sep_leas;  // lea r8, "\" between the name and the file (model of LF and RF)
    std::span<const uint32_t> file_leas; // lea r8, "_wheelLF.modelbin" (may be empty: then files has only it)
    // where a car's package has its stock rim model: folders between the car's name and the file's name, and file
    // name endings after it, tried in order (the first that exists is loaded)
    std::span<const char *const> folders;
    std::span<const char *const> files;
};

// The mod's version of each SQL fragment, built from the game's own: false when a token is missing or repeated.
bool view_sql(const char *game, char *out, size_t cap);
bool categories_sql(const char *game, char *out, size_t cap);
bool names_sql(const char *game, char *out, size_t cap);
bool joins_sql(const char *game, char *out, size_t cap);

// Installs the option for g (from its Option::install; the sites already matched). Once per process.
bool install(const Game &g, uint8_t *base, size_t image_size);

} // namespace core::stock_rims
