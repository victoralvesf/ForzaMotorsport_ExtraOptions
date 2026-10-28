// Byte signatures in the code of a mapped module (no fixed addresses: what the mod hooks in azimuth.exe is found
// by its bytes, and a signature that is missing or ambiguous leaves that feature off).
#pragma once

#include <cstddef>
#include <cstdint>

namespace core::scan {

constexpr size_t k_max_bytes = 256;

// The only place in the executable sections of the PE64 mapped at base where hex matches ("??" = any byte,
// spaces ignored); null when it matches nowhere, more than once, or hex is malformed.
const uint8_t *find_unique(const uint8_t *base, const char *hex);

// Target of the rel32 at p (the instruction ends at p + 4 + extra, extra = bytes of an immediate after it).
inline const uint8_t *rel32(const uint8_t *p, size_t extra = 0)
{
    int32_t d;
    __builtin_memcpy(&d, p, 4);
    return p + 4 + extra + d;
}

} // namespace core::scan
