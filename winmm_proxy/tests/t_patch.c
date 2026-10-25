/*
 * Testes de main.c, azimuth.c, common/{hook,tls,azimuth_mem,ui}.c, fm5/{fm5,xg}.c e fm6/fm6.c: helpers de patch/hook, registro do FM5, XG e - com os binarios reais mapeados
 * (azimuth.exe na raiz, exe do FM5 em game_binaries) - azimuth_install() e fm5_patches() completos.
 */
#include "../main.c"
#include "../azimuth.c"
#include "../common/hook.c"
#include "../common/tls.c"
#include "../common/azimuth_mem.c"
#include "../common/ui.c"
#include "../fm5/fm5.c"
#include "../fm5/xg.c"
#include "../fm6/fm6.c"
#include "test.h"

extern int dr_on;
#include <stdlib.h>
extern void debug12_create_device(void);   /* common/debug12.c (so o endereco) */

/* ---- log / ini / g_base falso (usados pelos outros t_*.c) ---- */
static char log_path[MAX_PATH];
static char *log_buf;

void log_capture(void)
{
    if (g_log) fclose(g_log);
    snprintf(log_path, sizeof log_path, "%s\\winmm_proxy\\build\\test_log.txt", t_root());
    g_log = fopen(log_path, "w+");
}

const char *log_text(void)
{
    long n;
    free(log_buf);
    log_buf = NULL;
    if (!g_log) return "";
    fflush(g_log);
    n = ftell(g_log);
    log_buf = calloc(1, n + 1);
    fseek(g_log, 0, SEEK_SET);
    n = (long)fread(log_buf, 1, n, g_log);
    fseek(g_log, 0, SEEK_END);
    return log_buf;
}

int log_has(const char *s) { return strstr(log_text(), s) != NULL; }

void ini_set(const char *body)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\winmm_proxy\\build\\test_fm5.ini", t_root());
    DeleteFileA(path);
    if (body) { FILE *f = fopen(path, "w"); fputs(body, f); fclose(f); }
    MultiByteToWideChar(CP_ACP, 0, path, -1, g_ini, MAX_PATH);
}

void ini6_set(const char *body)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\winmm_proxy\\build\\test_fm6.ini", t_root());
    DeleteFileA(path);
    if (body) { FILE *f = fopen(path, "w"); fputs(body, f); fclose(f); }
    MultiByteToWideChar(CP_ACP, 0, path, -1, g_ini6, MAX_PATH);
}

void dbgini_set(const char *body)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\winmm_proxy\\build\\test_debug.ini", t_root());
    DeleteFileA(path);
    if (body) { FILE *f = fopen(path, "w"); fputs(body, f); fclose(f); }
    MultiByteToWideChar(CP_ACP, 0, path, -1, g_ini_dbg, MAX_PATH);
}

uint8_t *fake_azimuth(int game)
{
    static uint8_t *img;
    if (!img) img = t_rwx(0x3b9000);            /* SizeOfImage do azimuth.exe */
    memset(img, 0, 0x3b9000);
    if (game) *(void **)(img + RVA_GAME_RECORD) = game == 2 ? fm6_rec : fm5_rec;
    g_base = img;
    return img;
}

/* imprime o log se o teste falhou (diagnostico) */
static void dump_log_on_fail(void)
{
    if (t_failed) printf("    --- azimuth_motorsport.log do teste ---\n%s    ---\n", log_text());
}

/* ------------------------------------------------------------------------ */
TEST(bytes_match_hex)
{
    static const uint8_t b[] = { 0x48, 0x89, 0x5c, 0x24, 0xAB };
    CHECK(bytes_match(b, "48895c24ab", 5));
    CHECK(bytes_match(b, "48895C24AB", 5));     /* maiusculas */
    CHECK(bytes_match(b, "48895c", 3));         /* prefixo */
    CHECK(!bytes_match(b, "48895c24ac", 5));
}

TEST(abs_jmp_encoding)
{
    uint8_t *p = t_rwx(64);
    void *to = (void *)0x1122334455667788ull;
    memset(p, 0x90, 64);
    abs_jmp(p, to, 16);
    CHECK_HEX(p, "ff2500000000" "8877665544332211" "cccc");   /* jmp [rip+0] ; destino ; padding int3 */
    CHECK_EQ(p[16], 0x90);                                    /* nao passa do tamanho */
}

/* funcao de teste: mov [rsp+8],rbx ; mov [rsp+0x10],rsi ; mov [rsp+0x18],rdi ; mov eax,ecx ; add eax,7 ; ret
   (o jmp [rip+0] do hook ocupa 14 bytes: os bytes roubados tem de ser >= 14 e terminar numa instrucao) */
static const char fn_hex[] = "48895c2408" "4889742410" "48897c2418" "89c8" "83c007" "c3";
typedef int (*int_fn)(int);
static int_fn hooked_orig;
static int hook_body(int x) { return hooked_orig(x) * 100; }

TEST(hook_at_trampoline_runs)
{
    uint8_t *f = t_rwx(64);
    unhex(fn_hex, f);
    CHECK_EQ(((int_fn)f)(1), 8);
    REQUIRE(hook_at(f, "48895c2408488974241048897c2418", 15, (void *)hook_body, (void **)&hooked_orig));
    CHECK_HEX(f, "ff2500000000");
    CHECK_EQ(f[14], 0xcc);
    CHECK_EQ(((int_fn)f)(1), 800);               /* hook -> trampolim (15 bytes roubados) -> resto da funcao */
    CHECK_EQ(hooked_orig(2), 9);
}

TEST(hook_at_refuses_wrong_bytes)
{
    uint8_t *f = t_rwx(64);
    void *tramp = NULL;
    memset(f, 0x90, 64);
    log_capture();
    CHECK(!hook_at(f, "48895c2408", 5, (void *)hook_body, &tramp));
    CHECK(tramp == NULL);
    CHECK_EQ(f[0], 0x90);                        /* nada escrito */
    CHECK(log_has("bytes do not match"));
}

TEST(is_fm5_reads_game_record)
{
    fake_azimuth(0);
    CHECK(!is_fm5());
    fake_azimuth(1);
    CHECK(is_fm5());
}

/* A2C sem xadrez: os PSOs da pipeline library nascem antes do registro do jogo -> vale se todos os jogos querem */
TEST(game_a2c_no_dither_before_and_after_record)
{
    fake_azimuth(0);
    ini6_set("[FM6]\nAlphaToCoverageNoDither=1\n");
    CHECK_EQ(game_a2c_no_dither(), 1);                 /* jogo ainda desconhecido: FM5 (1) e FM6 (ini 1) */
    ini6_set("[FM6]\nAlphaToCoverageNoDither=0\n");
    CHECK_EQ(game_a2c_no_dither(), 0);                 /* um jogo nao quer: nada antes do registro */
    fake_azimuth(1);
    CHECK_EQ(game_a2c_no_dither(), 1);                 /* FM5 carregado: sempre */
    fake_azimuth(2);
    CHECK_EQ(game_a2c_no_dither(), 0);                 /* FM6 carregado: o ini */
    ini6_set(NULL);
    CHECK_EQ(game_a2c_no_dither(), 1);                 /* FM6 sem a chave: padrao 1 */
    fake_azimuth(0);
}

static int orig_apply_calls;
static void fake_orig_apply(void *image, const void *options) { (void)image; (void)options; orig_apply_calls++; }

/* FM6 (registro carregado = fm6_rec): sites do FH2 nao rodam; outro jogo segue para o original */
TEST(apply_patches_skips_fm6)
{
    uint8_t *game = VirtualAlloc(NULL, 0xdab000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);   /* PE sem TLS */
    uint8_t *img[1] = { game }, opt[0x40] = { 0 };
    uint8_t *az = fake_azimuth(0);
    ((IMAGE_DOS_HEADER *)game)->e_lfanew = 0x80;
    log_capture();
    ini6_set("[FM6]\nSkipIntro=1\n");
    orig_apply_patches = fake_orig_apply;
    orig_apply_calls = 0;
    *(void **)(az + RVA_GAME_RECORD) = fm6_rec;
    /* ApuCreateHeap falso (xor eax,eax ; ret) com a tabela como o Azimuth deixaria */
    memcpy(az + RVA_APU_CREATE_HEAP, "\x31\xc0\xc3", 3);
    *(void **)(az + RVA_APU_HEAPS) = VirtualAlloc(NULL, 0x500000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *(uint32_t *)(az + RVA_APU_HEAPS + 8) = 0x500000; *(uint32_t *)(az + RVA_APU_HEAPS + 0xc) = 0x1000000;
    memcpy(az + 0xdeadd, "\x48\x3d\xa0\x0f\x00\x00", 6);             /* cmp rax, 4000 do pool de allocators */
    dbgini_set("[Debug]\nDeferRelease=0\n");                            /* sem a thread de drenagem no teste */
    CHECK(is_fm6() && !is_fm5());
    hook_apply_patches(img, opt);
    CHECK(!dr_on && !log_has("DeferRelease"));
    dbgini_set(NULL);
    CHECK_EQ(orig_apply_calls, 0);
    CHECK_HEX(az + 0xdeadd, "483dffffff7f");                            /* FM6: nunca destroi */
    CHECK(log_has("FM6"));
    CHECK(log_has("APU: heap created for FM6"));                         /* XDK sem ApuCreateHeap */
    CHECK_EQ(*(uint32_t *)(az + RVA_APU_HEAPS + 8), 0x1000000);
    VirtualFree(*(void **)(az + RVA_APU_HEAPS), 0, MEM_RELEASE);
    CHECK(slot0_game == game && slot0_n == 329 && game_tls_pending);   /* sites do slot 0 para o 1o import */
    CHECK(log_has("FM6 batch builder at game+0xda9b94 does not match"));   /* zerado: nada muda */
    CHECK(log_has("FM6: SkipIntro: site 0xa06b33 does not match, ignored"));     /* opcoes do azimuth_fm6.ini */
    CHECK(g_game == game && g_trace_game == game);
    CHECK(!log_has("0x14c2f0") && !log_has("XG: FM5"));                   /* nada do FM5 (envio do XG) no FM6 */
    ini6_set(NULL);
    slot0_n = 0; game_tls_pending = 0;
    *(void **)(az + RVA_GAME_RECORD) = NULL;
    memcpy(az + 0xdeadd, "\x48\x3d\xa0\x0f\x00\x00", 6);
    hook_apply_patches(img, opt);
    CHECK_EQ(orig_apply_calls, 1);
    CHECK_HEX(az + 0xdeadd, "483da00f0000");                            /* outro jogo sem debug.ini: igual */
    orig_apply_patches = NULL;
    VirtualFree(game, 0, MEM_RELEASE);
}

/* TLS do FM6: template guardado/zerado na imagem e aplicado no azimuth-tls.dll real (thread atual + nova) */
static DWORD tls_index_seen;
static uint8_t tls_new_thread[0x48];
static DWORD WINAPI tls_thread(void *arg)
{
    (void)arg;
    memcpy(tls_new_thread, ((void **)__readgsqword(0x58))[tls_index_seen], sizeof tls_new_thread);
    return 0;
}

TEST(fm6_tls_template_applied)
{
    static uint8_t img[0x1000] __attribute__((aligned(16)));
    IMAGE_DOS_HEADER *dos = (void *)img;
    IMAGE_NT_HEADERS64 *nt = (void *)(img + 0x80);
    IMAGE_TLS_DIRECTORY64 *t = (void *)(img + 0x400);
    uint8_t *tmpl = img + 0x800, *mine;
    HMODULE helper;
    HANDLE th;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\data\\bin\\azimuth-tls.dll", t_root());
    if (!(helper = LoadLibraryExA(path, NULL, 0x900))) { test_skip("data\\bin\\azimuth-tls.dll ausente"); return; }
    log_capture();
    dos->e_lfanew = 0x80;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress = 0x400;
    t->StartAddressOfRawData = (uintptr_t)tmpl;
    t->EndAddressOfRawData = (uintptr_t)tmpl + 0x48;
    memset(tmpl + 0x20, 0xff, 4);                    /* como o FM6 */
    take_tls_template(img);
    CHECK(game_tls_pending && game_tls_size == 0x48);
    CHECK_HEX(tmpl + 0x20, "00000000");               /* imagem zerada para a checagem do Azimuth */
    game_tls_apply();
    CHECK(!game_tls_pending);
    tls_index_seen = *(const DWORD *)GetProcAddress(helper, "azimuth_tls_index");
    mine = ((uint8_t **)__readgsqword(0x58))[tls_index_seen];
    CHECK_HEX(mine + 0x1f, "00ffffffff00");           /* thread existente */
    th = CreateThread(NULL, 0, tls_thread, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    CHECK_HEX(tls_new_thread + 0x1f, "00ffffffff00");  /* thread nova copia do template do helper */
    CHECK(log_has("TLS: game template in azimuth-tls.dll"));
    /* template ja zerado: nada pendente */
    take_tls_template(img);
    CHECK(!game_tls_pending);
    dump_log_on_fail();
}

/* stubs do TLS "slot 0": mov T, gs:[0x58] ; lea T, [T + indice*8] ; ret */
TEST(tls_slot0_stub_encoding)
{
    uint8_t s[32];
    CHECK_EQ(slot0_stub(s, 0, 27), 17);
    CHECK_HEX(s, "65488b042558000000488d80d8000000c3");             /* rax */
    CHECK_EQ(slot0_stub(s, 12, 27), 18);
    CHECK_HEX(s, "654c8b2425580000004d8da424d8000000c3");           /* r12 (SIB) */
    CHECK_EQ(slot0_stub(s, 9, 3), 17);
    CHECK_HEX(s, "654c8b0c25580000004d8d8918000000c3");             /* r9 */
    CHECK_EQ(slot0_reg((const uint8_t *)"\x65\x4c\x8b\x24\x25\x58\x00\x00\x00"), 12);
    CHECK_EQ(slot0_reg((const uint8_t *)"\x65\x48\x8b\x0c\x25\x58\x00\x00\x00"), 1);
    CHECK_EQ(slot0_reg((const uint8_t *)"\x65\x48\x8b\x04\x25\x30\x00\x00\x00"), -1);   /* gs:[0x30] */
    CHECK_EQ(slot0_reg((const uint8_t *)"\x65\x48\x8b\x44\x25\x58\x00\x00\x00"), -1);   /* outro modrm */
}

/* sites falsos executaveis: depois do redirect [T] = ThreadLocalStoragePointer[indice] */
TEST(tls_slot0_redirect_runs)
{
    static const uint32_t sites[3] = { 0x1100, 0x1200, 0x1300 };
    uint8_t *img = t_rwx(0x2000);
    IMAGE_DOS_HEADER *dos = (void *)img;
    IMAGE_NT_HEADERS64 *nt = (void *)(img + 0x80);
    void **tlsp = (void **)__readgsqword(0x58);
    uint64_t (*f)(void);
    int32_t rel;
    memset(img, 0xcc, 0x2000);
    dos->e_lfanew = 0x80;
    nt->OptionalHeader.SizeOfImage = 0x2000;
    memcpy(img + 0x1100, "\x65\x48\x8b\x04\x25\x58\x00\x00\x00\xc3", 10);                  /* rax ; ret */
    memcpy(img + 0x1200, "\x65\x48\x8b\x0c\x25\x58\x00\x00\x00\x48\x89\xc8\xc3", 13);      /* rcx ; mov rax,rcx */
    memcpy(img + 0x1300, "\x65\x4c\x8b\x1c\x25\x58\x00\x00\x00\x4c\x89\xd8\xc3", 13);      /* r11 ; mov rax,r11 */
    log_capture();
    slot0_game = img; slot0_sites = sites; slot0_n = 3;
    tls_slot0_redirect(0);
    CHECK_EQ(img[0x1100], 0xe8);
    CHECK_HEX(img + 0x1105, "0f1f4000c3");
    memcpy(&rel, img + 0x1101, 4);
    CHECK_HEX(img + 0x1105 + rel, "65488b04255800000048");                              /* stub do rax */
    f = (void *)(img + 0x1100); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[0]);
    f = (void *)(img + 0x1200); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[0]);
    tls_slot0_redirect(1);                                                                 /* ja patchado: nao confere */
    CHECK(log_has("does not match; none changed"));
    memcpy(img + 0x1100, "\x65\x48\x8b\x04\x25\x58\x00\x00\x00\xc3", 10);
    memcpy(img + 0x1200, "\x65\x48\x8b\x0c\x25\x58\x00\x00\x00\x48\x89\xc8\xc3", 13);
    memcpy(img + 0x1300, "\x65\x4c\x8b\x1c\x25\x58\x00\x00\x00\x4c\x89\xd8\xc3", 13);
    tls_slot0_redirect(5);
    f = (void *)(img + 0x1100); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    f = (void *)(img + 0x1200); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    f = (void *)(img + 0x1300); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    CHECK(log_has("TLS: 3 reads of the game's slot 0 -> index 5 (3 stubs"));
    /* um site que nao confere: nenhum alterado */
    memcpy(img + 0x1100, "\x65\x48\x8b\x04\x25\x58\x00\x00\x00\xc3", 10);
    memcpy(img + 0x1300, "\x65\x48\x8b\x04\x25\x30\x00\x00\x00\xc3", 10);
    tls_slot0_redirect(5);
    CHECK_HEX(img + 0x1100, "65488b0425580000");
    slot0_n = 0;
    dump_log_on_fail();
}

/* exe real do FM6: todos os sites da tabela sao "mov T, gs:[0x58]" e viram call para o stub do mesmo T */
TEST(fm6_tls_slot0_sites_match_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL), regs[FM6_TLS_SLOT0_N];
    size_t i, ok = 0;
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    for (i = 0; i < FM6_TLS_SLOT0_N; i++) regs[i] = (uint8_t)slot0_reg(game + fm6_tls_slot0[i]);
    for (i = 0; i < FM6_TLS_SLOT0_N; i++) CHECK(regs[i] < 16);
    log_capture();
    slot0_game = game; slot0_sites = fm6_tls_slot0; slot0_n = FM6_TLS_SLOT0_N;
    tls_slot0_redirect(27);
    for (i = 0; i < FM6_TLS_SLOT0_N; i++) {
        uint8_t *at = game + fm6_tls_slot0[i], *stub;
        int32_t rel;
        memcpy(&rel, at + 1, 4);
        stub = at + 5 + rel;
        ok += at[0] == 0xe8 && !memcmp(at + 5, "\x0f\x1f\x40\x00", 4) && slot0_reg(stub) == regs[i] &&
              stub[10] == 0x8d && *(uint32_t *)(stub + ((regs[i] & 7) == 4 ? 13 : 12)) == 27 * 8;
    }
    CHECK_EQ(ok, FM6_TLS_SLOT0_N);
    slot0_n = 0;
    pe_unmap(game);
    dump_log_on_fail();
}

TEST(cfg_int_reads_ini)
{
    ini_set("[FM5]\nSkipIntro=1\nUnlockDLC=1\n");
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 1);
    CHECK_EQ(cfg_int(L"UnlockDLC", 0), 1);
    CHECK_EQ(cfg_int(L"EnableMarketplace", 5), 5);       /* ausente -> padrao */
    ini_set(NULL);
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 0);
}

/* D3D12Hooks=0 (diagnostico de overlays): o call do D3D12CreateDevice fica como esta; padrao: aponta para o proxy */
extern int debug12_install(void);
TEST(debug12_hooks_switch)
{
    static const uint8_t call[6] = { 0xff, 0x15, 0x83, 0x90, 0x21, 0x00 };
    uint8_t *az = fake_azimuth(0);
    memcpy(az + 0x567df, call, 6);
    dbgini_set("[Debug]\nD3D12Hooks=0\n");
    CHECK_EQ(debug12_install(), 1);
    CHECK(!memcmp(az + 0x567df, call, 6));
    dbgini_set("[Debug]\nD3D12Hooks=0\nD3D12Debug=1\n");     /* debug layer precisa do gancho */
    CHECK_EQ(debug12_install(), 1);
    CHECK(!memcmp(az + 0x567df, call, 2));
    CHECK(memcmp(az + 0x567df + 2, call + 2, 4) != 0);
    memcpy(az + 0x567df, call, 6);
    memcpy(az + 0x567df, call, 6);
    log_capture();
    dbgini_set("[Debug]\nD3D12Hooks=2\n");                   /* bisseccao: com gancho, so o gpuguard */
    CHECK_EQ(debug12_install(), 1);
    CHECK(memcmp(az + 0x567df + 2, call + 2, 4) != 0);
    CHECK(log_has("D3D12Hooks=2 -> only the gpuguard hooks"));
    memcpy(az + 0x567df, call, 6);
    dbgini_set(NULL);                                         /* padrao: com gancho */
    CHECK_EQ(debug12_install(), 1);
    CHECK(memcmp(az + 0x567df + 2, call + 2, 4) != 0);
    CHECK(!log_has("Wine/Proton"));
}

/* Wine/Proton: sem D3D12Hooks no debug.ini o padrao e 3 (sem gpuguard); a chave explicita e o D3D12Debug valem */
extern int (*debug12_wine)(void);
static int fake_wine(void) { return 1; }
TEST(debug12_wine_default)
{
    static const uint8_t call[6] = { 0xff, 0x15, 0x83, 0x90, 0x21, 0x00 };
    uint8_t *az = fake_azimuth(0);
    int (*real)(void) = debug12_wine;
    debug12_wine = fake_wine;
    memcpy(az + 0x567df, call, 6);
    log_capture();
    dbgini_set(NULL);
    CHECK_EQ(debug12_install(), 1);
    CHECK(memcmp(az + 0x567df + 2, call + 2, 4) != 0);       /* gancho fica (hooks do stutter) */
    CHECK(log_has("Wine/Proton -> gpuguard off"));
    CHECK(log_has("D3D12Hooks=3 -> only the common/stutter.c hooks"));
    memcpy(az + 0x567df, call, 6);
    log_capture();
    dbgini_set("[Debug]\nD3D12Hooks=1\n");                 /* explicito: gpuguard ligado */
    CHECK_EQ(debug12_install(), 1);
    CHECK(!log_has("Wine/Proton"));
    CHECK(!log_has("D3D12Hooks=3"));
    memcpy(az + 0x567df, call, 6);
    log_capture();
    dbgini_set("[Debug]\nD3D12Debug=3\n");                 /* diagnostico: todos os hooks */
    CHECK_EQ(debug12_install(), 1);
    CHECK(!log_has("Wine/Proton"));
    memcpy(az + 0x567df, call, 6);
    dbgini_set(NULL);
    debug12_wine = real;
}

/* debug.ini: so a secao [Debug] vale (D3D12Debug, DrawSkip, DrawSkipHashes); o azimuth_fm5.ini nao conta */
TEST(dbg_reads_debug_ini)
{
    char s[64];
    ini_set("[FM5]\nD3D12Debug=1\n");
    dbgini_set(NULL);
    CHECK_EQ(dbg_int(L"D3D12Debug", 0), 0);
    dbgini_set("[Debug]\nD3D12Debug=2\nDrawSkip=7\nDrawSkipHashes=1a2b3c4d, ff\n");
    CHECK_EQ(dbg_int(L"D3D12Debug", 0), 2);
    CHECK_EQ(dbg_int(L"DrawSkip", 0), 7);
    CHECK_EQ(dbg_str(L"DrawSkipHashes", s, sizeof s), 12);
    CHECK(strcmp(s, "1a2b3c4d, ff") == 0);
    CHECK_EQ(dbg_str(L"DrawSkip", s, 2), 1);              /* corta no tamanho do buffer */
    CHECK(strcmp(s, "7") == 0);
    CHECK_EQ(dbg_str(L"Ausente", s, sizeof s), 0);
    CHECK(s[0] == 0);
    dbgini_set("[FM5]\nD3D12Debug=2\n");
    CHECK_EQ(dbg_int(L"D3D12Debug", 0), 0);
    dbgini_set(NULL);
    ini_set(NULL);
}

/* azimuth_fm6.ini e separado: so a secao [FM6] dele vale para o FM6 */
TEST(cfg6_int_reads_fm6_ini)
{
    ini_set("[FM5]\nSkipIntro=1\n");
    ini6_set("[FM6]\nSkipIntro=1\n");
    CHECK_EQ(cfg6_int(L"SkipIntro", 0), 1);
    ini6_set("[FM5]\nSkipIntro=1\n");
    CHECK_EQ(cfg6_int(L"SkipIntro", 0), 0);
    ini6_set(NULL);
    CHECK_EQ(cfg6_int(L"SkipIntro", 0), 0);
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 1);
    ini_set(NULL);
}

/* exe real do FM6: SkipIntro troca os je do init por nop e o jne do logo parade por jmp */
TEST(fm6_skip_intro_patches_exe)
{
    static const uint32_t sites[] = { 0xa06b33, 0xa06c4d, 0xa07e0c };
    uint8_t *game = pe_map(FM6_EXE, NULL);
    size_t i;
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    dbgini_set("[Debug]\nPlacementCommitted=0\n");   /* so os patches do exe do jogo */
    log_capture();
    ini6_set(NULL);
    fm6_patches(game);
    for (i = 0; i < 3; i++) CHECK_HEX(game + sites[i], "4138b424d730000074");
    CHECK_HEX(game + 0xa08eb6, "4438b8d730000075");
    ini6_set("[FM6]\nSkipIntro=1\n");
    fm6_patches(game);
    for (i = 0; i < 3; i++) CHECK_HEX(game + sites[i], "4138b424d73000006690");
    CHECK_HEX(game + 0xa08eb6, "4438b8d7300000eb2f");
    CHECK(log_has("FM6: SkipIntro=1"));
    CHECK(!log_has("does not match") && !log_has("do not match"));
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_patches(game);
    CHECK(log_has("FM6: SkipIntro: site 0xa06b33 does not match, ignored"));
    ini6_set(NULL);
    dbgini_set(NULL);
    pe_unmap(game);
    dump_log_on_fail();
}

/* exe real do FM6: SkipPresentationVideo esvazia o IntroVideo nos dois leitores da coluna do GameDB */
TEST(fm6_skip_presentation_video_patches_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    dbgini_set("[Debug]\nPlacementCommitted=0\n");   /* so os patches do exe do jogo */
    log_capture();
    ini6_set(NULL);
    fm6_patches(game);
    CHECK_HEX(game + 0xce1c55, "488d4b28e832d846ff");
    CHECK_HEX(game + 0xce1eb4, "85c07519");
    ini6_set("[FM6]\nSkipPresentationVideo=1\n");
    fm6_patches(game);
    CHECK_HEX(game + 0xce1c55, "488d4b280f1f4400004533c0b201");
    CHECK_HEX(game + 0xce1eb4, "85c06690488d15");
    CHECK(log_has("FM6: SkipPresentationVideo=1"));
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK(!log_has("FM6: SkipIntro=1"));
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_patches(game);
    CHECK(log_has("FM6: SkipPresentationVideo: site does not match, ignored"));
    CHECK_HEX(game + 0xce1eb4, "85c06690");
    ini6_set(NULL);
    dbgini_set(NULL);
    pe_unmap(game);
    dump_log_on_fail();
}

/* exe real do FM6: UnlockDLC faz a consulta de posse devolver 1 e o refresh sem inventario marcar tudo comprado */
TEST(fm6_unlock_dlc_patches_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    dbgini_set("[Debug]\nPlacementCommitted=0\n");   /* so os patches do exe do jogo */
    log_capture();
    ini6_set(NULL);
    fm6_patches(game);
    CHECK_HEX(game + 0xa8ad80, "48895c2410565741564883ec20488db9");
    CHECK_HEX(game + 0x43be01, "488dbba000000033d2488bcfe86a000000");
    ini6_set("[FM6]\nUnlockDLC=1\n");
    fm6_patches(game);
    CHECK_HEX(game + 0xa8ad80, "b801000000c35741");
    CHECK_HEX(game + 0x43be01, "488dbba0000000b201488bcfe86a000000");
    CHECK(log_has("FM6: UnlockDLC=1"));
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK(!log_has("FM6: SkipIntro=1"));
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_patches(game);
    CHECK(log_has("FM6: UnlockDLC: sites do not match, ignored"));
    CHECK_HEX(game + 0x43be08, "b201");
    ini6_set(NULL);
    dbgini_set(NULL);
    pe_unmap(game);
    dump_log_on_fail();
}

/* exe real do FM6: frame_rate >= 60 troca o setter do modo de present (slot 0x1a0) do Frame/Frame11 pelo do proxy;
   o mesmo setter nas vtables de PresentationView fica intacto */
TEST(fm6_frame_rate_patches_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL), frame[0x600] = { 0 };
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    log_capture();
    CHECK(*(uint8_t **)(game + 0x19f38e8 + 0x1a0) == game + 0x849940);
    fm6_frame_rate(game, 60);
    CHECK(*(void **)(game + 0x19f4008 + 0x1a0) == (void *)fm5_frame_set_sync_stub);
    CHECK(*(void **)(game + 0x19f38e8 + 0x1a0) == (void *)fm5_frame_set_sync_stub);
    CHECK(*(uint8_t **)(game + 0x1a089f0 + 0x98) == game + 0x849940);   /* PresentationView11 */
    CHECK(log_has("FM6: fps: frame_rate 60 -> present always with interval 1"));
    CHECK(fps_force && fps_queue_ret == 0);
    CHECK_HEX(game + 0x3fe908, "ff2500000000");   /* getter do FrameRate dos cenarios */
    CHECK(!log_has("FrameRate getter"));
    CHECK_HEX(game + 0x25d011, "80bb71250000000f85f0287000");   /* 60: passo fixo 1/59.94 mantido */
    CHECK(*(void **)(game + 0x19f38e8 + 0x158) == (void *)fm6_present_hook);      /* medicao de fps */
    CHECK(*(void **)(game + 0x19f38e8 + 0x108) == (void *)fm6_vline_wait_hook);
    CHECK(fm6_present_orig == (fm6_vcall4)(game + 0x207868) && fm6_vline_wait_orig == (fm6_vcall4)(game + 0x25cc90));
    fm5_frame_set_sync(frame, 1, 0, (uintptr_t)game + 0x1234);
    CHECK_EQ(*(uint32_t *)(frame + 0xa0), 0);    /* 30 fps pedido -> 60 */
    CHECK(log_has("FM6: fps: present mode 1 -> 0 (caller game+0x1234)"));
    /* 2a vez: slots ja trocados -> nada muda */
    fm6_frame_rate(game, 30);
    CHECK(log_has("FM6: fps: setter/Present do not match, ignored"));
    CHECK(fps_force);
    pe_unmap(game);
    game = pe_map(FM6_EXE, NULL);
    fm6_frame_rate(game, 30);                     /* 30: setter trocado sem forcar, getter sem hook */
    CHECK(!fps_force);
    CHECK_HEX(game + 0x3fe908, "40574883ec4048c7442420feffffff48895c2458");
    fm5_frame_set_sync(frame, 1, 0, 0);
    CHECK_EQ(*(uint32_t *)(frame + 0xa0), 1);
    pe_unmap(game);
    /* 180: vline na taxa do launcher e simulacao com dt medido (jne do passo fixo -> nop) */
    game = pe_map(FM6_EXE, NULL);
    fm6_frame_rate(game, 180);
    CHECK_HEX(game + 0x25d011, "80bb7125000000660f1f440000");
    CHECK(log_has("FM6: fps: vline at 180 Hz, simulation with measured dt"));
    fm6_frame_rate(game, 180);                    /* 2a vez: bytes ja trocados -> nada muda */
    CHECK_HEX(game + 0x25d011, "80bb7125000000660f1f440000");
    fm6_vline_rate(60);
    pe_unmap(game);
    dump_log_on_fail();
}

/* exe real do FM6: resolucao 2560x1440 (altura padrao, largura, UI fixa em 1920x1080 e viewport da UI escalado) */
static float ui6_seen[6];
static void ui6_fake_setvp(void *obj, uint32_t n, const float *vp, uintptr_t r9)
{
    (void)obj; (void)n; (void)r9;
    memcpy(ui6_seen, vp, sizeof ui6_seen);
}

static int32_t chart_seen[6];
static uintptr_t chart_fake_setvp(void *obj, const int32_t *r)
{
    (void)obj;
    memcpy(chart_seen, r, sizeof chart_seen);
    return 0;
}

TEST(fm6_resolution_patches_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    void *obj[0x540 / 8] = { 0 };
    static const float vp[6] = { 0, 0, 1920, 1080, 0, 1 };
    if (!game) { test_skip("exe do FM6 ausente"); return; }
    log_capture();
    fm6_resolution(game, 1440, 2560);
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK_HEX(game + 0x552dff, "c78378250000a0050000");                  /* mov dword [rbx+0x2578], 1440 */
    CHECK_HEX(game + 0x55c6a0, "ba000a0000");                            /* mov edx, 2560 */
    CHECK_HEX(game + 0x9faf52, "b80000f044894538" "b80000874489453c" "0f1f840000000000" "0f1f840000000000" "6690");
    CHECK_HEX(game + 0x9faf74, "488b01");                                /* depois: mov rax,[rcx] intacto */
    CHECK_HEX(game + 0x127ea5, "ff152d090000");                          /* call [rip -> 0x1287d8] */
    CHECK(*(void **)(game + 0x1287d8) == (void *)fm5_ui_vcall_stub);
    CHECK_HEX((uint8_t *)fm5_ui_vcall_stub, "4989f9e9");                 /* mov r9, rdi ; jmp fm5_ui_vcall */
    CHECK(log_has("FM6: resolution: UI viewport scaled to 2560x1440"));
    obj[0x538 / 8] = (void *)ui6_fake_setvp;
    ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(game + 0x127ea5 + 6,
                                                                      (void *)fm5_ui_vcall))(obj, 1, vp, 0);
    CHECK(ui6_seen[2] == 2560.0f && ui6_seen[3] == 1440.0f && ui6_seen[0] == 0.0f && ui6_seen[5] == 1.0f);
    CHECK(vp[2] == 1920.0f);
    /* renderizador com RT proprio (painel digital do cockpit): so o RT do tamanho do render e escalado */
    {
        uint8_t screen[0x80] = { 0 }, dash[0x80] = { 0 }, buf[0x80] = { 0 }, rnd[0x710] = { 0 };
        void *rtv_screen = (void *)0x5000, *rtv_dash = (void *)0x6000, *rtv_buf = (void *)0x7000;
        *(uint32_t *)(screen + 0x38) = 0xc; *(uint32_t *)(screen + 0x70) = 2560; *(uint32_t *)(screen + 0x74) = 1440;
        *(uint32_t *)(dash + 0x38) = 0xc;   *(uint32_t *)(dash + 0x70) = 1024;   *(uint32_t *)(dash + 0x74) = 512;
        *(uint32_t *)(buf + 0x38) = 0x1;    *(uint32_t *)(buf + 0x70) = 1024;    *(uint32_t *)(buf + 0x74) = 512;
        ui_rtv_note(rtv_screen, screen); ui_rtv_note(rtv_dash, dash); ui_rtv_note(rtv_buf, buf);
        *(void **)(rnd + 0x708) = rtv_dash;
        ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(game + 0x127ea5 + 6,
                                                   (void *)fm5_ui_vcall))(obj, 1, vp, (uintptr_t)rnd);
        CHECK(ui6_seen[2] == 1920.0f && ui6_seen[3] == 1080.0f);
        CHECK(log_has("FM6: UI viewport 0,0 1920x1080 on a 1024x512 target (not the screen): kept"));
        *(void **)(rnd + 0x708) = rtv_screen;
        ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(game + 0x127ea5 + 6,
                                                   (void *)fm5_ui_vcall))(obj, 1, vp, (uintptr_t)rnd);
        CHECK(ui6_seen[2] == 2560.0f && ui6_seen[3] == 1440.0f);
        *(void **)(rnd + 0x708) = rtv_buf;          /* nao e Texture2D: tamanho desconhecido -> escala */
        ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(game + 0x127ea5 + 6,
                                                   (void *)fm5_ui_vcall))(obj, 1, vp, (uintptr_t)rnd);
        CHECK(ui6_seen[2] == 2560.0f && ui6_seen[3] == 1440.0f);
        ui_rtv_note(rtv_dash, screen);              /* view reaproveitada: entrada sobrescrita */
        *(void **)(rnd + 0x708) = rtv_dash;
        ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(game + 0x127ea5 + 6,
                                                   (void *)fm5_ui_vcall))(obj, 1, vp, (uintptr_t)rnd);
        CHECK(ui6_seen[2] == 2560.0f && ui6_seen[3] == 1440.0f);
    }
    /* grafico da UI (relacao de marchas): retangulo inteiro do renderizador escalado */
    CHECK_HEX(game + 0xcff5c5, "c5f829442430" "c5fb114c2440" "ff1549" "7f30ff");   /* call [rip -> 0x7520] */
    CHECK(*(void **)(game + 0x7520) == (void *)ui_chart_vcall);
    {
        static const int32_t rect[6] = { 1406, 695, 397, 170, 0, 0x3f800000 };
        void *vt[7] = { 0 }, *robj = vt;
        vt[0] = vt;                                  /* [obj] = vtable = o proprio array */
        vt[6] = (void *)chart_fake_setvp;
        ui_chart_vcall(robj, rect);
        CHECK(chart_seen[0] == 1875 && chart_seen[1] == 927 && chart_seen[2] == 529 && chart_seen[3] == 226);
        CHECK(chart_seen[5] == 0x3f800000 && rect[0] == 1406);
        CHECK(log_has("FM6: UI graphic 1406,695 397x170 -> 1875,927 529x226"));
    }
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_resolution(game, 1440, 2560);
    CHECK(log_has("FM6: resolution: height site 0x552dff does not match, ignored"));
    pe_unmap(game);
    /* so a altura (720): largura = altura * 16/9 do proprio jogo */
    game = pe_map(FM6_EXE, NULL);
    fm6_resolution(game, 720, 0);
    CHECK_HEX(game + 0x552dff, "c78378250000d0020000");
    CHECK_HEX(game + 0x55c6a0, "c4e1fa2cd2");
    CHECK(ui_sx > 0.666f && ui_sx < 0.667f && ui_sy > 0.666f && ui_sy < 0.667f);
    pe_unmap(game);
    /* 0x0: nada */
    game = pe_map(FM6_EXE, NULL);
    fm6_resolution(game, 0, 0);
    CHECK_HEX(game + 0x552dff, "c7837825000038040000");
    CHECK_HEX(game + 0x127ea5, "ff9038050000");
    pe_unmap(game);
    ui_sites = &fm5_res_sites;
    dump_log_on_fail();
}

/* medicao de fps do FM6: 1 s de quadros a 30 fps (Present 1 ms, vline 20 ms) loga os tempos medios */
static uintptr_t fake_vcall(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return 7; }

TEST(fm6_fps_meter_logs)
{
    LONGLONG f, t = 1000000;
    int i;
    log_capture();
    QueryPerformanceFrequency(&fm6_qpf);
    f = fm6_qpf.QuadPart;
    fm6_fps_sample(t - f / 1000, t);
    for (i = 0; i < 31; i++) {
        InterlockedAdd64(&fm6_wait_sum, f / 50);
        t += f / 30;
        fm6_fps_sample(t - f / 1000, t);
    }
    CHECK(log_has("FM6: fps 30 (frame 33.33 ms: Present 1.00 ms, vline wait 20.00 ms)"));
    fm6_present_orig = fm6_vline_wait_orig = fake_vcall;
    CHECK_EQ(fm6_present_hook(0, 0, 0, 0), 7);
    CHECK_EQ(fm6_vline_wait_hook(0, 0, 0, 0), 7);
    fm6_present_orig = fm6_vline_wait_orig = NULL;
    dump_log_on_fail();
}

/* getter do FrameRate do FM6: 30 do cenario (pre-corrida, replay...) vira 60 so com frame_rate >= 60 */
static uint32_t fake_frame_rate_val;
static uint32_t fake_get_frame_rate(void *mgr, int idx) { (void)mgr; (void)idx; return fake_frame_rate_val; }

TEST(fm6_get_frame_rate_scenario)
{
    log_capture();
    fm6_get_frame_rate_orig = fake_get_frame_rate;
    fps_force = 1;
    fake_frame_rate_val = 30;
    CHECK_EQ(fm6_get_frame_rate(NULL, 0), 60);
    CHECK(log_has("FM6: fps: scenario FrameRate 30 -> 60"));
    fake_frame_rate_val = 60;
    CHECK_EQ(fm6_get_frame_rate(NULL, 0), 60);
    fps_force = 0;
    fake_frame_rate_val = 30;
    CHECK_EQ(fm6_get_frame_rate(NULL, 0), 30);
    fm6_get_frame_rate_orig = NULL;
    dump_log_on_fail();
}

/* D3D12Debug=5: criterio de recurso reservado do title_storage (azimuth 0x13bae0) -> xor eax,eax ; ret */
TEST(fm6_debug5_committed_placement)
{
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL), *game = pe_map(FM6_EXE, NULL);
    if (!az || !game) { pe_unmap(az); pe_unmap(game); test_skip("azimuth.exe/exe do FM6 ausentes"); return; }
    g_base = az;
    log_capture();
    ini6_set(NULL);
    dbgini_set("[Debug]\nD3D12Debug=3\nPlacementCommitted=0\n");
    fm6_patches(game);
    CHECK_HEX(az + 0x13bae0, "48895c241055565748");          /* desligado: reservado como no Azimuth */
    dbgini_set("[Debug]\nD3D12Debug=5\nPlacementCommitted=0\n");
    fm6_patches(game);
    CHECK_HEX(az + 0x13bae0, "31c0c324105556574881");        /* D3D12Debug=5 antigo ainda liga */
    CHECK(log_has("FM6: placement textures as committed resources"));
    fm6_patches(game);                            /* bytes ja trocados -> nada muda */
    CHECK(log_has("FM6: placement textures committed: site 0x13bae0 does not match, ignored"));
    CHECK_HEX(az + 0x13bae0, "31c0c3");
    pe_unmap(az);
    az = pe_map(AZIMUTH_EXE, NULL);
    g_base = az;
    dbgini_set(NULL);                             /* padrao do FM6: committed */
    fm6_patches(game);
    CHECK_HEX(az + 0x13bae0, "31c0c324105556574881");
    dbgini_set(NULL);
    pe_unmap(game);
    pe_unmap(az);
    dump_log_on_fail();
}

/* D3D12Debug=6: hook no destroy comum (azimuth 0xda270); recursos/views (tipo em +0x38) nao sao destruidos */
static uint8_t *destroyed_obj;
static void fake_gfx_destroy(uint8_t *obj) { destroyed_obj = obj; }

TEST(fm6_debug6_leak_gfx_objects)
{
    static const uint32_t leak[] = { 5, 0xb, 0xc, 0xd, 0x10, 0x11 }, keep[] = { 1, 6, 7, 8, 9, 0xa, 0xf };
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL), *game = pe_map(FM6_EXE, NULL);
    uint8_t obj[0x40];
    size_t i;
    if (!az || !game) { pe_unmap(az); pe_unmap(game); test_skip("azimuth.exe/exe do FM6 ausentes"); return; }
    g_base = az;
    log_capture();
    ini6_set(NULL);
    dbgini_set("[Debug]\nD3D12Debug=5\n");
    fm6_patches(game);
    CHECK_HEX(az + 0xda270, "48895c241855565741564157488bec");
    dbgini_set("[Debug]\nD3D12Debug=6\n");
    fm6_patches(game);
    CHECK_HEX(az + 0xda270, "ff2500000000");
    CHECK(log_has("FM6: D3D12Debug=6: resources/views never destroyed"));
    orig_gfx_destroy = fake_gfx_destroy;          /* nao roda o destroy real do Azimuth */
    for (i = 0; i < sizeof leak / sizeof *leak; i++) {
        destroyed_obj = NULL;
        *(uint32_t *)(obj + 0x38) = leak[i];
        fm6_leak_destroy(obj);
        CHECK(destroyed_obj == NULL);
    }
    CHECK(log_has("destroy skipped type 0xc (1)"));
    for (i = 0; i < sizeof keep / sizeof *keep; i++) {
        destroyed_obj = NULL;
        *(uint32_t *)(obj + 0x38) = keep[i];
        fm6_leak_destroy(obj);
        CHECK(destroyed_obj == obj);
    }
    fm6_patches(game);                            /* bytes ja trocados -> nada muda */
    CHECK(log_has("FM6: D3D12Debug=6: site 0xda270 does not match, ignored"));
    dbgini_set(NULL);
    pe_unmap(game);
    pe_unmap(az);
    dump_log_on_fail();
}

/* PASSO 47: protecao Xbox 0x40404 de memoria grafica traduzida p/ PAGE_READWRITE no VirtualAlloc do Azimuth */
static uint32_t seen_valloc_protect;
static void *fake_az_valloc(void *addr, size_t size, uint32_t type, uint32_t protect)
{
    (void)size; (void)type;
    seen_valloc_protect = protect;
    return addr ? addr : (void *)0x1230000;
}

TEST(fm6_graphics_protect_translated)
{
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL);
    if (!az) { test_skip("azimuth.exe ausente"); return; }
    g_base = az;
    log_capture();
    CHECK_EQ(fm6_graphics_protect(0x30003000, 0x40404), 4);
    CHECK_EQ(fm6_graphics_protect(0x30001000, 0x40404), 4);
    CHECK_EQ(fm6_graphics_protect(0x30002000, 0x40404), 4);
    CHECK_EQ(fm6_graphics_protect(0x20003000, 0x40404), 0x40404);   /* sem MEM_GRAPHICS: intocada */
    CHECK_EQ(fm6_graphics_protect(0x30003000, 0x10004), 0x10004);   /* as que o Azimuth ja traduz */
    CHECK_EQ(fm6_graphics_protect(0x30003000, 0x404), 0x404);
    CHECK(az_valloc_install(fm6_graphics_protect));
    CHECK_HEX(az + 0xaad30, "ff2500000000");
    orig_az_valloc = fake_az_valloc;               /* nao roda o VirtualAlloc real do Azimuth */
    CHECK(hook_az_valloc(NULL, 0x52000, 0x30003000, 0x40404) == (void *)0x1230000);
    CHECK_EQ(seen_valloc_protect, 4);
    CHECK(log_has("graphics VirtualAlloc #1"));
    CHECK(log_has("protection 0x40404 -> 0x4"));
    hook_az_valloc(NULL, 0x1000, 0x3000, 0x40404);
    CHECK_EQ(seen_valloc_protect, 0x40404);
    CHECK(!az_valloc_install(fm6_graphics_protect));       /* bytes ja trocados -> nada muda */
    CHECK(log_has("Azimuth VirtualAlloc (0xaad30) does not match"));
    pe_unmap(az);
    dump_log_on_fail();
}

/* quedas de usuario 2026-10-23: a faixa fixa 0x800000000 do alocador do jogo fica segura desde o DllMain (o
   nvcuda64.dll reservava por cima) e e solta so quando o jogo pede um endereco dentro dela */
TEST(fixed_va_held_until_game_reserves)
{
    MEMORY_BASIC_INFORMATION mi;
    log_capture();
    fixed_va_hold();
    if (!g_fixed_va) { test_skip("0x800000000 ocupado no processo de teste"); return; }
    CHECK(log_has("fixed VA: 0x800000000..0xa01000000 held"));
    CHECK(VirtualQuery((void *)0x800000000ull, &mi, sizeof mi) && mi.State == MEM_RESERVE);
    orig_az_valloc = fake_az_valloc;
    g_valloc_protect = NULL;
    /* termina exatamente na base: fora da faixa, a reserva continua */
    CHECK(hook_az_valloc((void *)0x7ff000000ull, 0x1000000, 0x30002000, 4) == (void *)0x7ff000000ull);
    CHECK(hook_az_valloc(NULL, 0x1000, 0x3000, 4) == (void *)0x1230000);
    CHECK(g_fixed_va != NULL);
    CHECK(hook_az_valloc((void *)0xa00800000ull, 0x400000, 0x30002000, 0x40404) == (void *)0xa00800000ull);
    CHECK_EQ(seen_valloc_protect, 0x40404);         /* fora do FM6 a protecao nao e traduzida */
    CHECK(g_fixed_va == NULL);
    CHECK(log_has("fixed VA: released (the game reserves there): 1"));
    CHECK(VirtualQuery((void *)0x800000000ull, &mi, sizeof mi) && mi.State == MEM_FREE);
    fixed_va_release("again");                      /* ja solta: nada */
    CHECK(!log_has("again"));
    dump_log_on_fail();
}

/* ------------------------------------------------------------------------ */
/* Registro do FM5 (PASSO 4)                                                  */
/* ------------------------------------------------------------------------ */
TEST(record_layout)
{
    static uint8_t fh2[0x350];
    const LangEntry *langs;
    int i;
    uint8_t *az = fake_azimuth(0);
    for (i = 0; i < 4; i++) *(void **)(fh2 + 0x58 + i * 0x10) = (void *)(uintptr_t)(0x1000 + i);
    *(void **)(az + RVA_ALLOW_ARRAY) = fh2;
    fm5_build_record(fm5_rec, fh2);
    CHECK(!wcscmp(*(const wchar_t **)(fm5_rec + 0x000), L"7b35d917"));
    CHECK_STR(*(const char **)(fm5_rec + 0x008), "1.0.0.20");
    for (i = 0; i < 4; i++) CHECK_EQ(*(uintptr_t *)(fm5_rec + 0x58 + i * 0x10), 0x1000 + i);   /* caminhos do FH2 */
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x098), 0x279d000);
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x09c), 0xca7ed0);
    CHECK_EQ(*(uint64_t *)(fm5_rec + 0x0a0), 0x244);
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x1e8), 0x5d6ca7);
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x208), 0x5a8efc);
    CHECK_EQ(strlen(*(const char **)(fm5_rec + 0x318)), 64);
    CHECK_EQ(*(uint64_t *)(fm5_rec + 0x338), 21);
    langs = *(const LangEntry **)(fm5_rec + 0x330);
    CHECK(!wcscmp(langs[0].locale, L"en"));
    CHECK(!wcscmp(langs[2].locale, L"de") && langs[2].index == 4 && langs[2].speech);
    CHECK_EQ(fm5_rec[0x341], 1);
    /* todos os sha256 do registro: 64 digitos hexa minusculos */
    {
        static const int offs[] = { 0x10, 0x18, 0x28, 0x38, 0x48, 0x60, 0x70, 0x80, 0x90 };
        for (i = 0; i < (int)(sizeof offs / sizeof *offs); i++) {
            const char *h = *(const char **)(fm5_rec + offs[i]);
            CHECK(h && strlen(h) == 64 && strspn(h, "0123456789abcdef") == 64);
        }
    }
}

TEST(fm6_record_layout)
{
    static uint8_t fh2[0x350];
    const LangEntry *langs;
    int i;
    uint8_t *az = fake_azimuth(0);
    for (i = 0; i < 4; i++) *(void **)(fh2 + 0x58 + i * 0x10) = (void *)(uintptr_t)(0x1000 + i);
    *(void **)(az + RVA_ALLOW_ARRAY) = fh2;
    fm6_build_record(fm6_rec, fh2);
    CHECK(!wcscmp(*(const wchar_t **)(fm6_rec + 0x000), L"64f9397e"));
    CHECK_STR(*(const char **)(fm6_rec + 0x008), "1.0.0.44");
    for (i = 0; i < 4; i++) CHECK_EQ(*(uintptr_t *)(fm6_rec + 0x58 + i * 0x10), 0x1000 + i);   /* caminhos do FH2 */
    CHECK_STR(*(const char **)(fm6_rec + 0x060), "9b458bcf0bd5a11ce51c29ef8003c62c4de3015f1c129d82edaeb9d41d602aa4");
    CHECK_EQ(*(uint64_t *)(fm6_rec + 0x030), 0xa52);
    CHECK_EQ(*(uint32_t *)(fm6_rec + 0x098), 0x1fc8000);
    CHECK_EQ(*(uint32_t *)(fm6_rec + 0x09c), 0x6341d8);
    CHECK_EQ(*(uint64_t *)(fm6_rec + 0x0a0), 0x264);
    CHECK_EQ(*(uint32_t *)(fm6_rec + 0x1e8), 0);
    CHECK_EQ(*(uint32_t *)(fm6_rec + 0x208), 0);
    CHECK_EQ(strlen(*(const char **)(fm6_rec + 0x318)), 64);
    CHECK_EQ(*(uint64_t *)(fm6_rec + 0x338), 21);
    langs = *(const LangEntry **)(fm6_rec + 0x330);
    CHECK(!wcscmp(langs[0].locale, L"en") && langs[0].speech);
    CHECK(!wcscmp(langs[16].locale, L"pt-BR") && !langs[16].speech);
    CHECK_EQ(fm6_rec[0x341], 1);
    {
        static const int offs[] = { 0x10, 0x18, 0x28, 0x38, 0x48, 0x60, 0x70, 0x80, 0x90 };
        for (i = 0; i < (int)(sizeof offs / sizeof *offs); i++) {
            const char *h = *(const char **)(fm6_rec + offs[i]);
            CHECK(h && strlen(h) == 64 && strspn(h, "0123456789abcdef") == 64);
        }
    }
}

/* o registro tem de bater com os arquivos do jogo copiados em game_binaries\<jogo> */
static void check_record_files(const uint8_t *rec, const char *dir)
{
    char path[MAX_PATH], hex[65];
    uint64_t size;
    uint32_t soi;
    uint8_t *game;
    IMAGE_NT_HEADERS64 *nt;
    snprintf(path, sizeof path, "%sforza_durango_release_final.exe", dir);
    if (!sha256_file(path, hex, &size)) { test_skip("exe do jogo ausente em game_binaries"); return; }
    CHECK_STR(*(const char **)(rec + 0x018), hex);
    CHECK_EQ(*(uint64_t *)(rec + 0x020), size);
    REQUIRE((game = pe_map(path, &soi)));
    nt = (IMAGE_NT_HEADERS64 *)(game + ((IMAGE_DOS_HEADER *)game)->e_lfanew);
    CHECK_EQ(*(uint32_t *)(rec + 0x098), nt->OptionalHeader.SizeOfImage);
    CHECK_EQ(*(uint32_t *)(rec + 0x09c), nt->OptionalHeader.AddressOfEntryPoint);
    pe_unmap(game);
    snprintf(path, sizeof path, "%sAppxManifest.xml", dir);
    if (sha256_file(path, hex, &size))
        CHECK_STR(*(const char **)(rec + 0x010), hex);
    snprintf(path, sizeof path, "%sxg_x.dll", dir);
    if (sha256_file(path, hex, &size)) {
        CHECK_STR(*(const char **)(rec + 0x038), hex);
        CHECK_EQ(*(uint64_t *)(rec + 0x040), size);
    }
}

TEST(record_matches_game_files)
{
    static uint8_t fh2[0x350];
    fake_azimuth(0);
    *(void **)(g_base + RVA_ALLOW_ARRAY) = fh2;
    fm5_build_record(fm5_rec, fh2);
    check_record_files(fm5_rec, FM5_DIR);
}

TEST(fm6_record_matches_game_files)
{
    static uint8_t fh2[0x350];
    fake_azimuth(0);
    *(void **)(g_base + RVA_ALLOW_ARRAY) = fh2;
    fm6_build_record(fm6_rec, fh2);
    check_record_files(fm6_rec, FM6_DIR);
}

/* ------------------------------------------------------------------------ */
/* XG (PASSO 6): descritor no layout do XDK do FM5 e CopyFromSubresource      */
/* ------------------------------------------------------------------------ */
static uint32_t xg_seen_desc[16];
static void *xg_fake_vtbl[6], *xg_fake_obj[1] = { xg_fake_vtbl };
static uint8_t *xg_fake_self;
static void xg_fake_orig(void *out, const char *name, uint32_t *desc)
{
    (void)name;
    memcpy(xg_seen_desc, desc, sizeof xg_seen_desc);
    *(void **)out = xg_fake_obj;
}
/* texel "tiled" de mentira: transposto (x,y) -> (y,x), 4 bytes por elemento, w = h = 4 */
static uint64_t xg_fake_texel(void *self, uint32_t plane, uint32_t mip, uint64_t x, uint32_t y, uint32_t z, uint32_t s)
{
    (void)self; (void)plane; (void)mip; (void)s;
    return ((uint64_t)z * 16 + x * 4 + y) * 4;
}

TEST(xg_desc_layout_converted)
{
    uint32_t d2[16], d3[16];
    void *out = NULL;
    int i;
    fake_azimuth(1);
    orig_xg_computer = xg_fake_orig;
    xg_fake_vtbl[4] = (void *)xg_fake_texel;
    for (i = 0; i < 16; i++) d2[i] = d3[i] = 100 + i;
    hook_xg_computer(&out, "XGCreateTexture2DComputer", d2);
    CHECK_EQ(xg_seen_desc[11], 111);             /* desc D3D11 (11 dwords) + 1 dword ESRAM */
    CHECK_EQ(xg_seen_desc[12], 113);             /* TileMode 4 bytes antes */
    CHECK_EQ(xg_seen_desc[13], 114);             /* Pitch */
    CHECK_EQ(xg_seen_desc[14], 0);
    hook_xg_computer(&out, "XGCreateTexture3DComputer", d3);
    CHECK_EQ(xg_seen_desc[10], 111);
    CHECK_EQ(xg_seen_desc[11], 112);
    CHECK_EQ(xg_seen_desc[12], 0);
    /* computer devolvido com a vtable nova (16 slots, 6 = CopyFromSubresource) */
    CHECK(*(void **)xg_fake_obj == (void *)xg_vtbl);
    CHECK(xg_vtbl[4] == (void *)xg_fake_texel);
    CHECK(xg_vtbl[6] == (void *)xg_copy_from_subresource);
    *(void **)xg_fake_obj = xg_fake_vtbl;
    /* fora do FM5: repassa sem mexer */
    fake_azimuth(0);
    for (i = 0; i < 16; i++) d2[i] = 100 + i;
    hook_xg_computer(&out, "XGCreateTexture2DComputer", d2);
    CHECK_EQ(xg_seen_desc[12], 112);
}

/* FM6: vtable de 12 slots (3/4 = getters novos) -> 8 slots do FH2; desc sem conversao */
static uint8_t xg_get54b8[] = { 0x48, 0x8b, 0x81, 0xb8, 0x54, 0x00, 0x00, 0xc3 };
static uint8_t xg_get54c0[] = { 0x48, 0x8b, 0x81, 0xc0, 0x54, 0x00, 0x00, 0xc3 };
static void *xg_fm6_fake_vtbl[12], *xg_fm6_fake_obj[1] = { xg_fm6_fake_vtbl };
static void xg_fm6_fake_orig(void *out, const char *name, uint32_t *desc)
{
    (void)name;
    memcpy(xg_seen_desc, desc, sizeof xg_seen_desc);
    *(void **)out = xg_fm6_fake_obj;
}

TEST(xg_fm6_computer_remapped)
{
    static const int fh2_to_fm6[8] = { 0, 1, 2, 5, 6, 8, 9, 10 };
    uint32_t d2[16];
    void *out = NULL;
    int i;
    fake_azimuth(2);
    orig_xg_computer = xg_fm6_fake_orig;
    for (i = 0; i < 12; i++) xg_fm6_fake_vtbl[i] = (void *)(uintptr_t)(0x2000 + i);
    /* vtable inesperada (slot 3 nao e o getter): nada muda */
    xg_fm6_fake_vtbl[3] = xg_get54c0;
    xg_fm6_fake_vtbl[4] = xg_get54c0;
    hook_xg_computer(&out, "XGCreateTexture2DComputer", d2);
    CHECK(*(void **)xg_fm6_fake_obj == (void *)xg_fm6_fake_vtbl);
    xg_fm6_fake_vtbl[3] = xg_get54b8;
    for (i = 0; i < 16; i++) d2[i] = 100 + i;
    hook_xg_computer(&out, "XGCreateTexture2DComputer", d2);
    CHECK_EQ(xg_seen_desc[12], 112);             /* desc do Azimuth repassado como esta */
    CHECK(*(void **)xg_fm6_fake_obj == (void *)xg_fm6_new);
    for (i = 0; i < 8; i++) CHECK(xg_fm6_new[i] == xg_fm6_fake_vtbl[fh2_to_fm6[i]]);
    CHECK(xg_fm6_new[6] == (void *)(uintptr_t)0x2009);   /* CopyFromSubresource */
    /* 2o computer: mesma vtable nova */
    *(void **)xg_fm6_fake_obj = xg_fm6_fake_vtbl;
    hook_xg_computer(&out, "XGCreateTexture3DComputer", d2);
    CHECK(*(void **)xg_fm6_fake_obj == (void *)xg_fm6_new);
}

TEST(xg_copy_from_subresource_detiles)
{
    uint8_t *self = xg_fake_self ? xg_fake_self : (xg_fake_self = calloc(1, 0x6200));
    uint32_t src[16], dst[16 + 4];
    int i;
    xg_fm5_vtbl = (void *const *)xg_fake_vtbl;
    xg_fake_vtbl[4] = (void *)xg_fake_texel;
    *(uint32_t *)(self + 0x54c8) = 1;            /* mips */
    *(uint32_t *)(self + 0x54f0) = 4;            /* bytes por elemento */
    *(uint32_t *)(self + 0x5528) = 4;            /* w */
    *(uint32_t *)(self + 0x552c) = 4;            /* h */
    for (i = 0; i < 16; i++) src[i] = i;
    memset(dst, 0xff, sizeof dst);
    CHECK_EQ(xg_copy_from_subresource(self, (uint8_t *)dst, 0, 0, (const uint8_t *)src, 16, 64), 0);
    for (i = 0; i < 16; i++) CHECK_EQ(dst[i], (uint32_t)((i % 4) * 4 + i / 4));   /* transposto de volta */
    CHECK_EQ(dst[16], 0xffffffff);               /* nao escreve alem */
    *(uint32_t *)(self + 0x54c8) = 0;
    CHECK_EQ((uint32_t)xg_copy_from_subresource(self, (uint8_t *)dst, 0, 0, (const uint8_t *)src, 16, 64), 0x80070057);
}

/* XG do FM5: elementos dos mips > 0 de BC = mip 0 >> mip (960x540 BC7: 120x67 no mip 1) -> arredondados p/ cima */
static uint8_t *xg_bc_self;
static void *xg_bc_obj_orig_out;
/* como o 0x14009fba0: computer em out[0] e copia do XG_RESOURCE_LAYOUT (computer+0x54b8, 0xd40 bytes) em out+0x10 */
static void xg_bc_fake_orig(void *out, const char *name, uint32_t *desc)
{
    (void)name; (void)desc;
    *(void **)out = xg_bc_obj_orig_out;
    memcpy((uint8_t *)out + 0x10, (uint8_t *)xg_bc_obj_orig_out + 0x54b8, 0xd40);
}
static void xg_bc_fill(uint8_t *self, uint32_t mips, const uint32_t (*we)[2])
{
    uint32_t i;
    memset(self, 0, 0x6200);
    *(void **)self = xg_fake_vtbl;
    *(uint32_t *)(self + 0x54c8) = mips;
    for (i = 0; i < mips; i++) {
        *(uint32_t *)(self + 0x5528 + i * 0x48) = we[i][0];
        *(uint32_t *)(self + 0x552c + i * 0x48) = we[i][1];
    }
}

TEST(xg_fm5_bc_mip_elements_rounded_up)
{
    /* o que o xg_x.dll do FM5 devolve p/ 960x540 BC7_UNORM_SRGB com 10 mips (tools\xg_layout_probe.c) */
    static const uint32_t xg[10][2] = { {240, 135}, {120, 67}, {60, 33}, {30, 16}, {15, 8}, {7, 4}, {3, 2}, {1, 1},
                                        {1, 1}, {1, 1} };
    static const uint32_t want[10][2] = { {240, 135}, {120, 68}, {60, 34}, {30, 17}, {15, 9}, {8, 4}, {4, 2}, {2, 1},
                                          {1, 1}, {1, 1} };
    uint32_t d2[16] = { 960, 540, 10, 1, 99, 1, 0, 0, 8, 0, 0, 0, 0, 14, 0 }, i;
    uint64_t out[(0x10 + 0xd40) / 8];
    if (!xg_bc_self) xg_bc_self = calloc(1, 0x6200);
    xg_bc_fill(xg_bc_self, 10, xg);
    CHECK_EQ(xg_fix_bc_mips(xg_bc_self, d2), 7);
    for (i = 0; i < 10; i++) {
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x5528 + i * 0x48), want[i][0]);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + i * 0x48), want[i][1]);
    }
    /* 970 de largura: a coluna tambem (mip 1 121 -> 122) */
    {
        static const uint32_t xw[3][2] = { {243, 135}, {121, 67}, {60, 33} };
        uint32_t dw[16] = { 970, 540, 3, 1, 71 };
        xg_bc_fill(xg_bc_self, 3, xw);
        CHECK_EQ(xg_fix_bc_mips(xg_bc_self, dw), 4);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x5528 + 0x48), 122);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 68);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x5528 + 2 * 0x48), 61);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 2 * 0x48), 34);
    }
    /* nao BC (R8G8B8A8 = 28): nada muda */
    {
        static const uint32_t xr[2][2] = { {970, 541}, {485, 270} };
        uint32_t dr[16] = { 970, 541, 2, 1, 28 };
        xg_bc_fill(xg_bc_self, 2, xr);
        CHECK_EQ(xg_fix_bc_mips(xg_bc_self, dr), 0);
        CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 270);
    }
    /* mip 0 diferente do esperado: nao mexe */
    xg_bc_fill(xg_bc_self, 10, xg);
    *(uint32_t *)(xg_bc_self + 0x552c) = 136;
    CHECK_EQ(xg_fix_bc_mips(xg_bc_self, d2), 0);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 67);
    /* pelo hook do XGCreateTexture2DComputer no FM5 (desc do Azimuth: 2 dwords ESRAM, TileMode [13]) */
    fake_azimuth(1);
    xg_bc_fill(xg_bc_self, 10, xg);
    xg_bc_obj_orig_out = xg_bc_self;
    orig_xg_computer = xg_bc_fake_orig;
    hook_xg_computer(out, "XGCreateTexture2DComputer", d2);
    CHECK((void *)(uintptr_t)out[0] == xg_bc_self);
    CHECK_EQ(d2[12], 14);                        /* TileMode convertido p/ o layout do FM5 */
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 68);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x5528 + 5 * 0x48), 8);
    /* a copia do layout em out+0x10 (de onde sai o mapa de enderecos) tambem: mip N Width/Height em +0x70/+0x74 */
    CHECK_EQ(*(uint32_t *)((uint8_t *)out + 0x10 + 0x74 + 0x48), 68);
    CHECK_EQ(*(uint32_t *)((uint8_t *)out + 0x10 + 0x70 + 5 * 0x48), 8);
    CHECK_EQ(*(uint32_t *)((uint8_t *)out + 0x10 + 0x74 + 3 * 0x48), 17);
    CHECK_EQ(*(uint32_t *)((uint8_t *)out + 0x10 + 0x74), 135);   /* mip 0 igual */
    /* FM6: o XG conta certo, o hook nao mexe nas contagens */
    fake_azimuth(2);
    xg_bc_fill(xg_bc_self, 10, xg);
    hook_xg_computer(out, "XGCreateTexture2DComputer", d2);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 67);
    CHECK_EQ(*(uint32_t *)((uint8_t *)out + 0x10 + 0x74 + 0x48), 67);
    fake_azimuth(1);
}

/* CreatePlacementTexture2D do FM5: os blocos acrescentados pelo arredondamento sao zerados na memoria do jogo */
static int xg_released;
static unsigned long xg_fake_release(void *self) { (void)self; xg_released++; return 0; }
/* enderecos de mentira: 0x4000 por mip, linha de 128 elementos de 16 bytes */
static uint64_t xg_lin_texel(void *self, uint32_t plane, uint32_t mip, uint64_t x, uint32_t y, uint32_t z, uint32_t s)
{
    (void)self; (void)plane; (void)z; (void)s;
    return (uint64_t)mip * 0x40000 + ((uint64_t)y * 128 + x) * 16;
}

/* como o 0x14009fba0: computer em out[0] e o XG_RESOURCE_LAYOUT (0xd40 bytes) em out+0x10 */
static void xg_layout_fake_orig(void *out, const char *name, uint32_t *desc)
{
    (void)name; (void)desc;
    *(void **)out = xg_bc_obj_orig_out;
    memset((uint8_t *)out + 0x10, 0xcd, 0xd40);
}

TEST(xg_fm5_bc_tail_zeroed)
{
    static const uint32_t xg[3][2] = { {240, 135}, {120, 67}, {60, 33} };
    uint32_t desc[11] = { 960, 540, 3, 1, 99, 1, 0, 0, 8, 0, 0 };
    uint8_t *mem = malloc(0xc0000);
    uint32_t i;
    REQUIRE(mem);
    fake_azimuth(1);
    if (!xg_bc_self) xg_bc_self = calloc(1, 0x6200);
    xg_bc_fill(xg_bc_self, 3, xg);
    *(uint64_t *)(xg_bc_self + 0x54b8) = 0xc0000;     /* SizeBytes */
    *(uint32_t *)(xg_bc_self + 0x54f0) = 16;          /* BC7 */
    xg_bc_obj_orig_out = xg_bc_self;
    orig_xg_computer = xg_layout_fake_orig;
    xg_fm5_vtbl = (void *const *)xg_fake_vtbl;
    xg_fake_vtbl[1] = xg_vtbl[1] = (void *)xg_fake_release;
    xg_fake_vtbl[4] = (void *)xg_lin_texel;
    xg_released = 0;
    memset(mem, 0xab, 0xc0000);
    /* mip 1: linha 67 (120 elementos); mip 2: linha 33 (60) */
    CHECK_EQ(xg_fm5_zero_bc_tail(desc, 14, 0, mem), 180);
    CHECK_EQ(xg_released, 1);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 68);   /* contagens arredondadas pelo hook */
    for (i = 0; i < 120 * 16; i++) CHECK_EQ(mem[0x40000 + 67 * 128 * 16 + i], 0);
    CHECK_EQ(mem[0x40000 + 66 * 128 * 16 + 119 * 16 + 15], 0xab);   /* linha de dentro intacta */
    CHECK_EQ(mem[0x40000 + 67 * 128 * 16 + 120 * 16], 0xab);        /* alem da largura intacto */
    CHECK_EQ(mem[0x80000 + 33 * 128 * 16], 0);
    CHECK_EQ(mem[0x80000 + 32 * 128 * 16], 0xab);
    CHECK_EQ(mem[0], 0xab);                                          /* mip 0 intacto */
    /* nao BC: nada */
    desc[4] = 28;
    CHECK_EQ(xg_fm5_zero_bc_tail(desc, 14, 0, mem), 0);
    CHECK_EQ(xg_released, 1);
    xg_fake_vtbl[4] = (void *)xg_fake_texel;
    free(mem);
}

/* XGCreateTexture2DComputer do xg_x.dll do FM5 enganchado: todo computer (tambem os da thread do learn_later) sai
   com as contagens arredondadas */
static long xg_fake_create(const uint32_t *desc, void **computer)
{
    (void)desc;
    *computer = xg_bc_self;
    return 0;
}

TEST(xg_fm5_create_export_rounds_up)
{
    static const uint32_t xg[3][2] = { {240, 135}, {120, 67}, {60, 33} };
    uint32_t desc[11] = { 960, 540, 3, 1, 99, 1, 0, 0, 8, 0, 0 };
    void *c = NULL;
    if (!xg_bc_self) xg_bc_self = calloc(1, 0x6200);
    xg_bc_fill(xg_bc_self, 3, xg);
    orig_xg_create[0] = xg_fake_create;
    CHECK_EQ(xg_create2d_fixed(desc, &c), 0);
    CHECK(c == xg_bc_self);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 68);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 2 * 0x48), 34);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x5528 + 2 * 0x48), 60);
    /* nao BC: intacto */
    xg_bc_fill(xg_bc_self, 3, xg);
    desc[4] = 28;
    CHECK_EQ(xg_create2d_fixed(desc, &c), 0);
    CHECK_EQ(*(uint32_t *)(xg_bc_self + 0x552c + 0x48), 67);
}

/* envio por subrecurso: mapa comprimido mais baixo que as linhas do registro (lente 960x540 BC7, mip 1: 120x67,
   tiles 15x9, registro de 68 linhas) sobe para 68; fora disso nada muda */
static void *xg_send_seen;
static void *xg_fake_send(uint64_t *cl, uint64_t item) { (void)item; xg_send_seen = cl; return (void *)0x1234; }

TEST(xg_send_map_height_raised)
{
    static uint8_t map[0x140];
    static uint32_t rec[0x24 * 2], it[4 * 2];
    static uint64_t items, recs, cl[4];
    fake_azimuth(1);
    memset(map, 0, sizeof map);
    map[0] = 1;
    *(uint32_t *)(map + 8) = 120; *(uint32_t *)(map + 0xc) = 67;
    *(uint32_t *)(map + 0x14) = 15; *(uint32_t *)(map + 0x18) = 9;
    memset(rec, 0, sizeof rec);
    rec[0x24 + 0] = 1; rec[0x24 + 8] = 2048; rec[0x24 + 10] = 68;
    *(uint8_t **)(rec + 0x24 + 0x18) = map;
    memset(it, 0, sizeof it);
    it[4] = 1;                                     /* item 1 -> registro 1 */
    items = (uint64_t)(uintptr_t)it; recs = (uint64_t)(uintptr_t)rec;
    cl[0] = (uint64_t)(uintptr_t)&items; cl[1] = (uint64_t)(uintptr_t)&recs;
    orig_xg_send = xg_fake_send;
    CHECK(hook_xg_send(cl, 1) == (void *)0x1234);
    CHECK(xg_send_seen == cl);
    CHECK_EQ(*(uint32_t *)(map + 0xc), 68);
    /* tiles nao cobrem (8 x 8 = 64 < 68): nao mexe */
    *(uint32_t *)(map + 0xc) = 63; *(uint32_t *)(map + 0x18) = 8;
    hook_xg_send(cl, 1);
    CHECK_EQ(*(uint32_t *)(map + 0xc), 63);
    /* mapa nao comprimido (lista): nao mexe */
    *(uint32_t *)(map + 0x18) = 9; *(uint32_t *)(map + 0xc) = 67; map[0] = 0;
    hook_xg_send(cl, 1);
    CHECK_EQ(*(uint32_t *)(map + 0xc), 67);
    /* fora do FM5 o hook nem existe: so o apply_patches do FM5 o instala (install_on_real_azimuth) */
}

/* ------------------------------------------------------------------------ */
/* azimuth.exe real: azimuth_install() (travas, allow list, hooks)                    */
/* ------------------------------------------------------------------------ */
static int hooked_to(const uint8_t *p, const void *fn)
{
    return p[0] == 0xff && p[1] == 0x25 && !memcmp(p + 2, "\0\0\0\0", 4) && *(void *const *)(p + 6) == fn;
}

TEST(install_on_real_azimuth)
{
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL), *fh2_rec;
    struct { const void **ptr; uint64_t count; } *span;
    size_t i;
    if (!az) { test_skip(AZIMUTH_EXE " ausente"); return; }
    g_base = az;
    ini_set(NULL);
    log_capture();
    fh2_rec = *(uint8_t **)(az + RVA_ALLOW_ARRAY);
    CHECK(fh2_rec > az && fh2_rec < az + 0x3b9000);  /* relocado para dentro da imagem */
    REQUIRE(azimuth_install());
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK(log_has("FmodAudioClient (vtable 0x2a1da0) with FM6 diagnostics"));   /* slots do Azimuth conferidos */
    /* allow list {fh2, fh2 f&f, fm5, fm6} */
    span = (void *)(az + RVA_ALLOW_SPAN);
    CHECK_EQ(span->count, 4);
    CHECK(span->ptr[0] == fh2_rec && span->ptr[2] == fm5_rec && span->ptr[3] == fm6_rec);
    CHECK(!wcscmp(*(const wchar_t **)fm5_rec, L"7b35d917"));
    CHECK(!wcscmp(*(const wchar_t **)fm6_rec, L"64f9397e"));
    CHECK(*(void **)(fm5_rec + 0x58) == *(void **)(fh2_rec + 0x58));
    CHECK(*(void **)(fm6_rec + 0x58) == *(void **)(fh2_rec + 0x58));
    /* bytes esperados escritos aqui de novo (nao lidos de exe_patches): PASSO 7, 09_proxy_winmm.txt */
    CHECK_EQ(sizeof exe_patches / sizeof *exe_patches, 7);
    CHECK_HEX(az + 0x85c8b, "725b");              /* template: jne -> jb */
    CHECK_HEX(az + 0xbe6e2, "0f87f1000000");      /* TLS tamanho: jne -> ja */
    CHECK_HEX(az + 0xbe6f9, "0f87da000000");      /* TLS Characteristics: jne -> ja */
    CHECK_HEX(az + 0xbe70d, "488b4608909090");    /* TLS zeros: mov rax,[rsi+8] */
    CHECK_HEX(az + 0xbe4db, "909090909090");      /* exe: NumberOfSections == 14 */
    CHECK_HEX(az + 0x9f33b, "909090909090");      /* XG SizeOfImage */
    CHECK_HEX(az + 0x9f348, "909090909090");      /* XG AddressOfEntryPoint */
    for (i = 0; i < sizeof exe_patches / sizeof *exe_patches; i++)
        CHECK_HEX(az + exe_patches[i].rva, exe_patches[i].repl);
    CHECK(hooked_to(az + RVA_XG_NAME_CMP, (void *)xg_name_stub));
    CHECK(xg_match_target == az + RVA_XG_NAME_MATCH && xg_next_target == az + RVA_XG_NAME_NEXT);
    CHECK(hooked_to(az + RVA_APPLY_PATCHES, (void *)hook_apply_patches));
    CHECK(hooked_to(az + RVA_XG_COMPUTER, (void *)hook_xg_computer));
    /* envio das placement textures (correcao dos mips BC do FM5): fora do DllMain, so no apply_patches do FM5 */
    CHECK(!hooked_to(az + RVA_XG_SEND, (void *)hook_xg_send));
    fm5_xg_send_install();
    CHECK(hooked_to(az + RVA_XG_SEND, (void *)hook_xg_send));
    /* trampolim do apply_patches: 14 bytes originais + jmp de volta */
    CHECK_HEX((uint8_t *)orig_apply_patches, "48895c2418488974242055574154");
    CHECK(*(uint8_t **)((uint8_t *)orig_apply_patches + 14 + 6) == az + RVA_APPLY_PATCHES + 14);
    /* hooks de d3d/winrt/shape: todos instalados (log "hook %#x instalado") */
    CHECK(log_has("hook 0x12b780 installed") && log_has("hook 0x1338e0 installed") && log_has("hook 0x3a290 installed"));
    CHECK(log_has("hook 0xda9e0 installed") && log_has("hook 0x9a680 installed") && log_has("hook 0x208790 installed"));
    CHECK(log_has("hook 0x203b60 installed"));                  /* despacho de tabela (GameChat do FM6) */
    /* LocalSpaceStatics slot 8 (GetForMachineAsync): stub fatal 0x189bd0 -> proxy (common/winrt.c) */
    CHECK(*(uint8_t **)(az + 0x2b6628) != az + 0x189bd0 && log_has("patch 0x2b6628: LocalSpaceStatics"));
    /* D3D12CreateDevice (common/debug12.c): o call [rip] em 0x567df passa a ler um ponteiro nosso, nao a IAT 0x26f868
       (o RenderDoc reescreve a IAT); a IAT fica intacta */
    {
        uint8_t *call = az + 0x567df;
        int32_t disp;
        CHECK_HEX(call, "ff15");
        memcpy(&disp, call + 2, 4);
        CHECK(call + 6 + disp != az + 0x26f868);
        CHECK(*(void **)(call + 6 + disp) == (void *)debug12_create_device);
        CHECK(log_has("debug12: hook on D3D12CreateDevice call 0x567df"));
    }
    dump_log_on_fail();
    pe_unmap(az);
}

TEST(install_writes_nothing_if_any_site_differs)
{
    uint8_t *az = pe_map(AZIMUTH_EXE, NULL), *copy;
    if (!az) { test_skip(AZIMUTH_EXE " ausente"); return; }
    g_base = az;
    az[RVA_TLS_ZERO_END] ^= 1;                   /* um site no meio da lista nao confere */
    copy = malloc(0x3b9000);
    memcpy(copy, az, 0x3b9000);
    log_capture();
    CHECK(!azimuth_install());
    CHECK(log_has("bytes do not match at 0xbe70d"));
    CHECK(!memcmp(copy, az, 0x3b9000));          /* imagem intacta */
    free(copy);
    pe_unmap(az);
}

/* ------------------------------------------------------------------------ */
/* exe do FM5 real: fm5_patches() com as opcoes do .ini e do launcher         */
/* ------------------------------------------------------------------------ */
typedef struct { uint8_t *az, *game; uint8_t opt[0x40]; } Fm5Run;

static int fm5_run(Fm5Run *r, uint32_t height, uint32_t width, uint32_t fps, int shadows, int refl, int car,
                   const LangEntry *speech)
{
    uint8_t *h;
    r->az = pe_map(AZIMUTH_EXE, NULL);
    r->game = pe_map(FM5_EXE, NULL);
    if (!r->az || !r->game) { pe_unmap(r->az); pe_unmap(r->game); test_skip("azimuth.exe/exe do FM5 ausentes"); return 0; }
    g_base = r->az;
    /* ApuCreateHeap do Azimuth (nao executavel aqui): xor eax,eax ; ret, com a tabela como ele deixaria */
    memcpy(r->az + RVA_APU_CREATE_HEAP, "\x31\xc0\xc3", 3);
    h = r->az + RVA_APU_HEAPS;
    *(void **)h = VirtualAlloc(NULL, 0x500000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *(uint32_t *)(h + 8) = 0x500000; *(uint32_t *)(h + 0xc) = 0x1000000;
    memset(r->opt, 0, sizeof r->opt);
    *(uint32_t *)(r->opt + 4) = height; *(uint32_t *)(r->opt + 8) = width; *(uint32_t *)(r->opt + 0xc) = fps;
    ((int32_t *)(r->opt + 0x14))[0] = shadows; ((int32_t *)(r->opt + 0x14))[1] = refl;
    ((int32_t *)(r->opt + 0x14))[2] = car;
    *(const LangEntry **)(r->opt + 0x30) = speech;
    log_capture();
    g_game = r->game;   /* = hook_apply_patches (azimuth.c) antes do apply_patches do jogo */
    fm5_patches(r->game, r->opt);
    return 1;
}

static void fm5_run_end(Fm5Run *r)
{
    dump_log_on_fail();
    VirtualFree(*(void **)(r->az + RVA_APU_HEAPS), 0, MEM_RELEASE);
    pe_unmap(r->az); pe_unmap(r->game);
}

static int is_hook(const uint8_t *p) { uint32_t d; memcpy(&d, p + 2, 4); return p[0] == 0xff && p[1] == 0x25 && d == 0; }

static uint32_t imm32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

TEST(fm5_patches_all_options_ultra)
{
    Fm5Run r;
    uint8_t *g;
    int32_t links_a, links_b;
    size_t i;
    ini_set("[FM5]\nSkipIntro=1\nSkipPresentationVideo=1\nUnlockDLC=1\nEnableMarketplace=1\n");
    if (!fm5_run(&r, 1440, 2560, 120, 3, 3, 3, &fm5_langs[2] /* de */)) return;
    g = r.game;
    links_a = *(int32_t *)(r.az + RVA_LINKS_END_A + 3);
    links_b = *(int32_t *)(r.az + RVA_LINKS_END_B + 3);
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK(!log_has("ignored"));
    /* Live table: fim dos links recuado em um link (PartyConfig) */
    CHECK_EQ(links_a, 0x1651e5 - LINK_SIZE);
    CHECK_EQ(links_b, 0x164263 - LINK_SIZE);
    /* heap da APU cacheado ampliado */
    CHECK_EQ(*(uint32_t *)(r.az + RVA_APU_HEAPS + 8), FM5_APU_CACHED_SIZE);
    /* SkipIntro / SkipPresentationVideo / UnlockDLC / EnableMarketplace */
    for (i = 0; i < 3; i++) CHECK_HEX(g + fm5_splash_intro_cmps[i], "80be9f300000ff");
    CHECK_EQ(g[FM5_IE_VIDEO_PLAY], 0xe9);
    CHECK_EQ(FM5_IE_VIDEO_PLAY + 5 + (int32_t)imm32(g + FM5_IE_VIDEO_PLAY + 1), FM5_IE_VIDEO_DONE);
    CHECK_HEX(g + FM5_OFFER_OWNED, "b801000000c3");
    CHECK_HEX(g + FM5_OFFER_NOINV, "b201");
    CHECK_HEX(g + FM5_MARKET_DEFAULT, "c783bc30000000000101");
    /* resolucao 2560x1440 */
    CHECK_EQ(imm32(g + FM5_RES_HEIGHT + 6), 1440);
    CHECK_EQ(g[FM5_RES_WIDTH + 0x10], 0xbd);
    CHECK_EQ(imm32(g + FM5_RES_WIDTH + 0x11), 2560);
    CHECK_HEX(g + FM5_RES_UI_SIZE, "b80000f04489456f" "b800008744894573");
    CHECK_HEX(g + FM5_UI_VP_CALL, "ff15");
    CHECK(*(void **)(g + FM5_UI_SLOT) == (void *)fm5_ui_vcall_stub);
    CHECK_EQ(FM5_UI_VP_CALL + 6 + (int32_t)imm32(g + FM5_UI_VP_CALL + 2), FM5_UI_SLOT);
    /* fps 120: setter do modo de present + limitador + passo medido */
    CHECK(*(void **)(g + FM5_FRAME_VTBL + 0x180) == (void *)fm5_frame_set_sync_stub);
    CHECK(*(void **)(g + FM5_FRAME11_VTBL + 0x180) == (void *)fm5_frame_set_sync_stub);
    CHECK(*(void **)(g + FM5_FRAME11_VTBL + 0xe8) == (void *)fm5_frame_limit);
    CHECK_EQ(g[FM5_STEP_BRANCH], 0xeb);
    CHECK(fps_force && fps_step > 0.0083 && fps_step < 0.0084);
    /* graficos ultra (detalhes em t_gfx.c): getters com hook, cubemap 2048, espelho 2304x768 */
    CHECK(is_hook(g + 0x8433c0) && is_hook(g + 0x735550) && is_hook(g + 0x859710) && is_hook(g + 0x6faf60));
    CHECK(is_hook(g + 0x8aa8b0) && is_hook(g + 0x8aaa20) && is_hook(g + 0x735910) && is_hook(g + 0x14e720));
    CHECK(is_hook(g + 0x6faad0));
    CHECK_EQ(imm32(g + 0x6d6977 + 1), 2048);
    CHECK_EQ(imm32(g + 0x73adc3 + 1), 2304);
    CHECK_HEX(g + 0x8739f1, "4138ad1c03000075089090");
    /* fala forcada (de = indice 4): mov ebp,4 ; jmp +0x16 */
    CHECK_HEX(g + FM5_SPEECH_SITE, "bd04000000eb16");
    fm5_run_end(&r);
}

TEST(fm5_patches_low_no_ini)
{
    Fm5Run r;
    uint8_t *g;
    size_t i;
    ini_set(NULL);
    if (!fm5_run(&r, 0, 0, 30, 0, 0, 0, NULL)) return;
    g = r.game;
    CHECK(!log_has("does not match") && !log_has("do not match"));
    /* sem .ini: nada das opcoes */
    for (i = 0; i < 3; i++) CHECK_HEX(g + fm5_splash_intro_cmps[i], "80be9f30000000");
    CHECK_HEX(g + FM5_IE_VIDEO_PLAY, "4883ec78");
    CHECK_HEX(g + FM5_OFFER_OWNED, "48895c2410");
    CHECK_HEX(g + FM5_MARKET_DEFAULT, "c783bc30000000000001");
    /* resolucao 0x0: nada */
    CHECK_HEX(g + FM5_RES_HEIGHT, "c7836025000038040000");
    CHECK_HEX(g + FM5_UI_VP_CALL, "ff9088010000");
    /* 30 fps: setter trocado (sem forcar), limitador original */
    CHECK(*(void **)(g + FM5_FRAME_VTBL + 0x180) == (void *)fm5_frame_set_sync_stub);
    CHECK(!fps_force);
    CHECK(*(uint8_t **)(g + FM5_FRAME11_VTBL + 0xe8) == g + FM5_FRAME11_LIMIT);
    CHECK_HEX(g + FM5_STEP_BRANCH - 7, "803dff16000200740f");
    /* low: sombra so a resolucao, LOD so ranking/distancia, cubemap 256, espelho 384x128 */
    CHECK(is_hook(g + 0x8433c0) && !is_hook(g + 0x735550));
    CHECK(is_hook(g + 0x859710) && !is_hook(g + 0x8aa8b0) && !is_hook(g + 0x6faad0));
    CHECK_EQ(imm32(g + 0x6d6977 + 1), 256);
    CHECK_EQ(imm32(g + 0x73adc3 + 1), 384);
    CHECK_HEX(g + 0x8739f1, "4138ad1c0300007508fec3");
    CHECK_HEX(g + FM5_SPEECH_SITE, "418b6e043bee7c");
    fm5_run_end(&r);
}

TEST(fm5_patches_default_levels_untouched)
{
    Fm5Run r;
    uint8_t *g;
    ini_set(NULL);
    if (!fm5_run(&r, 1080, 1920, 60, 1, 1, 1, NULL)) return;
    g = r.game;
    CHECK(!log_has("does not match") && !log_has("do not match"));
    CHECK(!is_hook(g + 0x8433c0) && !is_hook(g + 0x859710) && !is_hook(g + 0x735910));
    CHECK_EQ(imm32(g + 0x6d6977 + 1), 512);
    CHECK_EQ(imm32(g + 0x73adc3 + 1), 768);
    CHECK_EQ(imm32(g + FM5_RES_HEIGHT + 6), 1080);
    CHECK(fps_force);                             /* 60: modo 1 (30 fps) vira 0 */
    CHECK(*(uint8_t **)(g + FM5_FRAME11_VTBL + 0xe8) == g + FM5_FRAME11_LIMIT);   /* limitador so com > 60 */
    fm5_run_end(&r);
}

TEST(fm5_resolution_ultrawide_and_height_only)
{
    Fm5Run r;
    ini_set(NULL);
    if (!fm5_run(&r, 1080, 2560, 60, 1, 1, 1, NULL)) return;
    CHECK(log_has("!= 16:9"));
    CHECK(ui_sx > 1.333f && ui_sx < 1.334f && ui_sy == 1.0f);
    fm5_run_end(&r);
    if (!fm5_run(&r, 720, 0, 60, 1, 1, 1, NULL)) return;
    CHECK_EQ(imm32(r.game + FM5_RES_HEIGHT + 6), 720);
    CHECK_HEX(r.game + FM5_RES_WIDTH + 0x10, "c4e1fa2cea");     /* largura = altura * 16/9 do proprio jogo */
    CHECK(ui_sx > 0.666f && ui_sx < 0.667f && ui_sy > 0.666f && ui_sy < 0.667f);   /* 1280x720 */
    fm5_run_end(&r);
}

/* viewport da UI: so a chamada de 0xb60db8 e escalada */
static float ui_seen[6];
static void ui_fake_setvp(void *obj, uint32_t n, const float *vp, uintptr_t r9)
{
    (void)obj; (void)n; (void)r9;
    memcpy(ui_seen, vp, sizeof ui_seen);
}

TEST(fm5_ui_viewport_scaled)
{
    Fm5Run r;
    void *obj[0x190 / 8] = { 0 };
    static const float vp[6] = { 0, 0, 1920, 1080, 0, 1 };
    ini_set(NULL);
    if (!fm5_run(&r, 1440, 2560, 60, 1, 1, 1, NULL)) return;
    obj[0x188 / 8] = (void *)ui_fake_setvp;
    ((void (*)(void *, uint32_t, const float *, uintptr_t))t_call_from(r.game + FM5_UI_VP_CALL + 6,
                                                                      (void *)fm5_ui_vcall))(obj, 1, vp, 0);
    CHECK(ui_seen[2] == 2560.0f && ui_seen[3] == 1440.0f && ui_seen[0] == 0.0f && ui_seen[5] == 1.0f);
    CHECK(vp[2] == 1920.0f);                     /* o original do jogo nao e alterado */
    /* grafico da UI (relacao de marchas): SetViewport do renderizador no slot 0x28 */
    CHECK_HEX(r.game + 0x52175b, "c5f829442430" "c5fb114c2440" "ff15db" "91e0ff");   /* call [rip -> 0x32a948] */
    CHECK(*(void **)(r.game + 0x32a948) == (void *)ui_chart_vcall);
    {
        static const int32_t rect[6] = { 1406, 695, 397, 170, 0, 0x3f800000 };
        void *vt[6] = { 0 }, *robj = vt;
        vt[0] = vt;                                  /* [obj] = vtable = o proprio array */
        vt[5] = (void *)chart_fake_setvp;
        ui_chart_vcall(robj, rect);
        CHECK(chart_seen[0] == 1875 && chart_seen[1] == 927 && chart_seen[2] == 529 && chart_seen[3] == 226);
        CHECK(log_has("FM5: UI graphic 1406,695 397x170 -> 1875,927 529x226"));
    }
    fm5_run_end(&r);
}

TEST(fm5_frame_set_sync_modes)
{
    uint8_t frame[0x600] = { 0 };
    fps_game = (uint8_t *)0x140000000;
    fps_force = 1;
    fm5_frame_set_sync(frame, 1, 0, 0);
    CHECK_EQ(*(uint32_t *)(frame + 0xa0), 0);    /* 30 fps pedido -> 60 */
    fm5_frame_set_sync(frame, 2, 0, 0);
    CHECK_EQ(*(uint32_t *)(frame + 0xa0), 2);
    fps_force = 0;
    fm5_frame_set_sync(frame, 1, 0, 0);
    CHECK_EQ(*(uint32_t *)(frame + 0xa0), 1);
}

static double fake_clock;
static double fake_now(void *c) { (void)c; return fake_clock += 0.001; }

TEST(fm5_frame_limit_waits_step)
{
    uint8_t frame[0x600] = { 0 };
    *(double (**)(void *))(frame + 0x440 + 0x58) = fake_now;
    fake_clock = 1.0;
    *(double *)(frame + 0x440) = 1.0;
    fps_step = 1.0 / 120;
    fm5_frame_limit(frame);
    CHECK(fake_clock >= 1.0 + 1.0 / 120);
    CHECK(*(double *)(frame + 0x440) == fake_clock);
}
