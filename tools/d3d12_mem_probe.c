/* Mede quanto cada command allocator / command list do D3D12 custa de memoria privada do processo neste driver.
 * Fases: N allocators vazios; N listas (um allocator so, cada lista gravada e fechada); N pares allocator+lista
 * com K comandos gravados e executados (o padrao do Azimuth: uma lista por allocator).
 * Build: gcc -O2 -o tools/d3d12_mem_probe.exe tools/d3d12_mem_probe.c -ld3d12 -ldxgi -lpsapi
 * Uso: tools/d3d12_mem_probe.exe [N=1000] [K=200] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>

static double priv_mib(void)
{
    PROCESS_MEMORY_COUNTERS_EX pm = { sizeof pm };
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof pm);
    return pm.PrivateUsage / 1048576.0;
}

static ID3D12Device *dev;
static ID3D12CommandQueue *q;
static ID3D12Fence *fence;
static UINT64 fv;
static ID3D12Resource *buf_a, *buf_b;

static void wait_gpu(void)
{
    HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL);
    ID3D12CommandQueue_Signal(q, fence, ++fv);
    ID3D12Fence_SetEventOnCompletion(fence, fv, e);
    WaitForSingleObject(e, INFINITE);
    CloseHandle(e);
}

static ID3D12Resource *make_buffer(UINT64 size)
{
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 },
                               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource *r = NULL;
    ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL,
                                         &IID_ID3D12Resource, (void **)&r);
    return r;
}

static void record(ID3D12GraphicsCommandList *l, int k)
{
    int i;
    for (i = 0; i < k; i++) ID3D12GraphicsCommandList_CopyBufferRegion(l, buf_b, (i % 64) * 256, buf_a, 0, 256);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000, k = argc > 2 ? atoi(argv[2]) : 200, i;
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    ID3D12CommandAllocator **al = calloc(n, sizeof *al);
    ID3D12GraphicsCommandList **ls = calloc(n, sizeof *ls);
    ID3D12CommandAllocator *one;
    double m0, m1;
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev))) return 1;
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    buf_a = make_buffer(1 << 16); buf_b = make_buffer(1 << 16);
    printf("N=%d K=%d inicio %.1f MiB\n", n, k, priv_mib());

    /* 1: allocators vazios */
    m0 = priv_mib();
    for (i = 0; i < n; i++)
        ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&al[i]);
    m1 = priv_mib();
    printf("1) %d allocators vazios: %+.1f MiB (%.1f KiB cada)\n", n, m1 - m0, (m1 - m0) * 1024 / n);

    /* 2: listas num allocator so (cada uma gravada com K comandos e fechada; Reset no mesmo allocator) */
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&one);
    m0 = priv_mib();
    for (i = 0; i < n; i++) {
        ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, one, NULL, &IID_ID3D12GraphicsCommandList,
                                       (void **)&ls[i]);
        record(ls[i], k);
        ID3D12GraphicsCommandList_Close(ls[i]);
    }
    m1 = priv_mib();
    printf("2) %d listas num allocator so (K comandos cada): %+.1f MiB (%.1f KiB por lista)\n", n, m1 - m0,
           (m1 - m0) * 1024 / n);
    ID3D12CommandQueue_ExecuteCommandLists(q, (UINT)(n < 64 ? n : 64), (ID3D12CommandList **)ls);
    wait_gpu();
    for (i = 0; i < n; i++) ID3D12GraphicsCommandList_Release(ls[i]);
    ID3D12CommandAllocator_Reset(one);
    printf("   listas soltas: %.1f MiB\n", priv_mib());

    /* 3: um allocator por lista (padrao do Azimuth), gravadas, executadas, mantidas vivas */
    m0 = priv_mib();
    for (i = 0; i < n; i++) {
        ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, al[i], NULL, &IID_ID3D12GraphicsCommandList,
                                       (void **)&ls[i]);
        record(ls[i], k);
        ID3D12GraphicsCommandList_Close(ls[i]);
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&ls[i]);
    }
    wait_gpu();
    m1 = priv_mib();
    printf("3) %d pares allocator+lista gravados/executados: %+.1f MiB (%.1f KiB por par)\n", n, m1 - m0,
           (m1 - m0) * 1024 / n);
    for (i = 0; i < n; i++) ID3D12GraphicsCommandList_Release(ls[i]);
    m0 = priv_mib();
    printf("   listas soltas (allocators vivos): %.1f MiB\n", m0);
    for (i = 0; i < n; i++) ID3D12CommandAllocator_Reset(al[i]);
    printf("   allocators com Reset: %.1f MiB\n", priv_mib());
    for (i = 0; i < n; i++) ID3D12CommandAllocator_Release(al[i]);
    printf("   allocators soltos: %.1f MiB\n", priv_mib());
    return 0;
}
