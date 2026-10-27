#include "loader_watch.hpp"

#include "log.hpp"
#include "pe.hpp"

namespace core::loader_watch {

namespace {

using FlushIC = BOOL(WINAPI *)(HANDLE, LPCVOID, SIZE_T);
FlushIC g_real;
OnImage g_on_image;

BOOL WINAPI hook_flush(HANDLE process, LPCVOID base, SIZE_T size)
{
    if (base && size >= 0x1000) {
        const IMAGE_NT_HEADERS64 *nt = pe::headers(base);
        if (nt && nt->OptionalHeader.SizeOfImage == size)
            g_on_image(static_cast<uint8_t *>(const_cast<void *>(base)), nt);
    }
    return g_real(process, base, size);
}

} // namespace

bool install(HMODULE exe, OnImage on_image)
{
    void **slot = pe::iat_slot(exe, "KERNEL32.dll", "FlushInstructionCache");
    DWORD old;
    if (!slot || !VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
        log::write("loader watch: FlushInstructionCache is not imported, nothing will be applied");
        return false;
    }
    g_on_image = on_image;
    g_real = reinterpret_cast<FlushIC>(*slot);
    *slot = reinterpret_cast<void *>(hook_flush);
    VirtualProtect(slot, sizeof *slot, old, &old);
    log::write("loader watch: FlushInstructionCache hooked (IAT slot %p)", static_cast<void *>(slot));
    return true;
}

} // namespace core::loader_watch
