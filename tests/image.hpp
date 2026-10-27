// Game images for the tests: a sparse fake one (only the pages of the sites) and the real exe mapped by section.
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace test_image {

// size bytes reserved; pages are committed on demand and left PAGE_EXECUTE_READ, like the game's code.
struct Fake {
    uint8_t *base;
    size_t size;
    explicit Fake(size_t size);
    ~Fake();
    // Commits the pages of [rva, rva + n + 32) and copies b to rva.
    void put(uint32_t rva, std::span<const uint8_t> b);
};

// The exe at path mapped as Azimuth does (headers + sections at their RVAs), every section PAGE_EXECUTE_READ.
// base is null when the file is missing or is not a PE32+.
struct Mapped {
    uint8_t *base = nullptr;
    const IMAGE_NT_HEADERS64 *nt = nullptr;
    explicit Mapped(const char *path);
    ~Mapped();
};

// Path of a file under game_binaries\ (local only: the tests that need it are skipped in CI).
const char *game_exe(const char *game);

} // namespace test_image
