/*
 * FM6: command lists GRAVADORAS e uma command list do D3D12 por envio (PASSO 98, opcao B do PASSO 96).
 *
 * O executor do Azimuth abre uma command list (com um allocator do pool dele) para cada pedaco gravado: o fluxo do
 * contexto imediato reproduzido na 'Azimuth queue' e quebrado a cada lista diferida executada (fechar, inserir a
 * lista do diferido, abrir outra), e no FM6 cada bundle aninhado de um juntador vira uma parte (d3d_fm5.c). Para o
 * mesmo trabalho do FH2 (~2500 draws/quadro) sao ~600 listas/quadro em ~17 ExecuteCommandLists (FH2: ~15), ~2000
 * allocators vivos e a rajada que vem antes do DEVICE_RESET do fim da 1a corrida.
 *
 * Aqui o CreateCommandList do Azimuth (0x1400dee90, retorno azimuth+0xdef70) recebe uma lista GRAVADORA do proxy:
 * um objeto com a vtable da ID3D12GraphicsCommandList2 que so copia cada chamada (e os vetores apontados) para um
 * buffer. No ExecuteCommandLists (lp_ecl, stutter_fm6.c, sob a trava da fila) cada sequencia de gravadoras vira UMA
 * command list real: Reset com o PSO inicial da 1a, reproducao de todas em ordem com ClearState(PSO inicial da
 * seguinte) entre elas (= estado de lista recem aberta) e Close. As listas reais e os allocators delas sao do proxy
 * (anel por fila, reusados quando o fence do proxy da fila passa do envio). A gravadora nao tem memoria de GPU:
 * Reset/Close sao baratos, o allocator que o Azimuth passa nao e usado, e reenviar a mesma gravadora antes da GPU
 * terminar e legal (a reproducao e na hora do ECL).
 * debug.ini ListMerge=0/1 (padrao: FM6 ligado).
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <d3d12.h>

extern uint8_t *g_base;
extern void wlog(const char *fmt, ...);
extern int dbg_int(const wchar_t *key, int def);
extern int is_fm6(void);
extern int bytes_match(const uint8_t *p, const char *hex, size_t n);
extern void gg_cl_restart(void *cl, void *pso);

typedef ID3D12GraphicsCommandList2 GCL2;
typedef HRESULT (STDMETHODCALLTYPE *rc_create_fn)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
                                                  ID3D12PipelineState *, REFIID, void **);

/* chamada de CreateCommandList em 0x1400dee90 (call [rax+0x60]; jmp) e o endereco de retorno dela */
#define RC_OPEN_CALL_RVA 0xdef6d
#define RC_OPEN_RET_RVA  0xdef70
#define RC_POOL_MAX      8192            /* gravadoras guardadas para reuso */
#define RC_KEEP_BUF      (64u << 10)     /* buffer maior que isso nao fica com a gravadora guardada */

/* operacao = indice do metodo na vtable da ID3D12GraphicsCommandList2 */
enum {
    RC_ClearState = 11, RC_DrawInstanced, RC_DrawIndexedInstanced, RC_Dispatch, RC_CopyBufferRegion, RC_CopyTextureRegion,
    RC_CopyResource, RC_CopyTiles, RC_ResolveSubresource, RC_IASetPrimitiveTopology, RC_RSSetViewports, RC_RSSetScissorRects,
    RC_OMSetBlendFactor, RC_OMSetStencilRef, RC_SetPipelineState, RC_ResourceBarrier, RC_ExecuteBundle, RC_SetDescriptorHeaps,
    RC_SetComputeRootSignature, RC_SetGraphicsRootSignature, RC_SetComputeRootDescriptorTable,
    RC_SetGraphicsRootDescriptorTable, RC_SetComputeRoot32BitConstant, RC_SetGraphicsRoot32BitConstant,
    RC_SetComputeRoot32BitConstants, RC_SetGraphicsRoot32BitConstants, RC_SetComputeRootConstantBufferView,
    RC_SetGraphicsRootConstantBufferView, RC_SetComputeRootShaderResourceView, RC_SetGraphicsRootShaderResourceView,
    RC_SetComputeRootUnorderedAccessView, RC_SetGraphicsRootUnorderedAccessView, RC_IASetIndexBuffer, RC_IASetVertexBuffers,
    RC_SOSetTargets, RC_OMSetRenderTargets, RC_ClearDepthStencilView, RC_ClearRenderTargetView,
    RC_ClearUnorderedAccessViewUint, RC_ClearUnorderedAccessViewFloat, RC_DiscardResource, RC_BeginQuery, RC_EndQuery,
    RC_ResolveQueryData, RC_SetPredication, RC_SetMarker, RC_BeginEvent, RC_EndEvent, RC_ExecuteIndirect,
    RC_AtomicCopyBufferUINT, RC_AtomicCopyBufferUINT64, RC_OMSetDepthBounds, RC_SetSamplePositions,
    RC_ResolveSubresourceRegion, RC_SetViewInstanceMask, RC_WriteBufferImmediate
};
_Static_assert(RC_ExecuteIndirect * sizeof(void *) == offsetof(ID3D12GraphicsCommandList2Vtbl, ExecuteIndirect), "vtable");
_Static_assert(RC_WriteBufferImmediate * sizeof(void *) == offsetof(ID3D12GraphicsCommandList2Vtbl, WriteBufferImmediate), "vtable");

typedef struct { uint16_t op, pad; uint32_t size; } rc_hdr;   /* size = cabecalho + argumentos, multiplo de 8 */

typedef struct rc_priv { GUID g; UINT size; IUnknown *u; struct rc_priv *next; } rc_priv;   /* + size bytes */
typedef struct rc_list {
    const ID3D12GraphicsCommandList2Vtbl *vt;
    volatile LONG ref;
    ID3D12Device *dev;              /* sem referencia: o device vive a sessao toda */
    ID3D12PipelineState *pso0;      /* PSO inicial (criacao/Reset) */
    uint8_t *buf;
    uint32_t len, cap, ncmd;
    int open, oom, pred;            /* pred: SetPredication gravado (desligado na fronteira seguinte) */
    rc_priv *priv;                  /* Set/GetPrivateData */
    struct rc_list *next;           /* pool */
} rc_list;

int rc_on;
static rc_create_fn rc_inner_create;  /* CreateCommandList por dentro do pool/gravadoras (gpuguard -> D3D12) */
static const ID3D12GraphicsCommandList2Vtbl rc_vt;
static SRWLOCK rc_lock = SRWLOCK_INIT;
static rc_list *rc_free;
static LONG rc_nfree;
static volatile LONG rc_live, rc_opens, rc_execs, rc_reals, rc_cmds, rc_slots_total, rc_oom_logged;
static volatile LONG64 rc_ticks;
static LARGE_INTEGER rc_freq;

static int rc_is(const void *p) { return p && *(const void *const *)p == (const void *)&rc_vt; }

#define RC_ALIGN(n) (((n) + 7) & ~(size_t)7)

/* reserva uma operacao com n bytes de argumentos (zerados); NULL sem memoria (a gravadora fica marcada) */
static void *rc_put(rc_list *l, int op, size_t n)
{
    size_t sz = sizeof(rc_hdr) + RC_ALIGN(n);
    rc_hdr *h;
    if (l->oom) return NULL;
    if (l->len + sz > l->cap) {
        size_t cap = l->cap ? l->cap : 4096;
        uint8_t *b;
        while (cap < l->len + sz) cap *= 2;
        if (cap > 0x40000000 || !(b = realloc(l->buf, cap))) {
            l->oom = 1;
            if (!InterlockedExchange(&rc_oom_logged, 1)) wlog("d3d: command list recorder out of memory (%u bytes recorded)", l->len);
            return NULL;
        }
        l->buf = b;
        l->cap = (uint32_t)cap;
    }
    h = (rc_hdr *)(l->buf + l->len);
    h->op = (uint16_t)op;
    h->pad = 0;
    h->size = (uint32_t)sz;
    memset(h + 1, 0, sz - sizeof *h);
    l->len += (uint32_t)sz;
    l->ncmd++;
    return h + 1;
}

/* ---- argumentos gravados (vetores logo depois, alinhados a 8) ---- */
typedef struct { void *p; } rc_a_ptr;
typedef struct { UINT a, b, c, d; } rc_a_draw;
typedef struct { UINT a, b, c; INT d; UINT e; } rc_a_drawidx;
typedef struct { UINT x, y, z; } rc_a_dispatch;
typedef struct { ID3D12Resource *dst; UINT64 doff; ID3D12Resource *src; UINT64 soff, n; } rc_a_copybuf;
typedef struct { D3D12_TEXTURE_COPY_LOCATION dst, src; D3D12_BOX box; UINT x, y, z, has_box; } rc_a_copytex;
typedef struct { ID3D12Resource *dst, *src; } rc_a_copyres;
typedef struct { ID3D12Resource *res, *buf; D3D12_TILED_RESOURCE_COORDINATE co; D3D12_TILE_REGION_SIZE sz; UINT64 off;
                 D3D12_TILE_COPY_FLAGS flags; } rc_a_copytiles;
typedef struct { ID3D12Resource *dst, *src; UINT ds, ss; DXGI_FORMAT fmt; } rc_a_resolve;
typedef struct { UINT v; } rc_a_u32;
typedef struct { UINT n, has; } rc_a_arr;                   /* + n elementos se has */
typedef struct { FLOAT f[4]; UINT has; } rc_a_blend;
typedef struct { UINT i; D3D12_GPU_DESCRIPTOR_HANDLE h; } rc_a_table;
typedef struct { UINT i, v, off; } rc_a_const1;
typedef struct { UINT i, n, off; } rc_a_consts;           /* + n UINTs */
typedef struct { UINT i; D3D12_GPU_VIRTUAL_ADDRESS va; } rc_a_rootva;
typedef struct { D3D12_INDEX_BUFFER_VIEW v; UINT has; } rc_a_ib;
typedef struct { UINT start, n, has; } rc_a_views;         /* + n views se has */
typedef struct { D3D12_CPU_DESCRIPTOR_HANDLE ds; UINT n, single, has_rt, has_ds, nrt; } rc_a_omrt;   /* + nrt handles */
typedef struct { D3D12_CPU_DESCRIPTOR_HANDLE h; D3D12_CLEAR_FLAGS flags; FLOAT depth; UINT stencil, n; } rc_a_cleards;
typedef struct { D3D12_CPU_DESCRIPTOR_HANDLE h; FLOAT c[4]; UINT n; } rc_a_clearrt;
typedef struct { D3D12_GPU_DESCRIPTOR_HANDLE g; D3D12_CPU_DESCRIPTOR_HANDLE c; ID3D12Resource *res; UINT v[4]; UINT n; } rc_a_clearuav;
typedef struct { ID3D12Resource *res; D3D12_DISCARD_REGION r; UINT has; } rc_a_discard;   /* + r.NumRects rects */
typedef struct { ID3D12QueryHeap *heap; D3D12_QUERY_TYPE type; UINT idx; } rc_a_query;
typedef struct { ID3D12QueryHeap *heap; ID3D12Resource *dst; UINT64 off; D3D12_QUERY_TYPE type; UINT start, n; } rc_a_resolveq;
typedef struct { ID3D12Resource *buf; UINT64 off; D3D12_PREDICATION_OP op; } rc_a_pred;
typedef struct { UINT meta, size, has; } rc_a_marker;       /* + size bytes */
typedef struct { ID3D12CommandSignature *sig; ID3D12Resource *args, *cnt; UINT64 aoff, coff; UINT max; } rc_a_indirect;
typedef struct { ID3D12Resource *dst, *src; UINT64 doff, soff; UINT n; } rc_a_atomic;   /* + n recursos + n faixas */
typedef struct { FLOAT mn, mx; } rc_a_bounds;
typedef struct { UINT s, p, has; } rc_a_samplepos;          /* + s*p posicoes */
typedef struct { ID3D12Resource *dst, *src; D3D12_RECT r; UINT ds, x, y, ss, has; DXGI_FORMAT fmt; D3D12_RESOLVE_MODE mode; } rc_a_resolvereg;
typedef struct { UINT n, has_modes; } rc_a_wbi;             /* + n parametros + n modos */

#define L(This) ((rc_list *)(This))
#define AFTER(a) ((uint8_t *)(a) + RC_ALIGN(sizeof *(a)))

/* ---- IUnknown / ID3D12Object / ID3D12DeviceChild / ID3D12CommandList ---- */
static HRESULT STDMETHODCALLTYPE rc_QueryInterface(GCL2 *This, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) || IsEqualGUID(riid, &IID_ID3D12DeviceChild) ||
        IsEqualGUID(riid, &IID_ID3D12CommandList) || IsEqualGUID(riid, &IID_ID3D12GraphicsCommandList) ||
        IsEqualGUID(riid, &IID_ID3D12GraphicsCommandList1) || IsEqualGUID(riid, &IID_ID3D12GraphicsCommandList2)) {
        InterlockedIncrement(&L(This)->ref);
        *out = This;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE rc_AddRef(GCL2 *This) { return (ULONG)InterlockedIncrement(&L(This)->ref); }

static void rc_priv_drop(rc_priv *p)
{
    if (p->u) p->u->lpVtbl->Release(p->u);
    free(p);
}

static ULONG STDMETHODCALLTYPE rc_Release(GCL2 *This)
{
    rc_list *l = L(This);
    LONG r = InterlockedDecrement(&l->ref);
    if (r) return (ULONG)r;
    InterlockedDecrement(&rc_live);
    while (l->priv) { rc_priv *p = l->priv; l->priv = p->next; rc_priv_drop(p); }
    if (l->cap > RC_KEEP_BUF) { free(l->buf); l->buf = NULL; l->cap = 0; }
    l->len = l->ncmd = 0;
    l->oom = l->open = l->pred = 0;
    AcquireSRWLockExclusive(&rc_lock);
    if (rc_nfree < RC_POOL_MAX) {
        l->next = rc_free;
        rc_free = l;
        rc_nfree++;
        l = NULL;
    }
    ReleaseSRWLockExclusive(&rc_lock);
    if (l) { free(l->buf); free(l); }
    return 0;
}

/* private data como no D3D12: por GUID; dado NULL apaga; interface guardada com referencia */
static rc_priv **rc_priv_find(rc_list *l, REFGUID g)
{
    rc_priv **pp = &l->priv;
    while (*pp && !IsEqualGUID(&(*pp)->g, g)) pp = &(*pp)->next;
    return pp;
}
static HRESULT rc_priv_set(rc_list *l, REFGUID g, UINT n, const void *d, IUnknown *u)
{
    rc_priv **pp = rc_priv_find(l, g), *p = *pp;
    if (p) { *pp = p->next; rc_priv_drop(p); }
    if (!d && !u) return S_OK;
    if (!(p = malloc(sizeof *p + n))) return E_OUTOFMEMORY;
    p->g = *g; p->size = n; p->u = u;
    if (u) u->lpVtbl->AddRef(u);
    else memcpy(p + 1, d, n);
    p->next = l->priv;
    l->priv = p;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rc_GetPrivateData(GCL2 *This, REFGUID g, UINT *n, void *d)
{
    rc_priv *p = *rc_priv_find(L(This), g);
    if (!n) return E_INVALIDARG;
    if (!p) { *n = 0; return DXGI_ERROR_NOT_FOUND; }
    if (p->u) {
        if (d && *n < sizeof(IUnknown *)) { *n = sizeof(IUnknown *); return DXGI_ERROR_MORE_DATA; }
        *n = sizeof(IUnknown *);
        if (d) { p->u->lpVtbl->AddRef(p->u); *(IUnknown **)d = p->u; }
        return S_OK;
    }
    if (d && *n < p->size) { *n = p->size; return DXGI_ERROR_MORE_DATA; }
    *n = p->size;
    if (d) memcpy(d, p + 1, p->size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rc_SetPrivateData(GCL2 *This, REFGUID g, UINT n, const void *d) { return rc_priv_set(L(This), g, n, d, NULL); }
static HRESULT STDMETHODCALLTYPE rc_SetPrivateDataInterface(GCL2 *This, REFGUID g, const IUnknown *u) { return rc_priv_set(L(This), g, 0, NULL, (IUnknown *)u); }
static HRESULT STDMETHODCALLTYPE rc_SetName(GCL2 *This, const WCHAR *n) { (void)This; (void)n; return S_OK; }

static HRESULT STDMETHODCALLTYPE rc_GetDevice(GCL2 *This, REFIID riid, void **out)
{
    ID3D12Device *d = L(This)->dev;
    if (!out) return E_POINTER;
    if (!d) { *out = NULL; return E_FAIL; }
    return d->lpVtbl->QueryInterface(d, riid, out);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE rc_GetType(GCL2 *This) { (void)This; return D3D12_COMMAND_LIST_TYPE_DIRECT; }

static HRESULT STDMETHODCALLTYPE rc_Close(GCL2 *This)
{
    rc_list *l = L(This);
    if (!l->open) return E_FAIL;
    l->open = 0;
    return S_OK;
}

/* o allocator nao e usado (a lista real e o allocator dela sao do proxy); aceita tambem a gravadora ainda aberta */
static HRESULT STDMETHODCALLTYPE rc_Reset(GCL2 *This, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    rc_list *l = L(This);
    (void)a;
    l->len = l->ncmd = 0;
    l->oom = l->pred = 0;
    l->pso0 = ps;
    l->open = 1;
    InterlockedIncrement(&rc_opens);
    return S_OK;
}

/* ---- gravacao ---- */
static void STDMETHODCALLTYPE rc_ClearState(GCL2 *This, ID3D12PipelineState *ps)
{ rc_a_ptr *a = rc_put(L(This), RC_ClearState, sizeof *a); if (a) a->p = ps; }
static void STDMETHODCALLTYPE rc_DrawInstanced(GCL2 *This, UINT v, UINT i, UINT sv, UINT si)
{ rc_a_draw *a = rc_put(L(This), RC_DrawInstanced, sizeof *a); if (a) { a->a = v; a->b = i; a->c = sv; a->d = si; } }
static void STDMETHODCALLTYPE rc_DrawIndexedInstanced(GCL2 *This, UINT n, UINT i, UINT si, INT bv, UINT sinst)
{ rc_a_drawidx *a = rc_put(L(This), RC_DrawIndexedInstanced, sizeof *a); if (a) { a->a = n; a->b = i; a->c = si; a->d = bv; a->e = sinst; } }
static void STDMETHODCALLTYPE rc_Dispatch(GCL2 *This, UINT x, UINT y, UINT z)
{ rc_a_dispatch *a = rc_put(L(This), RC_Dispatch, sizeof *a); if (a) { a->x = x; a->y = y; a->z = z; } }
static void STDMETHODCALLTYPE rc_CopyBufferRegion(GCL2 *This, ID3D12Resource *d, UINT64 doff, ID3D12Resource *s, UINT64 soff, UINT64 n)
{ rc_a_copybuf *a = rc_put(L(This), RC_CopyBufferRegion, sizeof *a); if (a) { a->dst = d; a->doff = doff; a->src = s; a->soff = soff; a->n = n; } }
static void STDMETHODCALLTYPE rc_CopyTextureRegion(GCL2 *This, const D3D12_TEXTURE_COPY_LOCATION *d, UINT x, UINT y, UINT z,
                                                   const D3D12_TEXTURE_COPY_LOCATION *s, const D3D12_BOX *box)
{
    rc_a_copytex *a = rc_put(L(This), RC_CopyTextureRegion, sizeof *a);
    if (!a) return;
    if (d) a->dst = *d;
    if (s) a->src = *s;
    if (box) { a->box = *box; a->has_box = 1; }
    a->x = x; a->y = y; a->z = z;
}
static void STDMETHODCALLTYPE rc_CopyResource(GCL2 *This, ID3D12Resource *d, ID3D12Resource *s)
{ rc_a_copyres *a = rc_put(L(This), RC_CopyResource, sizeof *a); if (a) { a->dst = d; a->src = s; } }
static void STDMETHODCALLTYPE rc_CopyTiles(GCL2 *This, ID3D12Resource *res, const D3D12_TILED_RESOURCE_COORDINATE *co,
                                           const D3D12_TILE_REGION_SIZE *sz, ID3D12Resource *buf, UINT64 off, D3D12_TILE_COPY_FLAGS f)
{
    rc_a_copytiles *a = rc_put(L(This), RC_CopyTiles, sizeof *a);
    if (!a) return;
    a->res = res; a->buf = buf; a->off = off; a->flags = f;
    if (co) a->co = *co;
    if (sz) a->sz = *sz;
}
static void STDMETHODCALLTYPE rc_ResolveSubresource(GCL2 *This, ID3D12Resource *d, UINT ds, ID3D12Resource *s, UINT ss, DXGI_FORMAT f)
{ rc_a_resolve *a = rc_put(L(This), RC_ResolveSubresource, sizeof *a); if (a) { a->dst = d; a->ds = ds; a->src = s; a->ss = ss; a->fmt = f; } }
static void STDMETHODCALLTYPE rc_IASetPrimitiveTopology(GCL2 *This, D3D12_PRIMITIVE_TOPOLOGY t)
{ rc_a_u32 *a = rc_put(L(This), RC_IASetPrimitiveTopology, sizeof *a); if (a) a->v = (UINT)t; }

/* n elementos de `size` bytes (copiados se p) */
static void rc_put_arr(GCL2 *This, int op, UINT n, const void *p, size_t size)
{
    rc_a_arr *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + (p ? n * size : 0));
    if (!a) return;
    a->n = n;
    if (p) { a->has = 1; memcpy(AFTER(a), p, n * size); }
}
static void STDMETHODCALLTYPE rc_RSSetViewports(GCL2 *This, UINT n, const D3D12_VIEWPORT *v) { rc_put_arr(This, RC_RSSetViewports, n, v, sizeof *v); }
static void STDMETHODCALLTYPE rc_RSSetScissorRects(GCL2 *This, UINT n, const D3D12_RECT *r) { rc_put_arr(This, RC_RSSetScissorRects, n, r, sizeof *r); }
static void STDMETHODCALLTYPE rc_OMSetBlendFactor(GCL2 *This, const FLOAT f[4])
{ rc_a_blend *a = rc_put(L(This), RC_OMSetBlendFactor, sizeof *a); if (a && f) { memcpy(a->f, f, sizeof a->f); a->has = 1; } }
static void STDMETHODCALLTYPE rc_OMSetStencilRef(GCL2 *This, UINT v) { rc_a_u32 *a = rc_put(L(This), RC_OMSetStencilRef, sizeof *a); if (a) a->v = v; }
static void STDMETHODCALLTYPE rc_SetPipelineState(GCL2 *This, ID3D12PipelineState *p) { rc_a_ptr *a = rc_put(L(This), RC_SetPipelineState, sizeof *a); if (a) a->p = p; }
static void STDMETHODCALLTYPE rc_ResourceBarrier(GCL2 *This, UINT n, const D3D12_RESOURCE_BARRIER *b) { rc_put_arr(This, RC_ResourceBarrier, n, b, sizeof *b); }
static void STDMETHODCALLTYPE rc_ExecuteBundle(GCL2 *This, ID3D12GraphicsCommandList *b) { rc_a_ptr *a = rc_put(L(This), RC_ExecuteBundle, sizeof *a); if (a) a->p = b; }
static void STDMETHODCALLTYPE rc_SetDescriptorHeaps(GCL2 *This, UINT n, ID3D12DescriptorHeap *const *h) { rc_put_arr(This, RC_SetDescriptorHeaps, n, h, sizeof *h); }
static void STDMETHODCALLTYPE rc_SetComputeRootSignature(GCL2 *This, ID3D12RootSignature *r) { rc_a_ptr *a = rc_put(L(This), RC_SetComputeRootSignature, sizeof *a); if (a) a->p = r; }
static void STDMETHODCALLTYPE rc_SetGraphicsRootSignature(GCL2 *This, ID3D12RootSignature *r) { rc_a_ptr *a = rc_put(L(This), RC_SetGraphicsRootSignature, sizeof *a); if (a) a->p = r; }
static void rc_put_table(GCL2 *This, int op, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) { rc_a_table *a = rc_put(L(This), op, sizeof *a); if (a) { a->i = i; a->h = h; } }
static void STDMETHODCALLTYPE rc_SetComputeRootDescriptorTable(GCL2 *This, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) { rc_put_table(This, RC_SetComputeRootDescriptorTable, i, h); }
static void STDMETHODCALLTYPE rc_SetGraphicsRootDescriptorTable(GCL2 *This, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) { rc_put_table(This, RC_SetGraphicsRootDescriptorTable, i, h); }
static void rc_put_const1(GCL2 *This, int op, UINT i, UINT v, UINT off) { rc_a_const1 *a = rc_put(L(This), op, sizeof *a); if (a) { a->i = i; a->v = v; a->off = off; } }
static void STDMETHODCALLTYPE rc_SetComputeRoot32BitConstant(GCL2 *This, UINT i, UINT v, UINT off) { rc_put_const1(This, RC_SetComputeRoot32BitConstant, i, v, off); }
static void STDMETHODCALLTYPE rc_SetGraphicsRoot32BitConstant(GCL2 *This, UINT i, UINT v, UINT off) { rc_put_const1(This, RC_SetGraphicsRoot32BitConstant, i, v, off); }
static void rc_put_consts(GCL2 *This, int op, UINT i, UINT n, const void *d, UINT off)
{
    rc_a_consts *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + (d ? n * 4 : 0));
    if (!a) return;
    a->i = i; a->off = off;
    if (d) { a->n = n; memcpy(AFTER(a), d, n * 4); }
}
static void STDMETHODCALLTYPE rc_SetComputeRoot32BitConstants(GCL2 *This, UINT i, UINT n, const void *d, UINT off) { rc_put_consts(This, RC_SetComputeRoot32BitConstants, i, n, d, off); }
static void STDMETHODCALLTYPE rc_SetGraphicsRoot32BitConstants(GCL2 *This, UINT i, UINT n, const void *d, UINT off) { rc_put_consts(This, RC_SetGraphicsRoot32BitConstants, i, n, d, off); }
static void rc_put_va(GCL2 *This, int op, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_a_rootva *a = rc_put(L(This), op, sizeof *a); if (a) { a->i = i; a->va = va; } }
static void STDMETHODCALLTYPE rc_SetComputeRootConstantBufferView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetComputeRootConstantBufferView, i, va); }
static void STDMETHODCALLTYPE rc_SetGraphicsRootConstantBufferView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetGraphicsRootConstantBufferView, i, va); }
static void STDMETHODCALLTYPE rc_SetComputeRootShaderResourceView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetComputeRootShaderResourceView, i, va); }
static void STDMETHODCALLTYPE rc_SetGraphicsRootShaderResourceView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetGraphicsRootShaderResourceView, i, va); }
static void STDMETHODCALLTYPE rc_SetComputeRootUnorderedAccessView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetComputeRootUnorderedAccessView, i, va); }
static void STDMETHODCALLTYPE rc_SetGraphicsRootUnorderedAccessView(GCL2 *This, UINT i, D3D12_GPU_VIRTUAL_ADDRESS va) { rc_put_va(This, RC_SetGraphicsRootUnorderedAccessView, i, va); }
static void STDMETHODCALLTYPE rc_IASetIndexBuffer(GCL2 *This, const D3D12_INDEX_BUFFER_VIEW *v)
{ rc_a_ib *a = rc_put(L(This), RC_IASetIndexBuffer, sizeof *a); if (a && v) { a->v = *v; a->has = 1; } }
static void rc_put_views(GCL2 *This, int op, UINT start, UINT n, const void *v, size_t size)
{
    rc_a_views *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + (v ? n * size : 0));
    if (!a) return;
    a->start = start; a->n = n;
    if (v) { a->has = 1; memcpy(AFTER(a), v, n * size); }
}
static void STDMETHODCALLTYPE rc_IASetVertexBuffers(GCL2 *This, UINT s, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v) { rc_put_views(This, RC_IASetVertexBuffers, s, n, v, sizeof *v); }
static void STDMETHODCALLTYPE rc_SOSetTargets(GCL2 *This, UINT s, UINT n, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *v) { rc_put_views(This, RC_SOSetTargets, s, n, v, sizeof *v); }

/* single = TRUE: rts aponta para UM handle (inicio de uma faixa de n descritores) */
static void STDMETHODCALLTYPE rc_OMSetRenderTargets(GCL2 *This, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE *rts, WINBOOL single,
                                                    const D3D12_CPU_DESCRIPTOR_HANDLE *ds)
{
    UINT nrt = rts ? (single ? (n ? 1 : 0) : n) : 0;
    rc_a_omrt *a = rc_put(L(This), RC_OMSetRenderTargets, RC_ALIGN(sizeof *a) + nrt * sizeof *rts);
    if (!a) return;
    a->n = n; a->single = single ? 1 : 0; a->nrt = nrt;
    if (rts) { a->has_rt = 1; memcpy(AFTER(a), rts, nrt * sizeof *rts); }
    if (ds) { a->has_ds = 1; a->ds = *ds; }
}
static void STDMETHODCALLTYPE rc_ClearDepthStencilView(GCL2 *This, D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CLEAR_FLAGS f, FLOAT d, UINT8 s,
                                                       UINT n, const D3D12_RECT *r)
{
    rc_a_cleards *a = rc_put(L(This), RC_ClearDepthStencilView, RC_ALIGN(sizeof *a) + (r ? n * sizeof *r : 0));
    if (!a) return;
    a->h = h; a->flags = f; a->depth = d; a->stencil = s;
    if (r) { a->n = n; memcpy(AFTER(a), r, n * sizeof *r); }
}
static void STDMETHODCALLTYPE rc_ClearRenderTargetView(GCL2 *This, D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT c[4], UINT n, const D3D12_RECT *r)
{
    rc_a_clearrt *a = rc_put(L(This), RC_ClearRenderTargetView, RC_ALIGN(sizeof *a) + (r ? n * sizeof *r : 0));
    if (!a) return;
    a->h = h;
    if (c) memcpy(a->c, c, sizeof a->c);
    if (r) { a->n = n; memcpy(AFTER(a), r, n * sizeof *r); }
}
static void rc_put_clearuav(GCL2 *This, int op, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource *res,
                            const void *v, UINT n, const D3D12_RECT *r)
{
    rc_a_clearuav *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + (r ? n * sizeof *r : 0));
    if (!a) return;
    a->g = g; a->c = c; a->res = res;
    if (v) memcpy(a->v, v, sizeof a->v);
    if (r) { a->n = n; memcpy(AFTER(a), r, n * sizeof *r); }
}
static void STDMETHODCALLTYPE rc_ClearUnorderedAccessViewUint(GCL2 *This, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c,
                                                              ID3D12Resource *res, const UINT v[4], UINT n, const D3D12_RECT *r)
{ rc_put_clearuav(This, RC_ClearUnorderedAccessViewUint, g, c, res, v, n, r); }
static void STDMETHODCALLTYPE rc_ClearUnorderedAccessViewFloat(GCL2 *This, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c,
                                                               ID3D12Resource *res, const float v[4], UINT n, const D3D12_RECT *r)
{ rc_put_clearuav(This, RC_ClearUnorderedAccessViewFloat, g, c, res, v, n, r); }
static void STDMETHODCALLTYPE rc_DiscardResource(GCL2 *This, ID3D12Resource *res, const D3D12_DISCARD_REGION *reg)
{
    UINT nr = reg && reg->pRects ? reg->NumRects : 0;
    rc_a_discard *a = rc_put(L(This), RC_DiscardResource, RC_ALIGN(sizeof *a) + nr * sizeof(D3D12_RECT));
    if (!a) return;
    a->res = res;
    if (reg) {
        a->has = 1;
        a->r = *reg;
        a->r.pRects = NULL;
        a->r.NumRects = nr;
        memcpy(AFTER(a), reg->pRects, nr * sizeof(D3D12_RECT));
    }
}
static void rc_put_query(GCL2 *This, int op, ID3D12QueryHeap *h, D3D12_QUERY_TYPE t, UINT i)
{ rc_a_query *a = rc_put(L(This), op, sizeof *a); if (a) { a->heap = h; a->type = t; a->idx = i; } }
static void STDMETHODCALLTYPE rc_BeginQuery(GCL2 *This, ID3D12QueryHeap *h, D3D12_QUERY_TYPE t, UINT i) { rc_put_query(This, RC_BeginQuery, h, t, i); }
static void STDMETHODCALLTYPE rc_EndQuery(GCL2 *This, ID3D12QueryHeap *h, D3D12_QUERY_TYPE t, UINT i) { rc_put_query(This, RC_EndQuery, h, t, i); }
static void STDMETHODCALLTYPE rc_ResolveQueryData(GCL2 *This, ID3D12QueryHeap *h, D3D12_QUERY_TYPE t, UINT s, UINT n, ID3D12Resource *d, UINT64 off)
{ rc_a_resolveq *a = rc_put(L(This), RC_ResolveQueryData, sizeof *a); if (a) { a->heap = h; a->type = t; a->start = s; a->n = n; a->dst = d; a->off = off; } }
static void STDMETHODCALLTYPE rc_SetPredication(GCL2 *This, ID3D12Resource *b, UINT64 off, D3D12_PREDICATION_OP op)
{ rc_a_pred *a = rc_put(L(This), RC_SetPredication, sizeof *a); L(This)->pred = 1; if (a) { a->buf = b; a->off = off; a->op = op; } }
static void rc_put_marker(GCL2 *This, int op, UINT meta, const void *d, UINT size)
{
    rc_a_marker *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + (d ? size : 0));
    if (!a) return;
    a->meta = meta;
    if (d) { a->has = 1; a->size = size; memcpy(AFTER(a), d, size); }
}
static void STDMETHODCALLTYPE rc_SetMarker(GCL2 *This, UINT m, const void *d, UINT n) { rc_put_marker(This, RC_SetMarker, m, d, n); }
static void STDMETHODCALLTYPE rc_BeginEvent(GCL2 *This, UINT m, const void *d, UINT n) { rc_put_marker(This, RC_BeginEvent, m, d, n); }
static void STDMETHODCALLTYPE rc_EndEvent(GCL2 *This) { rc_put(L(This), RC_EndEvent, 0); }
static void STDMETHODCALLTYPE rc_ExecuteIndirect(GCL2 *This, ID3D12CommandSignature *sig, UINT max, ID3D12Resource *args, UINT64 aoff,
                                                 ID3D12Resource *cnt, UINT64 coff)
{ rc_a_indirect *a = rc_put(L(This), RC_ExecuteIndirect, sizeof *a); if (a) { a->sig = sig; a->max = max; a->args = args; a->aoff = aoff; a->cnt = cnt; a->coff = coff; } }
static void rc_put_atomic(GCL2 *This, int op, ID3D12Resource *d, UINT64 doff, ID3D12Resource *s, UINT64 soff, UINT n,
                          ID3D12Resource *const *deps, const D3D12_SUBRESOURCE_RANGE_UINT64 *ranges)
{
    UINT k = deps && ranges ? n : 0;
    rc_a_atomic *a = rc_put(L(This), op, RC_ALIGN(sizeof *a) + k * (sizeof *deps + sizeof *ranges));
    uint8_t *p;
    if (!a) return;
    a->dst = d; a->doff = doff; a->src = s; a->soff = soff; a->n = k;
    p = AFTER(a);
    memcpy(p, deps, k * sizeof *deps);
    memcpy(p + k * sizeof *deps, ranges, k * sizeof *ranges);
}
static void STDMETHODCALLTYPE rc_AtomicCopyBufferUINT(GCL2 *This, ID3D12Resource *d, UINT64 doff, ID3D12Resource *s, UINT64 soff, UINT n,
                                                      ID3D12Resource *const *deps, const D3D12_SUBRESOURCE_RANGE_UINT64 *r)
{ rc_put_atomic(This, RC_AtomicCopyBufferUINT, d, doff, s, soff, n, deps, r); }
static void STDMETHODCALLTYPE rc_AtomicCopyBufferUINT64(GCL2 *This, ID3D12Resource *d, UINT64 doff, ID3D12Resource *s, UINT64 soff, UINT n,
                                                        ID3D12Resource *const *deps, const D3D12_SUBRESOURCE_RANGE_UINT64 *r)
{ rc_put_atomic(This, RC_AtomicCopyBufferUINT64, d, doff, s, soff, n, deps, r); }
static void STDMETHODCALLTYPE rc_OMSetDepthBounds(GCL2 *This, FLOAT mn, FLOAT mx) { rc_a_bounds *a = rc_put(L(This), RC_OMSetDepthBounds, sizeof *a); if (a) { a->mn = mn; a->mx = mx; } }
static void STDMETHODCALLTYPE rc_SetSamplePositions(GCL2 *This, UINT s, UINT p, D3D12_SAMPLE_POSITION *pos)
{
    UINT k = pos ? s * p : 0;
    rc_a_samplepos *a = rc_put(L(This), RC_SetSamplePositions, RC_ALIGN(sizeof *a) + k * sizeof *pos);
    if (!a) return;
    a->s = s; a->p = p;
    if (pos) { a->has = 1; memcpy(AFTER(a), pos, k * sizeof *pos); }
}
static void STDMETHODCALLTYPE rc_ResolveSubresourceRegion(GCL2 *This, ID3D12Resource *d, UINT ds, UINT x, UINT y, ID3D12Resource *s, UINT ss,
                                                          D3D12_RECT *r, DXGI_FORMAT f, D3D12_RESOLVE_MODE m)
{
    rc_a_resolvereg *a = rc_put(L(This), RC_ResolveSubresourceRegion, sizeof *a);
    if (!a) return;
    a->dst = d; a->ds = ds; a->x = x; a->y = y; a->src = s; a->ss = ss; a->fmt = f; a->mode = m;
    if (r) { a->r = *r; a->has = 1; }
}
static void STDMETHODCALLTYPE rc_SetViewInstanceMask(GCL2 *This, UINT m) { rc_a_u32 *a = rc_put(L(This), RC_SetViewInstanceMask, sizeof *a); if (a) a->v = m; }
static void STDMETHODCALLTYPE rc_WriteBufferImmediate(GCL2 *This, UINT n, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *p,
                                                      const D3D12_WRITEBUFFERIMMEDIATE_MODE *m)
{
    UINT k = p ? n : 0;
    rc_a_wbi *a = rc_put(L(This), RC_WriteBufferImmediate, RC_ALIGN(sizeof *a) + k * sizeof *p + (m ? k * sizeof *m : 0));
    if (!a) return;
    a->n = k;
    memcpy(AFTER(a), p, k * sizeof *p);
    if (m) { a->has_modes = 1; memcpy(AFTER(a) + k * sizeof *p, m, k * sizeof *m); }
}

static const ID3D12GraphicsCommandList2Vtbl rc_vt = {
    rc_QueryInterface, rc_AddRef, rc_Release, rc_GetPrivateData, rc_SetPrivateData, rc_SetPrivateDataInterface, rc_SetName,
    rc_GetDevice, rc_GetType, rc_Close, rc_Reset, rc_ClearState, rc_DrawInstanced, rc_DrawIndexedInstanced, rc_Dispatch,
    rc_CopyBufferRegion, rc_CopyTextureRegion, rc_CopyResource, rc_CopyTiles, rc_ResolveSubresource, rc_IASetPrimitiveTopology,
    rc_RSSetViewports, rc_RSSetScissorRects, rc_OMSetBlendFactor, rc_OMSetStencilRef, rc_SetPipelineState, rc_ResourceBarrier,
    rc_ExecuteBundle, rc_SetDescriptorHeaps, rc_SetComputeRootSignature, rc_SetGraphicsRootSignature,
    rc_SetComputeRootDescriptorTable, rc_SetGraphicsRootDescriptorTable, rc_SetComputeRoot32BitConstant,
    rc_SetGraphicsRoot32BitConstant, rc_SetComputeRoot32BitConstants, rc_SetGraphicsRoot32BitConstants,
    rc_SetComputeRootConstantBufferView, rc_SetGraphicsRootConstantBufferView, rc_SetComputeRootShaderResourceView,
    rc_SetGraphicsRootShaderResourceView, rc_SetComputeRootUnorderedAccessView, rc_SetGraphicsRootUnorderedAccessView,
    rc_IASetIndexBuffer, rc_IASetVertexBuffers, rc_SOSetTargets, rc_OMSetRenderTargets, rc_ClearDepthStencilView,
    rc_ClearRenderTargetView, rc_ClearUnorderedAccessViewUint, rc_ClearUnorderedAccessViewFloat, rc_DiscardResource,
    rc_BeginQuery, rc_EndQuery, rc_ResolveQueryData, rc_SetPredication, rc_SetMarker, rc_BeginEvent, rc_EndEvent,
    rc_ExecuteIndirect, rc_AtomicCopyBufferUINT, rc_AtomicCopyBufferUINT64, rc_OMSetDepthBounds, rc_SetSamplePositions,
    rc_ResolveSubresourceRegion, rc_SetViewInstanceMask, rc_WriteBufferImmediate
};

/* ---- reproducao numa lista real ---- */
static void rc_play(GCL2 *g, const rc_list *l)
{
    const ID3D12GraphicsCommandList2Vtbl *v = g->lpVtbl;
    uint32_t pos = 0;
    while (pos + sizeof(rc_hdr) <= l->len) {
        const rc_hdr *h = (const rc_hdr *)(l->buf + pos);
        const void *p = h + 1;
        if (h->size < sizeof *h || pos + h->size > l->len) break;
        pos += h->size;
        switch (h->op) {
        case RC_ClearState: v->ClearState(g, ((const rc_a_ptr *)p)->p); break;
        case RC_DrawInstanced: { const rc_a_draw *a = p; v->DrawInstanced(g, a->a, a->b, a->c, a->d); break; }
        case RC_DrawIndexedInstanced: { const rc_a_drawidx *a = p; v->DrawIndexedInstanced(g, a->a, a->b, a->c, a->d, a->e); break; }
        case RC_Dispatch: { const rc_a_dispatch *a = p; v->Dispatch(g, a->x, a->y, a->z); break; }
        case RC_CopyBufferRegion: { const rc_a_copybuf *a = p; v->CopyBufferRegion(g, a->dst, a->doff, a->src, a->soff, a->n); break; }
        case RC_CopyTextureRegion: { const rc_a_copytex *a = p; v->CopyTextureRegion(g, &a->dst, a->x, a->y, a->z, &a->src, a->has_box ? &a->box : NULL); break; }
        case RC_CopyResource: { const rc_a_copyres *a = p; v->CopyResource(g, a->dst, a->src); break; }
        case RC_CopyTiles: { const rc_a_copytiles *a = p; v->CopyTiles(g, a->res, &a->co, &a->sz, a->buf, a->off, a->flags); break; }
        case RC_ResolveSubresource: { const rc_a_resolve *a = p; v->ResolveSubresource(g, a->dst, a->ds, a->src, a->ss, a->fmt); break; }
        case RC_IASetPrimitiveTopology: v->IASetPrimitiveTopology(g, (D3D12_PRIMITIVE_TOPOLOGY)((const rc_a_u32 *)p)->v); break;
        case RC_RSSetViewports: { const rc_a_arr *a = p; v->RSSetViewports(g, a->n, a->has ? (const void *)AFTER(a) : NULL); break; }
        case RC_RSSetScissorRects: { const rc_a_arr *a = p; v->RSSetScissorRects(g, a->n, a->has ? (const void *)AFTER(a) : NULL); break; }
        case RC_OMSetBlendFactor: { const rc_a_blend *a = p; v->OMSetBlendFactor(g, a->has ? a->f : NULL); break; }
        case RC_OMSetStencilRef: v->OMSetStencilRef(g, ((const rc_a_u32 *)p)->v); break;
        case RC_SetPipelineState: v->SetPipelineState(g, ((const rc_a_ptr *)p)->p); break;
        case RC_ResourceBarrier: { const rc_a_arr *a = p; v->ResourceBarrier(g, a->n, a->has ? (const void *)AFTER(a) : NULL); break; }
        case RC_ExecuteBundle: v->ExecuteBundle(g, ((const rc_a_ptr *)p)->p); break;
        case RC_SetDescriptorHeaps: { const rc_a_arr *a = p; v->SetDescriptorHeaps(g, a->n, a->has ? (void *)AFTER(a) : NULL); break; }
        case RC_SetComputeRootSignature: v->SetComputeRootSignature(g, ((const rc_a_ptr *)p)->p); break;
        case RC_SetGraphicsRootSignature: v->SetGraphicsRootSignature(g, ((const rc_a_ptr *)p)->p); break;
        case RC_SetComputeRootDescriptorTable: { const rc_a_table *a = p; v->SetComputeRootDescriptorTable(g, a->i, a->h); break; }
        case RC_SetGraphicsRootDescriptorTable: { const rc_a_table *a = p; v->SetGraphicsRootDescriptorTable(g, a->i, a->h); break; }
        case RC_SetComputeRoot32BitConstant: { const rc_a_const1 *a = p; v->SetComputeRoot32BitConstant(g, a->i, a->v, a->off); break; }
        case RC_SetGraphicsRoot32BitConstant: { const rc_a_const1 *a = p; v->SetGraphicsRoot32BitConstant(g, a->i, a->v, a->off); break; }
        case RC_SetComputeRoot32BitConstants: { const rc_a_consts *a = p; v->SetComputeRoot32BitConstants(g, a->i, a->n, AFTER(a), a->off); break; }
        case RC_SetGraphicsRoot32BitConstants: { const rc_a_consts *a = p; v->SetGraphicsRoot32BitConstants(g, a->i, a->n, AFTER(a), a->off); break; }
        case RC_SetComputeRootConstantBufferView: { const rc_a_rootva *a = p; v->SetComputeRootConstantBufferView(g, a->i, a->va); break; }
        case RC_SetGraphicsRootConstantBufferView: { const rc_a_rootva *a = p; v->SetGraphicsRootConstantBufferView(g, a->i, a->va); break; }
        case RC_SetComputeRootShaderResourceView: { const rc_a_rootva *a = p; v->SetComputeRootShaderResourceView(g, a->i, a->va); break; }
        case RC_SetGraphicsRootShaderResourceView: { const rc_a_rootva *a = p; v->SetGraphicsRootShaderResourceView(g, a->i, a->va); break; }
        case RC_SetComputeRootUnorderedAccessView: { const rc_a_rootva *a = p; v->SetComputeRootUnorderedAccessView(g, a->i, a->va); break; }
        case RC_SetGraphicsRootUnorderedAccessView: { const rc_a_rootva *a = p; v->SetGraphicsRootUnorderedAccessView(g, a->i, a->va); break; }
        case RC_IASetIndexBuffer: { const rc_a_ib *a = p; v->IASetIndexBuffer(g, a->has ? &a->v : NULL); break; }
        case RC_IASetVertexBuffers: { const rc_a_views *a = p; v->IASetVertexBuffers(g, a->start, a->n, a->has ? (const void *)AFTER(a) : NULL); break; }
        case RC_SOSetTargets: { const rc_a_views *a = p; v->SOSetTargets(g, a->start, a->n, a->has ? (const void *)AFTER(a) : NULL); break; }
        case RC_OMSetRenderTargets: {
            const rc_a_omrt *a = p;
            v->OMSetRenderTargets(g, a->n, a->has_rt ? (const void *)AFTER(a) : NULL, a->single, a->has_ds ? &a->ds : NULL);
            break;
        }
        case RC_ClearDepthStencilView: {
            const rc_a_cleards *a = p;
            v->ClearDepthStencilView(g, a->h, a->flags, a->depth, (UINT8)a->stencil, a->n, a->n ? (const void *)AFTER(a) : NULL);
            break;
        }
        case RC_ClearRenderTargetView: { const rc_a_clearrt *a = p; v->ClearRenderTargetView(g, a->h, a->c, a->n, a->n ? (const void *)AFTER(a) : NULL); break; }
        case RC_ClearUnorderedAccessViewUint: {
            const rc_a_clearuav *a = p;
            v->ClearUnorderedAccessViewUint(g, a->g, a->c, a->res, a->v, a->n, a->n ? (const void *)AFTER(a) : NULL);
            break;
        }
        case RC_ClearUnorderedAccessViewFloat: {
            const rc_a_clearuav *a = p;
            v->ClearUnorderedAccessViewFloat(g, a->g, a->c, a->res, (const float *)a->v, a->n, a->n ? (const void *)AFTER(a) : NULL);
            break;
        }
        case RC_DiscardResource: {
            const rc_a_discard *a = p;
            D3D12_DISCARD_REGION r = a->r;
            r.pRects = r.NumRects ? (const void *)AFTER(a) : NULL;
            v->DiscardResource(g, a->res, a->has ? &r : NULL);
            break;
        }
        case RC_BeginQuery: { const rc_a_query *a = p; v->BeginQuery(g, a->heap, a->type, a->idx); break; }
        case RC_EndQuery: { const rc_a_query *a = p; v->EndQuery(g, a->heap, a->type, a->idx); break; }
        case RC_ResolveQueryData: { const rc_a_resolveq *a = p; v->ResolveQueryData(g, a->heap, a->type, a->start, a->n, a->dst, a->off); break; }
        case RC_SetPredication: { const rc_a_pred *a = p; v->SetPredication(g, a->buf, a->off, a->op); break; }
        case RC_SetMarker: { const rc_a_marker *a = p; v->SetMarker(g, a->meta, a->has ? (const void *)AFTER(a) : NULL, a->size); break; }
        case RC_BeginEvent: { const rc_a_marker *a = p; v->BeginEvent(g, a->meta, a->has ? (const void *)AFTER(a) : NULL, a->size); break; }
        case RC_EndEvent: v->EndEvent(g); break;
        case RC_ExecuteIndirect: { const rc_a_indirect *a = p; v->ExecuteIndirect(g, a->sig, a->max, a->args, a->aoff, a->cnt, a->coff); break; }
        case RC_AtomicCopyBufferUINT: case RC_AtomicCopyBufferUINT64: {
            const rc_a_atomic *a = p;
            const uint8_t *q = AFTER(a);
            ID3D12Resource *const *deps = a->n ? (ID3D12Resource *const *)q : NULL;
            const D3D12_SUBRESOURCE_RANGE_UINT64 *r = a->n ? (const void *)(q + a->n * sizeof(void *)) : NULL;
            if (h->op == RC_AtomicCopyBufferUINT) v->AtomicCopyBufferUINT(g, a->dst, a->doff, a->src, a->soff, a->n, deps, r);
            else v->AtomicCopyBufferUINT64(g, a->dst, a->doff, a->src, a->soff, a->n, deps, r);
            break;
        }
        case RC_OMSetDepthBounds: { const rc_a_bounds *a = p; v->OMSetDepthBounds(g, a->mn, a->mx); break; }
        case RC_SetSamplePositions: { const rc_a_samplepos *a = p; v->SetSamplePositions(g, a->s, a->p, a->has ? (void *)AFTER(a) : NULL); break; }
        case RC_ResolveSubresourceRegion: {
            const rc_a_resolvereg *a = p;
            D3D12_RECT r = a->r;
            v->ResolveSubresourceRegion(g, a->dst, a->ds, a->x, a->y, a->src, a->ss, a->has ? &r : NULL, a->fmt, a->mode);
            break;
        }
        case RC_SetViewInstanceMask: v->SetViewInstanceMask(g, ((const rc_a_u32 *)p)->v); break;
        case RC_WriteBufferImmediate: {
            const rc_a_wbi *a = p;
            const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *wp = (const void *)AFTER(a);
            v->WriteBufferImmediate(g, a->n, wp, a->has_modes ? (const void *)(wp + a->n) : NULL);
            break;
        }
        default: break;
        }
    }
}

/* ---- criacao (lp_create, stutter_fm6.c) ---- */

/* CreateCommandList DIRECT chamado de dentro do 0x1400dee90 do Azimuth -> gravadora */
int rc_wants(D3D12_COMMAND_LIST_TYPE type, const void *ret)
{
    return rc_on && type == D3D12_COMMAND_LIST_TYPE_DIRECT && g_base && ret == g_base + RC_OPEN_RET_RVA;
}

HRESULT rc_create(ID3D12Device *d, ID3D12PipelineState *ps, REFIID riid, void **out)
{
    rc_list *l;
    HRESULT hr;
    if (!out) return E_POINTER;
    AcquireSRWLockExclusive(&rc_lock);
    if ((l = rc_free) != NULL) { rc_free = l->next; rc_nfree--; }
    ReleaseSRWLockExclusive(&rc_lock);
    if (!l && !(l = calloc(1, sizeof *l))) return E_OUTOFMEMORY;
    l->vt = &rc_vt;
    l->ref = 1;
    l->dev = d;
    l->next = NULL;
    InterlockedIncrement(&rc_live);
    rc_Reset((GCL2 *)l, NULL, ps);
    hr = rc_QueryInterface((GCL2 *)l, riid, out);
    rc_Release((GCL2 *)l);
    return hr;
}

/* ---- uma lista real por sequencia de gravadoras no ExecuteCommandLists ---- */
typedef struct { ID3D12CommandAllocator *a; GCL2 *l; UINT64 fence; } rc_slot;
typedef struct rc_queue {
    ID3D12Device *dev;
    rc_slot *s;
    uint32_t n, cap, next;
    ID3D12CommandList **out;   /* vetor do ECL montado */
    uint32_t out_cap;
    int fail_logged;
} rc_queue;

int rc_any(UINT n, ID3D12CommandList *const *l)
{
    UINT i;
    if (!rc_on) return 0;
    for (i = 0; i < n; i++) if (rc_is(l[i])) return 1;
    return 0;
}

/* par lista + allocator livre (a GPU passou do envio em que foi usado) ou um novo; NULL se nao deu */
static rc_slot *rc_take(rc_queue *rq, UINT64 done)
{
    uint32_t i;
    rc_slot *s;
    for (i = 0; i < rq->n; i++) {
        uint32_t k = (rq->next + i) % rq->n;
        if (rq->s[k].fence <= done) {
            rq->next = (k + 1) % rq->n;
            s = &rq->s[k];
            if (FAILED(s->a->lpVtbl->Reset(s->a))) return NULL;
            return s;
        }
    }
    if (rq->n == rq->cap) {
        uint32_t cap = rq->cap ? rq->cap * 2 : 16;
        rc_slot *v = realloc(rq->s, cap * sizeof *v);
        if (!v) return NULL;
        rq->s = v;
        rq->cap = cap;
    }
    s = &rq->s[rq->n];
    memset(s, 0, sizeof *s);
    if (FAILED(rq->dev->lpVtbl->CreateCommandAllocator(rq->dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&s->a)))
        return NULL;
    if (!rc_inner_create ||
        FAILED(rc_inner_create(rq->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, s->a, NULL, &IID_ID3D12GraphicsCommandList2, (void **)&s->l))) {
        s->a->lpVtbl->Release(s->a);
        return NULL;
    }
    s->l->lpVtbl->Close(s->l);   /* nasce aberta; o chamador faz o Reset */
    rq->n++;
    {
        LONG t = InterlockedIncrement(&rc_slots_total);
        if ((t & (t - 1)) == 0) wlog("d3d: unified lists: %ld real proxy lists (%u on this queue)", t, rq->n);
    }
    return s;
}

/* l[0..n) -> rq->out: cada sequencia de gravadoras vira uma lista real (fence = valor do proxy sinalizado depois
 * deste ECL; done = o que a GPU ja concluiu). Devolve quantas listas o ECL leva; *outv = rq->out. */
UINT rc_merge(void **rqp, ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l, UINT64 fence, UINT64 done,
              ID3D12CommandList ***outv)
{
    rc_queue *rq = *rqp;
    UINT i = 0, k = 0, j;
    int pred;
    LARGE_INTEGER t0, t1;
    if (!rq) {
        if (!(rq = calloc(1, sizeof *rq))) return 0;
        if (FAILED(q->lpVtbl->GetDevice(q, &IID_ID3D12Device, (void **)&rq->dev))) { free(rq); return 0; }
        rq->dev->lpVtbl->Release(rq->dev);   /* o device vive a sessao toda */
        *rqp = rq;
    }
    if (rq->out_cap < n) {
        ID3D12CommandList **o = realloc(rq->out, n * sizeof *o);
        if (!o) return 0;
        rq->out = o;
        rq->out_cap = n;
    }
    QueryPerformanceCounter(&t0);
    while (i < n) {
        rc_slot *s;
        GCL2 *g;
        HRESULT hr;
        if (!rc_is(l[i])) { rq->out[k++] = l[i++]; continue; }
        for (j = i; j < n && rc_is(l[j]); j++) {}
        if (!(s = rc_take(rq, done))) {
            if (!rq->fail_logged++) wlog("d3d: unified lists: no real proxy list/allocator; %u recorders dropped", j - i);
            i = j;
            continue;
        }
        g = s->l;
        g->lpVtbl->Reset(g, s->a, L(l[i])->pso0);
        for (pred = 0; i < j; i++) {
            rc_list *f = L(l[i]);
            if (k && rq->out[k - 1] == (ID3D12CommandList *)g) {   /* fronteira: estado de lista recem aberta */
                if (pred) g->lpVtbl->SetPredication(g, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                g->lpVtbl->ClearState(g, f->pso0);
                gg_cl_restart(g, f->pso0);
            }
            pred = f->pred;
            if (f->open || f->oom) {
                static volatile LONG warned;
                if (InterlockedIncrement(&warned) <= 8) wlog("d3d: unified lists: recorder %p %s in ExecuteCommandLists", f, f->oom ? "incomplete (out of memory)" : "open");
            }
            rc_play(g, f);
            InterlockedAdd(&rc_cmds, (LONG)f->ncmd);
            InterlockedIncrement(&rc_execs);
            if (!k || rq->out[k - 1] != (ID3D12CommandList *)g) rq->out[k++] = (ID3D12CommandList *)g;
        }
        if (FAILED(hr = g->lpVtbl->Close(g))) {
            static volatile LONG bad;
            LONG c = InterlockedIncrement(&bad);
            if (c <= 8 || (c & (c - 1)) == 0) wlog("d3d: unified lists: Close of real list %p failed hr=%#lx (#%ld)", g, hr, c);
        }
        s->fence = fence;
        InterlockedIncrement(&rc_reals);
    }
    QueryPerformanceCounter(&t1);
    InterlockedAdd64(&rc_ticks, t1.QuadPart - t0.QuadPart);
    {
        static volatile LONG first;
        if (!first && !InterlockedExchange(&first, 1))
            wlog("d3d: unified lists: 1st ExecuteCommandLists with recorders: %u lists -> %u", n, k);
    }
    *outv = rq->out;
    return k;
}

/* " | gravadoras: ..." da linha "vazao 10 s" (stutter_fm6.c) */
int rc_format(char *buf, int cap, double frames)
{
    double ms;
    if (!rc_on) return 0;
    if (!rc_freq.QuadPart) QueryPerformanceFrequency(&rc_freq);
    ms = (double)InterlockedExchange64(&rc_ticks, 0) * 1000.0 / (double)rc_freq.QuadPart;
    return snprintf(buf, cap, " | recorders: opened %.0f/frame, executed %.0f/frame in %.1f real lists/frame (%.0f commands, "
                    "%.2f ms), %ld alive, %ld real proxy lists", InterlockedExchange(&rc_opens, 0) / frames,
                    InterlockedExchange(&rc_execs, 0) / frames, InterlockedExchange(&rc_reals, 0) / frames,
                    InterlockedExchange(&rc_cmds, 0) / frames, ms / frames, rc_live, rc_slots_total);
}

/* hook do apply_patches (fm5_patch.c), depois do pool de listas: inner = CreateCommandList por dentro (gpuguard) */
int rc_wanted(void) { return dbg_int(L"ListMerge", is_fm6()) > 0; }
void rc_setup(void *inner)
{
    rc_on = 0;
    rc_inner_create = (rc_create_fn)inner;
    if (!inner || !rc_wanted()) return;
    if (!g_base || !bytes_match(g_base + RC_OPEN_CALL_RVA, "ff5060eb0c", 5)) {
        wlog("d3d: Azimuth CreateCommandList (azimuth+%#x) does not match -> no unified lists", RC_OPEN_CALL_RVA);
        return;
    }
    rc_on = 1;
    wlog("d3d: unified lists: Azimuth records into proxy lists; each ExecuteCommandLists submits one real list per sequence");
}
