/* SHAPE (audio da APU): hook do interpretador de grafos do Azimuth, 0x14018fbd0(proc, graph); graph+0x48/+0x50 =  */
/* vetor de u64 copiado da memoria APU (comando ACP tipo 1, 0x14018f710). O jogo carregado trata o grafo           */
/* (fm5/shape.c: traducao dos opcodes do XDK nov-2012; fm6/audio.c: diagnostico) ou ele vai direto ao Azimuth.   */
#include <windows.h>
#include <stdint.h>
#include "../proxy.h"

#define RVA_SHAPE_GRAPH 0x18fbd0
#define SHAPE_GRAPH_STOLEN 18   /* mov rax,rsp; mov [rax+18],rbx; push rbp..r15 */

typedef uint64_t (*shape_graph_fn)(uint8_t *proc, uint8_t *graph);
static shape_graph_fn orig_shape_graph;

uint64_t shape_graph_orig(uint8_t *proc, uint8_t *graph) { return orig_shape_graph(proc, graph); }

static uint64_t hook_shape_graph(uint8_t *proc, uint8_t *graph)
{
    const game *g = game_current();
    if (g && g->shape_graph) return g->shape_graph(proc, graph);
    return orig_shape_graph(proc, graph);
}

int shape_install(void)
{
    return hook_function(RVA_SHAPE_GRAPH, "488bc448895818555657415441554156" "4157", SHAPE_GRAPH_STOLEN,
                         (void *)hook_shape_graph, (void **)&orig_shape_graph);
}
