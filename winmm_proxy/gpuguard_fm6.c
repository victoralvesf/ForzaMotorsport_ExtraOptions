/*
 * FM6: trava contra page fault da GPU (17_fm6.txt "DIAGNOSTICO DO PAGE FAULT"). O FM6 para com a GPU lendo um
 * descritor com bytes 0xdeadbeef (V# da AMD -> endereco 0xbeefdeadb000); cada page fault e um TDR, e uma sequencia
 * deles chegou a derrubar o driver de video (BSOD 0x3b, 2026-10-18). A trava fica no device D3D12 do Azimuth
 * (ligada pelo hook do D3D12CreateDevice em debug12_fm5.c, so no FM6) e age antes de a GPU ver o dado:
 *  - heaps CBV/SRV/UAV: bitmap de slots gravados (Create*View / destino de CopyDescriptors*); copiar um slot de
 *    origem nunca gravado, ou com dois dwords 0xdeadbeef seguidos, loga a pilha e grava um SRV nulo nele antes;
 *  - endereco de GPU envenenado (0xdeadbeef / 0xbeefdead.. / fora de 48 bits) em CBV, root views, vertex/index
 *    buffer e stream out: loga a pilha; CBV vira nulo; root view/buffer vira 0 (nulo) no lugar do endereco.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <d3d12.h>

extern uint8_t *g_base;
extern uint8_t *g_fm6_game;
extern void wlog(const char *fmt, ...);

#define GG_MAX_HEAPS 512
#define GG_MAX_REPORTS 40

typedef struct {
    uintptr_t start;
    uint32_t count, inc;
    volatile LONG *bits;
} gg_heap;

static gg_heap heaps[GG_MAX_HEAPS];
static int nheaps;
static SRWLOCK heap_lock = SRWLOCK_INIT;
static volatile LONG reports;

/* ---- logica pura (testada em tests/t_d3d.c) ---- */

/* endereco de GPU com o veneno 0xdeadbeef: inteiro (fora de 48 bits) ou truncado (0xbeefdead....) */
int gg_va_poisoned(uint64_t va)
{
    uint32_t hi = (uint32_t)(va >> 32);
    if (va >> 48) return 1;
    return (hi & 0xffff) == 0xbeef && ((uint32_t)va >> 20) == 0xdea;
}

/* descritor com dois dwords 0xdeadbeef seguidos (bytes de slot nunca escrito / lixo) */
int gg_desc_poisoned(const uint32_t *d, unsigned dwords)
{
    unsigned i;
    for (i = 0; i + 1 < dwords; i++)
        if (d[i] == 0xdeadbeef && d[i + 1] == 0xdeadbeef) return 1;
    return 0;
}

/* registra um heap (substitui os que se sobrepoem: memoria reaproveitada de um heap solto) */
void gg_heap_add(uintptr_t start, uint32_t count, uint32_t inc)
{
    int i;
    volatile LONG *bits = (volatile LONG *)calloc((count + 31) / 32, sizeof(LONG));
    uintptr_t end = start + (uintptr_t)count * inc;
    if (!bits || !inc) { free((void *)bits); return; }
    AcquireSRWLockExclusive(&heap_lock);
    for (i = 0; i < nheaps; i++) {
        if (heaps[i].start < end && start < heaps[i].start + (uintptr_t)heaps[i].count * heaps[i].inc) {
            free((void *)heaps[i].bits);
            heaps[i] = heaps[--nheaps];
            i--;
        }
    }
    if (nheaps < GG_MAX_HEAPS) heaps[nheaps++] = (gg_heap){ start, count, inc, bits };
    else free((void *)bits);
    ReleaseSRWLockExclusive(&heap_lock);
}

void gg_heap_reset(void)
{
    AcquireSRWLockExclusive(&heap_lock);
    while (nheaps) free((void *)heaps[--nheaps].bits);
    ReleaseSRWLockExclusive(&heap_lock);
}

/* marca n slots a partir de h como gravados (ou consulta: devolve -1 fora de heap conhecido, 0/1 senao) */
static int gg_slots(uintptr_t h, uint32_t n, int mark)
{
    int i, r = -1;
    AcquireSRWLockShared(&heap_lock);
    for (i = 0; i < nheaps; i++) {
        gg_heap *p = &heaps[i];
        uintptr_t idx;
        if (h < p->start || h >= p->start + (uintptr_t)p->count * p->inc) continue;
        idx = (h - p->start) / p->inc;
        r = 1;
        for (; n && idx < p->count; n--, idx++) {
            if (mark) InterlockedOr(&p->bits[idx / 32], (LONG)(1u << (idx % 32)));
            else if (!(p->bits[idx / 32] & (1u << (idx % 32)))) r = 0;
        }
        break;
    }
    ReleaseSRWLockShared(&heap_lock);
    return r;
}

void gg_mark(uintptr_t h, uint32_t n) { gg_slots(h, n, 1); }
int gg_written(uintptr_t h) { return gg_slots(h, 1, 0); }

/* ---- hooks ---- */

static void (STDMETHODCALLTYPE *real_cbv)(ID3D12Device *, const D3D12_CONSTANT_BUFFER_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_srv)(ID3D12Device *, ID3D12Resource *, const D3D12_SHADER_RESOURCE_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_uav)(ID3D12Device *, ID3D12Resource *, ID3D12Resource *, const D3D12_UNORDERED_ACCESS_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE);
static void (STDMETHODCALLTYPE *real_copy)(ID3D12Device *, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, UINT,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE *, const UINT *, D3D12_DESCRIPTOR_HEAP_TYPE);
static void (STDMETHODCALLTYPE *real_copy1)(ID3D12Device *, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
static HRESULT (STDMETHODCALLTYPE *real_heap)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_list)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
                                              ID3D12PipelineState *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_list1)(ID3D12Device4 *, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void **);

typedef void (STDMETHODCALLTYPE *root_va_fn)(ID3D12GraphicsCommandList *, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
static root_va_fn real_ccbv, real_gcbv, real_csrv, real_gsrv, real_cuav, real_guav;
static void (STDMETHODCALLTYPE *real_ib)(ID3D12GraphicsCommandList *, const D3D12_INDEX_BUFFER_VIEW *);
static void (STDMETHODCALLTYPE *real_vb)(ID3D12GraphicsCommandList *, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW *);
static void (STDMETHODCALLTYPE *real_so)(ID3D12GraphicsCommandList *, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *);

static UINT res_inc;

static void label(char *out, void *p)
{
    uint8_t *a = p;
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)label, &self);
    if (g_fm6_game && a >= g_fm6_game && a < g_fm6_game + 0x8000000) wsprintfA(out, " game+%#x", (unsigned)(a - g_fm6_game));
    else if (a >= g_base && a < g_base + 0x1000000) wsprintfA(out, " azimuth+%#x", (unsigned)(a - g_base));
    else if (self && a >= (uint8_t *)self && a < (uint8_t *)self + 0x200000) wsprintfA(out, " winmm+%#x", (unsigned)(a - (uint8_t *)self));
    else out[0] = 0;
}

/* um relatorio por evento (limite GG_MAX_REPORTS): o que foi achado + a pilha nos modulos conhecidos */
static void report(const char *what, uint64_t v)
{
    void *frames[24];
    char buf[640], one[40];
    int len, n, i;
    LONG k = InterlockedIncrement(&reports);
    if (k > GG_MAX_REPORTS) return;
    len = wsprintfA(buf, "gpuguard #%ld tid=%lu: %s %#llx; pilha:", k, GetCurrentThreadId(), what, (unsigned long long)v);
    n = RtlCaptureStackBackTrace(1, 24, frames, NULL);
    for (i = 0; i < n && len < 580; i++) {
        label(one, frames[i]);
        if (one[0]) len += wsprintfA(buf + len, "%s", one);
    }
    wlog("%s", buf);
}

static uint64_t guard_va(const char *what, uint64_t va)
{
    if (!va || !gg_va_poisoned(va)) return va;
    report(what, va);
    return 0;
}

/* slot de origem nunca gravado ou envenenado -> relatorio + SRV nulo nele (copia segura) */
static void check_src(ID3D12Device *dev, uintptr_t h)
{
    int w = gg_written(h);
    if (w < 0) return;
    if (!w || gg_desc_poisoned((const uint32_t *)h, res_inc / 4)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d = { 0 };
        report(w ? "CopyDescriptors: origem com 0xdeadbeef" : "CopyDescriptors: origem nunca gravada", h);
        d.Format = DXGI_FORMAT_R32_UINT;
        d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Buffer.NumElements = 1;
        real_srv(dev, NULL, &d, (D3D12_CPU_DESCRIPTOR_HANDLE){ h });
        gg_mark(h, 1);
    }
}

static void STDMETHODCALLTYPE hook_cbv(ID3D12Device *dev, const D3D12_CONSTANT_BUFFER_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (d && d->BufferLocation && !guard_va("CreateConstantBufferView", d->BufferLocation)) d = NULL;
    real_cbv(dev, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_srv(ID3D12Device *dev, ID3D12Resource *r, const D3D12_SHADER_RESOURCE_VIEW_DESC *d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    real_srv(dev, r, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_uav(ID3D12Device *dev, ID3D12Resource *r, ID3D12Resource *c, const D3D12_UNORDERED_ACCESS_VIEW_DESC *d,
                                       D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    real_uav(dev, r, c, d, h);
    gg_mark(h.ptr, 1);
}

static void STDMETHODCALLTYPE hook_copy1(ID3D12Device *dev, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        UINT i;
        for (i = 0; i < n; i++) check_src(dev, src.ptr + (uintptr_t)i * res_inc);
        gg_mark(dst.ptr, n);
    }
    real_copy1(dev, n, dst, src, type);
}

static void STDMETHODCALLTYPE hook_copy(ID3D12Device *dev, UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE *dst, const UINT *dsz, UINT ns,
                                        const D3D12_CPU_DESCRIPTOR_HANDLE *src, const UINT *ssz, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        UINT i, j;
        for (i = 0; i < ns; i++)
            for (j = 0; j < (ssz ? ssz[i] : 1); j++) check_src(dev, src[i].ptr + (uintptr_t)j * res_inc);
        for (i = 0; i < nd; i++) gg_mark(dst[i].ptr, dsz ? dsz[i] : 1);
    }
    real_copy(dev, nd, dst, dsz, ns, src, ssz, type);
}

static HRESULT STDMETHODCALLTYPE hook_heap(ID3D12Device *dev, const D3D12_DESCRIPTOR_HEAP_DESC *d, REFIID riid, void **out)
{
    HRESULT hr = real_heap(dev, d, riid, out);
    ID3D12DescriptorHeap *heap = NULL;
    if (SUCCEEDED(hr) && out && *out && d && d->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        SUCCEEDED(((IUnknown *)*out)->lpVtbl->QueryInterface(*out, &IID_ID3D12DescriptorHeap, (void **)&heap))) {
        D3D12_CPU_DESCRIPTOR_HANDLE s;
        MEMORY_BASIC_INFORMATION mbi;
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &s);
        heap->lpVtbl->Release(heap);
        /* so heaps cujo handle e um ponteiro legivel (a checagem do conteudo le o descritor) */
        if (VirtualQuery((void *)s.ptr, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOMBINE)) && !(mbi.Protect & PAGE_GUARD)) {
            static volatile LONG shown;
            gg_heap_add(s.ptr, d->NumDescriptors, res_inc);
            if (InterlockedIncrement(&shown) <= 4)
                wlog("gpuguard: heap CBV/SRV/UAV %p x%u (flags %u), 1o descritor %08lx %08lx %08lx %08lx", (void *)s.ptr,
                     d->NumDescriptors, (unsigned)d->Flags, ((DWORD *)s.ptr)[0], ((DWORD *)s.ptr)[1], ((DWORD *)s.ptr)[2],
                     ((DWORD *)s.ptr)[3]);
        }
    }
    return hr;
}

static void STDMETHODCALLTYPE hook_ccbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_ccbv(l, i, guard_va("SetComputeRootConstantBufferView", va)); }
static void STDMETHODCALLTYPE hook_gcbv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_gcbv(l, i, guard_va("SetGraphicsRootConstantBufferView", va)); }
static void STDMETHODCALLTYPE hook_csrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_csrv(l, i, guard_va("SetComputeRootShaderResourceView", va)); }
static void STDMETHODCALLTYPE hook_gsrv(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_gsrv(l, i, guard_va("SetGraphicsRootShaderResourceView", va)); }
static void STDMETHODCALLTYPE hook_cuav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_cuav(l, i, guard_va("SetComputeRootUnorderedAccessView", va)); }
static void STDMETHODCALLTYPE hook_guav(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { real_guav(l, i, guard_va("SetGraphicsRootUnorderedAccessView", va)); }

static void STDMETHODCALLTYPE hook_ib(ID3D12GraphicsCommandList *l, const D3D12_INDEX_BUFFER_VIEW *v)
{
    D3D12_INDEX_BUFFER_VIEW c;
    if (v && v->BufferLocation && gg_va_poisoned(v->BufferLocation)) {
        report("IASetIndexBuffer", v->BufferLocation);
        c = *v; c.BufferLocation = 0; c.SizeInBytes = 0; v = &c;
    }
    real_ib(l, v);
}

static void STDMETHODCALLTYPE hook_vb(ID3D12GraphicsCommandList *l, UINT start, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v)
{
    D3D12_VERTEX_BUFFER_VIEW c[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT i;
    for (i = 0; v && i < n && i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        if (!v[i].BufferLocation || !gg_va_poisoned(v[i].BufferLocation)) continue;
        report("IASetVertexBuffers", v[i].BufferLocation);
        if (n > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) n = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        memcpy(c, v, n * sizeof *v);
        for (i = 0; i < n; i++) if (gg_va_poisoned(c[i].BufferLocation)) { c[i].BufferLocation = 0; c[i].SizeInBytes = 0; }
        v = c;
        break;
    }
    real_vb(l, start, n, v);
}

static void STDMETHODCALLTYPE hook_so(ID3D12GraphicsCommandList *l, UINT start, UINT n, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *v)
{
    UINT i;
    for (i = 0; v && i < n; i++)
        if (gg_va_poisoned(v[i].BufferLocation) || gg_va_poisoned(v[i].BufferFilledSizeLocation)) {
            report("SOSetTargets", v[i].BufferLocation);
            v = NULL; n = 0;   /* desliga o stream out em vez de gravar no endereco envenenado */
            break;
        }
    real_so(l, start, n, v);
}

/* troca um slot de vtable (memoria somente leitura da dll) e guarda o original */
static void swap(void **slot, void *hook, void **orig)
{
    DWORD old;
    if (*slot == hook) return;
    *orig = *slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

static void hook_list_vtable(void *obj)
{
    ID3D12GraphicsCommandList *l = NULL;
    static volatile LONG done;
    if (InterlockedCompareExchange(&done, 1, 0)) return;
    if (FAILED(((IUnknown *)obj)->lpVtbl->QueryInterface(obj, &IID_ID3D12GraphicsCommandList, (void **)&l))) { done = 0; return; }
    swap((void **)&l->lpVtbl->SetComputeRootConstantBufferView, hook_ccbv, (void **)&real_ccbv);
    swap((void **)&l->lpVtbl->SetGraphicsRootConstantBufferView, hook_gcbv, (void **)&real_gcbv);
    swap((void **)&l->lpVtbl->SetComputeRootShaderResourceView, hook_csrv, (void **)&real_csrv);
    swap((void **)&l->lpVtbl->SetGraphicsRootShaderResourceView, hook_gsrv, (void **)&real_gsrv);
    swap((void **)&l->lpVtbl->SetComputeRootUnorderedAccessView, hook_cuav, (void **)&real_cuav);
    swap((void **)&l->lpVtbl->SetGraphicsRootUnorderedAccessView, hook_guav, (void **)&real_guav);
    swap((void **)&l->lpVtbl->IASetIndexBuffer, hook_ib, (void **)&real_ib);
    swap((void **)&l->lpVtbl->IASetVertexBuffers, hook_vb, (void **)&real_vb);
    swap((void **)&l->lpVtbl->SOSetTargets, hook_so, (void **)&real_so);
    l->lpVtbl->Release(l);
    wlog("gpuguard: vtable da command list trocada (root views, IA, SO)");
}

static HRESULT STDMETHODCALLTYPE hook_list(ID3D12Device *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *a,
                                           ID3D12PipelineState *ps, REFIID riid, void **out)
{
    HRESULT hr = real_list(dev, mask, type, a, ps, riid, out);
    if (SUCCEEDED(hr) && out && *out && type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_list1(ID3D12Device4 *dev, UINT mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS f,
                                            REFIID riid, void **out)
{
    HRESULT hr = real_list1(dev, mask, type, f, riid, out);
    if (SUCCEEDED(hr) && out && *out && type != D3D12_COMMAND_LIST_TYPE_COPY) hook_list_vtable(*out);
    return hr;
}

/* chamado pelo hook do D3D12CreateDevice (debug12_fm5.c) no FM6 */
void gpuguard_install(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device4 *dev4 = NULL;
    static int done;
    if (done || FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) return;
    done = 1;
    res_inc = dev->lpVtbl->GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    swap((void **)&dev->lpVtbl->CreateDescriptorHeap, hook_heap, (void **)&real_heap);
    swap((void **)&dev->lpVtbl->CreateConstantBufferView, hook_cbv, (void **)&real_cbv);
    swap((void **)&dev->lpVtbl->CreateShaderResourceView, hook_srv, (void **)&real_srv);
    swap((void **)&dev->lpVtbl->CreateUnorderedAccessView, hook_uav, (void **)&real_uav);
    swap((void **)&dev->lpVtbl->CopyDescriptors, hook_copy, (void **)&real_copy);
    swap((void **)&dev->lpVtbl->CopyDescriptorsSimple, hook_copy1, (void **)&real_copy1);
    swap((void **)&dev->lpVtbl->CreateCommandList, hook_list, (void **)&real_list);
    dev->lpVtbl->Release(dev);
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device4, (void **)&dev4))) {
        swap((void **)&dev4->lpVtbl->CreateCommandList1, hook_list1, (void **)&real_list1);
        dev4->lpVtbl->Release(dev4);
    }
    wlog("gpuguard: ligada no device D3D12 (descritor CBV/SRV/UAV de %u bytes)", res_inc);
}
