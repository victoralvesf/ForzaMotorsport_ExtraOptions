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
    CHECK(!gg_skip_draw(1, GG_OP_DRAW));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW));
    gg_trace_on = 6;
    CHECK(gg_skip_draw(1, GG_OP_DRAW));
    CHECK(gg_skip_draw(0, GG_OP_DRAW));
    gg_draw_skip = 1;
    CHECK(gg_skip_draw(1, GG_OP_DRAW));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW));
    gg_draw_skip = 2;
    CHECK(!gg_skip_draw(1, GG_OP_DRAW));
    CHECK(gg_skip_draw(0, GG_OP_DRAW));
    gg_draw_skip = 4;                                          /* so DrawInstanced do replayer */
    CHECK(gg_skip_draw(0, GG_OP_DRAW));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED));
    CHECK(!gg_skip_draw(0, GG_OP_EXECUTE_INDIRECT));
    CHECK(!gg_skip_draw(1, GG_OP_DRAW));
    gg_draw_skip = 5;
    CHECK(gg_skip_draw(0, GG_OP_DRAW_INDEXED));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW));
    gg_draw_skip = 3;
    CHECK(gg_skip_draw(0, GG_OP_EXECUTE_INDIRECT));
    CHECK(!gg_skip_draw(0, GG_OP_DRAW_INDEXED));
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
