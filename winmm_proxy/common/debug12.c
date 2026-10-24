/*
 * Diagnostico: debug layer do D3D12 (debug.ini [Debug] D3D12Debug=1; =2 tambem GPU-based validation).
 * O azimuth.exe importa d3d12.dll!D3D12CreateDevice por ordinal (101) no slot da IAT 0x26f868, usado so pelo
 * call [rip] em 0x567df. O hook troca o disp32 desse call por um ponteiro nosso perto da imagem (a IAT fica como
 * esta: o RenderDoc reescreve as IATs depois de nos e apagava um hook ali) e chama o que estiver na IAT. Ele liga a
 * debug layer antes de criar o device e registra um callback do ID3D12InfoQueue1 que grava cada mensagem de
 * validacao no azimuth_motorsport.log ("d3d12: ...") no momento da chamada que a gerou (antes do STOPPED do Azimuth).
 * D3D12Debug=3 (4 = + experimento do common/gpuguard.c: pula a compute interna k12 + [SRV, UAV]; 5 = + placement textures
 * do FM6 committed em vez de reservadas, fm6/fm6.c; 6 = + recursos/views nunca destruidos, fm6/fm6.c; 7 = + nenhum CopyTextureRegion; 8 = + nenhum draw; 9 = + nenhum Dispatch fora da compute interna, common/gpuguard.c): sem debug layer, so os breadcrumbs do DRED; uma thread espera o device removido (fence que vai a
 * UINT64_MAX) e grava no log as ultimas listas com a contagem de operacoes e, de cada uma, os pontos de chamada de
 * Dispatch/CopyTextureRegion/draws/ClearRenderTargetView guardados pela trava (common/gpuguard.c, gg_trace_*).
 * O hook fica sempre ligado (mesmo com D3D12Debug=0) para a trava de page fault (common/gpuguard.c): o device nasce
 * antes do apply_patches, quando o jogo ainda nao e conhecido. No Wine/Proton (vkd3d-proton) a trava fica desligada
 * por padrao (D3D12Hooks=3): ela corrige comportamentos do driver da AMD no Windows e, no RADV, os descritores e root
 * views nulos dela davam page fault em VA 0 (issue de usuario Linux, 2026-10-23).
 */
#define INITGUID
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include "../proxy.h"

#define RVA_IAT_D3D12_CREATE_DEVICE 0x26f868  /* d3d12.dll ordinal 101 */
#define RVA_CALL_D3D12_CREATE_DEVICE 0x567df /* ff 15 disp32 -> IAT 0x26f868 (unico uso) */

extern void gpuguard_install(IUnknown *dev);
extern void stutter_device(IUnknown *dev);
extern void gg_trace_dump(void *cl);
extern void gg_ring_dump(void);
extern void cx_dump(void);
extern void gt_dump(void);
extern int st_on;
extern int gg_trace_on, gg_draw_skip;
extern void gg_parse_hashes(const char *str);

typedef HRESULT (WINAPI *create_device_fn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
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

static const char *dred_op_name(D3D12_AUTO_BREADCRUMB_OP op)
{
    static const char *const names[] = {
        "SetMarker", "BeginEvent", "EndEvent", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect",
        "Dispatch", "CopyBufferRegion", "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource",
        "ClearRenderTargetView", "ClearUnorderedAccessView", "ClearDepthStencilView", "ResourceBarrier",
        "ExecuteBundle", "Present", "ResolveQueryData", "BeginSubmission", "EndSubmission", "DecodeFrame",
        "ProcessFrames", "AtomicCopyBufferUint", "AtomicCopyBufferUint64", "ResolveSubresourceRegion",
        "WriteBufferImmediate", "DecodeFrame1", "SetProtectedResourceSession", "DecodeFrame2", "ProcessFrames1",
        "BuildRaytracingAccelerationStructure", "EmitRaytracingAccelerationStructurePostbuildInfo",
        "CopyRaytracingAccelerationStructure", "DispatchRays", "InitializeMetaCommand", "ExecuteMetaCommand",
        "EstimateMotion", "ResolveMotionVectorHeap", "SetPipelineState1", "InitializeExtensionCommand",
        "ExecuteExtensionCommand", "DispatchMesh", "EncodeFrame", "ResolveEncoderOutputMetadata", "Barrier",
        "BeginCommandList", "DispatchGraph", "SetProgram",
    };
    return (unsigned)op < sizeof names / sizeof *names ? names[op] : "?";
}

/* espera a remocao do device e grava os breadcrumbs */
static DWORD WINAPI dred_watch(void *param)
{
    ID3D12Device *dev = param;
    ID3D12Fence *fence = NULL;
    ID3D12DeviceRemovedExtendedData1 *dred = NULL;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 out;
    const D3D12_AUTO_BREADCRUMB_NODE1 *node;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    int shown = 0, total = 0, idx = 0;
    if (!ev || FAILED(dev->lpVtbl->CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence)) ||
        FAILED(fence->lpVtbl->SetEventOnCompletion(fence, UINT64_MAX, ev))) {
        wlog("debug12: DRED: no wait fence");
        return 0;
    }
    WaitForSingleObject(ev, INFINITE);
    wlog("debug12: DRED: device removed hr=%#lx", (unsigned long)dev->lpVtbl->GetDeviceRemovedReason(dev));
    if (FAILED(dev->lpVtbl->QueryInterface(dev, &IID_ID3D12DeviceRemovedExtendedData1, (void **)&dred)) ||
        FAILED(dred->lpVtbl->GetAutoBreadcrumbsOutput1(dred, &out))) {
        wlog("debug12: DRED: no breadcrumbs");
        return 0;
    }
    for (node = out.pHeadAutoBreadcrumbNode; node; node = node->pNext) total++;
    for (node = out.pHeadAutoBreadcrumbNode; node; node = node->pNext, idx++) {
        UINT32 last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0, i;
        unsigned counts[64] = { 0 };
        char buf[512];
        int len = 0;
        if (!node->pCommandHistory) continue;
        if (last >= node->BreadcrumbCount && idx + 6 < total) continue;   /* terminou: so as ultimas listas */
        shown++;
        for (i = 0; i < node->BreadcrumbCount; i++)
            if ((unsigned)node->pCommandHistory[i] < 64) counts[node->pCommandHistory[i]]++;
        for (i = 0; i < 64 && len < 400; i++)
            if (counts[i]) len += wsprintfA(buf + len, " %s=%u", dred_op_name(i), counts[i]);
        buf[len] = 0;
        wlog("debug12: DRED: node %d/%d list %p queue %p: %u of %u operations;%s", idx, total, (void *)node->pCommandList,
             (void *)node->pCommandQueue, last, node->BreadcrumbCount, buf);
        gg_trace_dump(node->pCommandList);
    }
    wlog("debug12: DRED: %d lists shown", shown);
    if (gg_trace_on) gg_ring_dump();
    if (st_on) gt_dump();
    if (st_on) cx_dump();
    return 0;
}

static void enable_dred(void)
{
    PFN_D3D12_GET_DEBUG_INTERFACE get_debug =
        (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12GetDebugInterface");
    ID3D12DeviceRemovedExtendedDataSettings1 *s = NULL;
    if (!get_debug || FAILED(get_debug(&IID_ID3D12DeviceRemovedExtendedDataSettings1, (void **)&s))) {
        wlog("debug12: DRED: no ID3D12DeviceRemovedExtendedDataSettings1");
        return;
    }
    s->lpVtbl->SetAutoBreadcrumbsEnablement(s, D3D12_DRED_ENABLEMENT_FORCED_ON);
    s->lpVtbl->SetPageFaultEnablement(s, D3D12_DRED_ENABLEMENT_FORCED_ON);
    s->lpVtbl->SetBreadcrumbContextEnablement(s, D3D12_DRED_ENABLEMENT_FORCED_ON);
    s->lpVtbl->Release(s);
    wlog("debug12: DRED: breadcrumbs enabled");
}

/* chamado na primeira criacao de device (fora do DllMain/loader lock) */
static void enable_debug_layer(void)
{
    static int done;
    PFN_D3D12_GET_DEBUG_INTERFACE get_debug;
    ID3D12Debug *dbg = NULL;
    if (done) return;
    done = 1;
    if (g_mode >= 3) { enable_dred(); return; }
    get_debug = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12GetDebugInterface");
    if (!get_debug || FAILED(get_debug(&IID_ID3D12Debug, (void **)&dbg))) {
        wlog("debug12: D3D12GetDebugInterface failed (Graphics Tools installed?)");
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
    wlog("debug12: D3D12 debug layer enabled (mode %d)", g_mode);
}

static int g_hooks = 1;                 /* debug.ini D3D12Hooks (debug12_install) */

/* Wine/Proton: a ntdll dele exporta wine_get_version (ponteiro trocavel pelos testes) */
static int on_wine(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version");
}
int (*debug12_wine)(void) = on_wine;

HRESULT WINAPI debug12_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL level, REFIID riid, void **out)
{
    HRESULT hr;
    if (g_mode) enable_debug_layer();
    /* o que estiver na IAT agora: d3d12.dll ou o hook do RenderDoc (device embrulhado, capturado com a trava) */
    hr = (*(create_device_fn *)(g_base + RVA_IAT_D3D12_CREATE_DEVICE))(adapter, level, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        if (g_hooks != 3) gpuguard_install((IUnknown *)*out);
        if (g_hooks != 2) stutter_device((IUnknown *)*out);   /* StutterLog=1 (common/stutter.c); por fora dos hooks do gpuguard */
    }
    if (!g_mode) return hr;
    if (g_mode >= 3 && SUCCEEDED(hr) && out && *out) {
        static int watching;
        ID3D12Device *dev = NULL;
        if (!watching && SUCCEEDED(((IUnknown *)*out)->lpVtbl->QueryInterface(*out, &IID_ID3D12Device, (void **)&dev))) {
            watching = 1;
            CloseHandle(CreateThread(NULL, 0, dred_watch, dev, 0, NULL));
        }
    } else if (SUCCEEDED(hr) && out && *out) {
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
            wlog("debug12: only ID3D12InfoQueue (no InfoQueue1) - messages will not be logged");
            q->lpVtbl->Release(q);
        } else {
            wlog("debug12: device without InfoQueue (debug layer not active?)");
        }
    }
    wlog("debug12: D3D12CreateDevice(level %#x) -> hr=%#lx", (unsigned)level, (unsigned long)hr);
    return hr;
}

int debug12_install(void)
{
    uint8_t *call = g_base + RVA_CALL_D3D12_CREATE_DEVICE, *ptr;
    int32_t disp;
    DWORD old;

    g_mode = dbg_int(L"D3D12Debug", 0);
    gg_trace_on = g_mode >= 3 ? g_mode - 2 : 0;   /* 4: tambem pula a compute interna; 5: placement textures committed (fm6/fm6.c); 8/9/10: common/gpuguard.c */
    gg_draw_skip = dbg_int(L"DrawSkip", 0);      /* com D3D12Debug=8: 0 todos, 1 diretos, 2 replayer, 3-5 tipo, 6 IA, 7 hashes, 8 DS/HS/GS/SO */
    {
        char hashes[1024];
        if (dbg_str(L"DrawSkipHashes", hashes, sizeof hashes)) gg_parse_hashes(hashes);
    }
    /* diagnostico (overlays como o RivaTuner): D3D12Hooks=0 deixa o D3D12CreateDevice sem gancho -> nenhum hook do
       proxy no D3D12 (gpuguard, pool de listas, DeferRelease, gravadoras, placement); 2 = so o gpuguard, 3 = so os do
       common/stutter.c; so sem D3D12Debug */
    g_hooks = g_mode ? 1 : dbg_int(L"D3D12Hooks", -1);
    if (g_hooks < 0) {
        g_hooks = debug12_wine() ? 3 : 1;
        if (g_hooks == 3) wlog("debug12: Wine/Proton -> gpuguard off (D3D12Hooks=3 by default; D3D12Hooks=1 turns it on)");
    }
    if (!g_hooks) {
        wlog("debug12: D3D12Hooks=0 -> D3D12CreateDevice not hooked (no proxy hooks in D3D12)");
        return 1;
    }
    /* call qword [rip+0x219083] (-> IAT 0x26f868) */
    if (!bytes_match(call, "ff1583902100", 6)) {
        wlog("debug12: D3D12CreateDevice call at %#x does not match; lock and debug layer NOT enabled",
             RVA_CALL_D3D12_CREATE_DEVICE);
        return 1;
    }
    if (!(ptr = alloc_near(g_base, 8))) {
        wlog("debug12: no memory near azimuth.exe; lock and debug layer NOT enabled");
        return 1;
    }
    *(void **)ptr = (void *)debug12_create_device;
    disp = (int32_t)(ptr - (call + 6));
    VirtualProtect(call + 2, 4, PAGE_EXECUTE_READWRITE, &old);
    memcpy(call + 2, &disp, 4);
    VirtualProtect(call + 2, 4, old, &old);
    wlog("debug12: hook on D3D12CreateDevice call %#x (D3D12Debug=%d)", RVA_CALL_D3D12_CREATE_DEVICE, g_mode);
    if (g_hooks == 2 || g_hooks == 3)
        wlog("debug12: D3D12Hooks=%d -> only the %s hooks in D3D12", g_hooks, g_hooks == 2 ? "gpuguard" : "common/stutter.c");
    return 1;
}
