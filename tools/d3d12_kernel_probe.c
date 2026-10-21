/* Quanto cada objeto D3D12 custa de memoria do KERNEL (nonpaged/paged do sistema) e privada do processo neste driver:
 * N texturas committed pequenas (o que o FM6 tem ~49k), as mesmas como placed num heap grande, N allocators usados.
 * Build: gcc -O2 -o probe.exe tools/d3d12_kernel_probe.c -ld3d12 -lpsapi
 * Uso: probe.exe [N=20000] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>

static void snap(double *np, double *pg, double *pv)
{
    PERFORMANCE_INFORMATION pi = { sizeof pi };
    PROCESS_MEMORY_COUNTERS_EX pm = { sizeof pm };
    Sleep(200);
    GetPerformanceInfo(&pi, sizeof pi);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof pm);
    *np = (double)pi.KernelNonpaged * pi.PageSize / 1048576.0;
    *pg = (double)pi.KernelPaged * pi.PageSize / 1048576.0;
    *pv = pm.PrivateUsage / 1048576.0;
}

static void show(const char *what, int n, double np0, double pg0, double pv0)
{
    double np, pg, pv;
    snap(&np, &pg, &pv);
    printf("%-44s nonpaged %+8.1f MiB (%5.1f KiB cada)  paged %+8.1f MiB (%5.1f KiB)  privada %+8.1f MiB (%5.1f KiB)\n",
           what, np - np0, (np - np0) * 1024 / n, pg - pg0, (pg - pg0) * 1024 / n, pv - pv0, (pv - pv0) * 1024 / n);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 20000, i;
    ID3D12Device *dev;
    ID3D12Resource **r = calloc(n, sizeof *r);
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 128, 128, 1, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 },
                               D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
    double np0, pg0, pv0;
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev))) return 1;
    td.MipLevels = 8;

    snap(&np0, &pg0, &pv0);
    for (i = 0; i < n; i++)
        if (FAILED(ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                        &IID_ID3D12Resource, (void **)&r[i]))) { printf("falhou em %d\n", i); n = i; break; }
    show("committed BC1 128x128 mips", n, np0, pg0, pv0);
    for (i = 0; i < n; i++) ID3D12Resource_Release(r[i]);

    {   /* as mesmas como reserved (o caminho do Azimuth com tiled tier 4, antes do PASSO 85), sem mapear tiles */
        snap(&np0, &pg0, &pv0);
        for (i = 0; i < n; i++)
            if (FAILED(ID3D12Device_CreateReservedResource(dev, &td, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                                           (void **)&r[i]))) { r[i] = NULL; }
        show("reserved BC1 128x128 mips (sem tiles)", n, np0, pg0, pv0);
        for (i = 0; i < n; i++) if (r[i]) ID3D12Resource_Release(r[i]);
    }
    {   /* as mesmas como placed num heap so (64 KB cada) */
        ID3D12Heap *heap;
        D3D12_HEAP_DESC hd = { (UINT64)n * 65536, { D3D12_HEAP_TYPE_DEFAULT }, 65536, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES };
        snap(&np0, &pg0, &pv0);
        if (SUCCEEDED(ID3D12Device_CreateHeap(dev, &hd, &IID_ID3D12Heap, (void **)&heap))) {
            for (i = 0; i < n; i++)
                ID3D12Device_CreatePlacedResource(dev, heap, (UINT64)i * 65536, &td, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                  &IID_ID3D12Resource, (void **)&r[i]);
            show("placed BC1 128x128 mips (1 heap)", n, np0, pg0, pv0);
            for (i = 0; i < n; i++) if (r[i]) ID3D12Resource_Release(r[i]);
            ID3D12Heap_Release(heap);
        }
    }
    {   /* allocators usados (uma lista gravada e fechada em cada) */
        int na = n < 3000 ? n : 3000;
        ID3D12CommandAllocator **al = calloc(na, sizeof *al);
        ID3D12GraphicsCommandList *l;
        snap(&np0, &pg0, &pv0);
        for (i = 0; i < na; i++) {
            ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&al[i]);
            ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, al[i], NULL, &IID_ID3D12GraphicsCommandList,
                                           (void **)&l);
            ID3D12GraphicsCommandList_Close(l);
            ID3D12GraphicsCommandList_Release(l);
        }
        show("allocators usados", na, np0, pg0, pv0);
    }
    return 0;
}
