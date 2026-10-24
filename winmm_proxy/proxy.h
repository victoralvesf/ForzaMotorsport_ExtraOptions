/*
 * Declaracoes comuns do proxy winmm.dll.
 *
 * Organizacao (separacao por jogo; tools/check_isolation.py roda no build.sh e nos testes):
 *  - raiz (main.c, azimuth.c): o que roda primeiro (DllMain) e os hooks do azimuth.exe que valem para todo jogo;
 *    cada hook repassa ao jogo carregado pelo descritor dele (game_current()) ou chama o original do Azimuth;
 *  - common\: codigo generico, usado pelos dois jogos (hooks, TLS, resolucao/fps da UI, camada D3D12 do Azimuth);
 *    o que muda por jogo vem do descritor (struct game), nunca de is_fm5()/is_fm6();
 *  - fm5\ e fm6\: codigo de um jogo so. So roda com aquele jogo carregado (pelo descritor), exceto os ganchos
 *    pre-jogo (early_install, d3d12_device), que se protegem sozinhos. fm5\ nunca usa simbolos de fm6\ e vice-versa;
 *    a raiz e common\ so chegam a um jogo pelos descritores fm5_game / fm6_game.
 */
#ifndef PROXY_H
#define PROXY_H
#include <windows.h>
#include <stdint.h>

/* ---- main.c ---- */
extern uint8_t *g_base;   /* azimuth.exe */
void wlog(const char *fmt, ...);
void proxy_path(const wchar_t *file, wchar_t *out);   /* arquivo na pasta do proxy (MAX_PATH) */
int dbg_int(const wchar_t *key, int def);             /* debug.ini [Debug] (FM5 e FM6) */
int dbg_str(const wchar_t *key, char *out, int len);

/* ---- common/hook.c: escrita conferida e hooks com trampolim ---- */
void unhex(const char *s, uint8_t *out);
int bytes_match(const uint8_t *p, const char *hex, size_t len);
void write_mem(void *dst, const void *src, size_t len);
void abs_jmp(uint8_t *at, const void *to, size_t len);
uint8_t *alloc_near(uint8_t *mod, size_t size);
int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp_out);
int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp_out);

/* ---- registro do jogo (layout no PASSO 4) ---- */
typedef struct { const wchar_t *locale; uint32_t index; uint8_t speech; uint8_t pad[3]; } LangEntry;
#define GAME_RECORD_SIZE 0x350
extern const char record_unknown_hash[];   /* hash de leitor desconhecido (+0x318) */

/* padroes por jogo da camada D3D12/audio do Azimuth (common\); o debug.ini sobrescreve cada um */
typedef struct {
    int allocator_max_age_ms;    /* AllocatorMaxAgeMs (-1 = nunca destruir os allocators ociosos) */
    int tile_no_hazard;          /* TileNoHazard */
    int xma_start_fix;           /* XmaStartFix */
    int defer_release;           /* DeferRelease */
    int defer_release_hold_ms;   /* DeferReleaseHoldMs */
    int list_pool;               /* ListPool */
    int list_merge;              /* ListMerge */
    int placement_placed;        /* PlacementPlaced */
    int trace_game_frames;       /* trava de page fault: quadros do exe do jogo contam nas pilhas/donos das listas */
} game_defaults;

/* Descritor de um jogo (fm5\fm5.c, fm6\fm6.c). Campos nulos = o Azimuth original. */
typedef struct game {
    const char *tag;                                         /* "FM5" */
    uint8_t *record;                                         /* registro (GAME_RECORD_SIZE) posto na allow list */
    void (*build_record)(uint8_t *rec, const uint8_t *fh2);  /* fh2 = registro do FH2 (caminhos do EmbeddedXvd) */
    game_defaults defaults;
    /* pre-jogo (o jogo ainda nao e conhecido): chamados para TODOS os jogos; o codigo deve conferir o proprio jogo */
    void (*load_config)(void);                               /* DllMain: caminho e log do .ini do jogo */
    void (*early_install)(void);                             /* DllMain, depois dos patches do azimuth.exe */
    /* so com este jogo carregado */
    void (*apply_patches)(uint8_t *game, const uint8_t *options);       /* loader::apply_patches do Azimuth */
    FARPROC (*resolve_import)(const uint8_t *entry, FARPROC az);        /* az = o do Azimuth (NULL = sem HLE) */
    void (*xg_computer)(void *out, const char *name, uint32_t *desc);   /* chama xg_computer_orig */
    uint64_t (*shape_graph)(uint8_t *proc, uint8_t *graph);             /* chama shape_graph_orig */
    long (*winrt_factory)(const wchar_t *cls, const GUID *iid, void **out, int *handled);
    long (*winrt_template)(void *self, const void *name, void **out, int *handled);
    uint64_t (*winrt_table_call)(uint32_t slot, uint64_t *args, void *ret, double *fargs, int *handled);
    int (*a2c_no_dither)(void);                              /* PSOs com alpha-to-coverage reescritos (gpuguard) */
    void (*object_released)(void *obj, void *caller);        /* Release comum do Azimuth chegou a 0 */
    int (*context_index)(void *ctx);                         /* diagnostico: indice do contexto (-1 = nenhum) */
    void (*note_pso)(void *pso, int wait, int ctx);          /* diagnostico: busca de pipeline do Azimuth */
    void (*note_draw_skip)(int ctx);                         /* diagnostico: draw descartado pelo Azimuth */
} game;

extern const game fm5_game, fm6_game;   /* unicos simbolos dos jogos que a raiz/common podem usar */
#define GAME_COUNT 2
extern const game *const games[GAME_COUNT];   /* azimuth.c: na ordem da allow list */

/* ---- azimuth.c ---- */
extern uint8_t *g_game;                  /* exe do jogo carregado (apply_patches; NULL antes / no FH2) */
extern uint8_t *g_trace_game;            /* = g_game se defaults.trace_game_frames, senao NULL */
const game *game_current(void);          /* jogo carregado; NULL = antes do launcher escolher, ou FH2 */
#define GAME_DEFAULT(f) (game_current() ? game_current()->defaults.f : 0)
/* a2c_no_dither do jogo carregado; antes dele (PSOs da pipeline library criados no boot) 1 so se todos os jogos querem */
int game_a2c_no_dither(void);
void xg_computer_orig(void *out, const char *name, uint32_t *desc);
uint64_t shape_graph_orig(uint8_t *proc, uint8_t *graph);

/* ---- common/azimuth_mem.c: heap da APU, VirtualAlloc do Azimuth, faixa de VA fixa ---- */
void implicit_apu_heap(const char *game_name);
int az_valloc_install(uint32_t (*protect)(uint32_t type, uint32_t protect));
void fixed_va_hold(void);
void fixed_va_release(const char *why);

/* ---- common/tls.c ---- */
void take_tls_template(uint8_t *game);
void tls_slot0_sites(uint8_t *game, const uint32_t *sites, size_t n);   /* aplicado no 1o import resolvido */
void game_tls_apply(void);

#endif
