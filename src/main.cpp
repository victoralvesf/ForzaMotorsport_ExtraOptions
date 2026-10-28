// winmm.dll proxy: azimuth.exe imports winmm statically, so this DLL loads before Azimuth's WinMain. It forwards
// every winmm export to System32 and, inside azimuth.exe, applies the extra options of the game Azimuth loads.
#include "forward.hpp"
#include "launcher.hpp"
#include "loader_watch.hpp"
#include "log.hpp"
#include "options.hpp"
#include "registry.hpp"
#include "version.hpp"

#include <windows.h>
#include <cwchar>

namespace {

// Replaces the file name in path (MAX_PATH buffer) with name; false (path unchanged) when it does not fit.
bool replace_file_name(wchar_t *path, const wchar_t *name)
{
    wchar_t *slash = wcsrchr(path, L'\\');
    wchar_t *dst = slash ? slash + 1 : path;
    size_t room = MAX_PATH - static_cast<size_t>(dst - path);
    if (wcslen(name) >= room) return false;
    wcscpy(dst, name);
    return true;
}

wchar_t g_ini[MAX_PATH]; // motorsport_extraoptions.ini next to winmm.dll (= azimuth.exe): one ini, a section per game
volatile LONG g_applied;

// Azimuth flushes the mapped game image just before running it: the first known game gets its options, once.
void on_image(uint8_t *base, const IMAGE_NT_HEADERS64 *nt)
{
    uint32_t ts = nt->FileHeader.TimeDateStamp, size = nt->OptionalHeader.SizeOfImage;
    const game::Module *g = games::find(ts, size);
    if (!g) {
        core::log::write("image at %p (TimeDateStamp %#x, SizeOfImage %#x): not a supported game",
                         static_cast<void *>(base), ts, size);
        return;
    }
    if (InterlockedCompareExchange(&g_applied, 1, 0) != 0) {
        core::log::write("%s at %p again: options already applied, ignored", g->tag, static_cast<void *>(base));
        return;
    }
    core::options::apply(*g, base, g_ini);
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
    if (!GetModuleFileNameW(self, g_ini, MAX_PATH) || !replace_file_name(g_ini, L"motorsport_extraoptions.ini")) {
        core::log::write("path of motorsport_extraoptions.ini too long: options off");
        return;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    core::version::Version v;
    if (!core::version::of_module(exe, v))
        core::log::write("azimuth.exe: no version information");
    else
        core::log::write("azimuth.exe %u.%u.%u.%u%s", v.major, v.minor, v.build, v.revision,
                         core::version::at_least(v, 1, 0, 2) ? "" : ": OLDER THAN 1.0.2, update Azimuth");
    core::loader_watch::install(exe, on_image);
    // the options in the settings page of each game, before WinMain shows the launcher
    launcher::install(exe, games::all(), g_ini);
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
