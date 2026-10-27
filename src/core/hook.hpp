// Detour with a trampoline on game code.
#pragma once

#include <cstdint>

namespace core::hook {

// Size of the absolute jmp written at the hooked address (ff 25 00000000 + 8-byte target).
constexpr size_t k_jmp_size = 14;

// stolen_hex: the whole instructions at `at` that the jmp overwrites (at least k_jmp_size bytes, none relative to
// RIP, checked byte by byte). They are copied to a trampoline that jumps back to at + their size; then `at` jumps
// to target. *tramp = the trampoline (calls the original function). false (nothing written) when the bytes do not
// match or the trampoline cannot be allocated.
bool install(uint8_t *at, const char *stolen_hex, void *target, void **tramp);

} // namespace core::hook
