/* SHAPE (audio da APU): grafos do FM5 (XDK nov-2012) x interpretador do Azimuth.          */
/* 0x14018fbd0(proc, graph): graph+0x48/+0x50 = vetor de u64 copiado da memoria APU        */
/* (comando ACP tipo 1, 0x14018f710). Opcode = bits 0..4; o Azimuth so implementa 1 (mix   */
/* buffer), 2 (SRC) e 0x10 (DMA); bits 5..7 = queued/disabled/error.                       */
/* O FM5 usa os MESMOS campos com outra numeracao (grafo capturado: 0x802000|op0 x2,        */
/* 0x8002000001 = SRC ctx 0 -> mix 1/2, 0x4004 / 0x8008004 = DMA mix 1/2 -> ctx 0/1):        */
/*   FM5 0 -> 1 (mix buffer), FM5 1 -> 2 (SRC), FM5 4 -> 0x10 (DMA).                        */
/* A traducao e feita na copia do Azimuth (nao na memoria do jogo). O Azimuth chama de novo */
/* com o MESMO grafo quando devolve 0 (anel de DMA cheio) -> nao traduzir duas vezes.       */
#include <windows.h>
#include <stdint.h>

extern uint8_t *g_base;
extern void wlog(const char *fmt, ...);
extern int is_fm5(void);
extern int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp);

#define RVA_SHAPE_GRAPH 0x18fbd0
#define SHAPE_GRAPH_STOLEN 18   /* mov rax,rsp; mov [rax+18],rbx; push rbp..r15 */

typedef uint64_t (*shape_graph_fn)(uint8_t *proc, uint8_t *graph);
static shape_graph_fn orig_shape_graph;
static volatile LONG graphs_logged, unknown_logged;

/* grafo ja traduzido e devolvido sem consumo (so a thread "Azimuth audio processor" chama) */
static __thread uint8_t *pending_graph;
static __thread uint64_t *pending_data;

static int fm5_opcode(unsigned op, unsigned *out)
{
    switch (op) {
    case 0: *out = 1; return 1;      /* mix buffer */
    case 1: *out = 2; return 1;      /* SRC */
    case 4: *out = 0x10; return 1;   /* DMA */
    }
    return 0;
}

static void translate_graph(uint64_t *b, uint64_t *e)
{
    LONG n = graphs_logged < 4 ? InterlockedIncrement(&graphs_logged) : 0;
    uint64_t *p;
    if (n) wlog("shape: grafo do FM5 (%u entradas)", (unsigned)(e - b));
    for (p = b; p != e; p++) {
        unsigned op = (unsigned)(*p & 0x1f), nop;
        if (fm5_opcode(op, &nop)) {
            *p = (*p & ~(uint64_t)0x1f) | nop;
        } else if (unknown_logged < 16) {
            InterlockedIncrement(&unknown_logged);
            wlog("shape: opcode %u do FM5 sem traducao: %016llx", op, (unsigned long long)*p);
        }
        if (n) wlog("shape:   [%u] %016llx op %u -> %u", (unsigned)(p - b), (unsigned long long)*p, op,
                    (unsigned)(*p & 0x1f));
    }
}

static uint64_t hook_shape_graph(uint8_t *proc, uint8_t *graph)
{
    uint64_t ret, *b;
    if (!is_fm5()) return orig_shape_graph(proc, graph);
    b = *(uint64_t **)(graph + 0x48);
    if (graph != pending_graph || b != pending_data)
        translate_graph(b, *(uint64_t **)(graph + 0x50));
    ret = orig_shape_graph(proc, graph);
    /* 0 = nao consumido (sera repassado igual); 1 = consumido e liberado pelo chamador */
    pending_graph = (uint8_t)ret ? NULL : graph;
    pending_data = (uint8_t)ret ? NULL : b;
    return ret;
}

int shape_install(void)
{
    return hook_function(RVA_SHAPE_GRAPH, "488bc448895818555657415441554156" "4157", SHAPE_GRAPH_STOLEN,
                         (void *)hook_shape_graph, (void **)&orig_shape_graph);
}
