/* kernelx.dll de teste para tools\xg_layout_probe.c (exports em xg_probe_kernelx.def; o resto vai pro kernel32).
 * gcc -shared -nostdlib -o build\xgprobe\kernelx.dll tools\xg_probe_kernelx.c tools\xg_probe_kernelx.def
 *   -lkernel32 -e DllMainCRTStartup */
#include <windows.h>

BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD r, void *p) { (void)h; (void)r; (void)p; return TRUE; }
void *XMemAlloc(SIZE_T n, ULONGLONG attr) { (void)attr; return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
void XMemFree(void *p, ULONGLONG attr) { (void)attr; if (p) VirtualFree(p, 0, MEM_RELEASE); }
