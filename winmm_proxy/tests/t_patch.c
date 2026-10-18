/*
 * Testes de fm5_patch.c: helpers de patch/hook, registro do FM5, XG e - com os binarios reais mapeados
 * (azimuth.exe na raiz, exe do FM5 em game_binaries) - install() e fm5_patches() completos.
 */
#include "../fm5_patch.c"
#include "test.h"
#include <stdlib.h>
extern void debug12_create_device(void);   /* debug12_fm5.c (so o endereco) */

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
    if (t_failed) printf("    --- azimuth_fm5.log do teste ---\n%s    ---\n", log_text());
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
    CHECK(log_has("bytes nao conferem"));
}

TEST(is_fm5_reads_game_record)
{
    fake_azimuth(0);
    CHECK(!is_fm5());
    fake_azimuth(1);
    CHECK(is_fm5());
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
    CHECK(is_fm6() && !is_fm5());
    hook_apply_patches(img, opt);
    CHECK_EQ(orig_apply_calls, 0);
    CHECK(log_has("FM6"));
    CHECK(log_has("APU: heap criado p/ o FM6"));                         /* XDK sem ApuCreateHeap */
    CHECK_EQ(*(uint32_t *)(az + RVA_APU_HEAPS + 8), 0x1000000);
    VirtualFree(*(void **)(az + RVA_APU_HEAPS), 0, MEM_RELEASE);
    CHECK(slot0_game == game && slot0_n == 329 && game_tls_pending);   /* sites do slot 0 para o 1o import */
    CHECK(log_has("montador de batch do FM6 em game+0xda9b94 nao confere"));   /* zerado: nada muda */
    CHECK(log_has("FM6: SkipIntro: site 0xa06b33 nao confere, ignorado"));     /* opcoes do azimuth_fm6.ini */
    ini6_set(NULL);
    slot0_n = 0; game_tls_pending = 0;
    *(void **)(az + RVA_GAME_RECORD) = NULL;
    hook_apply_patches(img, opt);
    CHECK_EQ(orig_apply_calls, 1);
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
    CHECK(log_has("TLS: template do jogo no azimuth-tls.dll"));
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
    CHECK(log_has("nao confere; nenhum alterado"));
    memcpy(img + 0x1100, "\x65\x48\x8b\x04\x25\x58\x00\x00\x00\xc3", 10);
    memcpy(img + 0x1200, "\x65\x48\x8b\x0c\x25\x58\x00\x00\x00\x48\x89\xc8\xc3", 13);
    memcpy(img + 0x1300, "\x65\x4c\x8b\x1c\x25\x58\x00\x00\x00\x4c\x89\xd8\xc3", 13);
    tls_slot0_redirect(5);
    f = (void *)(img + 0x1100); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    f = (void *)(img + 0x1200); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    f = (void *)(img + 0x1300); CHECK_EQ(f(), (uint64_t)(uintptr_t)&tlsp[5]);
    CHECK(log_has("TLS: 3 leituras do slot 0 do jogo -> indice 5 (3 stubs"));
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
    ini_set("[FM5]\nSkipIntro=1\nD3D12Debug=2\n");
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 1);
    CHECK_EQ(cfg_int(L"D3D12Debug", 0), 2);
    CHECK_EQ(cfg_int(L"UnlockDLC", 5), 5);       /* ausente -> padrao */
    ini_set(NULL);
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 0);
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
    CHECK(!log_has("nao confere"));
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_patches(game);
    CHECK(log_has("FM6: SkipIntro: site 0xa06b33 nao confere, ignorado"));
    ini6_set(NULL);
    pe_unmap(game);
    dump_log_on_fail();
}

/* exe real do FM6: SkipPresentationVideo esvazia o IntroVideo nos dois leitores da coluna do GameDB */
TEST(fm6_skip_presentation_video_patches_exe)
{
    uint8_t *game = pe_map(FM6_EXE, NULL);
    if (!game) { test_skip("exe do FM6 ausente"); return; }
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
    CHECK(!log_has("nao confere"));
    CHECK(!log_has("FM6: SkipIntro=1"));
    /* 2a vez: bytes ja trocados -> nada muda */
    fm6_patches(game);
    CHECK(log_has("FM6: SkipPresentationVideo: site nao confere, ignorado"));
    CHECK_HEX(game + 0xce1eb4, "85c06690");
    ini6_set(NULL);
    pe_unmap(game);
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
    ini_set("[FM5]\nD3D12Debug=3\n");
    fm6_patches(game);
    CHECK_HEX(az + 0x13bae0, "48895c241055565748");
    ini_set("[FM5]\nD3D12Debug=5\n");
    fm6_patches(game);
    CHECK_HEX(az + 0x13bae0, "31c0c324105556574881");
    CHECK(log_has("FM6: D3D12Debug=5: placement textures como recurso committed"));
    fm6_patches(game);                            /* bytes ja trocados -> nada muda */
    CHECK(log_has("FM6: D3D12Debug=5: site 0x13bae0 nao confere, ignorado"));
    CHECK_HEX(az + 0x13bae0, "31c0c3");
    ini_set(NULL);
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
    ini_set("[FM5]\nD3D12Debug=5\n");
    fm6_patches(game);
    CHECK_HEX(az + 0xda270, "48895c241855565741564157488bec");
    ini_set("[FM5]\nD3D12Debug=6\n");
    fm6_patches(game);
    CHECK_HEX(az + 0xda270, "ff2500000000");
    CHECK(log_has("FM6: D3D12Debug=6: recursos/views nunca destruidos"));
    orig_gfx_destroy = fake_gfx_destroy;          /* nao roda o destroy real do Azimuth */
    for (i = 0; i < sizeof leak / sizeof *leak; i++) {
        destroyed_obj = NULL;
        *(uint32_t *)(obj + 0x38) = leak[i];
        fm6_leak_destroy(obj);
        CHECK(destroyed_obj == NULL);
    }
    CHECK(log_has("destroy pulado tipo 0xc (1)"));
    for (i = 0; i < sizeof keep / sizeof *keep; i++) {
        destroyed_obj = NULL;
        *(uint32_t *)(obj + 0x38) = keep[i];
        fm6_leak_destroy(obj);
        CHECK(destroyed_obj == obj);
    }
    fm6_patches(game);                            /* bytes ja trocados -> nada muda */
    CHECK(log_has("FM6: D3D12Debug=6: site 0xda270 nao confere, ignorado"));
    ini_set(NULL);
    pe_unmap(game);
    pe_unmap(az);
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
    build_record();
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
    build_fm6_record();
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
    build_record();
    check_record_files(fm5_rec, FM5_DIR);
}

TEST(fm6_record_matches_game_files)
{
    static uint8_t fh2[0x350];
    fake_azimuth(0);
    *(void **)(g_base + RVA_ALLOW_ARRAY) = fh2;
    build_fm6_record();
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

/* ------------------------------------------------------------------------ */
/* azimuth.exe real: install() (travas, allow list, hooks)                    */
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
    REQUIRE(install());
    CHECK(!log_has("nao confere"));
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
    /* trampolim do apply_patches: 14 bytes originais + jmp de volta */
    CHECK_HEX((uint8_t *)orig_apply_patches, "48895c2418488974242055574154");
    CHECK(*(uint8_t **)((uint8_t *)orig_apply_patches + 14 + 6) == az + RVA_APPLY_PATCHES + 14);
    /* hooks de d3d/winrt/shape: todos instalados (log "hook %#x instalado") */
    CHECK(log_has("hook 0x12b780 instalado") && log_has("hook 0x1338e0 instalado") && log_has("hook 0x3a290 instalado"));
    CHECK(log_has("hook 0xda9e0 instalado") && log_has("hook 0x9a680 instalado") && log_has("hook 0x208790 instalado"));
    CHECK(log_has("hook 0x203b60 instalado"));                  /* despacho de tabela (GameChat do FM6) */
    /* LocalSpaceStatics slot 8 (GetForMachineAsync): stub fatal 0x189bd0 -> proxy (winrt_fm5.c) */
    CHECK(*(uint8_t **)(az + 0x2b6628) != az + 0x189bd0 && log_has("patch 0x2b6628: LocalSpaceStatics"));
    /* D3D12CreateDevice (debug12_fm5.c): o call [rip] em 0x567df passa a ler um ponteiro nosso, nao a IAT 0x26f868
       (o RenderDoc reescreve a IAT); a IAT fica intacta */
    {
        uint8_t *call = az + 0x567df;
        int32_t disp;
        CHECK_HEX(call, "ff15");
        memcpy(&disp, call + 2, 4);
        CHECK(call + 6 + disp != az + 0x26f868);
        CHECK(*(void **)(call + 6 + disp) == (void *)debug12_create_device);
        CHECK(log_has("debug12: hook no call 0x567df"));
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
    CHECK(!install());
    CHECK(log_has("bytes nao conferem em 0xbe70d"));
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
    CHECK(!log_has("nao confere"));
    CHECK(!log_has("ignorado"));
    CHECK(g_game == g);
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
    CHECK(*(void **)(g + FM5_UI_SLOT) == (void *)fm5_ui_vcall);
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
    CHECK(!log_has("nao confere"));
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
    CHECK(!log_has("nao confere"));
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
