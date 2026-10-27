// winmm.dll proxy: azimuth.exe imports winmm statically, so this DLL loads before Azimuth's WinMain. It forwards
// every winmm export to System32 and, inside azimuth.exe, applies the extra options of the game Azimuth loads.
#include "forward.hpp"
#include "log.hpp"
#include "registry.hpp"

#include <windows.h>
#include <cwchar>

namespace {

// Replaces the file name in path (MAX_PATH buffer) with name.
void replace_file_name(wchar_t *path, const wchar_t *name)
{
    wchar_t *slash = wcsrchr(path, L'\\');
    wchar_t *dst = slash ? slash + 1 : path;
    size_t room = MAX_PATH - static_cast<size_t>(dst - path);
    if (wcslen(name) < room) wcscpy(dst, name);
}

bool process_is_azimuth()
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t *slash = wcsrchr(path, L'\\');
    return _wcsicmp(slash ? slash + 1 : path, L"azimuth.exe") == 0;
}

void attach(HINSTANCE self)
{
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(self, path, MAX_PATH)) {
        replace_file_name(path, L"fm_extra_options.log");
        core::log::open(path);
    }
    core::log::write("Forza Motorsport Extra Options (winmm.dll)");

    int missing = proxy::load_real_winmm();
    if (missing < 0) core::log::write("real winmm.dll did not load: winmm calls will crash");
    else core::log::write("real winmm.dll: %d exports, %d missing", proxy::export_count(), missing);

    if (!process_is_azimuth()) {
        core::log::write("process is not azimuth.exe: forwarding only");
        return;
    }
    for (const game::Module *g : games::all())
        core::log::write("game %s: TimeDateStamp %#x SizeOfImage %#x", g->tag, g->time_date_stamp, g->size_of_image);
    // The loader watch (IAT hook on FlushInstructionCache) comes with the core; until then nothing is applied.
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        attach(self);
    }
    return TRUE;
}
