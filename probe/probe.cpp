// winmm.dll probe for the migration (MIGRATION.md, phase 0): forwards every winmm export to System32 and only logs.
// It changes nothing in the game. Log: azimuth_probe.log next to azimuth.exe.
//  - Azimuth version (VERSIONINFO) and the IAT hooks on kernel32!FlushInstructionCache / RtlAddFunctionTable;
//  - every FlushInstructionCache call: PE header at the base, game identified by TimeDateStamp + SizeOfImage;
//  - for FM5 / FM6: the bytes at each extra option site (original or not) and the protection of the code.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>

// ---- forwarding to the real winmm.dll ----
#define X(n) extern "C" { void *real_##n; }                                                   \
    asm(".text\n.globl fwd_" #n "\nfwd_" #n ":\n  jmp *real_" #n "(%rip)\n");
#include "winmm_exports.inc"
#undef X

struct Export { const char *name; void **slot; };
static const Export k_exports[] = {
#define X(n) { #n, &real_##n },
#include "winmm_exports.inc"
#undef X
};

// ---- log ----
static FILE *g_log;
static ULONGLONG g_t0;
static SRWLOCK g_log_lock = SRWLOCK_INIT;

static void wlog(const char *fmt, ...)
{
    if (!g_log) return;
    AcquireSRWLockExclusive(&g_log_lock);
    fprintf(g_log, "[%7llu ms t%05lu] ", GetTickCount64() - g_t0, GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    ReleaseSRWLockExclusive(&g_log_lock);
}

static void to_hex(const uint8_t *p, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i * 2] = d[p[i] >> 4]; out[i * 2 + 1] = d[p[i] & 15]; }
    out[n * 2] = 0;
}

static bool readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const uint8_t *)p + n <= (const uint8_t *)mi.BaseAddress + mi.RegionSize;
}

static uint8_t *g_az;   // azimuth.exe

static const char *az_rva(const void *p)
{
    static thread_local char buf[32];
    uintptr_t a = (uintptr_t)p, b = (uintptr_t)g_az;
    MEMORY_BASIC_INFORMATION mi;
    if (VirtualQuery(p, &mi, sizeof mi) && mi.AllocationBase == g_az) snprintf(buf, sizeof buf, "azimuth+%#llx", (unsigned long long)(a - b));
    else snprintf(buf, sizeof buf, "%p", p);
    return buf;
}

// ---- Azimuth version (VS_FIXEDFILEINFO of the exe, no version.dll) ----
static void log_azimuth_version(void)
{
    HRSRC r = FindResourceW((HMODULE)g_az, MAKEINTRESOURCEW(1), (LPCWSTR)RT_VERSION);
    HGLOBAL h = r ? LoadResource((HMODULE)g_az, r) : nullptr;
    const uint8_t *p = h ? (const uint8_t *)LockResource(h) : nullptr;
    DWORD n = r ? SizeofResource((HMODULE)g_az, r) : 0;
    for (DWORD i = 0; p && i + sizeof(VS_FIXEDFILEINFO) <= n; i += 4) {
        const VS_FIXEDFILEINFO *fi = (const VS_FIXEDFILEINFO *)(p + i);
        if (fi->dwSignature != 0xfeef04bd) continue;
        unsigned v[4] = { HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS),
                          LOWORD(fi->dwFileVersionLS) };
        bool ok = v[0] > 1 || (v[0] == 1 && (v[1] > 0 || v[2] >= 2));
        wlog("azimuth.exe version %u.%u.%u.%u (%s)", v[0], v[1], v[2], v[3], ok ? ">= 1.0.2, supported" : "OLDER THAN 1.0.2");
        return;
    }
    wlog("azimuth.exe: no VERSIONINFO");
}

// ---- IAT hook by import name ----
static void **iat_slot(uint8_t *mod, const char *dll, const char *func)
{
    auto *dos = (IMAGE_DOS_HEADER *)mod;
    auto *nt = (IMAGE_NT_HEADERS64 *)(mod + dos->e_lfanew);
    auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = (IMAGE_IMPORT_DESCRIPTOR *)(mod + dir.VirtualAddress); dir.VirtualAddress && d->Name; d++) {
        if (_stricmp((const char *)(mod + d->Name), dll) != 0) continue;
        auto *names = (IMAGE_THUNK_DATA64 *)(mod + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto *slots = (IMAGE_THUNK_DATA64 *)(mod + d->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            auto *ibn = (IMAGE_IMPORT_BY_NAME *)(mod + names[i].u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, func) == 0) return (void **)&slots[i].u1.Function;
        }
    }
    return nullptr;
}

static bool iat_hook(const char *dll, const char *func, void *hook, void **orig)
{
    void **slot = iat_slot(g_az, dll, func);
    DWORD old;
    if (!slot || !VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
        wlog("IAT hook %s!%s: import not found", dll, func);
        return false;
    }
    *orig = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
    wlog("IAT hook %s!%s: slot %s (was %p)", dll, func, az_rva(slot), *orig);
    return true;
}

// ---- games and the sites of the extra options ----
struct Site { const char *what; uint32_t rva; const char *orig; };
struct Game { const char *tag; uint32_t timestamp, size_of_image; const Site *sites; size_t n; };

static const Site k_fm5_sites[] = {
    { "SkipIntro",             0x13a33e, "80be9f30000000" },
    { "SkipIntro",             0x13a825, "80be9f30000000" },
    { "SkipIntro",             0x13b9ce, "80be9f30000000" },
    { "SkipPresentationVideo", 0x4ab060, "4883ec7848c7442420feffffff" },
    { "SkipPresentationVideo", 0x4b2f50, "4883ec48488d15851ac200" },
    { "UnlockDLC OfferOwned",  0x203050, "48895c241048896c2418565741564883" },
    { "UnlockDLC no-inventory (old)", 0x174df4, "488dbe8800000033d2488bcfe8abf7ffff" },
    { "Azimuth offers branch", 0x174d73, "48833dedf24502007477" },
    { "Azimuth ownership query (lambda)", 0x19e6a0, "488b0dc15943024883c208e8a0490600" },
    { "EnableMarketplace",     0x180c2a, "c783bc30000000000001" },
};
static const Site k_fm6_sites[] = {
    { "SkipIntro",             0xa06b33, "4138b424d730000074" },
    { "SkipIntro",             0xa06c4d, "4138b424d730000074" },
    { "SkipIntro",             0xa07e0c, "4138b424d730000074" },
    { "SkipIntro logo parade", 0xa08eb6, "4438b8d730000075" },
    { "SkipPresentationVideo", 0xce1c55, "488d4b28e832d846ff" },
    { "SkipPresentationVideo", 0xce1eb4, "85c07519" },
    { "UnlockDLC OfferOwned",  0xa8ad80, "48895c2410565741564883ec20488db9" },
    { "UnlockDLC no-inventory (old)", 0x43be01, "488dbba000000033d2488bcfe86a000000" },
    { "Azimuth offers branch", 0x43bdc5, "48833d632f9a01007432" },
    { "Azimuth ownership query (lambda)", 0xa40f50, "488b0dd9dd39014883c208e8209e0400" },
    { "Dash font measure",     0x10bf78, "488bc4488958084889681048897018" },
    { "Dash font glyph lookup", 0x10c270, "448b51044c8b5908448bc24533c9" },
};
static const Game k_games[] = {
    { "FM5", 0x53d8d1f2, 0x279d000, k_fm5_sites, sizeof k_fm5_sites / sizeof *k_fm5_sites },
    { "FM6", 0x58afb1cd, 0x1fc8000, k_fm6_sites, sizeof k_fm6_sites / sizeof *k_fm6_sites },
};

static const IMAGE_NT_HEADERS64 *pe_at(const uint8_t *base)
{
    if (!readable(base, 0x40) || ((const IMAGE_DOS_HEADER *)base)->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    LONG off = ((const IMAGE_DOS_HEADER *)base)->e_lfanew;
    if (off <= 0 || off > 0x1000 || !readable(base + off, sizeof(IMAGE_NT_HEADERS64))) return nullptr;
    auto *nt = (const IMAGE_NT_HEADERS64 *)(base + off);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    return nt;
}

static const char *protect_name(DWORD p)
{
    switch (p & 0xff) {
    case PAGE_EXECUTE_READ: return "RX";
    case PAGE_EXECUTE_READWRITE: return "RWX";
    case PAGE_READWRITE: return "RW";
    case PAGE_READONLY: return "R";
    case PAGE_EXECUTE: return "X";
    default: return "?";
    }
}

static void check_game(const Game &g, const uint8_t *base)
{
    int ok = 0, diff = 0;
    for (size_t i = 0; i < g.n; i++) {
        const Site &s = g.sites[i];
        size_t len = strlen(s.orig) / 2;
        char have[80];
        const uint8_t *p = base + s.rva;
        if (!readable(p, len)) { wlog("  %s %-34s %#8x: not readable", g.tag, s.what, s.rva); diff++; continue; }
        to_hex(p, len, have);
        bool same = strcmp(have, s.orig) == 0;
        MEMORY_BASIC_INFORMATION mi;
        VirtualQuery(p, &mi, sizeof mi);
        wlog("  %s %-34s %#8x %-3s %s%s%s", g.tag, s.what, s.rva, protect_name(mi.Protect), same ? "original" : "CHANGED ",
             same ? "" : " now ", same ? "" : have);
        same ? ok++ : diff++;
    }
    wlog("%s: %d sites original, %d changed/unreadable", g.tag, ok, diff);
}

// ---- hooks ----
using FlushIC = BOOL(WINAPI *)(HANDLE, LPCVOID, SIZE_T);
using AddFT = BOOLEAN(WINAPI *)(PRUNTIME_FUNCTION, DWORD, DWORD64);
static FlushIC g_flush;
static AddFT g_add_ft;
static volatile LONG g_identified;

static BOOL WINAPI hook_flush(HANDLE process, LPCVOID base, SIZE_T size)
{
    void *caller = __builtin_return_address(0);
    const uint8_t *b = (const uint8_t *)base;
    const IMAGE_NT_HEADERS64 *nt = b ? pe_at(b) : nullptr;
    if (!nt) {
        wlog("FlushInstructionCache(%p, %#zx) from %s: no PE at the base", base, (size_t)size, az_rva(caller));
    } else {
        DWORD ts = nt->FileHeader.TimeDateStamp, soi = nt->OptionalHeader.SizeOfImage;
        wlog("FlushInstructionCache(%p, %#zx) from %s: PE TimeDateStamp=%#lx SizeOfImage=%#lx CheckSum=%#lx%s", base,
             (size_t)size, az_rva(caller), ts, soi, nt->OptionalHeader.CheckSum, soi == size ? " (whole image)" : "");
        for (const Game &g : k_games) {
            if (g.timestamp != ts || g.size_of_image != soi || size != soi) continue;
            LONG n = InterlockedIncrement(&g_identified);
            wlog("%s identified at %p (call %ld): extra option sites, before the game runs:", g.tag, base, n);
            check_game(g, b);
        }
    }
    return g_flush(process, base, size);
}

static BOOLEAN WINAPI hook_add_ft(PRUNTIME_FUNCTION table, DWORD count, DWORD64 base)
{
    const IMAGE_NT_HEADERS64 *nt = pe_at((const uint8_t *)base);
    wlog("RtlAddFunctionTable(%p, %lu, base %#llx) from %s%s", (void *)table, count, (unsigned long long)base,
         az_rva(__builtin_return_address(0)), nt ? " (PE at the base)" : "");
    return g_add_ft(table, count, base);
}

// game code started: the first thread whose start address is outside azimuth.exe and the system DLLs is logged
using CreateThreadFn = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static CreateThreadFn g_create_thread;
static volatile LONG g_threads;

static HANDLE WINAPI hook_create_thread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID arg,
                                        DWORD flags, LPDWORD id)
{
    HANDLE h = g_create_thread(sa, stack, start, arg, flags, id);
    if (InterlockedIncrement(&g_threads) <= 64)
        wlog("CreateThread start %s arg %p from %s (identified games so far: %ld)", az_rva((void *)start), arg,
             az_rva(__builtin_return_address(0)), g_identified);
    return h;
}

static void load_real_winmm(void)
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    wcscpy(path + n, L"\\winmm.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) { wlog("real winmm did not load"); return; }
    int missing = 0;
    for (const Export &e : k_exports)
        if (!(*e.slot = (void *)GetProcAddress(real, e.name))) missing++;
    wlog("real winmm %ls: %zu exports, %d missing", path, sizeof k_exports / sizeof *k_exports, missing);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    g_t0 = GetTickCount64();
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(inst, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    wcscpy(slash ? slash + 1 : path, L"azimuth_probe.log");
    g_log = _wfopen(path, L"w");
    wlog("winmm probe (logs only, changes nothing)");
    load_real_winmm();

    g_az = (uint8_t *)GetModuleHandleW(nullptr);
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    slash = wcsrchr(path, L'\\');
    if (_wcsicmp(slash ? slash + 1 : path, L"azimuth.exe") != 0) {
        wlog("process is not azimuth.exe (%ls), forwarding only", path);
        return TRUE;
    }
    log_azimuth_version();
    iat_hook("KERNEL32.dll", "FlushInstructionCache", (void *)hook_flush, (void **)&g_flush);
    iat_hook("KERNEL32.dll", "RtlAddFunctionTable", (void *)hook_add_ft, (void **)&g_add_ft);
    iat_hook("KERNEL32.dll", "CreateThread", (void *)hook_create_thread, (void **)&g_create_thread);
    return TRUE;
}
