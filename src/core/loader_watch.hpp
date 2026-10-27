// Moment to patch the game: Azimuth maps the game exe, applies its own patches, resolves the imports, protects the
// sections and then calls FlushInstructionCache(process, base, SizeOfImage) before the game runs (Azimuth 1.0.2,
// docs: phase 0). The watch hooks that import in azimuth.exe's IAT, found by name.
#pragma once

#include <cstdint>
#include <windows.h>

namespace core::loader_watch {

// Called for each FlushInstructionCache whose range is a whole PE64 image (PE header at the base and
// size == SizeOfImage), before the real call.
using OnImage = void (*)(uint8_t *base, const IMAGE_NT_HEADERS64 *nt);

// Hooks kernel32!FlushInstructionCache in the IAT of exe. false when exe does not import it.
bool install(HMODULE exe, OnImage on_image);

} // namespace core::loader_watch
