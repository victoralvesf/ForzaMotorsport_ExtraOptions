/*
 * Testes de common/cmdrec.c: gravadora de command list (gravar/reproduzir com os vetores copiados), uma lista real por
 * sequencia de gravadoras no ExecuteCommandLists (ClearState nas fronteiras, reuso pelo fence) e instalacao.
 */
#include "test.h"
#include "../common/cmdrec.c"

static GCL2 *rec_new(ID3D12PipelineState *ps)
{
    void *out = NULL;
    if (FAILED(rc_create(NULL, ps, &IID_ID3D12GraphicsCommandList2, &out))) return NULL;
    return out;
}

/* uma chamada de cada metodo com vetor/ponteiro opcional */
static void rec_all(GCL2 *g)
{
    const ID3D12GraphicsCommandList2Vtbl *v = g->lpVtbl;
    D3D12_VIEWPORT vp[2] = { { 0, 0, 1920, 1080, 0, 1 }, { 1, 2, 3, 4, 0.5f, 1 } };
    D3D12_RECT rc[2] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } };
    D3D12_RESOURCE_BARRIER b[3];
    D3D12_CPU_DESCRIPTOR_HANDLE rt[3] = { { 0x100 }, { 0x200 }, { 0x300 } }, ds = { 0x900 };
    D3D12_VERTEX_BUFFER_VIEW vb[2] = { { 0x1000, 64, 16 }, { 0x2000, 32, 8 } };
    D3D12_INDEX_BUFFER_VIEW ib = { 0x3000, 128, DXGI_FORMAT_R16_UINT };
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_BOX box = { 1, 2, 0, 3, 4, 1 };
    D3D12_DISCARD_REGION dr = { 2, rc, 0, 1 };
    D3D12_SAMPLE_POSITION sp[4] = { { 1, 2 }, { 3, 4 }, { 5, 6 }, { 7, 8 } };
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER wp[2] = { { 0x5000, 1 }, { 0x5008, 2 } };
    D3D12_WRITEBUFFERIMMEDIATE_MODE wm[2] = { D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT, D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN };
    ID3D12DescriptorHeap *heaps[2] = { (ID3D12DescriptorHeap *)0x21, (ID3D12DescriptorHeap *)0x22 };
    ID3D12Resource *deps[1] = { (ID3D12Resource *)0x31 };
    D3D12_SUBRESOURCE_RANGE_UINT64 rg[1] = { { 2, { 0, 64 } } };
    D3D12_GPU_DESCRIPTOR_HANDLE gh = { 0xabc0 };
    const FLOAT col[4] = { 0.25f, 0.5f, 0.75f, 1 };
    const UINT u4[4] = { 1, 2, 3, 4 }, consts[3] = { 7, 8, 9 };
    D3D12_TILED_RESOURCE_COORDINATE co = { 1, 2, 3, 4 };
    D3D12_TILE_REGION_SIZE tsz = { 5, TRUE, 1, 1, 1 };
    D3D12_RECT one = { 0, 0, 8, 8 };
    memset(b, 0, sizeof b);
    memset(&dst, 0, sizeof dst);
    dst.pResource = (ID3D12Resource *)0x11;
    dst.SubresourceIndex = 2;
    src = dst;
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b[0].Transition.pResource = (ID3D12Resource *)0x41;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b[2].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    v->SetDescriptorHeaps(g, 2, heaps);
    v->ClearState(g, (ID3D12PipelineState *)0x51);
    v->DrawInstanced(g, 3, 1, 0, 0);
    v->DrawIndexedInstanced(g, 36, 2, 6, -4, 1);
    v->Dispatch(g, 8, 4, 1);
    v->CopyBufferRegion(g, (ID3D12Resource *)0x61, 16, (ID3D12Resource *)0x62, 32, 64);
    v->CopyTextureRegion(g, &dst, 1, 2, 0, &src, &box);
    v->CopyTextureRegion(g, &dst, 0, 0, 0, &src, NULL);
    v->CopyResource(g, (ID3D12Resource *)0x63, (ID3D12Resource *)0x64);
    v->CopyTiles(g, (ID3D12Resource *)0x65, &co, &tsz, (ID3D12Resource *)0x66, 128, D3D12_TILE_COPY_FLAG_NONE);
    v->ResolveSubresource(g, (ID3D12Resource *)0x67, 0, (ID3D12Resource *)0x68, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    v->IASetPrimitiveTopology(g, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    v->RSSetViewports(g, 2, vp);
    v->RSSetScissorRects(g, 2, rc);
    v->OMSetBlendFactor(g, col);
    v->OMSetBlendFactor(g, NULL);
    v->OMSetStencilRef(g, 0x80);
    v->SetPipelineState(g, (ID3D12PipelineState *)0x52);
    v->ResourceBarrier(g, 3, b);
    v->ExecuteBundle(g, (ID3D12GraphicsCommandList *)0x71);
    v->SetComputeRootSignature(g, (ID3D12RootSignature *)0x72);
    v->SetGraphicsRootSignature(g, (ID3D12RootSignature *)0x73);
    v->SetComputeRootDescriptorTable(g, 1, gh);
    v->SetGraphicsRootDescriptorTable(g, 2, gh);
    v->SetComputeRoot32BitConstant(g, 0, 5, 1);
    v->SetGraphicsRoot32BitConstant(g, 1, 6, 2);
    v->SetComputeRoot32BitConstants(g, 2, 3, consts, 0);
    v->SetGraphicsRoot32BitConstants(g, 3, 3, consts, 4);
    v->SetComputeRootConstantBufferView(g, 4, 0x7000);
    v->SetGraphicsRootConstantBufferView(g, 5, 0x7100);
    v->SetComputeRootShaderResourceView(g, 6, 0x7200);
    v->SetGraphicsRootShaderResourceView(g, 7, 0x7300);
    v->SetComputeRootUnorderedAccessView(g, 8, 0x7400);
    v->SetGraphicsRootUnorderedAccessView(g, 9, 0x7500);
    v->IASetIndexBuffer(g, &ib);
    v->IASetIndexBuffer(g, NULL);
    v->IASetVertexBuffers(g, 1, 2, vb);
    v->IASetVertexBuffers(g, 0, 2, NULL);
    v->SOSetTargets(g, 0, 0, NULL);
    v->OMSetRenderTargets(g, 3, rt, FALSE, &ds);
    v->OMSetRenderTargets(g, 3, rt, TRUE, NULL);
    v->OMSetRenderTargets(g, 0, NULL, FALSE, NULL);
    v->ClearDepthStencilView(g, ds, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 3, 1, &one);
    v->ClearRenderTargetView(g, rt[0], col, 0, NULL);
    v->ClearUnorderedAccessViewUint(g, gh, rt[1], (ID3D12Resource *)0x81, u4, 2, rc);
    v->ClearUnorderedAccessViewFloat(g, gh, rt[2], (ID3D12Resource *)0x82, col, 0, NULL);
    v->DiscardResource(g, (ID3D12Resource *)0x83, &dr);
    v->DiscardResource(g, (ID3D12Resource *)0x84, NULL);
    v->BeginQuery(g, (ID3D12QueryHeap *)0x91, D3D12_QUERY_TYPE_OCCLUSION, 3);
    v->EndQuery(g, (ID3D12QueryHeap *)0x91, D3D12_QUERY_TYPE_TIMESTAMP, 4);
    v->ResolveQueryData(g, (ID3D12QueryHeap *)0x91, D3D12_QUERY_TYPE_TIMESTAMP, 4, 2, (ID3D12Resource *)0x92, 256);
    v->SetPredication(g, (ID3D12Resource *)0x93, 8, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    v->SetMarker(g, 1, "marca", 6);
    v->BeginEvent(g, 1, "evento", 7);
    v->EndEvent(g);
    v->ExecuteIndirect(g, (ID3D12CommandSignature *)0xa1, 16, (ID3D12Resource *)0xa2, 32, (ID3D12Resource *)0xa3, 4);
    v->AtomicCopyBufferUINT(g, (ID3D12Resource *)0xb1, 0, (ID3D12Resource *)0xb2, 8, 1, deps, rg);
    v->AtomicCopyBufferUINT64(g, (ID3D12Resource *)0xb1, 0, (ID3D12Resource *)0xb2, 8, 0, NULL, NULL);
    v->OMSetDepthBounds(g, 0.25f, 0.75f);
    v->SetSamplePositions(g, 2, 2, sp);
    v->ResolveSubresourceRegion(g, (ID3D12Resource *)0xc1, 0, 4, 4, (ID3D12Resource *)0xc2, 0, &one, DXGI_FORMAT_R16_FLOAT, D3D12_RESOLVE_MODE_MAX);
    v->SetViewInstanceMask(g, 3);
    v->WriteBufferImmediate(g, 2, wp, wm);
    v->WriteBufferImmediate(g, 2, wp, NULL);
}

/* reproduzir uma gravadora noutra gravadora da o mesmo buffer: cada metodo e reproduzido com os mesmos argumentos */
TEST(cmdrec_record_replay_roundtrip)
{
    GCL2 *a = rec_new((ID3D12PipelineState *)0x50), *b = rec_new(NULL);
    rc_list *la = L(a), *lb = L(b);
    REQUIRE(a && b);
    CHECK(rc_is(a));
    CHECK(la->pso0 == (ID3D12PipelineState *)0x50);
    CHECK(la->open);
    rec_all(a);
    CHECK_EQ(la->ncmd, 64);
    CHECK(SUCCEEDED(a->lpVtbl->Close(a)));
    CHECK(FAILED(a->lpVtbl->Close(a)));            /* ja fechada */
    rc_play(b, la);
    CHECK_EQ(lb->ncmd, la->ncmd);
    CHECK_EQ(lb->len, la->len);
    CHECK(lb->len == la->len && !memcmp(lb->buf, la->buf, la->len));
    /* Reset limpa e troca o PSO inicial (o allocator nao importa) */
    CHECK(SUCCEEDED(a->lpVtbl->Reset(a, (ID3D12CommandAllocator *)0x1, (ID3D12PipelineState *)0x55)));
    CHECK_EQ(la->ncmd, 0);
    CHECK(la->pso0 == (ID3D12PipelineState *)0x55);
    CHECK(a->lpVtbl->GetType(a) == D3D12_COMMAND_LIST_TYPE_DIRECT);
    a->lpVtbl->Release(a);
    b->lpVtbl->Release(b);
}

/* argumentos que chegam na lista real (espiao), escritos aqui */
static ID3D12GraphicsCommandList2Vtbl spy_vt;
static UINT spy_omn, spy_single, spy_rt1, spy_ds, spy_vpn, spy_vbn, spy_consts[4];
static int spy_rts_null, spy_ds_null, spy_blend_null, spy_vb_null;
static float spy_vpw, spy_blend0;
static INT spy_bv;
static void STDMETHODCALLTYPE spy_om(GCL2 *g, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE *rts, WINBOOL s, const D3D12_CPU_DESCRIPTOR_HANDLE *ds)
{
    (void)g;
    spy_omn = n; spy_single = s; spy_rts_null = !rts; spy_ds_null = !ds;
    spy_rt1 = rts && !s && n > 1 ? (UINT)rts[1].ptr : rts ? (UINT)rts[0].ptr : 0;
    spy_ds = ds ? (UINT)ds->ptr : 0;
}
static void STDMETHODCALLTYPE spy_vp(GCL2 *g, UINT n, const D3D12_VIEWPORT *v) { (void)g; spy_vpn = n; spy_vpw = n > 1 ? v[1].Width : 0; }
static void STDMETHODCALLTYPE spy_blend(GCL2 *g, const FLOAT f[4]) { (void)g; spy_blend_null = !f; spy_blend0 = f ? f[0] : -1; }
static void STDMETHODCALLTYPE spy_vb(GCL2 *g, UINT s, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v) { (void)g; (void)s; spy_vbn = n; spy_vb_null = !v; }
static void STDMETHODCALLTYPE spy_consts_fn(GCL2 *g, UINT i, UINT n, const void *d, UINT off)
{ (void)g; spy_consts[0] = i; spy_consts[1] = n; spy_consts[2] = ((const UINT *)d)[2]; spy_consts[3] = off; }
static void STDMETHODCALLTYPE spy_drawidx(GCL2 *g, UINT n, UINT i, UINT si, INT bv, UINT sinst) { (void)g; (void)n; (void)i; (void)si; (void)sinst; spy_bv = bv; }

TEST(cmdrec_replay_arguments)
{
    GCL2 *a = rec_new(NULL);
    rc_list spy;
    const D3D12_CPU_DESCRIPTOR_HANDLE rt[3] = { { 0x100 }, { 0x200 }, { 0x300 } }, ds = { 0x900 };
    const D3D12_VIEWPORT vp[2] = { { 0, 0, 1, 1, 0, 1 }, { 0, 0, 1280, 720, 0, 1 } };
    const FLOAT col[4] = { 0.25f, 0, 0, 0 };
    const UINT c[3] = { 7, 8, 9 };
    REQUIRE(a);
    spy_vt = rc_vt;
    spy_vt.OMSetRenderTargets = spy_om; spy_vt.RSSetViewports = spy_vp; spy_vt.OMSetBlendFactor = spy_blend;
    spy_vt.IASetVertexBuffers = spy_vb; spy_vt.SetGraphicsRoot32BitConstants = spy_consts_fn;
    spy_vt.DrawIndexedInstanced = spy_drawidx;
    memset(&spy, 0, sizeof spy);
    spy.vt = &spy_vt;
    a->lpVtbl->OMSetRenderTargets(a, 3, rt, FALSE, &ds);
    rc_play((GCL2 *)&spy, L(a));
    CHECK_EQ(spy_omn, 3); CHECK_EQ(spy_single, 0); CHECK_EQ(spy_rt1, 0x200); CHECK_EQ(spy_ds, 0x900);
    a->lpVtbl->Reset(a, NULL, NULL);
    a->lpVtbl->OMSetRenderTargets(a, 3, rt, TRUE, NULL);   /* faixa: so o 1o handle */
    rc_play((GCL2 *)&spy, L(a));
    CHECK_EQ(spy_omn, 3); CHECK_EQ(spy_single, 1); CHECK_EQ(spy_rt1, 0x100); CHECK(spy_ds_null);
    a->lpVtbl->Reset(a, NULL, NULL);
    a->lpVtbl->OMSetRenderTargets(a, 0, NULL, FALSE, NULL);
    a->lpVtbl->RSSetViewports(a, 2, vp);
    a->lpVtbl->OMSetBlendFactor(a, NULL);
    a->lpVtbl->IASetVertexBuffers(a, 0, 3, NULL);
    a->lpVtbl->SetGraphicsRoot32BitConstants(a, 4, 3, c, 12);
    a->lpVtbl->DrawIndexedInstanced(a, 3, 1, 0, -7, 0);
    rc_play((GCL2 *)&spy, L(a));
    CHECK_EQ(spy_omn, 0); CHECK(spy_rts_null); CHECK(spy_ds_null);
    CHECK_EQ(spy_vpn, 2); CHECK(spy_vpw == 1280.0f);
    CHECK(spy_blend_null);
    CHECK_EQ(spy_vbn, 3); CHECK(spy_vb_null);
    CHECK_EQ(spy_consts[0], 4); CHECK_EQ(spy_consts[1], 3); CHECK_EQ(spy_consts[2], 9); CHECK_EQ(spy_consts[3], 12);
    CHECK(spy_bv == -7);
    a->lpVtbl->Reset(a, NULL, NULL);
    a->lpVtbl->OMSetBlendFactor(a, col);
    rc_play((GCL2 *)&spy, L(a));
    CHECK(!spy_blend_null); CHECK(spy_blend0 == 0.25f);
    a->lpVtbl->Release(a);
}

/* RTV/DSV gravados vao para copias num heap do proxy (o Azimuth pode reaproveitar o slot antes do ECL); os blocos
   voltam no Reset/Release */
static ID3D12DescriptorHeapVtbl dc_hvt;
static struct { ID3D12DescriptorHeapVtbl *vt; SIZE_T base; } dc_heaps[4];
static int dc_nheaps, dc_ncopies;
static struct { UINT n; SIZE_T dst, src; D3D12_DESCRIPTOR_HEAP_TYPE t; } dc_copy[32];
static D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE dc_start(ID3D12DescriptorHeap *h, D3D12_CPU_DESCRIPTOR_HANDLE *r)
{ r->ptr = ((const SIZE_T *)h)[1]; return r; }
static HRESULT STDMETHODCALLTYPE dc_create_heap(ID3D12Device *d, const D3D12_DESCRIPTOR_HEAP_DESC *hd, REFIID r, void **o)
{
    (void)d; (void)r;
    if (hd->Flags || hd->NumDescriptors != RC_DHEAP) return E_INVALIDARG;
    dc_hvt.GetCPUDescriptorHandleForHeapStart = dc_start;
    dc_heaps[dc_nheaps].vt = &dc_hvt;
    dc_heaps[dc_nheaps].base = hd->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? 0x100000 : 0x800000;
    *o = &dc_heaps[dc_nheaps++];
    return S_OK;
}
static UINT STDMETHODCALLTYPE dc_inc(ID3D12Device *d, D3D12_DESCRIPTOR_HEAP_TYPE t) { (void)d; return t == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? 32 : 8; }
static void STDMETHODCALLTYPE dc_copy1(ID3D12Device *d, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                       D3D12_DESCRIPTOR_HEAP_TYPE t)
{
    (void)d;
    dc_copy[dc_ncopies].n = n; dc_copy[dc_ncopies].dst = dst.ptr; dc_copy[dc_ncopies].src = src.ptr; dc_copy[dc_ncopies++].t = t;
}

TEST(cmdrec_rtv_dsv_copied_at_record)
{
    static ID3D12DeviceVtbl dvt;
    struct { ID3D12DeviceVtbl *vt; } dev = { &dvt };
    const D3D12_CPU_DESCRIPTOR_HANDLE rt[3] = { { 0x100 }, { 0x200 }, { 0x300 } }, ds = { 0x900 };
    const FLOAT col[4] = { 0 };
    rc_list spy;
    void *out = NULL;
    GCL2 *a, *b;
    int i;
    dvt.CreateDescriptorHeap = dc_create_heap;
    dvt.GetDescriptorHandleIncrementSize = dc_inc;
    dvt.CopyDescriptorsSimple = dc_copy1;
    REQUIRE(SUCCEEDED(rc_create((ID3D12Device *)&dev, NULL, &IID_ID3D12GraphicsCommandList2, &out)));
    a = out;
    spy_vt = rc_vt;
    spy_vt.OMSetRenderTargets = spy_om;
    memset(&spy, 0, sizeof spy);
    spy.vt = &spy_vt;
    log_capture();
    /* faixa de 3 RTVs: copiada inteira; DSV no outro heap */
    a->lpVtbl->OMSetRenderTargets(a, 3, rt, TRUE, &ds);
    CHECK_EQ(dc_nheaps, 2);
    CHECK_EQ(dc_ncopies, 2);
    CHECK_EQ(dc_copy[0].n, 3); CHECK_EQ(dc_copy[0].src, 0x100); CHECK_EQ(dc_copy[0].dst, 0x100000);
    CHECK(dc_copy[0].t == D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CHECK_EQ(dc_copy[1].n, 1); CHECK_EQ(dc_copy[1].src, 0x900); CHECK_EQ(dc_copy[1].dst, 0x800000);
    CHECK(dc_copy[1].t == D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CHECK(log_has("d3d: recorder RTV heap #1 (16384 descriptors)"));
    /* vetor de handles: cada um no slot seguinte do mesmo bloco */
    a->lpVtbl->OMSetRenderTargets(a, 2, rt + 1, FALSE, NULL);
    CHECK_EQ(dc_copy[2].src, 0x200); CHECK_EQ(dc_copy[2].dst, 0x100000 + 3 * 32);
    CHECK_EQ(dc_copy[3].src, 0x300); CHECK_EQ(dc_copy[3].dst, 0x100000 + 4 * 32);
    a->lpVtbl->ClearRenderTargetView(a, rt[0], col, 0, NULL);
    a->lpVtbl->ClearDepthStencilView(a, ds, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    CHECK_EQ(dc_copy[4].dst, 0x100000 + 5 * 32);
    CHECK_EQ(dc_copy[5].dst, 0x800000 + 8);
    /* a reproducao usa as copias, nunca os slots do Azimuth */
    rc_play((GCL2 *)&spy, L(a));
    CHECK_EQ(spy_omn, 2); CHECK_EQ(spy_single, 0); CHECK_EQ(spy_rt1, 0x100000 + 4 * 32); CHECK(spy_ds_null);
    CHECK_EQ(L(a)->d[RC_D_RTV].n, 1);
    CHECK_EQ(rc_dp[RC_D_RTV].nfree, 255);
    /* Reset devolve o bloco; a outra gravadora pega o mesmo */
    a->lpVtbl->Reset(a, NULL, NULL);
    CHECK_EQ(L(a)->d[RC_D_RTV].n, 0);
    CHECK_EQ(rc_dp[RC_D_RTV].nfree, 256);
    CHECK(SUCCEEDED(rc_create((ID3D12Device *)&dev, NULL, &IID_ID3D12GraphicsCommandList2, &out)));
    b = out;
    b->lpVtbl->ClearRenderTargetView(b, rt[2], col, 0, NULL);
    CHECK_EQ(dc_copy[6].dst, 0x100000);
    /* faixa que nao cabe no resto do bloco: bloco novo */
    for (i = 0; i < 9; i++) b->lpVtbl->OMSetRenderTargets(b, 7, rt, TRUE, NULL);   /* 1 + 63: bloco cheio */
    CHECK_EQ(L(b)->d[RC_D_RTV].n, 1);
    CHECK_EQ(dc_copy[15].dst, 0x100000 + 57 * 32);
    b->lpVtbl->OMSetRenderTargets(b, 7, rt, TRUE, NULL);
    CHECK_EQ(L(b)->d[RC_D_RTV].n, 2);
    CHECK_EQ(dc_copy[16].dst, 0x100000 + 64 * 32);
    b->lpVtbl->Release(b);
    CHECK_EQ(rc_dp[RC_D_RTV].nfree, 256);
    CHECK_EQ(dc_nheaps, 2);
    a->lpVtbl->Release(a);
}

/* fila/device/allocator falsos para o rc_merge; as listas "reais" sao gravadoras com vtable propria (espiao) */
static int fk_allocs, fk_alloc_resets, fk_reals, fk_real_resets;
static HRESULT STDMETHODCALLTYPE fk_alloc_reset(ID3D12CommandAllocator *a) { (void)a; fk_alloc_resets++; return S_OK; }
static ULONG STDMETHODCALLTYPE fk_release(void *o) { (void)o; return 1; }
static ID3D12CommandAllocatorVtbl fk_avt;
static struct { ID3D12CommandAllocatorVtbl *vt; } fk_alloc_obj[64];
static HRESULT STDMETHODCALLTYPE fk_create_alloc(ID3D12Device *d, D3D12_COMMAND_LIST_TYPE t, REFIID r, void **o)
{
    (void)d; (void)t; (void)r;
    fk_avt.Reset = fk_alloc_reset;
    fk_avt.Release = (void *)fk_release;
    fk_alloc_obj[fk_allocs].vt = &fk_avt;
    *o = &fk_alloc_obj[fk_allocs++];
    return S_OK;
}
static ID3D12DeviceVtbl fk_dvt;
static struct { ID3D12DeviceVtbl *vt; } fk_dev = { &fk_dvt };
static HRESULT STDMETHODCALLTYPE fk_get_device(ID3D12CommandQueue *q, REFIID r, void **o) { (void)q; (void)r; *o = &fk_dev; return S_OK; }
static ID3D12CommandQueueVtbl fk_qvt;
static struct { ID3D12CommandQueueVtbl *vt; } fk_queue = { &fk_qvt };
static ID3D12GraphicsCommandList2Vtbl fk_real_vt;
static HRESULT STDMETHODCALLTYPE fk_real_reset(GCL2 *g, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{ fk_real_resets++; return rc_Reset(g, a, ps); }
static HRESULT STDMETHODCALLTYPE fk_inner_create(ID3D12Device *d, UINT m, D3D12_COMMAND_LIST_TYPE t, ID3D12CommandAllocator *a,
                                                 ID3D12PipelineState *ps, REFIID r, void **o)
{
    HRESULT hr = rc_create(d, ps, r, o);
    (void)m; (void)t; (void)a;
    if (SUCCEEDED(hr)) {
        fk_real_vt = rc_vt;
        fk_real_vt.Reset = fk_real_reset;
        L(*o)->vt = &fk_real_vt;   /* nao e mais gravadora para o rc_is */
        fk_reals++;
    }
    return hr;
}

/* operacoes gravadas numa lista (a lista "real" do teste e uma gravadora) */
static int ops_of(GCL2 *g, uint16_t *ops, int max)
{
    const rc_list *l = L(g);
    uint32_t pos = 0;
    int n = 0;
    while (pos < l->len && n < max) {
        const rc_hdr *h = (const rc_hdr *)(l->buf + pos);
        ops[n++] = h->op;
        pos += h->size;
    }
    return n;
}

TEST(cmdrec_merge_one_real_list_per_run)
{
    void *rq = NULL;
    GCL2 *f1 = rec_new((ID3D12PipelineState *)0x101), *f2 = rec_new((ID3D12PipelineState *)0x102), *f3 = rec_new(NULL);
    ID3D12CommandList *in[4], **out = NULL, *real = (ID3D12CommandList *)&fk_queue;   /* outro objeto COM: lista real */
    uint16_t ops[16];
    int n;
    UINT k;
    REQUIRE(f1 && f2 && f3);
    fk_qvt.GetDevice = fk_get_device;
    fk_dvt.Release = (void *)fk_release;
    fk_dvt.CreateCommandAllocator = fk_create_alloc;
    fk_allocs = fk_alloc_resets = fk_reals = fk_real_resets = 0;
    rc_inner_create = fk_inner_create;
    rc_on = 1;
    f1->lpVtbl->DrawInstanced(f1, 3, 1, 0, 0);
    f1->lpVtbl->Close(f1);
    f2->lpVtbl->Dispatch(f2, 1, 1, 1);
    f2->lpVtbl->Close(f2);
    f3->lpVtbl->DrawInstanced(f3, 6, 1, 0, 0);
    f3->lpVtbl->Close(f3);
    in[0] = (ID3D12CommandList *)f1; in[1] = (ID3D12CommandList *)f2; in[2] = real; in[3] = (ID3D12CommandList *)f3;
    CHECK(rc_any(4, in));
    CHECK(!rc_any(1, &real));
    /* [f1 f2] real [f3] -> [A, real, B]; A = Reset(PSO de f1), draw, ClearState(PSO de f2), dispatch */
    k = rc_merge(&rq, (ID3D12CommandQueue *)&fk_queue, 4, in, 7, 0, &out);
    REQUIRE(k == 3);
    CHECK(out[1] == real);
    CHECK(!rc_is(out[0]) && !rc_is(out[2]) && out[0] != out[2]);
    CHECK_EQ(fk_reals, 2); CHECK_EQ(fk_allocs, 2); CHECK_EQ(fk_real_resets, 2);
    CHECK(L(out[0])->pso0 == (ID3D12PipelineState *)0x101);
    CHECK(!L(out[0])->open && !L(out[2])->open);       /* fechadas */
    n = ops_of((GCL2 *)out[0], ops, 16);
    CHECK_EQ(n, 3);
    CHECK_EQ(ops[0], RC_DrawInstanced); CHECK_EQ(ops[1], RC_ClearState); CHECK_EQ(ops[2], RC_Dispatch);
    {   /* o ClearState leva o PSO inicial da 2a gravadora */
        const rc_hdr *h = (const rc_hdr *)(L(out[0])->buf + ((const rc_hdr *)L(out[0])->buf)->size);
        CHECK(((const rc_a_ptr *)(h + 1))->p == (void *)0x102);
    }
    n = ops_of((GCL2 *)out[2], ops, 16);
    CHECK_EQ(n, 1); CHECK_EQ(ops[0], RC_DrawInstanced);
    /* GPU ainda em 6: as duas listas reais estao em voo -> uma nova */
    in[0] = (ID3D12CommandList *)f3;
    k = rc_merge(&rq, (ID3D12CommandQueue *)&fk_queue, 1, in, 8, 6, &out);
    CHECK_EQ(k, 1);
    CHECK_EQ(fk_reals, 3); CHECK_EQ(fk_alloc_resets, 0);
    /* GPU em 7: reusa uma das do 1o envio (allocator com Reset) */
    k = rc_merge(&rq, (ID3D12CommandQueue *)&fk_queue, 1, in, 9, 7, &out);
    CHECK_EQ(k, 1);
    CHECK_EQ(fk_reals, 3); CHECK_EQ(fk_alloc_resets, 1);
    /* a mesma gravadora duas vezes no mesmo envio: reproduzida duas vezes (reenvio e legal) */
    in[0] = in[1] = (ID3D12CommandList *)f3;
    k = rc_merge(&rq, (ID3D12CommandQueue *)&fk_queue, 2, in, 10, 9, &out);
    CHECK_EQ(k, 1);
    n = ops_of((GCL2 *)out[0], ops, 16);
    CHECK_EQ(n, 3); CHECK_EQ(ops[1], RC_ClearState);
    rc_on = 0;
    rc_inner_create = NULL;
    f1->lpVtbl->Release(f1); f2->lpVtbl->Release(f2); f3->lpVtbl->Release(f3);
}

/* Release: a gravadora volta ao pool e a proxima criacao a reaproveita aberta e vazia */
TEST(cmdrec_pool_reuses_released)
{
    GCL2 *a = rec_new(NULL), *b;
    REQUIRE(a);
    a->lpVtbl->DrawInstanced(a, 3, 1, 0, 0);
    a->lpVtbl->AddRef(a);
    CHECK_EQ(a->lpVtbl->Release(a), 1);
    CHECK_EQ(a->lpVtbl->Release(a), 0);
    b = rec_new((ID3D12PipelineState *)0x7);
    CHECK(b == a);
    CHECK_EQ(L(b)->ncmd, 0);
    CHECK(L(b)->open);
    CHECK(L(b)->pso0 == (ID3D12PipelineState *)0x7);
    b->lpVtbl->Release(b);
}

/* so o CreateCommandList DIRECT de dentro do 0x1400dee90 vira gravadora; instalacao confere a chamada do Azimuth */
TEST(cmdrec_setup_and_wants)
{
    uint8_t *az = fake_azimuth(2);
    void *inner = (void *)fk_inner_create;
    dbgini_set(NULL);
    log_capture();
    rc_setup(inner);
    CHECK(!rc_on);                                   /* bytes zerados: nao confere */
    CHECK(log_has("does not match"));
    memcpy(az + 0xdef6d, "\xff\x50\x60\xeb\x0c", 5);   /* call [rax+0x60]; jmp */
    rc_setup(inner);
    CHECK(rc_on);
    CHECK(rc_wants(D3D12_COMMAND_LIST_TYPE_DIRECT, az + 0xdef70));
    CHECK(!rc_wants(D3D12_COMMAND_LIST_TYPE_DIRECT, az + 0x91eac));
    CHECK(!rc_wants(D3D12_COMMAND_LIST_TYPE_COPY, az + 0xdef70));
    rc_setup(NULL);                                  /* sem o gancho do device */
    CHECK(!rc_on);
    dbgini_set("[Debug]\nListMerge=0\n");
    rc_setup(inner);
    CHECK(!rc_on);
    dbgini_set(NULL);
    fake_azimuth(0);                                 /* outro jogo: desligado */
    memcpy(g_base + 0xdef6d, "\xff\x50\x60\xeb\x0c", 5);
    rc_setup(inner);
    CHECK(!rc_on);
    CHECK(!rc_wants(D3D12_COMMAND_LIST_TYPE_DIRECT, g_base + 0xdef70));
    rc_inner_create = NULL;
}

/* a chamada conferida existe no azimuth.exe */
TEST(cmdrec_azimuth_signature)
{
    uint8_t *img = pe_map(AZIMUTH_EXE, NULL);
    if (!img) { test_skip("sem azimuth.exe"); return; }
    CHECK_HEX(img + 0xdef6d, "ff5060eb0c");
    pe_unmap(img);
}

/* private data por GUID (como no D3D12): grava, le, tamanho curto, troca, apaga */
TEST(cmdrec_private_data)
{
    static const GUID g1 = { 0x11111111, 0x2222, 0x3333, { 4, 4, 4, 4, 4, 4, 4, 4 } };
    GCL2 *a = rec_new(NULL);
    UINT n = 0, v = 0x1234, w = 0x5678, out = 0;
    REQUIRE(a);
    CHECK(a->lpVtbl->GetPrivateData(a, &g1, &n, NULL) == DXGI_ERROR_NOT_FOUND);
    CHECK(SUCCEEDED(a->lpVtbl->SetPrivateData(a, &g1, sizeof v, &v)));
    n = 0;
    CHECK(SUCCEEDED(a->lpVtbl->GetPrivateData(a, &g1, &n, NULL)));
    CHECK_EQ(n, sizeof v);
    n = 2;
    CHECK(a->lpVtbl->GetPrivateData(a, &g1, &n, &out) == DXGI_ERROR_MORE_DATA);
    CHECK(SUCCEEDED(a->lpVtbl->SetPrivateData(a, &g1, sizeof w, &w)));
    n = sizeof out;
    CHECK(SUCCEEDED(a->lpVtbl->GetPrivateData(a, &g1, &n, &out)));
    CHECK_EQ(out, 0x5678);
    CHECK(SUCCEEDED(a->lpVtbl->SetPrivateData(a, &g1, 0, NULL)));
    CHECK(a->lpVtbl->GetPrivateData(a, &g1, &n, NULL) == DXGI_ERROR_NOT_FOUND);
    a->lpVtbl->Release(a);
}

/* gravadora com SetPredication: a fronteira seguinte desliga a predicacao antes do ClearState */
TEST(cmdrec_merge_clears_predication)
{
    void *rq = NULL;
    GCL2 *f1 = rec_new(NULL), *f2 = rec_new(NULL);
    ID3D12CommandList *in[2], **out = NULL;
    uint16_t ops[8];
    int n;
    REQUIRE(f1 && f2);
    fk_qvt.GetDevice = fk_get_device;
    fk_dvt.Release = (void *)fk_release;
    fk_dvt.CreateCommandAllocator = fk_create_alloc;
    fk_allocs = 0;
    rc_inner_create = fk_inner_create;
    rc_on = 1;
    f1->lpVtbl->SetPredication(f1, (ID3D12Resource *)0x9, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    f1->lpVtbl->Close(f1);
    f2->lpVtbl->Close(f2);
    in[0] = (ID3D12CommandList *)f1; in[1] = (ID3D12CommandList *)f2;
    REQUIRE(rc_merge(&rq, (ID3D12CommandQueue *)&fk_queue, 2, in, 1, 0, &out) == 1);
    n = ops_of((GCL2 *)out[0], ops, 8);
    CHECK_EQ(n, 3);
    CHECK_EQ(ops[0], RC_SetPredication); CHECK_EQ(ops[1], RC_SetPredication); CHECK_EQ(ops[2], RC_ClearState);
    rc_on = 0;
    rc_inner_create = NULL;
    f1->lpVtbl->Release(f1); f2->lpVtbl->Release(f2);
}
