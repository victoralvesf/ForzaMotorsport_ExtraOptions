/*
 * Testes de common/shader.c: max3 do console (opcode 0x11a) -> dois max, container refeito e hash do DXBC.
 * Hashes esperados calculados com tools\dxbc.py (conferido contra ~7000 shaders reais de um dump do FM6).
 */
#include "../common/shader.c"
#include "test.h"

/* container DXBC {SHEX, STAT(4 bytes)} com o hash certo */
static size_t make_dxbc(uint8_t *out, const uint32_t *tok, size_t n)
{
    uint32_t hdr = 0x20 + 2 * 4, shex = hdr, stat = shex + 8 + (uint32_t)n * 4, total = stat + 12, v;
    memset(out, 0, total);
    memcpy(out, "DXBC", 4);
    v = 1; memcpy(out + 0x14, &v, 4);
    memcpy(out + 0x18, &total, 4);
    v = 2; memcpy(out + 0x1c, &v, 4);
    memcpy(out + 0x20, &shex, 4);
    memcpy(out + 0x24, &stat, 4);
    memcpy(out + shex, "SHEX", 4);
    v = (uint32_t)n * 4; memcpy(out + shex + 4, &v, 4);
    memcpy(out + shex + 8, tok, n * 4);
    memcpy(out + stat, "STAT", 4);
    v = 4; memcpy(out + stat + 4, &v, 4);
    v = 0xdeadbeef; memcpy(out + stat + 8, &v, 4);
    dxbc_checksum(out, total, out + 4);
    return total;
}

TEST(dxbc_checksum_matches_reference)
{
    uint8_t d[148], h[16];
    int i;
    for (i = 0; i < 148; i++) d[i] = (uint8_t)(i * 7 + 3);
    dxbc_checksum(d, 144, h);                                   /* resto 60 >= 56: dois blocos finais */
    CHECK_HEX(h, "3ad04d84eaae247bbd2a4ff49d53eb3b");
    dxbc_checksum(d, 148, h);                                   /* resto 0 */
    CHECK_HEX(h, "92f40506414668174c7615900f9394b2");
}

TEST(fm6_shader_max3_expanded)
{
    /* ps_5_0: dcl_temps 2; r1.w = max3(|r0.x|,|r0.y|,|r0.z|); r0.y = max3(|r0.y|,|r0.z|,|r0.w|) (dst e fonte);
       o0.x = max3_sat(|r1.w|, r0.y, |r0.x|) (saida: nao pode ser lida -> temporario r2); ret */
    static const uint32_t in[40] = {
        0x50, 40, 0x02000068, 2,
        0x0c00011a, 0x00100082, 1, 0x8010000a, 0x81, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0,
        0x0c00011a, 0x00100022, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0, 0x8010003a, 0x81, 0,
        0x0b00211a, 0x00102012, 0, 0x8010003a, 0x81, 1, 0x0010001a, 0, 0x8010000a, 0x81, 0,
        0x0100003e,
    };
    static const uint32_t want[55] = {
        0x50, 55, 0x02000068, 3,
        0x09000034, 0x00100082, 1, 0x8010000a, 0x81, 0, 0x8010001a, 0x81, 0,        /* max r1.w, |r0.x|, |r0.y| */
        0x08000034, 0x00100082, 1, 0x00100e46, 1, 0x8010002a, 0x81, 0,              /* max r1.w, r1, |r0.z| */
        0x09000034, 0x00100022, 0, 0x8010001a, 0x81, 0, 0x8010002a, 0x81, 0,        /* max r0.y, |r0.y|, |r0.z| */
        0x08000034, 0x00100022, 0, 0x00100e46, 0, 0x8010003a, 0x81, 0,              /* max r0.y, r0, |r0.w| */
        0x08000034, 0x00100012, 2, 0x8010003a, 0x81, 1, 0x0010001a, 0,              /* max r2.x, |r1.w|, r0.y */
        0x08002034, 0x00102012, 0, 0x00100e46, 2, 0x8010000a, 0x81, 0,              /* max_sat o0.x, r2, |r0.x| */
        0x0100003e,
    };
    static uint8_t src[512], exp[512];
    size_t n = make_dxbc(src, in, 40), en = make_dxbc(exp, want, 55), on = 0;
    uint8_t *out;
    CHECK_EQ(n, 220);
    CHECK_HEX(src + 4, "78a196928bce85edcfbb26bcd8205573");
    out = shader_console_rewrite(src, n, &on);
    REQUIRE(out);
    CHECK_EQ(on, 280);
    CHECK_EQ(en, 280);
    CHECK(!memcmp(out, exp, 280));                              /* tokens, tamanhos, offset do STAT */
    CHECK_HEX(out + 4, "713e5fad23b5fe896d41e683c0d56ca4");
    CHECK_HEX(out + 0x24, "0c010000");                          /* STAT 0xd0 deslocado 60 bytes */
    free(out);
}

TEST(fm6_shader_without_max3_untouched)
{
    static const uint32_t in[6] = { 0x50, 6, 0x02000068, 1, 0x0100003e, 0x0100003e };
    static uint8_t src[256];
    size_t n = make_dxbc(src, in, 6), on = 0;
    CHECK(shader_console_rewrite(src, n, &on) == NULL);
    CHECK(shader_console_rewrite("DXBX", 4, &on) == NULL);
    src[0x18] ^= 1;                                             /* tamanho total errado */
    CHECK(shader_console_rewrite(src, n, &on) == NULL);
}

/* ------------------------------------------------------------------------ */
/* alpha-to-coverage sem dithering (shader_a2c): shaders compilados pelo d3dcompiler_47 do Windows, conferidos */
/* no disassembly dele e num PSO MSAA 2x do D3D12 (WARP), que valida o hash e as assinaturas                      */
/* ------------------------------------------------------------------------ */
#define COBJMACROS
#include <d3d12.h>
#include <dxgi1_4.h>

typedef struct blob_vt { void *qi, *addref; ULONG (STDMETHODCALLTYPE *release)(void *);
                         void *(STDMETHODCALLTYPE *ptr)(void *); SIZE_T (STDMETHODCALLTYPE *size)(void *); } blob_vt;
typedef struct { const blob_vt *vt; } blob_t;
typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *, const char *,
                                     UINT, UINT, blob_t **, blob_t **);
typedef HRESULT (WINAPI *disasm_fn)(const void *, SIZE_T, UINT, const char *, blob_t **);
static compile_fn d3d_compile;
static disasm_fn d3d_disasm;

static int load_compiler(void)
{
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    if (!m) return 0;
    d3d_compile = (compile_fn)GetProcAddress(m, "D3DCompile");
    d3d_disasm = (disasm_fn)GetProcAddress(m, "D3DDisassemble");
    return d3d_compile && d3d_disasm;
}

/* compila (malloc); NULL se falhar */
static uint8_t *compile(const char *src, const char *profile, size_t *n)
{
    blob_t *code = NULL, *err = NULL;
    uint8_t *r = NULL;
    if (d3d_compile(src, strlen(src), NULL, NULL, NULL, "main", profile, 0, 0, &code, &err) >= 0 && code) {
        *n = code->vt->size(code);
        r = malloc(*n);
        memcpy(r, code->vt->ptr(code), *n);
    }
    if (code) code->vt->release(code);
    if (err) err->vt->release(err);
    return r;
}

static char *disasm(const uint8_t *b, size_t n)
{
    blob_t *t = NULL;
    char *r = NULL;
    if (d3d_disasm(b, n, 0, NULL, &t) >= 0 && t) {
        size_t k = t->vt->size(t);
        r = malloc(k + 1);
        memcpy(r, t->vt->ptr(t), k);
        r[k] = 0;
        t->vt->release(t);
    }
    return r;
}

static int count_str(const char *s, const char *what)
{
    int n = 0;
    for (; s && (s = strstr(s, what)); s++) n++;
    return n;
}

static const char a2c_vs[] =
    "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
    "V main(uint id : SV_VertexID) { V v; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  v.p = float4(uv * 2 - 1, 0, 1); v.c = float4(uv, 0, 0.5); return v; }\n";
static const char a2c_ps[] =       /* dois ret no programa principal e o0 escrito em partes */
    "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target {\n"
    "  [branch] if (c.x > 0.9) return float4(1, 0, 0, c.w);\n"
    "  float4 r; r.xy = c.yx; r.zw = c.zw * 0.5; return r; }\n";
static const char a2c_ps_cov[] =   /* ja escreve SV_Coverage: nao mexe */
    "struct O { float4 c : SV_Target0; uint m : SV_Coverage; };\n"
    "O main(float4 p : SV_Position, float4 c : COLOR0) { O o; o.c = c; o.m = 1; return o; }\n";
static const char a2c_ps_notemps[] =   /* sem temporarios: o compilador escreve o0 direto da entrada */
    "float4 main(float4 p : SV_Position, float4 c : COLOR0) : SV_Target { return c; }\n";

/* cria um PSO MSAA 2x no WARP; 1 = S_OK, 0 = falhou, -1 = sem D3D12/WARP */
static int warp_pso(const uint8_t *vs, size_t nvs, const uint8_t *ps, size_t nps, BOOL a2c)
{
    static const GUID iid_factory4 = { 0x1bc6ea02, 0xef36, 0x464f, { 0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a } };
    static const GUID iid_adapter = { 0x2411e7e1, 0x12ac, 0x4ccf, { 0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0 } };
    static const GUID iid_device = { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
    static const GUID iid_rs = { 0xc54a6b66, 0x72df, 0x4ee8, { 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14 } };
    static const GUID iid_pso = { 0x765a30f3, 0xf624, 0x4c6f, { 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45 } };
    typedef HRESULT (WINAPI *factory_fn)(REFIID, void **);
    typedef HRESULT (WINAPI *device_fn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    typedef HRESULT (WINAPI *ser_fn)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
    static ID3D12Device *dev;
    static ID3D12RootSignature *rs;
    static int state;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d;
    ID3D12PipelineState *pso = NULL;
    HRESULT hr;
    if (!state) {
        HMODULE dx = LoadLibraryA("dxgi.dll"), d12 = LoadLibraryA("d3d12.dll");
        factory_fn cf = dx ? (factory_fn)GetProcAddress(dx, "CreateDXGIFactory1") : NULL;
        device_fn cd = d12 ? (device_fn)GetProcAddress(d12, "D3D12CreateDevice") : NULL;
        ser_fn sr = d12 ? (ser_fn)GetProcAddress(d12, "D3D12SerializeRootSignature") : NULL;
        IDXGIFactory4 *f = NULL;
        IDXGIAdapter *a = NULL;
        ID3DBlob *b = NULL;
        D3D12_ROOT_SIGNATURE_DESC rd = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        state = -1;
        if (cf && cd && sr && cf(&iid_factory4, (void **)&f) >= 0) {
            if (IDXGIFactory4_EnumWarpAdapter(f, &iid_adapter, (void **)&a) >= 0 &&
                cd((IUnknown *)a, D3D_FEATURE_LEVEL_11_0, &iid_device, (void **)&dev) >= 0 &&
                sr(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &b, NULL) >= 0 &&
                ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b),
                                                 &iid_rs, (void **)&rs) >= 0)
                state = 1;
            if (b) ID3D10Blob_Release(b);
            if (a) IDXGIAdapter_Release(a);
            IDXGIFactory4_Release(f);
        }
    }
    if (state < 0) return -1;
    memset(&d, 0, sizeof d);
    d.pRootSignature = rs;
    d.VS.pShaderBytecode = vs; d.VS.BytecodeLength = nvs;
    d.PS.pShaderBytecode = ps; d.PS.BytecodeLength = nps;
    d.BlendState.AlphaToCoverageEnable = a2c;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    d.SampleMask = 0xffffffff;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 2;
    hr = ID3D12Device_CreateGraphicsPipelineState(dev, &d, &iid_pso, (void **)&pso);
    if (pso) ID3D12PipelineState_Release(pso);
    return hr >= 0;
}

TEST(shader_a2c_coverage_from_alpha)
{
    size_t nvs, nps, nout, nout2 = 0, ncov, nnt;
    uint8_t *vs, *ps, *out, *cov, *nt, *nt2;
    char *t;
    int w;
    if (!load_compiler()) { test_skip("d3dcompiler_47.dll ausente"); return; }
    vs = compile(a2c_vs, "vs_5_0", &nvs);
    ps = compile(a2c_ps, "ps_5_0", &nps);
    cov = compile(a2c_ps_cov, "ps_5_0", &ncov);
    nt = compile(a2c_ps_notemps, "ps_5_0", &nnt);
    CHECK(vs && ps && cov && nt);
    if (!vs || !ps || !cov || !nt) return;
    CHECK(!shader_a2c(vs, nvs, 2, &nout));                    /* so pixel shader */
    CHECK(!shader_a2c(ps, nps, 1, &nout));                    /* sem MSAA */
    CHECK(!shader_a2c(cov, ncov, 2, &nout));                  /* ja escreve a cobertura */
    out = shader_a2c(ps, nps, 2, &nout);
    CHECK(out != NULL);
    if (!out) return;
    CHECK_EQ(nout, nps + 36 + 4 * 2 + 37 * 4 * 2);               /* OSGN +36, dcl oMask, 2 epilogos */
    t = disasm(out, nout);
    CHECK(t != NULL);
    CHECK(t && strstr(t, "SV_Coverage") && strstr(t, "dcl_output oMask"));
    CHECK_EQ(count_str(t, "iadd oMask"), 2);                       /* um epilogo por ret */
    CHECK(t && strstr(t, "l(2.000000), l(0.500000)") && strstr(t, "mov o0.xyzw, r"));
    CHECK(t && !strstr(t, "mov o0.xy, ") && !strstr(t, "mul o0.zw"));   /* escritas parciais foram para r0 */
    free(t);
    /* o primeiro token do epilogo: mov o0.xyzw, r0.xyzw */
    {
        static const uint32_t ep[4] = { 0x05000036, 0x001020f2, 0, 0x00100e46 };
        size_t k, found = 0;
        for (k = 0; k + 4 <= nout / 4; k++) if (!memcmp((uint32_t *)out + k, ep, sizeof ep)) found++;
        CHECK_EQ(found, 2);
    }
    nt2 = shader_a2c(nt, nnt, 4, &nout2);
    CHECK(nt2 != NULL);
    if (nt2) {
        t = disasm(nt2, nout2);
        CHECK(t && strstr(t, "dcl_temps 2") && strstr(t, "l(4.000000)"));
        free(t);
    }
    w = warp_pso(vs, nvs, ps, nps, TRUE);
    if (w < 0) test_skip("D3D12/WARP ausente: PSO nao conferido");
    else {
        CHECK_EQ(w, 1);                                            /* original com A2C */
        CHECK_EQ(warp_pso(vs, nvs, out, nout, FALSE), 1);          /* reescrito, A2C desligado */
        free(nt2);
        nt2 = shader_a2c(nt, nnt, 2, &nout2);
        CHECK(nt2 && warp_pso(vs, nvs, nt2, nout2, FALSE) == 1);
        out[0x30] ^= 1;                                            /* hash errado: o D3D12 recusa */
        CHECK_EQ(warp_pso(vs, nvs, out, nout, FALSE), 0);
    }
    free(out); free(nt2); free(vs); free(ps); free(cov); free(nt);
}
