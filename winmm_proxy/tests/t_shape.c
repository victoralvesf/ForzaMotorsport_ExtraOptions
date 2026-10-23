/*
 * Testes de shape_fm5.c: traducao dos opcodes dos grafos SHAPE do XDK do FM5 e o grafo devolvido sem consumo
 * (nao pode ser traduzido duas vezes).
 */
#include "../shape_fm5.c"
#include "test.h"

TEST(shape_opcodes_translated)
{
    uint64_t g[5] = { 0xabcd000000000000ull | 0, 0x1234500000000020ull | 1, 0xffull << 8 | 4, 2, 0x1f };
    log_capture();
    translate_graph(g, g + 5);
    CHECK_EQ(g[0], 0xabcd000000000000ull | 1);    /* mix buffer */
    CHECK_EQ(g[1], 0x1234500000000020ull | 2);    /* SRC (bits acima do opcode preservados) */
    CHECK_EQ(g[2], 0xffull << 8 | 0x10);          /* DMA */
    CHECK_EQ(g[3], 2);                            /* desconhecido: intacto e logado */
    CHECK_EQ(g[4], 0x1f);
    CHECK(log_has("not translated"));
}

static int graph_calls;
static uint64_t consume;
static uint64_t fake_graph(uint8_t *proc, uint8_t *graph) { (void)proc; (void)graph; graph_calls++; return consume; }

TEST(shape_pending_graph_not_translated_twice)
{
    uint64_t data[2];
    uint8_t graph[0x58] = { 0 };
    *(uint64_t **)(graph + 0x48) = data;
    *(uint64_t **)(graph + 0x50) = data + 2;
    orig_shape_graph = fake_graph;
    fake_azimuth(1);
    data[0] = 0; data[1] = 1;
    consume = 0;                                  /* nao consumido: o chamador repassa o mesmo grafo */
    hook_shape_graph(NULL, graph);
    hook_shape_graph(NULL, graph);
    CHECK_EQ(data[0], 1); CHECK_EQ(data[1], 2);   /* 0->1 e 1->2 uma vez so (duas viraria 1->2->2) */
    consume = 1;
    hook_shape_graph(NULL, graph);                /* consumido: o proximo e grafo novo */
    data[0] = 0;
    hook_shape_graph(NULL, graph);
    CHECK_EQ(data[0], 1);
    CHECK_EQ(graph_calls, 4);
    /* fora do FM5 nao traduz */
    fake_azimuth(0);
    data[0] = 0;
    hook_shape_graph(NULL, graph);
    CHECK_EQ(data[0], 0);
}

TEST(shape_fm6_graphs_logged_not_translated)
{
    uint64_t data[2] = { 0x8002000001ull, 0x4004 };
    uint8_t graph[0x58] = { 0 };
    *(uint64_t **)(graph + 0x48) = data;
    *(uint64_t **)(graph + 0x50) = data + 2;
    orig_shape_graph = fake_graph;
    fake_azimuth(2);
    log_capture();
    fm6_graphs = 0;
    consume = 1;
    hook_shape_graph(NULL, graph);
    CHECK(log_has("FM6 graph #1 (2 entries)"));
    CHECK(log_has("[1] 0000000000004004 op 4"));
    CHECK_EQ(data[0], 0x8002000001ull);           /* FM6: sem traducao */
    CHECK_EQ(data[1], 0x4004);
}
