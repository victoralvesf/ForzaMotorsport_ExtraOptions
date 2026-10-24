/*
 * Patches do azimuth.exe (todos os jogos) e hooks que repassam ao jogo carregado (descritores fm5_game/fm6_game):
 *  - adiciona os registros do Forza Motorsport 5 (7b35d917) e do Forza Motorsport 6 (64f9397e) a allow list
 *  - relaxa as travas do azimuth.exe que o FM5/FM6 nao passam (template, TLS, layout do XG, xg.dll, n. de secoes)
 *  - loader::apply_patches: para o FM5/FM6 roda o apply_patches do jogo no lugar dos sites do FH2
 *  - XG texture computer: o descritor do jogo converte/corrige (FM5: layout do desc; FM6: vtable)
 * Enderecos: ver docs\contexto\ (PASSOS 2-7). Todos os bytes originais sao conferidos antes de escrever; se algo nao
 * bater, nada e alterado.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "proxy.h"

/* ---- RVAs no azimuth.exe (SHA-256 579ed8d8...) ---- */
#define RVA_ALLOW_SPAN     0x29a200  /* {ptr, count} da allow list */
#define RVA_ALLOW_ARRAY    0x29a1c0  /* 2 ponteiros p/ os registros existentes */
#define RVA_GAME_RECORD    0x31aa20  /* global: ponteiro p/ o registro do jogo carregado */
#define RVA_TEMPLATE_JNE   0x85c8b   /* cmp rax,0x20 ; jne -> erro "one secure template" */
#define RVA_TLS_SIZE_JNE   0xbe6e2   /* cmp rax,0x150 ; jne */
#define RVA_TLS_CHAR_JNE   0xbe6f9   /* cmp [rsi+0x24],0x500000 ; jne */
#define RVA_TLS_ZERO_END   0xbe70d   /* lea rax,[rdx+0x150] (fim do laco de zeros) */
#define RVA_EXE_NSEC_JNE   0xbe4db   /* mapeador do exe: cmp NumberOfSections,0xe ; jne */
#define RVA_XG_SIZE_JNE    0x9f33b   /* cmp SizeOfImage,0x1d000 ; jne */
#define RVA_XG_ENTRY_JNE   0x9f348   /* cmp AddressOfEntryPoint,0xdf84 ; jne */
#define RVA_XG_NAME_CMP    0x3a7b5   /* resolvedor: cmp rsi,8 ; ... "xg_x.dll" (21 bytes) */
#define RVA_XG_NAME_MATCH  0x3a7ca
#define RVA_XG_NAME_NEXT   0x3a7d7
#define RVA_APPLY_PATCHES  0x3f5b0   /* loader::apply_patches(Image&, PatchOptions const&) */
#define APPLY_STOLEN       14
#define RVA_RESOLVER       0x3a290   /* resolvedor de imports por DLL (hook em common/d3d.c) */
#define RVA_XG_COMPUTER    0x9fba0   /* (out, "XGCreateTexture2D/3DComputer", desc) do proprio Azimuth */
#define XG_COMPUTER_STOLEN 16

/* ------------------------------------------------------------------------ */
/* Jogos                                                                      */
/* ------------------------------------------------------------------------ */
const game *const games[GAME_COUNT] = { &fm5_game, &fm6_game };
const char record_unknown_hash[] = "0000000000000000000000000000000000000000000000000000000000000000";
static const void *new_allow[2 + GAME_COUNT];
uint8_t *g_game;
uint8_t *g_trace_game;

const game *game_current(void)
{
    const void *rec = g_base ? *(void **)(g_base + RVA_GAME_RECORD) : NULL;
    int i;
    if (rec)
        for (i = 0; i < GAME_COUNT; i++)
            if (rec == games[i]->record) return games[i];
    return NULL;
}

int game_a2c_no_dither(void)
{
    const game *g = game_current();
    int i, r = 1;
    if (g) return g->a2c_no_dither ? g->a2c_no_dither() : 0;
    for (i = 0; i < GAME_COUNT; i++) r &= games[i]->a2c_no_dither && games[i]->a2c_no_dither();
    return r;
}

/* ------------------------------------------------------------------------ */
/* Escrita com conferencia                                                    */
/* ------------------------------------------------------------------------ */
typedef struct { uint32_t rva; uint8_t len; const char *orig; const char *repl; const char *what; } Patch;

static const Patch exe_patches[] = {
    /* aceita >= 1 template (usa o primeiro): jne -> jb */
    { RVA_TEMPLATE_JNE, 2, "755b", "725b", "secure template: exactly 1 -> at least 1" },
    /* TLS do jogo: tamanho <= 0x150, alinhamento <= 16, zeros so dentro do template */
    { RVA_TLS_SIZE_JNE, 6, "0f85f1000000", "0f87f1000000", "TLS: size == 0x150 -> <= 0x150" },
    { RVA_TLS_CHAR_JNE, 6, "0f85da000000", "0f87da000000", "TLS: Characteristics == 0x500000 -> <=" },
    { RVA_TLS_ZERO_END, 7, "488d8250010000", "488b4608909090", "TLS: zero loop up to EndAddressOfRawData" },
    /* exe do jogo com 14 secoes fixas (FM6 tem 15); hash, SizeOfImage e entry do registro ja identificam o exe */
    { RVA_EXE_NSEC_JNE, 6, "0f8523010000", "909090909090", "exe: fixed NumberOfSections == 14" },
    /* layout fixo do xg_x.dll pinado (o hash do registro ja identifica o XG) */
    { RVA_XG_SIZE_JNE, 6, "0f859c020000", "909090909090", "XG: fixed SizeOfImage" },
    { RVA_XG_ENTRY_JNE, 6, "0f858f020000", "909090909090", "XG: fixed AddressOfEntryPoint" },
};

/* ------------------------------------------------------------------------ */
/* Resolvedor de imports: aceitar "xg.dll" (FM5) alem de "xg_x.dll"           */
/* rax = nome da DLL (minusculo), rsi = tamanho; rcx e livre nos dois destinos */
/* ------------------------------------------------------------------------ */
void *xg_match_target, *xg_next_target;
extern void xg_name_stub(void);
__asm__(
    ".text\n"
    ".globl xg_name_stub\n"
    "xg_name_stub:\n"
    "    cmpq $8, %rsi\n"
    "    jne 1f\n"
    "    movabsq $0x6c6c642e785f6778, %rcx\n"   /* "xg_x.dll" */
    "    cmpq %rcx, (%rax)\n"
    "    je 2f\n"
    "    jmp 3f\n"
    "1:  cmpq $6, %rsi\n"
    "    jne 3f\n"
    "    cmpl $0x642e6778, (%rax)\n"            /* "xg.d" */
    "    jne 3f\n"
    "    cmpw $0x6c6c, 4(%rax)\n"               /* "ll" */
    "    jne 3f\n"
    "2:  jmp *xg_match_target(%rip)\n"
    "3:  jmp *xg_next_target(%rip)\n");

/* ------------------------------------------------------------------------ */
/* loader::apply_patches                                                      */
/* ------------------------------------------------------------------------ */
typedef void (*apply_patches_fn)(void *image, const void *options);
static apply_patches_fn orig_apply_patches;

/* camada D3D12/audio do Azimuth (common\): ligada pelos padroes do jogo (game_defaults), o debug.ini sobrescreve */
extern int cmdalloc_age_patch(int ms);
extern int tile_nohazard_patch(int on);
extern int xma_start_patch(int on);
extern int retire_margin_patch(int n);
extern void defer_release_setup(void);
extern void list_pool_setup(void);
extern void list_merge_setup(void);
extern void alloc_share_setup(void);
extern void placed_textures_setup(void);

static void hook_apply_patches(void *image, const void *options)
{
    const game *g = game_current();
    /* allocators ociosos do pool do Azimuth: no FM6 nunca destruidos (stutters, 17_fm6.txt PASSO 67) */
    cmdalloc_age_patch(dbg_int(L"AllocatorMaxAgeMs", GAME_DEFAULT(allocator_max_age_ms)));
    /* mapeamentos de tiles do streaming sem sincronizar a fila: no FM6 sempre (GPU longa, PASSO 69) */
    tile_nohazard_patch(dbg_int(L"TileNoHazard", GAME_DEFAULT(tile_no_hazard)));
    /* vozes XMA sem o ataque cortado (FFmpeg ja descarta o 1o quadro): no FM6 sempre (clique do menu, PASSO 114) */
    xma_start_patch(dbg_int(L"XmaStartFix", GAME_DEFAULT(xma_start_fix)));
    /* ultima Release do reaper espalhada: no FM6 sempre (stutters e queda da GPU, PASSO 70) */
    defer_release_setup();
    /* EXPERIMENTO: aposentadoria do Azimuth atrasada N envios (PASSO 81; so com RetireMargin no debug.ini) */
    retire_margin_patch(dbg_int(L"RetireMargin", 0));
    /* command lists reaproveitadas em vez de criadas a cada gravacao: no FM6 sempre (vazao, PASSO 74) */
    list_pool_setup();
    /* Azimuth grava em listas do proxy, uma lista real por envio: no FM6 sempre (listas/allocators, PASSO 98) */
    list_merge_setup();
    /* aberturas da mesma thread dividem o command allocator: no FM6 sempre (memoria do driver, PASSO 91) */
    alloc_share_setup();
    /* placement textures committed -> placed em heaps do proxy: no FM6 sempre (memoria do kernel, PASSO 97) */
    placed_textures_setup();
    if (g) {   /* os sites do registro sao do FH2: o jogo aplica os proprios */
        g_game = *(uint8_t **)image;
        g_trace_game = g->defaults.trace_game_frames ? g_game : NULL;
        wlog("apply_patches: %s at %p, FH2 sites skipped", g->tag, g_game);
        g->apply_patches(g_game, options);
        return;
    }
    fixed_va_release("not FM5/FM6");
    orig_apply_patches(image, options);
}

/* ------------------------------------------------------------------------ */
/* XG texture computer do Azimuth: so as chamadas internas do Azimuth passam por aqui (o jogo chama o XG direto) */
/* ------------------------------------------------------------------------ */
typedef void (*xg_computer_fn)(void *out, const char *name, uint32_t *desc);
static xg_computer_fn orig_xg_computer;

void xg_computer_orig(void *out, const char *name, uint32_t *desc) { orig_xg_computer(out, name, desc); }

static void hook_xg_computer(void *out, const char *name, uint32_t *desc)
{
    const game *g = game_current();
    if (g && g->xg_computer) g->xg_computer(out, name, desc);
    else orig_xg_computer(out, name, desc);
}

/* ------------------------------------------------------------------------ */

extern int d3d_install(void);
extern int winrt_install(void);
extern int shape_install(void);
extern int debug12_install(void);
extern void stutter_install(void);

int azimuth_install(void)
{
    static const char apply_orig[] = "48895c24184889742420555741544156";
    static const char resolver_orig[] = "48895c2410488974241848897c242055";
    static const char xg_orig[] ="4883fe08751c48b978675f782e646c6c483908750d";
    size_t i;

    /* 1) conferir tudo antes de escrever qualquer coisa */
    if (!bytes_match(g_base + RVA_ALLOW_SPAN + 8, "0200000000000000", 8) ||
        *(void **)(g_base + RVA_ALLOW_SPAN) != g_base + RVA_ALLOW_ARRAY) {
        wlog("allow list does not match"); return 0;
    }
    for (i = 0; i < sizeof exe_patches / sizeof exe_patches[0]; i++)
        if (!bytes_match(g_base + exe_patches[i].rva, exe_patches[i].orig, exe_patches[i].len)) {
            wlog("bytes do not match at %#x (%s)", exe_patches[i].rva, exe_patches[i].what); return 0;
        }
    if (!bytes_match(g_base + RVA_XG_NAME_CMP, xg_orig, 21) ||
        !bytes_match(g_base + RVA_APPLY_PATCHES, apply_orig, 16) ||
        !bytes_match(g_base + RVA_RESOLVER, resolver_orig, 16)) {
        wlog("resolver/apply_patches bytes do not match"); return 0;
    }

    /* 2) registros + allow list {fh2, fh2 f&f, fm5, fm6} */
    new_allow[0] = *(void **)(g_base + RVA_ALLOW_ARRAY);
    new_allow[1] = *(void **)(g_base + RVA_ALLOW_ARRAY + 8);
    for (i = 0; i < GAME_COUNT; i++) {
        games[i]->build_record(games[i]->record, new_allow[0]);
        new_allow[2 + i] = games[i]->record;
    }
    {
        struct { const void **ptr; uint64_t count; } span = { new_allow, 2 + GAME_COUNT };
        write_mem(g_base + RVA_ALLOW_SPAN, &span, sizeof span);
    }
    wlog("allow list: %p %p %p (FM5) %p (FM6)", new_allow[0], new_allow[1], new_allow[2], new_allow[3]);

    /* 3) patches pontuais */
    for (i = 0; i < sizeof exe_patches / sizeof exe_patches[0]; i++) {
        uint8_t buf[16];
        unhex(exe_patches[i].repl, buf);
        write_mem(g_base + exe_patches[i].rva, buf, exe_patches[i].len);
        wlog("patch %#x: %s", exe_patches[i].rva, exe_patches[i].what);
    }

    /* 4) xg.dll -> mapa do xg_x.dll pinado */
    xg_match_target = g_base + RVA_XG_NAME_MATCH;
    xg_next_target = g_base + RVA_XG_NAME_NEXT;
    abs_jmp(g_base + RVA_XG_NAME_CMP, (const void *)xg_name_stub, 21);
    wlog("patch %#x: resolver accepts xg.dll", RVA_XG_NAME_CMP);

    /* 5) hook do apply_patches (14 bytes roubados, sem RIP-relativos) */
    if (!hook_function(RVA_APPLY_PATCHES, apply_orig, APPLY_STOLEN,
                       (void *)hook_apply_patches, (void **)&orig_apply_patches)) return 0;
    wlog("patch %#x: apply_patches hooked", RVA_APPLY_PATCHES);

    /* 6) resolvedor de imports: d3d11_x.dll!D3D11CreateDevice etc. por jogo (common/d3d.c) */
    if (!d3d_install()) return 0;
    wlog("patch %#x: resolver hooked (D3D11CreateDevice)", RVA_RESOLVER);

    /* 7) descritores do XG no layout do XDK do jogo */
    if (!hook_function(RVA_XG_COMPUTER, "48895c24205556574154415541564157", XG_COMPUTER_STOLEN,
                       (void *)hook_xg_computer, (void **)&orig_xg_computer)) return 0;
    wlog("patch %#x: XG texture computer hooked (per game desc layout)", RVA_XG_COMPUTER);

    /* 8) ativacao WinRT: classes do XDK do jogo sem equivalente -> fabrica stub (common/winrt.c) */
    if (!winrt_install()) return 0;
    wlog("patch 0x9a680/0x208790: WinRT activation (stub factories) and GetTemplateByName (5 FM5 templates)");

    /* 9) grafos SHAPE (audio) da APU (common/shape.c) */
    if (!shape_install()) return 0;

    /* 10) diagnostico opcional: debug layer do D3D12 (D3D12Debug no debug.ini, common/debug12.c) */
    debug12_install();
    stutter_install();   /* StutterLog no debug.ini: picos de quadro (common/stutter.c) */

    /* 11) ganchos pre-jogo de cada jogo (o jogo ainda nao e conhecido) */
    for (i = 0; i < GAME_COUNT; i++)
        if (games[i]->early_install) games[i]->early_install();
    return 1;
}
