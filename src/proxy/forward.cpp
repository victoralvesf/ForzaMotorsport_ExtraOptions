#include "forward.hpp"

#include <windows.h>
#include <cwchar>

// One slot per export (real_<name>) and one stub (fwd_<name>, exported by winmm.def) that jumps through it,
// so the arguments reach the real function untouched.
#define X(n) extern "C" { void *real_##n; }                                                   \
    asm(".text\n.globl fwd_" #n "\nfwd_" #n ":\n  jmp *real_" #n "(%rip)\n");
#include "winmm_exports.inc"
#undef X

namespace {

struct Export { const char *name; void **slot; };

const Export k_exports[] = {
#define X(n) { #n, &real_##n },
#include "winmm_exports.inc"
#undef X
};

} // namespace

namespace proxy {

int load_real_winmm()
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 12 > MAX_PATH) return -1;
    wcscpy(path + n, L"\\winmm.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) return -1;
    int missing = 0;
    for (const Export &e : k_exports)
        if (!(*e.slot = reinterpret_cast<void *>(GetProcAddress(real, e.name)))) missing++;
    return missing;
}

int export_count() { return static_cast<int>(sizeof k_exports / sizeof *k_exports); }

} // namespace proxy
