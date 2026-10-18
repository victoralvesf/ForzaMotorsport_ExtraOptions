/*
 * winmm.dll proxy para o azimuth.exe: carrega antes do WinMain (WINMM e import estatico) e
 *  - repassa todos os exports para o winmm.dll do System32 (winmm_stubs.S)
 *  - adiciona os registros do Forza Motorsport 5 (7b35d917) e do Forza Motorsport 6 (64f9397e) a allow list
 *  - relaxa as travas do azimuth.exe que o FM5/FM6 nao passam (template, TLS, layout do XG, xg.dll, n. de secoes)
 *  - para o FM5, substitui loader::apply_patches pelos patches proprios do FM5; para o FM6 aplica so os do FM6
 *    (TLS, heap da APU, batches e as opcoes do azimuth_fm6.ini)
 * Enderecos: ver docs\contexto\ (PASSOS 2-7; indice no CLAUDE.md). Todos os bytes originais sao
 * conferidos antes de escrever; se algo nao bater, nada e alterado.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <tlhelp32.h>

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
#define RVA_RESOLVER       0x3a290   /* resolvedor de imports por DLL (hook em d3d_fm5.c) */
#define RVA_XG_COMPUTER    0x9fba0   /* (out, "XGCreateTexture2D/3DComputer", desc) do proprio Azimuth */
#define XG_COMPUTER_STOLEN 16
#define RVA_LINKS_END_A    0x15772c  /* lea r14,[0x1402bc918] (fim da lista de links da Live table) */
#define RVA_LINKS_END_B    0x1586ae  /* lea rdi,[0x1402bc918] */
#define LINK_SIZE          0x38      /* ultimo link = Windows.Xbox.Multiplayer.PartyConfig */

/* ---- RVAs no exe do FM5 (SHA-256 94dad218...) ---- */
#define FM5_SPEECH_SITE    0x623cb7  /* mov ebp,[r14+4] ; cmp ebp,esi ; jl ... */
/* cmp byte [rsi+0x309f],0 = opcao "disablesplashintro" de CForzaCommandLineParameters, lida em 0x13a0b0 */
static const uint32_t fm5_splash_intro_cmps[] = { 0x13a33e, 0x13a825, 0x13b9ce };
/* SkipPresentationVideo: handler do botao A da tela de boas-vindas (brilho) -> toca IE_Blockbuster.bk2 e, no fim do
   video, o callback (lambda 02c16cb7, _Do_call 0x4b2f50) cria a proxima pagina com modo 3 (1a corrida) */
#define FM5_IE_VIDEO_PLAY  0x4ab060  /* cria a pagina de video com o callback */
#define FM5_IE_VIDEO_DONE  0x4b2f50  /* corpo do callback (nao usa capturas) */
/* UnlockDLC: posse das ofertas (ContentOffers) */
#define FM5_OFFER_OWNED    0x203050  /* EntitlementManager: produto no inventario da Live? (al) */
#define FM5_MARKET_DEFAULT 0x180c2a  /* opcoes: mov dword [rbx+0x30bc], 0x1000000 (+0x30be enablemarketplace, +0x30bf renderworld) */
#define FM5_OFFER_NOINV    0x174dfb  /* CContentManager 0x174d00 sem inventario: xor edx,edx -> 0x1745b0(raiz, 0) */
/* resolucao (= sites +0x100/+0x110 do FH2, PASSO 9): altura padrao e largura calculada da altura (16:9) */
#define FM5_RES_HEIGHT     0x5d1ce1  /* mov dword [rbx+0x2560], 0x438 */
#define FM5_RES_WIDTH      0x13bfc1  /* vmulss ; vmulss ; vcvttss2si rbp,xmm2 (+0x10) */
#define FM5_RES_UI_SIZE    0x8e4da5  /* tamanho da UI = float([r15+8]), float([r15+0xc]) -> [rbp+0x6f/0x73] (36 bytes) */

/* ---- RVAs no exe do FM6 (Monument 1.0.0.44) ---- */
/* SkipIntro: opcao "disablesplashintro" = byte [opcoes+0x30d7] (registrada em 0xa3ae3b). Sem imediato para trocar:
   no init 0xa06964 e "cmp [r12+0x30d7],sil ; je" (esi = 0 desde 0xa06a46) -> je vira nop (= opcao ligada);
   no logo parade 0xa08e78 (Health_Disclaimer/forzatech) e "cmp [rax+0x30d7],r15b ; jne" (r15 = 0) -> jmp */
static const uint32_t fm6_splash_intro_cmps[] = { 0xa06b33, 0xa06c4d, 0xa07e0c };
#define FM6_LOGO_PARADE_CMP 0xa08eb6

uint8_t *g_base;
static FILE *g_log;

/* ---- azimuth_fm5.ini (pasta do azimuth.exe), secao [FM5] ---- */
static wchar_t g_ini[MAX_PATH];

int cfg_int(const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(L"FM5", key, def, g_ini);
}

/* ---- azimuth_fm6.ini (mesma pasta), secao [FM6] ---- */
static wchar_t g_ini6[MAX_PATH];

int cfg6_int(const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(L"FM6", key, def, g_ini6);
}

void wlog(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

/* ------------------------------------------------------------------------ */
/* Registro do FM5 (layout no PASSO 4, valores de docs\fm5_record.json)       */
/* ------------------------------------------------------------------------ */
typedef struct { const wchar_t *locale; uint32_t index; uint8_t speech; uint8_t pad[3]; } LangEntry;

static const LangEntry fm5_langs[] = {
    { L"en", 0x0, 1 },  { L"cs", 0x0, 0 },  { L"de", 0x4, 1 },  { L"en-GB", 0x2, 0 },
    { L"en-AU", 0x0, 0 }, { L"en-CA", 0x0, 0 }, { L"en-IE", 0x1, 0 }, { L"en-NZ", 0x1, 0 },
    { L"es", 0x7, 1 },  { L"es-MX", 0x6, 1 }, { L"fr", 0x5, 1 },  { L"fr-CA", 0x5, 0 },
    { L"hu", 0x0, 0 },  { L"it", 0x8, 1 },  { L"ja", 0x3, 1 },  { L"ko", 0x9, 0 },
    { L"nl", 0x10, 1 }, { L"pl", 0x0, 0 },  { L"pt", 0xb, 1 },  { L"ru", 0xf, 1 },
    { L"zh-Hant", 0xa, 0 },
};

/* hash de leitor desconhecido (+0x318): string valida para nao virar ponteiro nulo */
static const char unknown_hash[] = "0000000000000000000000000000000000000000000000000000000000000000";

static uint8_t __attribute__((aligned(16))) fm5_rec[0x350];
static uint8_t __attribute__((aligned(16))) fm6_rec[0x350];
static const void *new_allow[4];

#define SET_PTR(off, v) (*(const void **)(rec + (off)) = (const void *)(v))
#define SET_U64(off, v) (*(uint64_t *)(rec + (off)) = (uint64_t)(v))
#define SET_U32(off, v) (*(uint32_t *)(rec + (off)) = (uint32_t)(v))

static void build_record(void)
{
    const uint8_t *fh2 = *(const uint8_t **)(g_base + RVA_ALLOW_ARRAY);
    uint8_t *rec = fm5_rec;
    int i;

    memset(rec, 0, sizeof fm5_rec);
    SET_PTR(0x000, L"7b35d917");
    SET_PTR(0x008, "1.0.0.20");
    SET_PTR(0x010, "ecff3ef723af018c600a8bfc1139d1508cc177a9dc4b65d1c01256c7ba8cfbf2");
    SET_PTR(0x018, "94dad21851d335e9a33231dc2a1de2ecd4b93eefdf5f6772085bf6886bf47db7");
    SET_U64(0x020, 0x257f000);
    SET_PTR(0x028, "68ccaadc51e6f950737df2f1e33f09e8f30742cb5dfb3bfb87d89217dd991873");
    SET_U64(0x030, 0xcc2);
    SET_PTR(0x038, "9d198fcf130793cf0e2c742ff7e934bfd87c8ced0940b189413e52d6e874125a");
    SET_U64(0x040, 0x16000);
    SET_PTR(0x048, "2ad4b45a930b6258c66ceb7a4103e88cbeb2ef8dc28bb2467d07289bbd839272");
    SET_U64(0x050, 0x4f2c00);
    /* caminhos do EmbeddedXvd: reaproveita as strings do registro do FH2 */
    for (i = 0; i < 4; i++)
        SET_PTR(0x058 + i * 0x10, *(const void **)(fh2 + 0x058 + i * 0x10));
    SET_PTR(0x060, "11b6903d567b8a65108c56f2d15b0bc6c4d2901c22868a183b498ab7b6397e1b");
    SET_PTR(0x070, "43e144c74d531989ec11ac6b96685c07ee56d8e6644c06fef63d1966c4227fab");
    SET_PTR(0x080, "aa85ad987f55698eb418d7e78655d74384522bfac27e60b2f6932f6b4b2c3490");
    SET_PTR(0x090, "36be920837929adc64038113fb486af45aecae725e96cc6fd79c33daed44914e");
    SET_U32(0x098, 0x279d000);   /* SizeOfImage */
    SET_U32(0x09c, 0xca7ed0);    /* AddressOfEntryPoint */
    SET_U64(0x0a0, 0x244);       /* slots da IAT */
    /* sites do apply_patches: so os RVAs usados pelo HLE (SwitchToThread/SleepEx);
       o apply_patches original nao roda para o FM5 (hook_apply_patches) */
    SET_U32(0x1e8, 0x5d6ca7);    /* window idle call */
    SET_U32(0x1f8, 0);           /* cinematic movie idle call: sem equivalente */
    SET_U32(0x208, 0x5a8efc);    /* cinematic render idle call */
    SET_PTR(0x318, unknown_hash);
    SET_PTR(0x330, fm5_langs);
    SET_U64(0x338, sizeof fm5_langs / sizeof fm5_langs[0]);
    rec[0x340] = 0;
    rec[0x341] = 1;              /* EXPERIMENTAL */
}

/* ------------------------------------------------------------------------ */
/* Registro do FM6 (Monument 1.0.0.44, XDK "era"; valores de docs/fm6_record.json) */
/* So para o Azimuth aceitar o jogo: sem patches proprios ainda (hook_apply_patches). */
/* O dump do FM6 nao tem EmbeddedXvd\...\config\SOFTWARE: usa-se uma copia do hive  */
/* do FH2 1.0.0.64 na pasta do jogo (o hash em +0x060 e o dele).                      */
/* ------------------------------------------------------------------------ */
/* indice so e usado pelo site de fala do apply_patches (nao roda no FM6); o texto usa so o locale */
static const LangEntry fm6_langs[] = {
    { L"en", 0, 1 },    { L"de", 0, 0 },    { L"en-GB", 0, 0 }, { L"en-AU", 0, 0 },
    { L"en-CA", 0, 0 }, { L"en-IE", 0, 0 }, { L"en-NZ", 0, 0 }, { L"es", 0, 0 },
    { L"es-MX", 0, 0 }, { L"fr", 0, 0 },    { L"fr-CA", 0, 0 }, { L"it", 0, 0 },
    { L"ja", 0, 0 },    { L"ko", 0, 0 },    { L"nl", 0, 0 },    { L"pl", 0, 0 },
    { L"pt-BR", 0, 0 }, { L"pt-PT", 0, 0 }, { L"ru", 0, 0 },    { L"zh-Hant", 0, 0 },
    { L"zh-Hans", 0, 0 },
};

static void build_fm6_record(void)
{
    const uint8_t *fh2 = *(const uint8_t **)(g_base + RVA_ALLOW_ARRAY);
    uint8_t *rec = fm6_rec;
    int i;

    memset(rec, 0, sizeof fm6_rec);
    SET_PTR(0x000, L"64f9397e");
    SET_PTR(0x008, "1.0.0.44");
    SET_PTR(0x010, "9b8f98bbe2a702ad9263b22fa4dd3d01bce4e287b78fd5f65a77f55bc63b0bd5");
    SET_PTR(0x018, "01ee5446d7d5afa655daf87751e63f9c2bff1e713236037ca2a9b6300642dbcf");
    SET_U64(0x020, 0x1d80000);
    SET_PTR(0x028, "87c25090d0fab334e3608646a53618804bdd34d3c7f27c96578962fab94dc22f");
    SET_U64(0x030, 0xa52);
    SET_PTR(0x038, "555441c0f34365de3092cda1d03d92efea338fed3ba37076aa319aa654b2504d");
    SET_U64(0x040, 0x21200);
    SET_PTR(0x048, "99a3f99db4a2ebeed726c6c741d606770c54e2486070dc726ef4e6f10b8bcdc0");
    SET_U64(0x050, 0x68c800);
    for (i = 0; i < 4; i++)
        SET_PTR(0x058 + i * 0x10, *(const void **)(fh2 + 0x058 + i * 0x10));
    SET_PTR(0x060, "9b458bcf0bd5a11ce51c29ef8003c62c4de3015f1c129d82edaeb9d41d602aa4");  /* SOFTWARE do FH2 */
    SET_PTR(0x070, "0e311f6c60b87582305de5f14e74e8aaa9805e4d93f29b5fb0e848c34a0a0338");
    SET_PTR(0x080, "aca2f84ab55b3b0e2e33ce6045acd88f30296f5e710f406799de01dc800734df");
    SET_PTR(0x090, "9a24d12e684b82911be7fc3f4509bcd02aaa57bd59218aaca64989b5b617d006");
    SET_U32(0x098, 0x1fc8000);   /* SizeOfImage */
    SET_U32(0x09c, 0x6341d8);    /* AddressOfEntryPoint */
    SET_U64(0x0a0, 0x264);       /* slots da IAT */
    /* +0x1e8/+0x1f8/+0x208 (idle calls do HLE): 0 = sem site ainda */
    SET_PTR(0x318, unknown_hash);
    SET_PTR(0x330, fm6_langs);
    SET_U64(0x338, sizeof fm6_langs / sizeof fm6_langs[0]);
    rec[0x341] = 1;              /* EXPERIMENTAL */
}

/* ------------------------------------------------------------------------ */
/* Escrita com conferencia                                                    */
/* ------------------------------------------------------------------------ */
typedef struct { uint32_t rva; uint8_t len; const char *orig; const char *repl; const char *what; } Patch;

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

static void unhex(const char *s, uint8_t *out) { while (*s) { *out++ = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1])); s += 2; } }

int bytes_match(const uint8_t *p, const char *hex, size_t len)
{
    uint8_t buf[64];
    unhex(hex, buf);
    return memcmp(p, buf, len) == 0;
}

void write_mem(void *dst, const void *src, size_t len)
{
    DWORD old;
    VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dst, src, len);
    VirtualProtect(dst, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, len);
}

static const Patch exe_patches[] = {
    /* aceita >= 1 template (usa o primeiro): jne -> jb */
    { RVA_TEMPLATE_JNE, 2, "755b", "725b", "secure template: exatamente 1 -> pelo menos 1" },
    /* TLS do jogo: tamanho <= 0x150, alinhamento <= 16, zeros so dentro do template */
    { RVA_TLS_SIZE_JNE, 6, "0f85f1000000", "0f87f1000000", "TLS: tamanho == 0x150 -> <= 0x150" },
    { RVA_TLS_CHAR_JNE, 6, "0f85da000000", "0f87da000000", "TLS: Characteristics == 0x500000 -> <=" },
    { RVA_TLS_ZERO_END, 7, "488d8250010000", "488b4608909090", "TLS: laco de zeros ate EndAddressOfRawData" },
    /* exe do jogo com 14 secoes fixas (FM6 tem 15); hash, SizeOfImage e entry do registro ja identificam o exe */
    { RVA_EXE_NSEC_JNE, 6, "0f8523010000", "909090909090", "exe: NumberOfSections == 14 fixo" },
    /* layout fixo do xg_x.dll pinado (o hash do registro ja identifica o XG) */
    { RVA_XG_SIZE_JNE, 6, "0f859c020000", "909090909090", "XG: SizeOfImage fixo" },
    { RVA_XG_ENTRY_JNE, 6, "0f858f020000", "909090909090", "XG: AddressOfEntryPoint fixo" },
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

void abs_jmp(uint8_t *at, const void *to, size_t len)
{
    uint8_t buf[32];
    memset(buf, 0xcc, sizeof buf);
    buf[0] = 0xff; buf[1] = 0x25; memset(buf + 2, 0, 4);
    memcpy(buf + 6, &to, 8);
    write_mem(at, buf, len);
}

/* ------------------------------------------------------------------------ */
/* loader::apply_patches                                                      */
/* ------------------------------------------------------------------------ */
typedef void (*apply_patches_fn)(void *image, const void *options);
static apply_patches_fn orig_apply_patches;

/* Live table: o runtime do console do FM5 (xdk-nov-2012) nao tem a classe
   Windows.Xbox.Multiplayer.PartyConfig -> tirar o ultimo link (os dois lacos usam o fim) */
static void drop_partyconfig_link(void)
{
    static const struct { uint32_t rva; const char *orig; } sites[] = {
        { RVA_LINKS_END_A, "4c8d35e5511600" }, { RVA_LINKS_END_B, "488d3d63421600" } };
    size_t i;
    for (i = 0; i < 2; i++)
        if (!bytes_match(g_base + sites[i].rva, sites[i].orig, 7)) {
            wlog("Live table: lea do fim dos links nao confere em %#x", sites[i].rva);
            return;
        }
    for (i = 0; i < 2; i++) {
        int32_t disp = *(int32_t *)(g_base + sites[i].rva + 3) - LINK_SIZE;
        write_mem(g_base + sites[i].rva + 3, &disp, 4);
    }
    wlog("Live table: link PartyConfig removido");
}

/* Heap da APU: os XDKs do FM5 (nov-2012) e do FM6 NAO tem ApuCreateHeap (heap implicito); o Azimuth exige que o
   jogo o crie antes de ApuAlloc ("ApuAlloc before the game's explicit heap creation", via AcpHalAllocateShapeContexts).
   ApuCreateHeap 0x1400aead0(cached, noncached) so aceita (0x500000, 0x40000) = o que o FH2 pede. Tabela dos 2 heaps
   em 0x14037c9a0 (0x20 cada: +0 base, +8 u32 tamanho, +0xc u32 endereco APU, +0x10 mapa de blocos); ApuAlloc/Free/
   Map* leem tudo dela. O FM5 usa ApuAlloc(&va, NULL, size, 0x800, 1) como alocador geral (0x627af0/0x627c10) ->
   heap cacheado ampliado p/ 16 MB (APU 0x1000000..0x2000000, sem invadir o nao cacheado em 0x2000000). */
#define RVA_APU_CREATE_HEAP 0xaead0
#define RVA_APU_HEAPS       0x37c9a0
#define FM5_APU_CACHED_SIZE 0x1000000
typedef long (*apu_create_heap_fn)(uint32_t cached, uint32_t noncached);

static void implicit_apu_heap(const char *game_name)
{
    uint8_t *h = g_base + RVA_APU_HEAPS;
    void *big;
    long hr = ((apu_create_heap_fn)(g_base + RVA_APU_CREATE_HEAP))(0x500000, 0x40000);
    if (hr < 0) { wlog("APU: ApuCreateHeap hr=%#lx", hr); return; }
    big = VirtualAlloc(NULL, FM5_APU_CACHED_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (big && *(uint32_t *)(h + 8) == 0x500000 && *(uint32_t *)(h + 0xc) == 0x1000000) {
        VirtualFree(*(void **)h, 0, MEM_RELEASE);
        *(void **)h = big;
        *(uint32_t *)(h + 8) = FM5_APU_CACHED_SIZE;
    } else if (big) VirtualFree(big, 0, MEM_RELEASE);
    wlog("APU: heap criado p/ o %s: cacheado %p (%#x bytes, APU %#x), nao cacheado %p (%#x bytes, APU %#x)",
         game_name, *(void **)h, *(uint32_t *)(h + 8), *(uint32_t *)(h + 0xc),
         *(void **)(h + 0x20), *(uint32_t *)(h + 0x28), *(uint32_t *)(h + 0x2c));
}

/* Resolucao do launcher (PatchOptions +4 altura, +8 largura; 0 = nao mexe), como 0x14003e720 faz no FH2:
   o imediato 1080 vira a altura; com largura != 0 o vcvttss2si rbp,xmm2 (largura = altura * 16/9) vira mov ebp,
   largura. Aspecto != 16:9: a UI (sites +0x130..+0x1c8 do FH2) ainda nao e ajustada no FM5. */
/* Viewport da UI (cont. 3): 0xb60ca0 le o tamanho da UI (1920x1080, fixo acima), monta a projecao ortografica com
   ele e usa o MESMO tamanho no viewport ([rdi+0x18] = contexto, rax = objeto: call [rax+0x188] = ponteiro de
   RSSetViewports guardado NO objeto (tabela rapida do XDK, nao a vtable) em 0xb60db8,
   {0,0,w,h,0,1} em rsp+0x30; o call [rax+0x130] seguinte e OMSetRenderTargets) -> a UI ficava num quadrado
   1920x1080 no canto. A chamada vira call [rip -> slot no padding 0xb60c98] = fm5_ui_vcall, que escala o viewport. */
#define FM5_UI_VP_CALL     0xb60db8  /* ff 90 88 01 00 00  call [rax+0x188] (viewport) */
#define FM5_UI_SLOT        0xb60c98  /* padding cc entre 0xb60c91 e 0xb60ca0 */
static uint8_t *ui_game;
static float ui_sx = 1, ui_sy = 1;

static void fm5_ui_vcall(void *obj, uint32_t n, void *p, uintptr_t r9)
{
    typedef void (*vc_fn)(void *, uint32_t, void *, uintptr_t);
    static volatile LONG logged_vp;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0) - (uintptr_t)ui_game;
    void **tab = (void **)obj;                   /* call [rax+0x188] com rax = objeto: ponteiro NO objeto (tabela rapida do XDK) */
    if (ret == FM5_UI_VP_CALL + 6 && p) {
        float v[6];
        memcpy(v, p, sizeof v);
        v[0] *= ui_sx; v[2] *= ui_sx; v[1] *= ui_sy; v[3] *= ui_sy;
        if (InterlockedIncrement(&logged_vp) <= 2)
            wlog("FM5: UI viewport %.0f,%.0f %.0fx%.0f -> %.0f,%.0f %.0fx%.0f (alvo game+%#llx)", ((float *)p)[0],
                 ((float *)p)[1], ((float *)p)[2], ((float *)p)[3], v[0], v[1], v[2], v[3],
                 (unsigned long long)((uintptr_t)tab[0x188 / 8] - (uintptr_t)ui_game));
        ((vc_fn)tab[0x188 / 8])(obj, n, v, r9);
        return;
    }
    wlog("FM5: fm5_ui_vcall chamado de game+%#llx inesperado", (unsigned long long)ret);
}

static void fm5_ui_viewport(uint8_t *game, uint32_t w, uint32_t h)
{
    static const uint8_t pad[8] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
    void *fn = (void *)fm5_ui_vcall;
    uint8_t code[6] = { 0xff, 0x15 };
    int32_t rel;
    if (!bytes_match(game + FM5_UI_VP_CALL, "ff9088010000", 6) || memcmp(game + FM5_UI_SLOT, pad, 8)) {
        wlog("FM5: resolucao: sites do viewport da UI nao conferem, ignorado");
        return;
    }
    ui_game = game; ui_sx = w / 1920.0f; ui_sy = h / 1080.0f;
    write_mem(game + FM5_UI_SLOT, &fn, 8);
    rel = (int32_t)(FM5_UI_SLOT - (FM5_UI_VP_CALL + 6));   memcpy(code + 2, &rel, 4); write_mem(game + FM5_UI_VP_CALL, code, 6);
    wlog("FM5: resolucao: viewport da UI escalado para %ux%u", w, h);
}

static void fm5_resolution(uint8_t *game, uint32_t height, uint32_t width)
{
    if (!height && !width) return;
    if (height && width && (uint64_t)width * 9 != (uint64_t)height * 16)
        wlog("FM5: resolucao: aspecto %ux%u != 16:9, UI nao ajustada (so a cena)", width, height);
    if (height) {
        if (!bytes_match(game + FM5_RES_HEIGHT, "c7836025000038040000", 10))
            wlog("FM5: resolucao: site da altura %#x nao confere, ignorado", FM5_RES_HEIGHT);
        else {
            write_mem(game + FM5_RES_HEIGHT + 6, &height, 4);
            wlog("FM5: resolucao: altura %u", height);
        }
    }
    if (width) {
        uint8_t code[5] = { 0xbd };
        if (!bytes_match(game + FM5_RES_WIDTH, "c5fa590d0b2aba01c5f259153f22ba01c4e1fa2cea", 21))
            wlog("FM5: resolucao: site da largura %#x nao confere, ignorado", FM5_RES_WIDTH);
        else {
            memcpy(code + 1, &width, 4);
            write_mem(game + FM5_RES_WIDTH + 0x10, code, sizeof code);
            wlog("FM5: resolucao: largura %u", width);
        }
    }
    /* = "UI size from the display" do FH2: o layout da UI e feito para 1920x1080; com o tamanho do render a UI
       ficava no canto superior esquerdo (cont. 2). Forca 1920.0/1080.0 nos dois floats passados a [vtable+0x78]. */
    {
        static const uint8_t code[36] = {
            0xb8, 0x00, 0x00, 0xf0, 0x44,  0x89, 0x45, 0x6f,       /* mov eax, 1920.0f ; mov [rbp+0x6f], eax */
            0xb8, 0x00, 0x00, 0x87, 0x44,  0x89, 0x45, 0x73,       /* mov eax, 1080.0f ; mov [rbp+0x73], eax */
            0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
            0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
            0x0f, 0x1f, 0x40, 0x00 };                              /* nop (4) */
        if (!bytes_match(game + FM5_RES_UI_SIZE,
                         "418b4708c5f857c0c4e1fa2ac0c5fa11456f418b470cc5f057c9c4e1f22ac8c5fa114d73", 36))
            wlog("FM5: resolucao: site do tamanho da UI %#x nao confere, ignorado", FM5_RES_UI_SIZE);
        else {
            write_mem(game + FM5_RES_UI_SIZE, code, sizeof code);
            wlog("FM5: resolucao: UI fixa em 1920x1080");
            fm5_ui_viewport(game, width ? width : (uint32_t)(((uint64_t)(height ? height : 1080) * 16 + 4) / 9),
                            height ? height : 1080);
        }
    }
}

/* FPS (PASSO 10): o Present do FM5 (Frame11 slot 0x138 = 0x7146e0) le [frame+0xa0]: 0 -> SyncInterval 1 (60 fps),
   1 -> 2 (30 fps), outro -> 0. O campo e escrito so pelo setter virtual slot 0x180 (0x711620 mov [rcx+0xa0],edx)
   e lido pelo getter slot 0x188 (0x711630). Nos menus/transicoes o jogo pede 1 (30 fps): RenderThreadBaseDeferred
   slot 0x58 (envio do frame, modo em r9d; sites 0x5bcd16 e 0x6017a4) -> slot 0x80 0x59d070 enfileira {0x535ec0,..}
   -> executor 0x682860 -> Frame slot 0x180. Com frame_rate >= 60 no launcher o wrapper troca 1 por 0. */
#define FM5_FRAME_VTBL     0x22c3350  /* .?AVFrame@@ */
#define FM5_FRAME11_VTBL   0x22c2f10  /* .?AVFrame11@@ */
#define FM5_FRAME_SET_SYNC 0x711620
static uint8_t *fps_game;
uint8_t *g_game;           /* base do exe do FM5 (setado em fm5_patches) */
uint8_t *g_fm6_game;       /* base do exe do FM6 (setado no apply_patches; pilhas do gpuguard_fm6.c) */
static int fps_force;

/* stub: rbx (preservado) = no da fila do executor 0x682860 ({fn, rcx, rdx, ?, next}) quando chamado de la */
void fm5_frame_set_sync(uint8_t *frame, uint32_t mode, uintptr_t rbx, uintptr_t ret);
void fm5_frame_set_sync_stub(void);
__asm__(".globl fm5_frame_set_sync_stub\n"
        "fm5_frame_set_sync_stub:\n"
        "  mov %rbx, %r8\n"
        "  mov (%rsp), %r9\n"
        "  jmp fm5_frame_set_sync\n");

void fm5_frame_set_sync(uint8_t *frame, uint32_t mode, uintptr_t rbx, uintptr_t ret)
{
    static volatile LONG last = -1;
    uint32_t v = (fps_force && mode == 1) ? 0 : mode;
    if (InterlockedExchange(&last, (LONG)mode) != (LONG)mode) {
        uintptr_t g = (uintptr_t)fps_game, fn = 0;
        MEMORY_BASIC_INFORMATION mi;
        if (ret - g == 0x6828d0 && VirtualQuery((void *)rbx, &mi, sizeof mi) && mi.State == MEM_COMMIT)
            fn = *(uintptr_t *)rbx - g;
        wlog("FM5: fps: modo de present %u -> %u (chamador game+%#llx, fn da fila game+%#llx, limitador [+0x500]=%u)",
             mode, v, (unsigned long long)(ret - g), (unsigned long long)fn, frame[0x500]);
    }
    *(uint32_t *)(frame + 0xa0) = v;
}

/* Limitador do FM5 (PASSO 10 cont. 1): Frame11 slot 0xe8 = 0x7141b0 espera ativa no relogio do Frame
   (frame+0x440: double ultimo tempo; [+0x58] = funcao now(clock) -> double s) ate passar 1/60 s (double 0x1cde500)
   -> o jogo fica em 60 mesmo com o host a 120/180 (16.66 ms entre Presents, ~0 ms no host). Com fps > 60 o slot
   aponta para a mesma espera com passo 1/fps; o passo da simulacao (App 0x5d1260) deixa de ser o fixo 1/59.94
   (je -> jmp em 0x5d130a = dt medido, limitado a 0.2 s pelo proprio jogo, como o pacing real-time do FH2). */
#define FM5_FRAME11_LIMIT  0x7141b0
#define FM5_STEP_BRANCH    0x5d130a
static double fps_step;

static void fm5_frame_limit(uint8_t *frame)
{
    typedef double (*now_fn)(void *);
    uint8_t *c = frame + 0x440;
    now_fn now = *(now_fn *)(c + 0x58);
    while (now(c) - *(double *)c < fps_step) YieldProcessor();
    *(double *)c = now(c);
}

static void fm5_frame_unlock(uint8_t *game, uint32_t fps)
{
    static const uint8_t eb = 0xeb;
    void *fn = (void *)fm5_frame_limit;
    if (!bytes_match(game + FM5_FRAME11_LIMIT, "40534883ec30c5f829742420c5fb10353ca35c01488d9940040000", 27) ||
        *(uint8_t **)(game + FM5_FRAME11_VTBL + 0xe8) != game + FM5_FRAME11_LIMIT ||
        !bytes_match(game + FM5_STEP_BRANCH - 7, "803dff16000200740f", 9)) {
        wlog("FM5: fps: limitador 60 / passo fixo nao conferem, fps > 60 ignorado");
        return;
    }
    fps_step = 1.0 / fps;
    write_mem(game + FM5_FRAME11_VTBL + 0xe8, &fn, 8);
    write_mem(game + FM5_STEP_BRANCH, &eb, 1);
    wlog("FM5: fps: limitador do jogo em %u fps (passo %.3f ms), simulacao com dt medido", fps, fps_step * 1000);
}

static void fm5_frame_rate(uint8_t *game, uint32_t fps)
{
    static const uint32_t vtbls[] = { FM5_FRAME_VTBL, FM5_FRAME11_VTBL };
    void *fn = (void *)fm5_frame_set_sync_stub;
    size_t i;
    if (!bytes_match(game + FM5_FRAME_SET_SYNC, "8991a0000000c3", 7)) {
        wlog("FM5: fps: setter %#x nao confere, ignorado", FM5_FRAME_SET_SYNC);
        return;
    }
    for (i = 0; i < 2; i++)
        if (*(uint8_t **)(game + vtbls[i] + 0x180) != game + FM5_FRAME_SET_SYNC) {
            wlog("FM5: fps: vtable %#x slot 0x180 nao confere, ignorado", vtbls[i]);
            return;
        }
    fps_game = game;
    fps_force = fps >= 60;
    for (i = 0; i < 2; i++) write_mem(game + vtbls[i] + 0x180, &fn, 8);
    wlog("FM5: fps: frame_rate %u -> %s", fps, fps_force ? "present sempre com intervalo 1" : "original");
    if (fps > 60) fm5_frame_unlock(game, fps);
}

extern void fm5_car_detail(uint8_t *game, int level);
extern void fm5_shadows(uint8_t *game, int level);
extern void fm5_reflections(uint8_t *game, int level);

static void fm5_patches(uint8_t *game, const uint8_t *options)
{
    const LangEntry *speech = *(const LangEntry *const *)(options + 0x30);

    g_game = game;

    drop_partyconfig_link();
    implicit_apu_heap("FM5");

    if (cfg_int(L"SkipIntro", 0)) {
        /* cmp [rsi+0x309f],0 -> cmp [rsi+0x309f],0xff: o jogo ve a opcao ligada (so je/jne depois) */
        static const uint8_t ff = 0xff;
        size_t i, n = sizeof fm5_splash_intro_cmps / sizeof *fm5_splash_intro_cmps;
        for (i = 0; i < n; i++)
            if (!bytes_match(game + fm5_splash_intro_cmps[i], "80be9f30000000", 7)) break;
        if (i < n) wlog("FM5: SkipIntro: site %#x nao confere, ignorado", fm5_splash_intro_cmps[i]);
        else {
            for (i = 0; i < n; i++) write_mem(game + fm5_splash_intro_cmps[i] + 6, &ff, 1);
            wlog("FM5: SkipIntro=1 (disablesplashintro forcado)");
        }
    }

    if (cfg_int(L"SkipPresentationVideo", 0)) {
        /* 0x4ab060: jmp 0x4b2f50 -> chama direto o fim do video (mesma pilha, nenhum argumento usado) */
        uint8_t *p = game + FM5_IE_VIDEO_PLAY;
        uint8_t code[5] = { 0xe9 };
        int32_t rel = (int32_t)(FM5_IE_VIDEO_DONE - (FM5_IE_VIDEO_PLAY + 5));
        if (!bytes_match(p, "4883ec7848c7442420feffffff", 13) ||
            !bytes_match(game + FM5_IE_VIDEO_DONE, "4883ec48488d15851ac200", 11))
            wlog("FM5: SkipPresentationVideo: site nao confere, ignorado");
        else {
            memcpy(code + 1, &rel, 4);
            write_mem(p, code, sizeof code);
            wlog("FM5: SkipPresentationVideo=1 (IE_Blockbuster.bk2 pulado)");
        }
    }

    if (cfg_int(L"UnlockDLC", 0)) {
        /* 0x203050 -> mov eax,1 ; ret: todo produto conta como comprado (refresh 0x174d00 -> lambda 0x19e690 ->
           IsPurchased=1 na arvore de ofertas; UI do marketplace 0x367ef0/0x369ef0). Sem inventario (ptr nulo)
           o refresh zera tudo via 0x1745b0(raiz, 0) -> mov dl,1. */
        static const uint8_t ret1[6] = { 0xb8, 1, 0, 0, 0, 0xc3 }, dl1[2] = { 0xb2, 0x01 };
        if (!bytes_match(game + FM5_OFFER_OWNED, "48895c241048896c2418565741564883", 16) ||
            !bytes_match(game + FM5_OFFER_NOINV - 7, "488dbe8800000033d2488bcfe8abf7ffff", 17))
            wlog("FM5: UnlockDLC: sites nao conferem, ignorado");
        else {
            write_mem(game + FM5_OFFER_OWNED, ret1, sizeof ret1);
            write_mem(game + FM5_OFFER_NOINV, dl1, sizeof dl1);
            wlog("FM5: UnlockDLC=1 (todas as ofertas de DLC contam como compradas)");
        }
    }

    if (cfg_int(L"EnableMarketplace", 0)) {
        /* padrao do enablemarketplace (normalmente ligado so pela config do servidor, Main/IsMarketplaceEnabled
           em 0x1a13d0): imediato 0x01000000 -> 0x01010000 */
        static const uint8_t one = 1;
        if (!bytes_match(game + FM5_MARKET_DEFAULT, "c783bc30000000000001", 10))
            wlog("FM5: EnableMarketplace: site nao confere, ignorado");
        else {
            write_mem(game + FM5_MARKET_DEFAULT + 8, &one, 1);
            wlog("FM5: EnableMarketplace=1 (enablemarketplace ligado)");
        }
    }

    fm5_resolution(game, *(const uint32_t *)(options + 4), *(const uint32_t *)(options + 8));
    fm5_frame_rate(game, *(const uint32_t *)(options + 0xc));
    fm5_shadows(game, ((const int32_t *)(options + 0x14))[0]);
    fm5_reflections(game, ((const int32_t *)(options + 0x14))[1]);
    fm5_car_detail(game, ((const int32_t *)(options + 0x14))[2]);

    if (speech) {
        uint8_t *p = game + FM5_SPEECH_SITE;
        uint8_t code[7] = { 0xbd, 0, 0, 0, 0, 0xeb, 0x16 };  /* mov ebp, idx ; jmp 0x623cd4 */
        if (!bytes_match(p, "418b6e043bee7c", 7)) {
            wlog("FM5: site de fala nao confere, ignorado");
            return;
        }
        memcpy(code + 1, &speech->index, 4);
        write_mem(p, code, sizeof code);
        wlog("FM5: fala forcada para %ls (indice %u)", speech->locale, speech->index);
    }
}

int is_fm5(void) { return *(void **)(g_base + RVA_GAME_RECORD) == fm5_rec; }
int is_fm6(void) { return *(void **)(g_base + RVA_GAME_RECORD) == fm6_rec; }

/* ------------------------------------------------------------------------ */
/* TLS estatico do FM6: o template (0x48 bytes) NAO e zerado ([+0x20] = ffffffff). O Azimuth so aceita */
/* template zerado (0x1400be6a0) porque o bloco do jogo e o do azimuth-tls.dll (0x150 zerados). Entao: */
/*  1) apply_patches: guarda o template e zera-o na imagem do jogo (passa na checagem);               */
/*  2) 1o import resolvido (o azimuth-tls.dll ja carregou, o entry do jogo ainda nao rodou): grava o  */
/*     template no do azimuth-tls.dll (threads novas copiam dele) e nos blocos das threads existentes. */
/* ------------------------------------------------------------------------ */
#define TLS_HELPER_SIZE 0x150
static uint8_t game_tls[TLS_HELPER_SIZE];
static size_t game_tls_size;
static volatile LONG game_tls_pending;

static IMAGE_TLS_DIRECTORY64 *tls_dir(uint8_t *mod)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(mod + ((IMAGE_DOS_HEADER *)mod)->e_lfanew);
    IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    return d->VirtualAddress ? (IMAGE_TLS_DIRECTORY64 *)(mod + d->VirtualAddress) : NULL;
}

static void take_tls_template(uint8_t *game)
{
    IMAGE_TLS_DIRECTORY64 *t = tls_dir(game);
    uint8_t *start;
    size_t n, i;
    if (!t) return;
    start = (uint8_t *)(uintptr_t)t->StartAddressOfRawData;
    n = (size_t)(t->EndAddressOfRawData - t->StartAddressOfRawData);
    if (n > TLS_HELPER_SIZE) { wlog("TLS: template de %#zx bytes > bloco do helper", n); return; }
    for (i = 0; i < n && !start[i]; i++) ;
    if (i == n) return;                              /* ja zerado: nada a fazer */
    memcpy(game_tls, start, n);
    game_tls_size = n;
    memset(start, 0, n);                             /* .tls do jogo e RW; so o loader le o template */
    game_tls_pending = 1;
    wlog("TLS: template de %#zx bytes guardado e zerado (1o byte nao nulo em +%#zx)", n, i);
}

typedef LONG (WINAPI *nt_query_thread_fn)(HANDLE, int, void *, ULONG, ULONG *);
typedef struct { LONG status; PVOID teb; HANDLE pid, tid; ULONG_PTR affinity; LONG prio, base_prio; } THREAD_BASIC_INFO;

/* ------------------------------------------------------------------------ */
/* TLS no modelo "exe" do XDK: o FM6 le TEB.ThreadLocalStoragePointer[0] direto (no console o titulo */
/* e o exe, indice 0), sem _tls_index; no PC o slot 0 e o bloco do azimuth.exe (epoca dos statics   */
/* do MSVC em +0x10) -> o jogo pisava nele e o Azimuth pulava inits (WindowsDeleteString na init do */
/* CRT). Cada "mov T, gs:[0x58]" desses sites (tools/tls_slot0.py) vira "call stub_T; nop4"; o stub */
/* faz T = &ThreadLocalStoragePointer[indice do azimuth-tls.dll], entao [T] = bloco do jogo. Sem     */
/* efeito em flags; os 72 sites que usam _tls_index ja caem nesse bloco.                             */
/* ------------------------------------------------------------------------ */
static const uint32_t fm6_tls_slot0[] = {
    0x0eda53, 0x0edf0e, 0x0ee72e, 0x0efdc3, 0x0f6ed3, 0x0f7d37, 0x0fa003, 0x0fac9c,
    0x0fc1d9, 0x0fdef8, 0x1068a1, 0x106aa1, 0x107af4, 0x109b6c, 0x10a3cc, 0x113e8e,
    0x114057, 0x116ea1, 0x1171e6, 0x1174e5, 0x1195c3, 0x11c4d6, 0x11d3db, 0x11d6d9,
    0x12400e, 0x124db2, 0x12524d, 0x127c49, 0x12817c, 0x129fe6, 0x12a28e, 0x12c4d0,
    0x12d175, 0x12d311, 0x12d5d1, 0x12e1cb, 0x12e35b, 0x12f855, 0x135c6f, 0x136413,
    0x141b0a, 0x14799e, 0x148e0e, 0x15492f, 0x15951d, 0x159a64, 0x15a68e, 0x15a767,
    0x15ad1c, 0x1611fb, 0x1616ff, 0x163442, 0x1634b8, 0x164d20, 0x164e8c, 0x164edc,
    0x1655f4, 0x1658b0, 0x166375, 0x16673b, 0x169074, 0x16a005, 0x16b4d4, 0x16cbca,
    0x16ccd8, 0x16f84d, 0x174fb1, 0x17520b, 0x188fde, 0x189269, 0x18b429, 0x18bc2c,
    0x18c366, 0x18c54f, 0x18c75a, 0x18cd3e, 0x18d36a, 0x18df46, 0x18ea1f, 0x18f267,
    0x190c1b, 0x1a0b12, 0x1a0c49, 0x1a0dd1, 0x1a0f96, 0x1a364f, 0x1a66bd, 0x1a6eb6,
    0x1bc74f, 0x1e0147, 0x1f7a8a, 0x1f94d5, 0x1fcebc, 0x1fd9e6, 0x1fecc5, 0x2047ce,
    0x2049a1, 0x204fce, 0x205141, 0x205c98, 0x2064a9, 0x2067fc, 0x2075f8, 0x207a23,
    0x209bdd, 0x20aa09, 0x20b519, 0x20bd03, 0x20cbe9, 0x20cf99, 0x20cfc4, 0x20cfea,
    0x20d189, 0x20f8e0, 0x215143, 0x215b72, 0x21b5bf, 0x220f7d, 0x227b5f, 0x2286d6,
    0x22876c, 0x23518a, 0x23c2b6, 0x241e46, 0x2422ec, 0x244b15, 0x27d0d8, 0x287723,
    0x2b3697, 0x2bf824, 0x2c3964, 0x2c4fea, 0x2eacd8, 0x2f17fe, 0x2f2d18, 0x2f7c44,
    0x2f7d78, 0x2f7f5e, 0x2f881c, 0x2faaac, 0x2fb974, 0x2fc04e, 0x304b40, 0x30a661,
    0x30ce47, 0x30dbd5, 0x317ebf, 0x317f50, 0x3286ed, 0x334502, 0x335187, 0x3360fd,
    0x3442bc, 0x344b4c, 0x346fca, 0x347310, 0x3485c1, 0x348eda, 0x3544f5, 0x354526,
    0x355746, 0x35746a, 0x35f7fe, 0x35f8c7, 0x35f9ea, 0x360c1e, 0x363b77, 0x364b6a,
    0x364bb6, 0x373e9e, 0x37cc87, 0x37cea8, 0x381aea, 0x3839df, 0x397b3c, 0x398cd5,
    0x39f4ca, 0x3a3762, 0x3a41aa, 0x3a445d, 0x3a4a23, 0x3a69c4, 0x3a6ed6, 0x3af5eb,
    0x3be292, 0x3be731, 0x3c263e, 0x3c431a, 0x3c45d8, 0x3e623d, 0x3e6ab2, 0x3e6d69,
    0x3e6e65, 0x40cb02, 0x418e5c, 0x429465, 0x429767, 0x42983b, 0x42a2b7, 0x42b7af,
    0x42b8bb, 0x42b9c7, 0x42bd57, 0x42bddb, 0x42be4f, 0x42bf33, 0x42bfa7, 0x42c0b7,
    0x42c1f7, 0x42c26b, 0x42e126, 0x42ef3c, 0x434851, 0x4416df, 0x443212, 0x449220,
    0x44e2b5, 0x44f7c2, 0x45481c, 0x4619f7, 0x4707fb, 0x472886, 0x4a5d80, 0x4b0b78,
    0x4d1cbe, 0x4fcaf2, 0x541e0f, 0x541ec7, 0x563ce5, 0x5724e7, 0x577701, 0x587746,
    0x589b0f, 0x5957a0, 0x5c0b57, 0x5c47ce, 0x5d135a, 0x5d183c, 0x5d19d5, 0x5d1e81,
    0x5da53c, 0x5daf31, 0x5eab06, 0x5ec0e0, 0x5ec7e7, 0x5efb95, 0x5f0659, 0x5f09cf,
    0x5f1375, 0x5f142e, 0x5f1510, 0x5f1bad, 0x5f5469, 0x83c660, 0x84ff60, 0x93a0a5,
    0x93a0db, 0x93a10c, 0x9b582f, 0x9f40ce, 0x9f5d31, 0x9f8abd, 0x9fb0ca, 0x9fbaa8,
    0x9fc088, 0x9fc548, 0x9fd9e5, 0x9fdda2, 0xa066b8, 0xa0698e, 0xa3466b, 0xa4dc13,
    0xa7239f, 0xcfeb1b, 0xd04132, 0xd406b3, 0xd41465, 0xd4fd92, 0xd54d83, 0xd967bc,
    0xd967ec, 0xd96818, 0xd9d025, 0xd9ece9, 0xda64c0, 0xda6f0f, 0xda700a, 0xda7983,
    0xdaa824, 0xdabe9d, 0xdac3a9, 0xdb69dd, 0xdc1679, 0xe02dfa, 0xe1b9c3, 0xe1ba57,
    0xe1baeb, 0xe237a2, 0xe23e49, 0xe23ee9, 0xe23f89, 0xe24029, 0xe240f7, 0xe3ccc1,
    0xe421f6, 0xe4b4b6, 0xe54bae, 0xe551c6, 0xe562d5, 0xe63633, 0xe64582, 0xe6524b,
    0xe6531d, 0xe66af6, 0xe670f5, 0xe6751e, 0xe6ac75, 0xe6afec, 0xe72caf, 0xe785a9,
    0xedd85c, 0xedee70, 0xedef09, 0xef45bc, 0xf094d6, 0xf0ae08, 0xf4aaef, 0xf4e018,
    0xf4e3e4,
};
#define FM6_TLS_SLOT0_N (sizeof fm6_tls_slot0 / sizeof *fm6_tls_slot0)   /* 329 */
static uint8_t *slot0_game;
static const uint32_t *slot0_sites;
static size_t slot0_n;

/* registro destino de "65 48/4c 8b XX 25 58000000" (mov T, gs:[0x58]) ou -1 */
static int slot0_reg(const uint8_t *p)
{
    if (p[0] != 0x65 || (p[1] & ~4) != 0x48 || p[2] != 0x8b || (p[3] & 0xc7) != 0x04 || p[4] != 0x25 ||
        p[5] != 0x58 || p[6] || p[7] || p[8]) return -1;
    return (p[1] & 4) << 1 | (p[3] >> 3 & 7);
}

/* mov T, gs:[0x58] ; lea T, [T + indice*8] ; ret */
static size_t slot0_stub(uint8_t *s, int r, DWORD index)
{
    size_t n = 0;
    uint32_t disp = index * 8;
    s[n++] = 0x65; s[n++] = 0x48 | (r >> 3) << 2; s[n++] = 0x8b; s[n++] = 0x04 | (r & 7) << 3;
    s[n++] = 0x25; s[n++] = 0x58; s[n++] = 0; s[n++] = 0; s[n++] = 0;
    s[n++] = 0x48 | (r >> 3) << 2 | (r >> 3); s[n++] = 0x8d; s[n++] = 0x80 | (r & 7) << 3 | (r & 7);
    if ((r & 7) == 4) s[n++] = 0x24;
    memcpy(s + n, &disp, 4); n += 4;
    s[n++] = 0xc3;
    return n;
}

/* memoria executavel a menos de 2 GB da imagem (para call rel32) */
uint8_t *alloc_near(uint8_t *mod, size_t size)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(mod + ((IMAGE_DOS_HEADER *)mod)->e_lfanew);
    uintptr_t a = ((uintptr_t)mod + nt->OptionalHeader.SizeOfImage + 0xffff) & ~(uintptr_t)0xffff;
    uintptr_t end = (uintptr_t)mod + 0x70000000;
    for (; a < end; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

static void tls_slot0_redirect(DWORD index)
{
    uint8_t *stubs, *stub[16] = { 0 };
    size_t i, used = 0;
    if (!slot0_n) return;
    for (i = 0; i < slot0_n; i++)
        if (slot0_reg(slot0_game + slot0_sites[i]) < 0) {
            wlog("TLS: site do slot 0 em game+%#x nao confere; nenhum alterado", slot0_sites[i]);
            return;
        }
    if (!(stubs = alloc_near(slot0_game, 16 * 32))) { wlog("TLS: sem memoria perto do jogo para os stubs"); return; }
    for (i = 0; i < slot0_n; i++) {
        uint8_t *at = slot0_game + slot0_sites[i], code[9] = { 0xe8, 0, 0, 0, 0, 0x0f, 0x1f, 0x40, 0x00 };
        int r = slot0_reg(at);
        int32_t rel;
        if (!stub[r]) { stub[r] = stubs + used * 32; slot0_stub(stub[r], r, index); used++; }
        rel = (int32_t)(stub[r] - (at + 5));
        memcpy(code + 1, &rel, 4);
        write_mem(at, code, sizeof code);
    }
    FlushInstructionCache(GetCurrentProcess(), stubs, 16 * 32);
    wlog("TLS: %zu leituras do slot 0 do jogo -> indice %lu (%zu stubs em %p)", slot0_n, index, used, (void *)stubs);
}

void game_tls_apply(void)
{
    HMODULE helper;
    const DWORD *index;
    IMAGE_TLS_DIRECTORY64 *t;
    nt_query_thread_fn query;
    HANDLE snap;
    THREADENTRY32 te;
    int fixed = 0;
    if (!game_tls_pending || InterlockedExchange(&game_tls_pending, 0) == 0) return;
    helper = GetModuleHandleW(L"azimuth-tls.dll");
    index = helper ? (const DWORD *)GetProcAddress(helper, "azimuth_tls_index") : NULL;
    t = helper ? tls_dir((uint8_t *)helper) : NULL;
    query = (nt_query_thread_fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    if (!index || !t || !query || t->EndAddressOfRawData - t->StartAddressOfRawData < game_tls_size) {
        wlog("TLS: azimuth-tls.dll nao confere (modulo %p indice %p); template do jogo NAO aplicado", (void *)helper, (void *)index);
        return;
    }
    write_mem((void *)(uintptr_t)t->StartAddressOfRawData, game_tls, game_tls_size);
    tls_slot0_redirect(*index);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    te.dwSize = sizeof te;
    if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
        do {
            HANDLE h;
            THREAD_BASIC_INFO bi;
            if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
            if (!(h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID))) continue;
            if (query(h, 0, &bi, sizeof bi, NULL) >= 0 && bi.teb) {
                void **slots = *(void ***)((uint8_t *)bi.teb + 0x58);   /* TEB.ThreadLocalStoragePointer */
                if (slots && slots[*index]) { memcpy(slots[*index], game_tls, game_tls_size); fixed++; }
            }
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    wlog("TLS: template do jogo no azimuth-tls.dll (indice %lu) e em %d threads", *index, fixed);
}

/* hook com trampolim: os bytes roubados nao podem ter enderecos relativos a RIP */
int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp_out)
{
    uint8_t *tramp;
    const void *back = at + stolen;
    if (!bytes_match(at, orig_hex, strlen(orig_hex) / 2)) { wlog("hook %p: bytes nao conferem", (void *)at); return 0; }
    tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { wlog("hook %p: VirtualAlloc falhou", (void *)at); return 0; }
    memcpy(tramp, at, stolen);
    tramp[stolen] = 0xff; tramp[stolen + 1] = 0x25;
    memset(tramp + stolen + 2, 0, 4);
    memcpy(tramp + stolen + 6, &back, 8);
    *tramp_out = tramp;
    abs_jmp(at, hook, stolen);
    return 1;
}

int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp_out)
{
    if (!hook_at(g_base + rva, orig_hex, stolen, hook, tramp_out)) return 0;
    wlog("hook %#x instalado", rva);
    return 1;
}

extern int d3d_install(void);
extern int winrt_install(void);
extern int shape_install(void);
extern int debug12_install(void);
extern int fm6_batch_install(uint8_t *game);

/* opcoes do azimuth_fm6.ini */
static void fm6_patches(uint8_t *game)
{
    if (cfg6_int(L"SkipIntro", 0)) {
        static const uint8_t nop2[2] = { 0x66, 0x90 }, jmp8 = 0xeb;
        size_t i, n = sizeof fm6_splash_intro_cmps / sizeof *fm6_splash_intro_cmps;
        for (i = 0; i < n; i++)
            if (!bytes_match(game + fm6_splash_intro_cmps[i], "4138b424d730000074", 9)) break;
        if (i < n) wlog("FM6: SkipIntro: site %#x nao confere, ignorado", fm6_splash_intro_cmps[i]);
        else if (!bytes_match(game + FM6_LOGO_PARADE_CMP, "4438b8d730000075", 8))
            wlog("FM6: SkipIntro: site %#x nao confere, ignorado", FM6_LOGO_PARADE_CMP);
        else {
            for (i = 0; i < n; i++) write_mem(game + fm6_splash_intro_cmps[i] + 8, nop2, 2);
            write_mem(game + FM6_LOGO_PARADE_CMP + 7, &jmp8, 1);
            wlog("FM6: SkipIntro=1 (disablesplashintro forcado: T10_MS_Combined.bk2 e logo parade pulados)");
        }
    }
}

static void hook_apply_patches(void *image, const void *options)
{
    if (is_fm5()) {
        uint8_t *game = *(uint8_t **)image;
        wlog("apply_patches: FM5 em %p, sites do FH2 pulados", game);
        fm5_patches(game, options);
        return;
    }
    if (is_fm6()) {  /* os sites do registro sao do FH2: TLS, heap da APU e batches */
        uint8_t *game = *(uint8_t **)image;
        wlog("apply_patches: FM6 em %p, sem patches", game);
        g_fm6_game = game;
        take_tls_template(game);
        implicit_apu_heap("FM6");
        fm6_batch_install(game);
        fm6_patches(game);
        slot0_game = game; slot0_sites = fm6_tls_slot0; slot0_n = FM6_TLS_SLOT0_N;
        game_tls_pending = 1;
        return;
    }
    orig_apply_patches(image, options);
}

/* ------------------------------------------------------------------------ */
/* XG_TEXTURE2D/3D_DESC: o Azimuth monta no layout do XDK novo (desc D3D11 + 2 dwords ESRAM +  */
/* TileMode + Pitch); o XG do FM5 (XDK nov-2012) tem 1 dword so: TileMode/Pitch 4 bytes antes. */
/* 2D: D3D11 = 11 dwords (TileMode em [0xc], Pitch [0xd]); 3D: 9 dwords ([10], [11]).          */
/* So as chamadas internas do Azimuth passam por aqui; o jogo chama o XG com o proprio layout. */
/* ------------------------------------------------------------------------ */
typedef void (*xg_computer_fn)(void *out, const char *name, uint32_t *desc);
static xg_computer_fn orig_xg_computer;

/* XGTextureAddressComputer: o XG do FM5 tem 6 metodos (0 AddRef, 1 Release, 2 GetResourceLayout,
   3 GetMipLevelOffsetBytes, 4 GetTexelElementOffsetBytes, 5 CopyIntoSubresource); o XDK novo tem
   6 = CopyFromSubresource (tiled -> linear), que o Azimuth usa em 0x1400a30ca para montar o mapa
   de enderecos (destino linear com 0xff, origem tiled com indices) e que nao existe no FM5 ->
   implementado aqui com o slot 4. Os computers do Azimuth recebem esta vtable; os do jogo nao.
   Campos internos do computer do FM5 (layout em +0x54b8, plano a cada 0x460, mip a cada 0x48):
     +0x54c8 MipLevels, +0x54f0 BytesPerElement, +0x5528/+0x552c/+0x5530 Width/Height/Depth em
     elementos do mip, +0x61f0 dimensao (4 = 3D). */
#define XG_NEW_SLOTS 16
static void *xg_vtbl[XG_NEW_SLOTS];
static void *const *xg_fm5_vtbl;

#define XG_BAD_SLOT(n) static long xg_bad_slot_##n(void *self) \
    { wlog("XG: computer %p chamou o slot %d (sem equivalente no XG do FM5)", self, n); return (long)0x80004001; }
XG_BAD_SLOT(7) XG_BAD_SLOT(8) XG_BAD_SLOT(9) XG_BAD_SLOT(10) XG_BAD_SLOT(11)
XG_BAD_SLOT(12) XG_BAD_SLOT(13) XG_BAD_SLOT(14) XG_BAD_SLOT(15)

typedef uint64_t (*xg_texel_offset_fn)(void *self, uint32_t plane, uint32_t mip, uint64_t x,
                                       uint32_t y, uint32_t z_or_slice, uint32_t sample);

static long xg_copy_from_subresource(uint8_t *self, uint8_t *dst, uint32_t plane, uint32_t subres,
                                     const uint8_t *src, uint32_t row_pitch, uint32_t depth_pitch)
{
    xg_texel_offset_fn texel = (xg_texel_offset_fn)xg_fm5_vtbl[4];
    uint32_t mips = *(uint32_t *)(self + 0x54c8), mip, slice, w, h, d, x, y, z, bpe;
    const uint8_t *pl = self + plane * 0x460;
    if (!mips) return (long)0x80070057;
    mip = subres % mips; slice = subres / mips;
    bpe = *(uint32_t *)(pl + 0x54f0);
    w = *(uint32_t *)(pl + mip * 0x48 + 0x5528);
    h = *(uint32_t *)(pl + mip * 0x48 + 0x552c);
    d = *(uint32_t *)(self + 0x61f0) == 4 ? *(uint32_t *)(pl + mip * 0x48 + 0x5530) : 1;
    for (z = 0; z < d; z++)
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint64_t off = texel(self, plane, mip, x, y, d > 1 ? z : slice, 0);
                if (off == (uint64_t)-1) return (long)0x80070057;
                memcpy(dst + (size_t)z * depth_pitch + (size_t)y * row_pitch + (size_t)x * bpe, src + off, bpe);
            }
    return 0;
}

static void xg_wrap_computer(void **obj)
{
    if (!xg_fm5_vtbl) {
        void *bad[] = { xg_bad_slot_7, xg_bad_slot_8, xg_bad_slot_9, xg_bad_slot_10, xg_bad_slot_11,
                        xg_bad_slot_12, xg_bad_slot_13, xg_bad_slot_14, xg_bad_slot_15 };
        int i;
        xg_fm5_vtbl = *(void *const **)obj;
        for (i = 0; i < XG_NEW_SLOTS; i++)
            xg_vtbl[i] = i < 6 ? xg_fm5_vtbl[i] : i == 6 ? (void *)xg_copy_from_subresource : bad[i - 7];
        wlog("XG: vtable do computer %p -> layout novo (6 = CopyFromSubresource proprio)", (void *)xg_fm5_vtbl);
    }
    if (*(void *const **)obj == xg_fm5_vtbl) *obj = xg_vtbl;
}

/* XG do FM6 (OS "era", tools\xg_vt.py): 12 metodos contra os 8 do FH2 - 3 e 4 novos (getters de +0x54b8/+0x54c0),
   7 novo (397 bytes) e 11 novo no fim. FH2 s -> FM6: 0..2 = s, 3 GetMipLevelOffsetBytes = 5, 4 GetTexel... = 6,
   5 CopyIntoSubresource = 8, 6 CopyFromSubresource = 9, 7 = 10 (mesmos tamanhos/prologos). Sem isso o slot 6 do
   Azimuth (refresh_title_texture, 0x1400a1db0) caia no GetTexelElementOffsetBytes do FM6 e voltava -1
   ("XG untile subresource=0 HRESULT=0xffffffff"). XG_RESOURCE_LAYOUT igual (GetResourceLayout 0x1400 = mesma copia). */
#define XG_FH2_SLOTS 8
static const uint8_t xg_fm6_slot[XG_FH2_SLOTS] = { 0, 1, 2, 5, 6, 8, 9, 10 };
static void *xg_fm6_new[XG_FH2_SLOTS];
static void *const *xg_fm6_vtbl;

static void xg_wrap_computer_fm6(void **obj)
{
    void *const *vt = *(void *const **)obj;
    if (!xg_fm6_vtbl) {
        static const char get54b8[] = "488b81b8540000c3", get54c0[] = "488b81c0540000c3";
        int i;
        if (!bytes_match(vt[3], get54b8, 8) || !bytes_match(vt[4], get54c0, 8)) {
            static int logged;
            if (!logged++) wlog("XG: computer %p do FM6 com vtable %p inesperada, sem conversao", (void *)obj, (void *)vt);
            return;
        }
        for (i = 0; i < XG_FH2_SLOTS; i++) xg_fm6_new[i] = vt[xg_fm6_slot[i]];
        xg_fm6_vtbl = vt;
        wlog("XG: vtable do computer do FM6 %p -> layout do FH2 (12 -> 8 slots)", (void *)vt);
    }
    if (vt == xg_fm6_vtbl) *obj = xg_fm6_new;
}

static void hook_xg_computer(void *out, const char *name, uint32_t *desc)
{
    if (is_fm6()) {
        orig_xg_computer(out, name, desc);
        if (*(void ***)out) xg_wrap_computer_fm6(*(void ***)out);
        return;
    }
    if (is_fm5()) {
        uint32_t n = !strcmp(name, "XGCreateTexture2DComputer") ? 11 :
                     !strcmp(name, "XGCreateTexture3DComputer") ? 9 : 0;
        if (n) {  /* desc e copia local do chamador: converter no lugar */
            desc[n + 1] = desc[n + 2];   /* TileMode */
            desc[n + 2] = desc[n + 3];   /* Pitch */
            desc[n + 3] = 0;
        }
        orig_xg_computer(out, name, desc);
        if (*(void ***)out) xg_wrap_computer(*(void ***)out);  /* out[0] = computer */
        return;
    }
    orig_xg_computer(out, name, desc);
}

/* ------------------------------------------------------------------------ */

static int install(void)
{
    static const char apply_orig[] = "48895c24184889742420555741544156";
    static const char resolver_orig[] = "48895c2410488974241848897c242055";
    static const char xg_orig[] ="4883fe08751c48b978675f782e646c6c483908750d";
    size_t i;

    /* 1) conferir tudo antes de escrever qualquer coisa */
    if (!bytes_match(g_base + RVA_ALLOW_SPAN + 8, "0200000000000000", 8) ||
        *(void **)(g_base + RVA_ALLOW_SPAN) != g_base + RVA_ALLOW_ARRAY) {
        wlog("allow list nao confere"); return 0;
    }
    for (i = 0; i < sizeof exe_patches / sizeof exe_patches[0]; i++)
        if (!bytes_match(g_base + exe_patches[i].rva, exe_patches[i].orig, exe_patches[i].len)) {
            wlog("bytes nao conferem em %#x (%s)", exe_patches[i].rva, exe_patches[i].what); return 0;
        }
    if (!bytes_match(g_base + RVA_XG_NAME_CMP, xg_orig, 21) ||
        !bytes_match(g_base + RVA_APPLY_PATCHES, apply_orig, 16) ||
        !bytes_match(g_base + RVA_RESOLVER, resolver_orig, 16)) {
        wlog("bytes do resolvedor/apply_patches nao conferem"); return 0;
    }

    /* 2) registros + allow list {fh2, fh2 f&f, fm5, fm6} */
    build_record();
    build_fm6_record();
    new_allow[0] = *(void **)(g_base + RVA_ALLOW_ARRAY);
    new_allow[1] = *(void **)(g_base + RVA_ALLOW_ARRAY + 8);
    new_allow[2] = fm5_rec;
    new_allow[3] = fm6_rec;
    {
        struct { const void **ptr; uint64_t count; } span = { new_allow, 4 };
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
    wlog("patch %#x: resolvedor aceita xg.dll", RVA_XG_NAME_CMP);

    /* 5) hook do apply_patches (14 bytes roubados, sem RIP-relativos) */
    if (!hook_function(RVA_APPLY_PATCHES, apply_orig, APPLY_STOLEN,
                       (void *)hook_apply_patches, (void **)&orig_apply_patches)) return 0;
    wlog("patch %#x: apply_patches com hook", RVA_APPLY_PATCHES);

    /* 6) resolvedor de imports: d3d11_x.dll!D3D11CreateDevice para o FM5 (d3d_fm5.c) */
    if (!d3d_install()) return 0;
    wlog("patch %#x: resolvedor com hook (D3D11CreateDevice)", RVA_RESOLVER);

    /* 7) descritores do XG no layout do XDK do FM5 */
    if (!hook_function(RVA_XG_COMPUTER, "48895c24205556574154415541564157", XG_COMPUTER_STOLEN,
                       (void *)hook_xg_computer, (void **)&orig_xg_computer)) return 0;
    wlog("patch %#x: XG texture computer com hook (layout do desc do FM5)", RVA_XG_COMPUTER);

    /* 8) ativacao WinRT: classes do XDK do FM5 sem equivalente (SpeechRecognizer) -> fabrica stub */
    if (!winrt_install()) return 0;
    wlog("patch 0x9a680/0x208790: ativacao WinRT (fabricas stub) e GetTemplateByName (5 templates do FM5)");

    /* 9) grafos SHAPE (audio) do XDK do FM5 (shape_fm5.c) */
    if (!shape_install()) return 0;

    /* 10) diagnostico opcional: debug layer do D3D12 (D3D12Debug no .ini, debug12_fm5.c) */
    debug12_install();
    return 1;
}

/* ---- repasse para o winmm real ---- */
extern void *winmm_real[];
extern const char *const winmm_names[];

static void load_real_winmm(void)
{
    wchar_t path[MAX_PATH];
    HMODULE real;
    int i;
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    wcscpy(path + n, L"\\winmm.dll");
    real = LoadLibraryW(path);
    if (!real) { wlog("winmm real nao carregou"); return; }
    for (i = 0; winmm_names[i]; i++)
        winmm_real[i] = (void *)GetProcAddress(real, winmm_names[i]);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH], *slash;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, path, MAX_PATH);
        slash = wcsrchr(path, L'\\');
        wcscpy(slash ? slash + 1 : path, L"azimuth_fm5.log");
        g_log = _wfopen(path, L"w");
        wcscpy(slash ? slash + 1 : path, L"azimuth_fm5.ini");
        wcscpy(g_ini, path);
        wlog("config: %ls (SkipIntro=%d SkipPresentationVideo=%d UnlockDLC=%d EnableMarketplace=%d)", g_ini, cfg_int(L"SkipIntro", 0),
             cfg_int(L"SkipPresentationVideo", 0), cfg_int(L"UnlockDLC", 0),
             cfg_int(L"EnableMarketplace", 0));
        wcscpy(slash ? slash + 1 : path, L"azimuth_fm6.ini");
        wcscpy(g_ini6, path);
        wlog("config: %ls (SkipIntro=%d)", g_ini6, cfg6_int(L"SkipIntro", 0));
        load_real_winmm();
        g_base = (uint8_t *)GetModuleHandleW(NULL);
        GetModuleFileNameW(NULL, path, MAX_PATH);
        slash = wcsrchr(path, L'\\');
        if (_wcsicmp(slash ? slash + 1 : path, L"azimuth.exe") != 0) {
            wlog("processo nao e azimuth.exe, so repasse");
            return TRUE;
        }
        if (!install()) {
            wlog("NADA FOI ALTERADO");
            MessageBoxW(NULL, L"azimuth.exe diferente do esperado; patches do FM5 nao aplicados.\n"
                              L"Veja azimuth_fm5.log.", L"winmm FM5", MB_ICONWARNING);
        }
    }
    return TRUE;
}
