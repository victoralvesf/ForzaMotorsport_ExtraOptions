/*
 * Testes de gpuguard_fm6.c: deteccao do veneno 0xdeadbeef em enderecos de GPU, bitmap de slots gravados por heap
 * de descritores, tamanho das tabelas da root signature e root signature atual por command list.
 */
#include "../gpuguard_fm6.c"
#include "test.h"

TEST(gpuguard_va_poisoned)
{
    CHECK(gg_va_poisoned(0xdeadbeefdeadbeefull));    /* inteiro: fora de 48 bits */
    CHECK(gg_va_poisoned(0xbeefdeadbeefull));        /* truncado a 48 bits (DRED: pagina 0xbeefdeadb000) */
    CHECK(gg_va_poisoned(0xbeefdeaeb000ull));        /* + indice * stride */
    CHECK(!gg_va_poisoned(0x80d0d2800ull));          /* placement buffer real do FM6 */
    CHECK(!gg_va_poisoned(0x7ff812340000ull));
    CHECK(!gg_va_poisoned(0xbeef00000000ull));
    CHECK(!gg_va_poisoned(0));
}

TEST(gpuguard_heap_slots)
{
    static uint8_t mem[64 * 32], mem2[16 * 32];
    uintptr_t a = (uintptr_t)mem;
    gg_heap_reset();
    CHECK_EQ(gg_written(a), (unsigned long long)-1);   /* fora de heap conhecido: sem opiniao */
    gg_heap_add(a, 64, 32, 0);
    CHECK_EQ(gg_written(a + 5 * 32), 0);
    gg_mark(a + 4 * 32, 3);                             /* slots 4..6 */
    CHECK_EQ(gg_written(a + 3 * 32), 0);
    CHECK_EQ(gg_written(a + 4 * 32), 1);
    CHECK_EQ(gg_written(a + 6 * 32), 1);
    CHECK_EQ(gg_written(a + 7 * 32), 0);
    gg_mark(a + 62 * 32, 5);                            /* passa do fim: so 62..63 */
    CHECK_EQ(gg_written(a + 63 * 32), 1);
    CHECK_EQ(gg_written(a + 64 * 32), (unsigned long long)-1);
    gg_heap_add((uintptr_t)mem2, 16, 32, 0);
    CHECK_EQ(gg_written(a + 4 * 32), 1);                /* outro heap nao mexe neste */
    gg_heap_add(a + 32 * 32, 8, 32, 0);                 /* sobrepoe: memoria reaproveitada -> heap antigo sai */
    CHECK_EQ(gg_written(a + 4 * 32), (unsigned long long)-1);
    CHECK_EQ(gg_written(a + 32 * 32), 0);
    gg_heap_reset();
}

TEST(gpuguard_gpu_handle_to_cpu)
{
    static uint8_t mem[16 * 32];
    gg_heap_reset();
    gg_heap_add((uintptr_t)mem, 16, 32, 0x10000000ull);
    CHECK_EQ(gg_gpu_to_cpu(0x10000000ull + 3 * 32), (uintptr_t)mem + 3 * 32);
    CHECK_EQ(gg_gpu_to_cpu(0x10000000ull + 16 * 32), 0);   /* fim do heap */
    CHECK_EQ(gg_gpu_to_cpu(0x0fffffe0ull), 0);
    gg_heap_reset();
}

TEST(gpuguard_table_size)
{
    D3D12_DESCRIPTOR_RANGE1 r[3] = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 14, 0, 0, 0, 0 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },   /* 14..21 */
        { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0, 0, 0, 30 },                                    /* 30..31 */
    };
    D3D12_ROOT_DESCRIPTOR_TABLE1 t = { 2, r };
    CHECK_EQ(gg_table_size(&t), 22);
    t.NumDescriptorRanges = 3;
    CHECK_EQ(gg_table_size(&t), 32);
    r[1].NumDescriptors = UINT_MAX;                     /* sem limite: nao conferida */
    CHECK_EQ(gg_table_size(&t), 0);
    r[1].NumDescriptors = 8;
    r[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    CHECK_EQ(gg_table_size(&t), 0);
}

TEST(gpuguard_root_signature_per_list)
{
    static int rs1, rs2, cl1, cl2;
    uint32_t size[3] = { 0, 22, 4 };
    gg_rs_add(&rs1, size, 3);
    CHECK_EQ(gg_rs_table(&rs1, 1), 22);
    CHECK_EQ(gg_rs_table(&rs1, 2), 4);
    CHECK_EQ(gg_rs_table(&rs1, 3), 0);                  /* parametro alem do fim */
    CHECK_EQ(gg_rs_table(&rs2, 1), 0);                  /* root signature desconhecida */
    CHECK_EQ(gg_rs_table(NULL, 1), 0);
    gg_cl_rs(&cl1, 1, &rs1, 1);
    gg_cl_rs(&cl1, 0, &rs2, 1);
    CHECK(gg_cl_rs(&cl1, 1, NULL, 0) == &rs1);
    CHECK(gg_cl_rs(&cl1, 0, NULL, 0) == &rs2);
    CHECK(gg_cl_rs(&cl2, 1, NULL, 0) == NULL);          /* lista sem root signature */
    gg_cl_rs(&cl1, 1, &rs2, 1);
    CHECK(gg_cl_rs(&cl1, 1, NULL, 0) == &rs2);
}

TEST(gpuguard_trace_per_list)
{
    static int cl1, cl2, rs, pso;
    void *fa[2] = { (void *)0x140001000, (void *)0x140002000 }, *fb[1] = { (void *)0x140003000 };
    uint32_t arg[3] = { 8, 8, 1 }, dropped = 99, i;
    gg_site s[GG_SITES];
    gg_trace_add(&cl1, GG_OP_DISPATCH, &rs, &pso, arg, fa, 2);
    gg_trace_add(&cl1, GG_OP_DISPATCH, &rs, NULL, arg, fa, 2); /* mesmo ponto: so conta */
    gg_trace_add(&cl1, GG_OP_COPY_TEXTURE, NULL, NULL, arg, fa, 2); /* mesma pilha, outra operacao */
    gg_trace_add(&cl1, GG_OP_DISPATCH, NULL, NULL, arg, fa, 2); /* outra root signature */
    gg_trace_add(&cl2, GG_OP_DISPATCH, &rs, NULL, arg, fb, 1);
    CHECK_EQ(gg_trace_get(&cl1, s, GG_SITES, &dropped), 3);
    CHECK_EQ(dropped, 0);
    CHECK_EQ(s[0].count, 2);
    CHECK_EQ(s[0].op, GG_OP_DISPATCH);
    CHECK(s[0].rs == &rs);
    CHECK(s[0].pso == &pso);                                  /* PSO da 1a ocorrencia */
    CHECK_EQ(s[0].nframes, 2);
    CHECK(s[0].frames[1] == (void *)0x140002000);
    CHECK_EQ(s[0].arg[0], 8);
    CHECK_EQ(s[1].op, GG_OP_COPY_TEXTURE);
    CHECK(s[2].rs == NULL);
    CHECK_EQ(gg_trace_get(&cl2, s, GG_SITES, NULL), 1);
    for (i = 0; i < GG_SITES + 3; i++) {                       /* pontos alem do limite: so contados */
        void *f[1] = { (void *)(uintptr_t)(0x140100000 + i * 16) };
        gg_trace_add(&cl2, GG_OP_DISPATCH, &rs, NULL, arg, f, 1);
    }
    CHECK_EQ(gg_trace_get(&cl2, s, GG_SITES, &dropped), GG_SITES);
    CHECK_EQ(dropped, 4);
    gg_trace_reset(&cl1);                                     /* Reset da lista: comeca de novo */
    CHECK_EQ(gg_trace_get(&cl1, s, GG_SITES, NULL), 0);
    CHECK_EQ(gg_trace_get(&rs, s, GG_SITES, NULL), 0);        /* lista desconhecida */
}

TEST(gpuguard_internal_rs_shape)
{
    D3D12_DESCRIPTOR_RANGE1 r[2] = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0, 0 },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
    };
    D3D12_ROOT_PARAMETER1 p[2] = { 0 };
    D3D12_ROOT_SIGNATURE_DESC1 d = { 2, p, 0, NULL, 0 };
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants.Num32BitValues = 12;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[1].DescriptorTable.NumDescriptorRanges = 2;
    p[1].DescriptorTable.pDescriptorRanges = r;
    CHECK(gg_is_internal_rs(&d));
    p[0].Constants.Num32BitValues = 2;                  /* outra quantidade de constantes */
    CHECK(!gg_is_internal_rs(&d));
    p[0].Constants.Num32BitValues = 12;
    r[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;   /* sem UAV */
    CHECK(!gg_is_internal_rs(&d));
    r[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    d.NumParameters = 1;
    CHECK(!gg_is_internal_rs(&d));
}

/* CopyTextureRegion: subrecurso, footprint dentro do buffer, box e regiao de destino (gg_copy_check) */
static D3D12_RESOURCE_DESC tex_desc(D3D12_RESOURCE_DIMENSION dim, UINT64 w, UINT h, UINT16 da, UINT16 mips, DXGI_FORMAT f)
{
    D3D12_RESOURCE_DESC d;
    memset(&d, 0, sizeof d);
    d.Dimension = dim; d.Width = w; d.Height = h; d.DepthOrArraySize = da; d.MipLevels = mips; d.Format = f;
    d.SampleDesc.Count = 1;
    return d;
}

TEST(gpuguard_copy_check)
{
    D3D12_RESOURCE_DESC cube = tex_desc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 256, 256, 6, 9, DXGI_FORMAT_R16G16B16A16_FLOAT);
    D3D12_RESOURCE_DESC buf = tex_desc(D3D12_RESOURCE_DIMENSION_BUFFER, 65536, 1, 1, 1, DXGI_FORMAT_UNKNOWN);
    D3D12_RESOURCE_DESC bc = tex_desc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 32, 32, 1, 6, DXGI_FORMAT_BC1_UNORM);
    D3D12_RESOURCE_DESC vol = tex_desc(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 32, 32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_BOX box = { 0, 0, 0, 16, 16, 1 };
    char why[200];
    memset(&dst, 0, sizeof dst); memset(&src, 0, sizeof src);
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    src.PlacedFootprint.Footprint.Width = 64; src.PlacedFootprint.Footprint.Height = 64;
    src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = 512;
    /* face 5, mip 2 (64x64) = subrecurso 47 de 54: ok */
    dst.SubresourceIndex = 5 * 9 + 2;
    CHECK(!gg_copy_check(&cube, &dst, 0, 0, 0, &buf, &src, NULL, why));
    /* subrecurso 54 nao existe */
    dst.SubresourceIndex = 54;
    CHECK(gg_copy_check(&cube, &dst, 0, 0, 0, &buf, &src, NULL, why));
    CHECK(strstr(why, "destino: subrecurso 54 >= 54") != NULL);
    /* mip 3 (32x32) nao cabe 64x64 */
    dst.SubresourceIndex = 3;
    CHECK(gg_copy_check(&cube, &dst, 0, 0, 0, &buf, &src, NULL, why));
    CHECK(strstr(why, "fora de 32x32x1") != NULL);
    /* com box 16x16 cabe em (16,16) do mip 3, nao em (17,0) */
    CHECK(!gg_copy_check(&cube, &dst, 16, 16, 0, &buf, &src, &box, why));
    CHECK(gg_copy_check(&cube, &dst, 17, 0, 0, &buf, &src, &box, why));
    /* footprint passando do fim do buffer: 512 * 64 = 32768 a partir de 40960 */
    dst.SubresourceIndex = 2;
    src.PlacedFootprint.Offset = 40960;
    CHECK(gg_copy_check(&cube, &dst, 0, 0, 0, &buf, &src, NULL, why));
    CHECK(strstr(why, "origem: footprint fora do buffer") != NULL);
    src.PlacedFootprint.Offset = 32768;                 /* termina exatamente no fim */
    CHECK(!gg_copy_check(&cube, &dst, 0, 0, 0, &buf, &src, NULL, why));
    /* footprint num recurso que nao e buffer */
    CHECK(gg_copy_check(&cube, &dst, 0, 0, 0, &cube, &src, NULL, why));
    CHECK(strstr(why, "nao e buffer") != NULL);
    /* BC: mip 5 de 32x32 = 1x1, copia de bloco 4x4 inteiro e valida */
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_BC1_UNORM;
    src.PlacedFootprint.Footprint.Width = 4; src.PlacedFootprint.Footprint.Height = 4;
    src.PlacedFootprint.Footprint.RowPitch = 256;
    dst.SubresourceIndex = 5;
    CHECK(!gg_copy_check(&bc, &dst, 0, 0, 0, &buf, &src, NULL, why));
    dst.SubresourceIndex = 6;
    CHECK(gg_copy_check(&bc, &dst, 0, 0, 0, &buf, &src, NULL, why));
    /* 3D: profundidade conta para a regiao, nao para o numero de subrecursos */
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 32; src.PlacedFootprint.Footprint.Height = 32;
    src.PlacedFootprint.Footprint.Depth = 32; src.PlacedFootprint.Footprint.RowPitch = 256;
    dst.SubresourceIndex = 0;
    CHECK(gg_copy_check(&vol, &dst, 0, 0, 0, &buf, &src, NULL, why));   /* 256*32*32 = 262144 > 65536 */
    buf.Width = 262144;
    CHECK(!gg_copy_check(&vol, &dst, 0, 0, 0, &buf, &src, NULL, why));
    CHECK(gg_copy_check(&vol, &dst, 0, 0, 1, &buf, &src, NULL, why));
    dst.SubresourceIndex = 1;
    CHECK(gg_copy_check(&vol, &dst, 0, 0, 0, &buf, &src, NULL, why));
    /* textura -> textura (subrecurso nos dois lados), box vazio = no-op */
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    dst.SubresourceIndex = 0;
    CHECK(!gg_copy_check(&cube, &dst, 0, 0, 0, &cube, &src, NULL, why));
    box.right = 0;
    CHECK(!gg_copy_check(&cube, &dst, 0, 0, 0, &cube, &src, &box, why));
}

/* D3D12Debug=7 (gg_trace_on 5): o CopyTextureRegion e registrado mas nao chega ao D3D12 */
static int real_copies;
static void STDMETHODCALLTYPE fake_copytex(ID3D12GraphicsCommandList *l, const D3D12_TEXTURE_COPY_LOCATION *d, UINT x, UINT y,
                                           UINT z, const D3D12_TEXTURE_COPY_LOCATION *s, const D3D12_BOX *b)
{
    (void)l; (void)d; (void)x; (void)y; (void)z; (void)s; (void)b;
    real_copies++;
}

TEST(gpuguard_debug7_skips_copies)
{
    static int cl;
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    int saved = gg_trace_on;
    memset(&dst, 0, sizeof dst); memset(&src, 0, sizeof src);   /* recursos nulos: so relatorio, sem GetDesc */
    real_copytex = fake_copytex;
    log_capture();
    gg_trace_on = 1;
    hook_copytex((ID3D12GraphicsCommandList *)&cl, &dst, 0, 0, 0, &src, NULL);
    CHECK_EQ(real_copies, 1);
    gg_trace_on = 5;
    hook_copytex((ID3D12GraphicsCommandList *)&cl, &dst, 0, 0, 0, &src, NULL);
    hook_copytex((ID3D12GraphicsCommandList *)&cl, &dst, 0, 0, 0, &src, NULL);
    CHECK_EQ(real_copies, 1);
    CHECK(log_has("gpuguard: experimento: CopyTextureRegion #1 pulado"));
    CHECK(log_has("gpuguard: experimento: CopyTextureRegion #2 pulado"));
    gg_trace_reset(&cl);
    gg_trace_on = saved;
}

/* D3D12Debug=8 (gg_trace_on 6): draws registrados (com root signature grafica e PSO) mas fora da GPU;
   D3D12Debug=9 (gg_trace_on 7): so o Dispatch da compute interna passa */
static int real_draws, real_dispatches;
static void STDMETHODCALLTYPE fake_draw(ID3D12GraphicsCommandList *l, UINT a, UINT b, UINT c, UINT d)
{
    (void)l; (void)a; (void)b; (void)c; (void)d;
    real_draws++;
}
static void STDMETHODCALLTYPE fake_draw_indexed(ID3D12GraphicsCommandList *l, UINT a, UINT b, UINT c, INT d, UINT e)
{
    (void)l; (void)a; (void)b; (void)c; (void)d; (void)e;
    real_draws++;
}
static void STDMETHODCALLTYPE fake_set_pso(ID3D12GraphicsCommandList *l, ID3D12PipelineState *ps) { (void)l; (void)ps; }
static void STDMETHODCALLTYPE fake_dispatch(ID3D12GraphicsCommandList *l, UINT x, UINT y, UINT z)
{
    (void)l; (void)x; (void)y; (void)z;
    real_dispatches++;
}

TEST(gpuguard_draw_trace_and_debug8)
{
    static int cl, grs, pso;
    gg_site s[GG_SITES];
    int saved = gg_trace_on;
    ID3D12GraphicsCommandList *l = (ID3D12GraphicsCommandList *)&cl;
    real_draw = fake_draw; real_draw_indexed = fake_draw_indexed; real_set_pso = fake_set_pso;
    log_capture();
    gg_trace_on = 1;
    gg_cl_rs(&cl, 0, &grs, 1);
    hook_set_pso(l, (ID3D12PipelineState *)&pso);
    CHECK(gg_cl_rs(&cl, GG_CL_PSO, NULL, 0) == &pso);
    CHECK(gg_cl_rs(&cl, 0, NULL, 0) == &grs);                 /* PSO nao apaga a root signature */
    hook_draw(l, 3, 1, 0, 0);
    hook_draw_indexed(l, 36, 2, 6, 0, 0);
    CHECK_EQ(real_draws, 2);
    CHECK_EQ(gg_trace_get(&cl, s, GG_SITES, NULL), 2);
    CHECK_EQ(s[0].op, GG_OP_DRAW);
    CHECK(s[0].rs == &grs);
    CHECK(s[0].pso == &pso);
    CHECK_EQ(s[0].arg[0], 3);
    CHECK_EQ(s[1].op, GG_OP_DRAW_INDEXED);
    CHECK_EQ(s[1].arg[0], 36);
    CHECK_EQ(s[1].arg[2], 6);
    CHECK(log_has("gpuguard: ponto novo de DrawInstanced (3,1,0)"));
    gg_trace_on = 6;
    hook_draw(l, 3, 1, 0, 0);
    hook_draw_indexed(l, 36, 2, 6, 0, 0);
    CHECK_EQ(real_draws, 2);
    CHECK(log_has("gpuguard: experimento: draw do replayer #1 pulado"));   /* pilha fora do jogo */
    CHECK(log_has("gpuguard: experimento: draw do replayer #2 pulado"));
    gg_trace_reset(&cl);
    gg_trace_on = saved;
}

TEST(gpuguard_debug9_skips_game_dispatch)
{
    static int cl, game_rs, internal;
    int saved = gg_trace_on;
    void *saved_internal = internal_rs;
    ID3D12GraphicsCommandList *l = (ID3D12GraphicsCommandList *)&cl;
    real_dispatch = fake_dispatch;
    internal_rs = &internal;
    log_capture();
    gg_trace_on = 7;
    gg_cl_rs(&cl, 1, &game_rs, 1);
    hook_dispatch(l, 4, 4, 1);
    CHECK_EQ(real_dispatches, 0);
    CHECK(log_has("gpuguard: experimento: Dispatch fora da compute interna #1 pulado"));
    gg_cl_rs(&cl, 1, &internal, 1);
    hook_dispatch(l, 30, 1080, 1);
    CHECK_EQ(real_dispatches, 1);
    gg_trace_reset(&cl);
    internal_rs = saved_internal;
    gg_trace_on = saved;
}

/* DrawSkip (com D3D12Debug=8): 0 todos, 1 so os diretos (pilha no jogo), 2 so os do replayer */
TEST(gpuguard_draw_skip_classes)
{
    int saved = gg_trace_on, saved_skip = gg_draw_skip;
    log_capture();
    gg_trace_on = 1; gg_draw_skip = 0;
    CHECK(!gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    gg_trace_on = 6;
    CHECK(gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    CHECK(gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    gg_draw_skip = 1;
    CHECK(gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    gg_draw_skip = 2;
    CHECK(!gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    CHECK(gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    gg_draw_skip = 4;                                          /* so DrawInstanced do replayer */
    CHECK(gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_EXECUTE_INDIRECT, 0, 0));
    CHECK(!gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    gg_draw_skip = 5;
    CHECK(gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW, 0, 0));
    gg_draw_skip = 3;
    CHECK(gg_skip_draw(0, GG_OP_EXECUTE_INDIRECT, 0, 0));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0));
    CHECK(log_has("gpuguard: experimento: draw direto #"));
    gg_trace_on = saved; gg_draw_skip = saved_skip;
}

/* anel dos ultimos draws: ordem, sobrescrita e o que cada entrada guarda */
TEST(gpuguard_draw_ring)
{
    static int cl, rs, pso;
    static gg_ring_ent e[GG_RING];
    uint32_t arg[3] = { 0, 1, 2 }, i, n, base = (uint32_t)ring_seq;
    for (i = 0; i < GG_RING + 5; i++) {
        arg[0] = i;
        gg_ring_add(&cl, GG_OP_DRAW_INDEXED, i & 1, &rs, &pso, arg);
    }
    n = gg_ring_get(e, GG_RING);
    CHECK_EQ(n, GG_RING);
    CHECK_EQ(e[0].arg[0], 5);                     /* as 5 mais antigas foram sobrescritas */
    CHECK_EQ(e[n - 1].arg[0], GG_RING + 4);
    CHECK_EQ(e[n - 1].seq, base + GG_RING + 5);
    CHECK_EQ(e[n - 1].direct, 0);
    CHECK_EQ(e[n - 2].direct, 1);
    CHECK(e[0].pso == &pso && e[0].rs == &rs && e[0].cl == &cl);
    CHECK_EQ(gg_ring_get(e, 3), 3);               /* so as 3 ultimas */
    CHECK_EQ(e[2].arg[0], GG_RING + 4);
    CHECK_EQ(e[0].arg[0], GG_RING + 2);
    log_capture();
    gg_ring_dump();
    CHECK(log_has("DrawIndexedInstanced replayer (132,1,2)"));
}

/* entrada do IA: slots do input layout (desc e stream), estado de VB/IB por lista e o que falta no draw */
TEST(gpuguard_ia_missing_slots)
{
    static int cl, pso, pso2;
    static D3D12_INPUT_ELEMENT_DESC el[3];
    D3D12_INPUT_LAYOUT_DESC il = { el, 3 };
    D3D12_VERTEX_BUFFER_VIEW vb[2] = { { 0x1000, 64, 16 }, { 0, 0, 0 } };
    struct {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t0; uint32_t pad0; void *rs;                  /* 0: ponteiro */
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t1; uint32_t pad1; D3D12_SHADER_BYTECODE vs;  /* 8 + 16 */
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t2; UINT mask;                                 /* SAMPLE_MASK: 4 + 4 */
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t3; uint32_t pad3; D3D12_INPUT_LAYOUT_DESC il;
    } st;
    el[0].InputSlot = 0; el[1].InputSlot = 0; el[2].InputSlot = 2;
    CHECK_EQ(gg_layout_slots(&il), 5);
    CHECK_EQ(gg_layout_slots(NULL), 0);
    memset(&st, 0, sizeof st);
    st.t0 = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    st.t1 = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
    st.t2 = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
    st.t3 = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT;
    st.il = il;
    CHECK_EQ(offsetof(__typeof__(st), il), 56);
    CHECK(gg_stream_layout(&st, sizeof st) == &st.il);
    CHECK(gg_stream_layout(&st, 48) == NULL);             /* stream acaba antes do input layout */
    st.t2 = 23;                                            /* subobjeto desconhecido: para */
    CHECK(gg_stream_layout(&st, sizeof st) == NULL);

    gg_pso_add(&pso, &il, 0x1234, 0x303, &cl);
    gg_pso_add(&pso2, NULL, 0, 0, NULL);
    CHECK(gg_pso_rs(&pso) == &cl && gg_pso_rs(&pso2) == NULL && gg_pso_rs(&il) == NULL);
    CHECK_EQ(gg_pso_slots(&pso), 5);
    CHECK_EQ(gg_pso_slots(&pso2), 0);
    CHECK_EQ(gg_pso_slots(&cl), -1);
    CHECK_EQ(gg_pso_info(&pso), 0x303);
    CHECK_EQ(gg_pso_info(&cl), 0);
    CHECK_EQ(gg_ia_missing(&cl, &cl, 1), 0);               /* PSO desconhecido: sem opiniao */
    CHECK_EQ(gg_ia_missing(&cl, &pso, 0), 5);              /* lista sem nada ligado */
    gg_ia_state(&cl, 1, 0, 2, vb, 0);                      /* slot 0 com buffer, slot 1 vazio */
    CHECK_EQ(gg_ia_missing(&cl, &pso, 0), 4);
    CHECK_EQ(gg_ia_missing(&cl, &pso, 1), 0x80000004u);    /* indexado sem index buffer */
    gg_ia_state(&cl, 1, 2, 1, vb, 0);
    gg_ia_state(&cl, 2, 0, 0, NULL, 1);
    CHECK_EQ(gg_ia_missing(&cl, &pso, 1), 0);
    gg_ia_state(&cl, 1, 0, 1, NULL, 0);                    /* desliga o slot 0 */
    CHECK_EQ(gg_ia_missing(&cl, &pso, 1), 1);
    CHECK_EQ(gg_ia_missing(&cl, &pso2, 1), 0);             /* PSO sem input layout */
    gg_ia_state(&cl, 3, 0, 0, NULL, 0);                    /* Reset */
    CHECK_EQ(gg_ia_missing(&cl, &pso, 1), 0x80000005u);
}

TEST(gpuguard_debug8_skip_ia_incomplete)
{
    int saved = gg_trace_on, saved_skip = gg_draw_skip;
    log_capture();
    gg_trace_on = 6; gg_draw_skip = 6;
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0));
    CHECK(gg_skip_draw(0, GG_OP_DRAW_INDEXED, 4, 0));
    CHECK(gg_skip_draw(1, GG_OP_DRAW, 0x80000000u, 0));
    CHECK(log_has("gpuguard: experimento: draw com IA incompleto #1 pulado"));
    gg_trace_on = saved; gg_draw_skip = saved_skip;
}

/* hash dos shaders (checksum do container) e DrawSkip=7 pela lista de hashes */
TEST(gpuguard_pso_hash_and_debug8_hash_list)
{
    static uint8_t vs[32], ps[32], ps2[32];
    D3D12_SHADER_BYTECODE bvs = { vs, 32 }, bps = { ps, 32 }, bps2 = { ps2, 32 }, tiny = { ps, 8 };
    const D3D12_SHADER_BYTECODE *a[5] = { &bvs, &bps, NULL, NULL, NULL }, *b[5] = { &bvs, &bps2, NULL, NULL, NULL };
    const D3D12_SHADER_BYTECODE *c[5] = { &bvs, &tiny, NULL, NULL, NULL }, *d[5] = { &bvs, NULL, NULL, NULL, NULL };
    int saved = gg_trace_on, saved_skip = gg_draw_skip;
    vs[4] = 1; ps[4] = 2; ps2[4] = 3; ps2[0] = 9;
    CHECK(gg_shader_hash(a) != gg_shader_hash(b));
    ps2[4] = 2;
    CHECK_EQ(gg_shader_hash(a), gg_shader_hash(b));       /* so o checksum (bytes 4..19) conta */
    CHECK_EQ(gg_shader_hash(c), gg_shader_hash(d));       /* bytecode curto demais = ausente */
    CHECK(gg_shader_hash(a) != gg_shader_hash(d));

    gg_parse_hashes("1a2b3c4d, ff 0x10");
    CHECK_EQ(gg_nskip_hash, 3);
    CHECK_EQ(gg_skip_hash[0], 0x1a2b3c4d);
    CHECK_EQ(gg_skip_hash[1], 0xff);
    CHECK_EQ(gg_skip_hash[2], 0x10);
    log_capture();
    gg_trace_on = 6; gg_draw_skip = 7;
    CHECK(gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0xff));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0xfe));
    CHECK(!gg_skip_draw(1, GG_OP_DRAW_INDEXED, 0, 0xff));  /* direto */
    CHECK(!gg_skip_draw(0, GG_OP_DRAW, 0, 0xff));          /* outro tipo */
    CHECK(log_has("gpuguard: experimento: DrawIndexedInstanced do replayer da lista de hashes #1 pulado"));
    gg_parse_hashes("");
    CHECK_EQ(gg_nskip_hash, 0);
    gg_trace_on = saved; gg_draw_skip = saved_skip;
}

/* estagios/stream output/topologia do PSO e DrawSkip=8 (so draws com DS/HS/GS/SO) */
TEST(gpuguard_pso_info_and_debug8_extra_stages)
{
    static uint8_t code[32];
    D3D12_SHADER_BYTECODE vs = { code, 32 }, ps = { code, 32 }, gs = { code, 32 }, none = { NULL, 0 };
    D3D12_SO_DECLARATION_ENTRY ent = { 0 };
    D3D12_STREAM_OUTPUT_DESC so = { &ent, 1, NULL, 0, 0 }, noso = { 0 };
    const D3D12_SHADER_BYTECODE *a[5] = { &vs, &ps, &none, NULL, NULL }, *b[5] = { &vs, NULL, NULL, NULL, &gs };
    char str[40];
    int saved = gg_trace_on, saved_skip = gg_draw_skip;
    CHECK_EQ(gg_pso_info_of(a, &noso, 3), 0x303);           /* VS + PS, triangulos; DS vazio nao conta */
    CHECK_EQ(gg_pso_info_of(b, &so, 3), 0x331);             /* VS + GS + SO */
    CHECK_EQ(gg_pso_info_of(a, NULL, 0), 0x3);
    gg_pso_info_str(0x331, str);
    CHECK_STR(str, "VS GS SO topo 3");
    gg_pso_info_str(0, str);
    CHECK_STR(str, "topo 0");

    log_capture();
    gg_trace_on = 6; gg_draw_skip = 8;
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED, 0, 0));       /* o filtro por tipo/origem nao vale no 8 */
    CHECK(!gg_skip_draw(1, GG_OP_DRAW, 0, 0));
    CHECK(!gg_skip_stages(0x303));
    CHECK(gg_skip_stages(0x331));
    CHECK(gg_skip_stages(0x30b));                            /* DS */
    CHECK(gg_skip_stages(0x413));                            /* HS, patch */
    CHECK(log_has("gpuguard: experimento: draw com DS/HS/GS/SO #1 pulado"));
    gg_draw_skip = 5;
    CHECK(!gg_skip_stages(0x331));
    gg_trace_on = 1; gg_draw_skip = 8;
    CHECK(!gg_skip_stages(0x331));

    gg_trace_on = 8;                                         /* D3D12Debug=10: PSO com GS fora da library */
    CHECK(fresh_gs_pso(0x313));
    CHECK(!fresh_gs_pso(0x303));
    CHECK(!fresh_gs_pso(0x30b));                             /* DS sem GS */
    CHECK(log_has("gpuguard: experimento: PSO com GS #1 criado fora da pipeline library"));
    gg_trace_on = 6;
    CHECK(!fresh_gs_pso(0x313));
    gg_trace_on = saved; gg_draw_skip = saved_skip;
}

/* tabela de UAV nao definida no draw (GS do FM6 grava em u0 sem UAV ligado) -> UAVs nulos */
/* sem rastreio (D3D12Debug 0/1): a vtable da lista ainda recebe os draws que ligam os UAVs nulos (antes so com >= 3) */
static ID3D12GraphicsCommandListVtbl lite_vt;
static struct { ID3D12GraphicsCommandListVtbl *lpVtbl; } lite_cl = { &lite_vt };
static int lite_draws, lite_tabs;
static UINT lite_tab_i;
static UINT64 lite_tab_g;
static HRESULT STDMETHODCALLTYPE lite_qi(ID3D12GraphicsCommandList *l, REFIID iid, void **out) { (void)iid; *out = l; return S_OK; }
static ULONG lite_refs = 1;
static ULONG STDMETHODCALLTYPE lite_release(ID3D12GraphicsCommandList *l) { (void)l; return lite_refs; }
static void STDMETHODCALLTYPE lite_grs(ID3D12GraphicsCommandList *l, ID3D12RootSignature *rs) { (void)l; (void)rs; }
static void STDMETHODCALLTYPE lite_set_pso(ID3D12GraphicsCommandList *l, ID3D12PipelineState *ps) { (void)l; (void)ps; }
static HRESULT STDMETHODCALLTYPE lite_reset(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    (void)l; (void)a; (void)ps;
    return S_OK;
}
static void STDMETHODCALLTYPE lite_draw_indexed(ID3D12GraphicsCommandList *l, UINT a, UINT b, UINT c, INT d, UINT e)
{
    (void)l; (void)a; (void)b; (void)c; (void)d; (void)e;
    lite_draws++;
}
static void STDMETHODCALLTYPE lite_gtab(ID3D12GraphicsCommandList *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE g)
{
    (void)l;
    lite_tabs++;
    lite_tab_i = i;
    lite_tab_g = g.ptr;
}

TEST(gpuguard_null_uavs_without_trace)
{
    static int rs, rs1, pso, pso1;
    uint32_t size[3] = { 4, 0, 8 };
    int saved = gg_trace_on;
    ID3D12GraphicsCommandList *l = (ID3D12GraphicsCommandList *)&lite_cl;
    lite_vt.QueryInterface = lite_qi;
    lite_vt.Release = lite_release;
    lite_vt.DrawIndexedInstanced = lite_draw_indexed;
    lite_vt.SetGraphicsRootDescriptorTable = lite_gtab;
    lite_vt.SetGraphicsRootSignature = lite_grs;
    lite_vt.SetPipelineState = lite_set_pso;
    lite_vt.Reset = lite_reset;
    gg_trace_on = 0;
    hook_list_vtable(l);
    CHECK(lite_vt.DrawInstanced == hook_draw_uav && lite_vt.DrawIndexedInstanced == hook_draw_indexed_uav);
    CHECK(lite_vt.ExecuteIndirect == hook_exec_indirect_uav && lite_vt.Reset == hook_reset_uav);
    CHECK(lite_vt.SetPipelineState == hook_set_pso);
    CHECK(lite_vt.CopyTextureRegion == NULL);                  /* rastreio continua desligado */
    gg_rs_add(&rs, size, 3);
    gg_rs_set_uav(&rs, 1ull << 2);
    gg_rs_add(&rs1, size, 1);                                  /* 1 parametro, sem tabela de UAV */
    gg_pso_add(&pso, NULL, 0, 0x313, &rs);
    gg_pso_add(&pso1, NULL, 0, 0x303, &rs1);
    null_uav_gpu = 0x1023e0000ull;
    lite_vt.SetGraphicsRootSignature(l, (ID3D12RootSignature *)&rs);
    lite_vt.SetPipelineState(l, (ID3D12PipelineState *)&pso);
    lite_vt.DrawIndexedInstanced(l, 126, 1, 71904, -14155, 0);
    CHECK(lite_draws == 1 && lite_tabs == 1 && lite_tab_i == 2 && lite_tab_g == 0x1023e0000ull);
    lite_vt.DrawIndexedInstanced(l, 126, 1, 71904, -14155, 0);
    CHECK(lite_draws == 2 && lite_tabs == 1);                  /* ja ligada */

    /* lista registrada com a root signature velha, PSO de outra (1 parametro): nada de bind fora dos limites */
    lite_vt.Reset(l, NULL, NULL);
    CHECK(gg_cl_rs(l, 0, NULL, 0) == NULL && gg_cl_rs(l, GG_CL_PSO, NULL, 0) == NULL);
    lite_vt.SetGraphicsRootSignature(l, (ID3D12RootSignature *)&rs);
    lite_vt.SetPipelineState(l, (ID3D12PipelineState *)&pso1);
    lite_vt.DrawIndexedInstanced(l, 6, 1, 0, 0, 0);
    CHECK(lite_draws == 3 && lite_tabs == 1);
    CHECK(log_has("gpuguard: root signature da lista"));
    lite_vt.SetPipelineState(l, (ID3D12PipelineState *)&lite_cl);   /* PSO desconhecido: idem */
    lite_vt.DrawIndexedInstanced(l, 6, 1, 0, 0, 0);
    CHECK(lite_draws == 4 && lite_tabs == 1);
    lite_vt.SetPipelineState(l, (ID3D12PipelineState *)&pso);
    lite_vt.DrawIndexedInstanced(l, 6, 1, 0, 0, 0);
    CHECK(lite_draws == 5 && lite_tabs == 2 && lite_tab_i == 2);
    CHECK(lite_vt.Release == hook_list_release);
    lite_refs = 0;                                            /* ultima referencia: registro da lista sai */
    lite_vt.Release(l);
    CHECK(gg_cl_rs(l, 0, NULL, 0) == NULL && gg_cl_rs(l, GG_CL_PSO, NULL, 0) == NULL);
    lite_refs = 1;
    null_uav_gpu = 0;
    gg_trace_on = saved;
}

/* tabelas por lista: tirar uma lista nao quebra a cadeia das que colidiram com ela; tabela nao enche com listas
   criadas e destruidas (PASSO 33: o fps caia ate travar) */
TEST(gpuguard_list_forget)
{
    void *k[8];
    int i, n = 0, cycle, before = 0, after = 0;
    uintptr_t p;
    unsigned h0 = hptr((void *)(uintptr_t)0x20000000u, GG_CL_SLOTS);
    for (p = 0x20000000u; n < 8 && p < 0x20000000u + 0x1000000u; p += 16)   /* 8 listas com a mesma casa */
        if (hptr((void *)p, GG_CL_SLOTS) == h0) k[n++] = (void *)p;
    CHECK_EQ(n, 8);
    for (i = 0; i < n; i++) gg_cl_rs(k[i], 0, (void *)(uintptr_t)(0x100 + i), 1);
    CHECK(gg_cl_forget(k[1]));
    CHECK(gg_cl_rs(k[1], 0, NULL, 0) == NULL);
    for (i = 0; i < n; i++)
        if (i != 1) CHECK(gg_cl_rs(k[i], 0, NULL, 0) == (void *)(uintptr_t)(0x100 + i));
    CHECK(!gg_cl_forget(k[1]));
    for (i = 0; i < n; i++) gg_cl_forget(k[i]);
    for (i = 0; i < GG_CL_SLOTS; i++) before += (cl_tab[i].cl != NULL) + (ia_tab[i].cl != NULL) + (gfx_tab[i].cl != NULL);
    /* 3x a tabela inteira de listas novas, cada uma destruida: nada sobra e a ultima continua registravel */
    for (cycle = 0; cycle < 3 * GG_CL_SLOTS; cycle++) {
        void *cl = (void *)(uintptr_t)(0x30000000u + (uintptr_t)cycle * 0x40);
        gg_cl_gtab(cl, 1, 1);
        gg_ia_state(cl, 2, 0, 0, NULL, 1);
        gg_gfx_set(cl, 0, 1, 0x55, NULL, 0);
        CHECK_EQ(gg_cl_gtab(cl, 1, 2), 3);
        CHECK(gg_cl_forget(cl));
    }
    for (i = 0; i < GG_CL_SLOTS; i++) after += (cl_tab[i].cl != NULL) + (ia_tab[i].cl != NULL) + (gfx_tab[i].cl != NULL);
    CHECK_EQ(after, before);
}

TEST(gpuguard_missing_uav_tables)
{
    static int rs, rs2, cl, cl2, pso, pso2;
    uint32_t size[3] = { 4, 0, 8 };
    D3D12_DESCRIPTOR_RANGE1 uav = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE1 mix[2] = { { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0, 0 },
                                       { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0, 1 } };
    D3D12_ROOT_DESCRIPTOR_TABLE1 t_uav = { 1, &uav }, t_mix = { 2, mix }, t_none = { 0, NULL };
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0xf4000, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };

    CHECK(gg_table_uav_only(&t_uav));
    CHECK(!gg_table_uav_only(&t_mix));
    CHECK(!gg_table_uav_only(&t_none));

    CHECK(gg_null_uav_heap(&hd));
    hd.NumDescriptors = 4096;
    CHECK(!gg_null_uav_heap(&hd));                           /* outro heap */
    hd.NumDescriptors = 0xf4000; hd.Flags = 0;
    CHECK(!gg_null_uav_heap(&hd));                           /* nao visivel */
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    CHECK(!gg_null_uav_heap(&hd));

    gg_rs_add(&rs, size, 3);
    gg_rs_set_uav(&rs, 1ull << 2);
    gg_rs_add(&rs2, size, 3);
    CHECK_EQ(gg_rs_uav(&rs), 4);
    CHECK_EQ(gg_rs_uav(&rs2), 0);
    CHECK_EQ(gg_rs_uav(NULL), 0);

    gg_pso_add(&pso, NULL, 0, 0, &rs);
    gg_pso_add(&pso2, NULL, 0, 0, &rs2);
    gg_cl_rs(&cl, 0, &rs, 1);
    gg_cl_rs(&cl, GG_CL_PSO, &pso2, 1);
    gg_cl_gtab(&cl, 0, 0);
    CHECK_EQ(gg_missing_uav_tables(&cl), 0);                 /* PSO de outra root signature: registro suspeito */
    gg_cl_rs(&cl, GG_CL_PSO, &pso, 1);
    CHECK_EQ(gg_missing_uav_tables(&cl), 4);                 /* tabela 2 nunca definida */
    gg_cl_gtab(&cl, 1, 1ull << 0);
    CHECK_EQ(gg_missing_uav_tables(&cl), 4);
    gg_cl_gtab(&cl, 1, 1ull << 2);
    CHECK_EQ(gg_missing_uav_tables(&cl), 0);                 /* o jogo ligou */
    gg_cl_gtab(&cl, 0, 0);                                   /* root signature nova / Reset */
    CHECK_EQ(gg_missing_uav_tables(&cl), 4);
    gg_cl_rs(&cl, 0, &rs2, 1);
    CHECK_EQ(gg_missing_uav_tables(&cl), 0);                 /* root signature sem tabela so de UAV */
    CHECK_EQ(gg_missing_uav_tables(&cl2), 0);                /* lista sem root signature */
}

/* espelho dos descritores: Create*View grava, CopyDescriptors propaga por ranges, slot fora de heap ignorado */
TEST(gpuguard_descriptor_shadow)
{
    static uint8_t a[16 * 32], b[16 * 32];
    uintptr_t ha = (uintptr_t)a, hb = (uintptr_t)b;
    gg_desc d = { 0 }, o;
    D3D12_CPU_DESCRIPTOR_HANDLE dst[2] = { { hb }, { hb + 5 * 32 } }, src[1] = { { ha + 2 * 32 } };
    UINT dsz[2] = { 2, 1 }, ssz[1] = { 3 };
    int k;
    gg_heap_reset();
    gg_heap_add(ha, 16, 32, 0);
    gg_heap_add(hb, 16, 32, 0x1000);
    for (k = 0; k < 4; k++) {
        d.kind = GG_DK_SRV; d.va = 0x100 + k; d.fmt = (uint16_t)(10 + k);
        gg_desc_set(ha + (uintptr_t)(2 + k) * 32, &d);
    }
    CHECK(gg_desc_get(ha + 3 * 32, &o));
    CHECK_EQ(o.va, 0x101);
    CHECK(!gg_desc_get(0x10, &o));                              /* fora de heap */
    gg_desc_copy(2, dst, dsz, 1, src, ssz, 32);                 /* a[2..4] -> b[0..1] + b[5] */
    CHECK(gg_desc_get(hb, &o));
    CHECK_EQ(o.va, 0x100);
    CHECK(gg_desc_get(hb + 32, &o));
    CHECK_EQ(o.va, 0x101);
    CHECK(gg_desc_get(hb + 5 * 32, &o));
    CHECK_EQ(o.va, 0x102);
    CHECK_EQ(o.fmt, 12);
    CHECK(gg_desc_get(hb + 2 * 32, &o));
    CHECK_EQ(o.kind, GG_DK_NONE);                               /* nao tocado */
    {
        char buf[200];
        o.kind = GG_DK_SRV; o.flags = GG_DF_RESERVED;
        desc_text(buf, 7, &o);
        CHECK(strstr(buf, "[7]SRV") && strstr(buf, "RESERVADO"));
    }
    gg_heap_reset();
}

/* estado grafico por lista: tabelas, root views, IB, VBs; Reset limpa */
TEST(gpuguard_gfx_state)
{
    static int cl;
    gg_gfx g;
    D3D12_INDEX_BUFFER_VIEW ib = { 0x5000, 600, DXGI_FORMAT_R16_UINT };
    D3D12_VERTEX_BUFFER_VIEW vb[2] = { { 0x6000, 64, 16 }, { 0x7000, 32, 8 } };
    gg_gfx_set(&cl, 0, 3, 0xabc0, NULL, 0);
    gg_gfx_set(&cl, 1, 1, 0x8000, NULL, 0);
    gg_gfx_set(&cl, 2, 0, 0, &ib, 0);
    gg_gfx_set(&cl, 3, 1, 0, vb, 2);
    CHECK(gg_gfx_get(&cl, &g));
    CHECK_EQ(g.tab[3], 0xabc0);
    CHECK_EQ(g.va[1], 0x8000);
    CHECK_EQ(g.ib.SizeInBytes, 600);
    CHECK_EQ(g.vb[1].BufferLocation, 0x6000);
    CHECK_EQ(g.vb[2].StrideInBytes, 8);
    CHECK_EQ(g.vb[0].BufferLocation, 0);
    gg_gfx_set(&cl, 3, 1, 0, NULL, 1);                          /* desliga o slot 1 */
    CHECK(gg_gfx_get(&cl, &g));
    CHECK_EQ(g.vb[1].BufferLocation, 0);
    gg_gfx_set(&cl, 4, 0, 0, NULL, 0);
    CHECK(gg_gfx_get(&cl, &g));
    CHECK_EQ(g.tab[3], 0);
    CHECK_EQ(g.ib.BufferLocation, 0);
}
