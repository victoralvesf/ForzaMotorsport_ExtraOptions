/*
 * Diagnostico: debug layer do D3D12 (azimuth_fm5.ini [FM5] D3D12Debug=1; =2 tambem GPU-based validation).
 * O azimuth.exe importa d3d12.dll!D3D12CreateDevice por ordinal (101) no slot da IAT 0x26f868. O hook liga a
 * debug layer antes de criar o device e registra um callback do ID3D12InfoQueue1 que grava cada mensagem de
 * validacao no azimuth_fm5.log ("d3d12: ...") no momento da chamada que a gerou (antes do STOPPED do Azimuth).
 */
#define INITGUID
#include <windows.h>
#include <stdint.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#define RVA_IAT_D3D12_CREATE_DEVICE 0x26f868  /* d3d12.dll ordinal 101 */

extern uint8_t *g_base;
extern int cfg_int(const wchar_t *key, int def);
extern void wlog(const char *fmt, ...);

typedef HRESULT (WINAPI *create_device_fn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
static create_device_fn real_create_device;
static volatile LONG msg_count;

static void __stdcall on_message(D3D12_MESSAGE_CATEGORY cat, D3D12_MESSAGE_SEVERITY sev, D3D12_MESSAGE_ID id,
                                 LPCSTR desc, void *ctx)
{
    static const char *const sevs[] = { "CORRUPTION", "ERROR", "WARNING", "INFO", "MESSAGE" };
    LONG n = InterlockedIncrement(&msg_count);
    (void)ctx;
    if (sev >= D3D12_MESSAGE_SEVERITY_INFO) return;
    if (id == 820 || id == 821 || id == 971) return;                 /* Clear sem valor otimizado, PipelineLibrary: ruido */
    if (n > 2000 && sev == D3D12_MESSAGE_SEVERITY_WARNING) return;   /* nao inundar o log */
    wlog("d3d12: [%s cat %d id %d tid %lu] %s", (unsigned)sev < 5 ? sevs[sev] : "?", (int)cat, (int)id,
         GetCurrentThreadId(), desc ? desc : "");
}

static int g_mode;

/* chamado na primeira criacao de device (fora do DllMain/loader lock) */
static void enable_debug_layer(void)
{
    static int done;
    PFN_D3D12_GET_DEBUG_INTERFACE get_debug;
    ID3D12Debug *dbg = NULL;
    if (done) return;
    done = 1;
    get_debug = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12GetDebugInterface");
    if (!get_debug || FAILED(get_debug(&IID_ID3D12Debug, (void **)&dbg))) {
        wlog("debug12: D3D12GetDebugInterface falhou (Graphics Tools instalado?)");
        return;
    }
    dbg->lpVtbl->EnableDebugLayer(dbg);
    if (g_mode >= 2) {
        ID3D12Debug1 *d1 = NULL;
        if (SUCCEEDED(dbg->lpVtbl->QueryInterface(dbg, &IID_ID3D12Debug1, (void **)&d1))) {
            d1->lpVtbl->SetEnableGPUBasedValidation(d1, TRUE);
            d1->lpVtbl->Release(d1);
        }
    }
    dbg->lpVtbl->Release(dbg);
    wlog("debug12: debug layer do D3D12 ativada (modo %d)", g_mode);
}

static HRESULT WINAPI hook_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL level, REFIID riid, void **out)
{
    HRESULT hr;
    enable_debug_layer();
    hr = real_create_device(adapter, level, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ID3D12InfoQueue1 *q1 = NULL;
        ID3D12InfoQueue *q = NULL;
        IUnknown *dev = (IUnknown *)*out;
        if (SUCCEEDED(dev->lpVtbl->QueryInterface(dev, &IID_ID3D12InfoQueue1, (void **)&q1))) {
            DWORD cookie = 0;
            HRESULT h = q1->lpVtbl->RegisterMessageCallback(q1, on_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                            NULL, &cookie);
            wlog("debug12: InfoQueue1 callback hr=%#lx cookie=%lu", (unsigned long)h, cookie);
            q1->lpVtbl->Release(q1);
        } else if (SUCCEEDED(dev->lpVtbl->QueryInterface(dev, &IID_ID3D12InfoQueue, (void **)&q))) {
            wlog("debug12: so ID3D12InfoQueue (sem InfoQueue1) - mensagens nao serao logadas");
            q->lpVtbl->Release(q);
        } else {
            wlog("debug12: device sem InfoQueue (debug layer nao ativa?)");
        }
    }
    wlog("debug12: D3D12CreateDevice(level %#x) -> hr=%#lx", (unsigned)level, (unsigned long)hr);
    return hr;
}

int debug12_install(void)
{
    HMODULE d3d12;
    void **slot = (void **)(g_base + RVA_IAT_D3D12_CREATE_DEVICE);
    DWORD old;

    g_mode = cfg_int(L"D3D12Debug", 0);
    if (!g_mode) return 1;
    d3d12 = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12 || *slot != (void *)GetProcAddress(d3d12, "D3D12CreateDevice")) {
        wlog("debug12: slot da IAT %#x nao e D3D12CreateDevice; debug layer NAO ativada", RVA_IAT_D3D12_CREATE_DEVICE);
        return 1;
    }
    real_create_device = (create_device_fn)*slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = (void *)hook_create_device;
    VirtualProtect(slot, sizeof *slot, old, &old);
    wlog("debug12: hook no slot %#x (D3D12Debug=%d)", RVA_IAT_D3D12_CREATE_DEVICE, g_mode);
    return 1;
}
