/*
 * winmm.dll proxy para o azimuth.exe: carrega antes do WinMain (WINMM e import estatico) e
 *  - repassa todos os exports para o winmm.dll do System32 (winmm_stubs.S)
 *  - aplica os patches do azimuth.exe e os hooks que repassam a cada jogo (azimuth.c)
 * Organizacao do codigo (raiz, common\, fm5\, fm6\): proxy.h. Enderecos: ver docs\contexto\ (indice no CLAUDE.md).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "proxy.h"

uint8_t *g_base;
static FILE *g_log;
static wchar_t g_dir[MAX_PATH];   /* pasta do proxy (= do azimuth.exe), com a barra final */

void proxy_path(const wchar_t *file, wchar_t *out)
{
    _snwprintf(out, MAX_PATH, L"%ls%ls", g_dir, file);
    out[MAX_PATH - 1] = 0;
}

/* ---- debug.ini (mesma pasta), secao [Debug]: diagnostico (D3D12Debug, DrawSkip...), vale para FM5 e FM6;
   fica no repositorio mas fora do zip do release ---- */
static wchar_t g_ini_dbg[MAX_PATH];

int dbg_int(const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(L"Debug", key, def, g_ini_dbg);
}

/* texto ASCII da chave (vazio se nao existe); devolve o tamanho */
int dbg_str(const wchar_t *key, char *out, int len)
{
    wchar_t w[1024];
    DWORD n = GetPrivateProfileStringW(L"Debug", key, L"", w, 1024, g_ini_dbg);
    int i;
    for (i = 0; i < (int)n && i < len - 1; i++) out[i] = w[i] < 128 ? (char)w[i] : '?';
    out[i] = 0;
    return i;
}

void wlog(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

/* azimuth.c */
int azimuth_install(void);

/* ---- repasse para o winmm real ---- */
extern void *winmm_real[];
extern const char *const winmm_names[];

static void load_real_winmm(void)
{
    wchar_t path[MAX_PATH];
    HMODULE real;
    int i;
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    wcscpy(path + n, L"\\winmm.dll");
    real = LoadLibraryW(path);
    if (!real) { wlog("real winmm did not load"); return; }
    for (i = 0; winmm_names[i]; i++)
        winmm_real[i] = (void *)GetProcAddress(real, winmm_names[i]);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH], *slash;
        int i;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0; else g_dir[0] = 0;
        proxy_path(L"azimuth_motorsport.log", path);
        g_log = _wfopen(path, L"w");
        for (i = 0; i < GAME_COUNT; i++) games[i]->load_config();
        proxy_path(L"debug.ini", g_ini_dbg);
        wlog("config: %ls (D3D12Debug=%d DrawSkip=%d)", g_ini_dbg, dbg_int(L"D3D12Debug", 0), dbg_int(L"DrawSkip", 0));
        load_real_winmm();
        g_base = (uint8_t *)GetModuleHandleW(NULL);
        GetModuleFileNameW(NULL, path, MAX_PATH);
        slash = wcsrchr(path, L'\\');
        if (_wcsicmp(slash ? slash + 1 : path, L"azimuth.exe") != 0) {
            wlog("process is not azimuth.exe, forwarding only");
            return TRUE;
        }
        if (azimuth_install()) fixed_va_hold();
        else {
            wlog("NOTHING WAS CHANGED");
            MessageBoxW(NULL, L"azimuth.exe is not the expected version; the mod's patches were not applied.\n"
                              L"See azimuth_motorsport.log.", L"winmm FM5", MB_ICONWARNING);
        }
    }
    return TRUE;
}
