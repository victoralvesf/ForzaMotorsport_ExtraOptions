/*
 * Testes de fm5_patch.c: helpers de patch/hook, registro do FM5, XG e - com os binarios reais mapeados
 * (azimuth.exe na raiz, exe do FM5 em game_binaries) - install() e fm5_patches() completos.
 */
#include "../fm5_patch.c"
#include "test.h"
#include <stdlib.h>

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

uint8_t *fake_azimuth(int fm5)
{
    static uint8_t *img;
    if (!img) img = t_rwx(0x3b9000);            /* SizeOfImage do azimuth.exe */
    memset(img, 0, 0x3b9000);
    if (fm5) *(void **)(img + RVA_GAME_RECORD) = fm5_rec;
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

TEST(cfg_int_reads_ini)
{
    ini_set("[FM5]\nSkipIntro=1\nD3D12Debug=2\n");
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 1);
    CHECK_EQ(cfg_int(L"D3D12Debug", 0), 2);
    CHECK_EQ(cfg_int(L"UnlockDLC", 5), 5);       /* ausente -> padrao */
    ini_set(NULL);
    CHECK_EQ(cfg_int(L"SkipIntro", 0), 0);
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

/* o registro tem de bater com os arquivos do jogo copiados em game_binaries */
TEST(record_matches_game_files)
{
    static uint8_t fh2[0x350];
    char hex[65];
    uint64_t size;
    uint32_t soi;
    uint8_t *game;
    IMAGE_NT_HEADERS64 *nt;
    fake_azimuth(0);
    *(void **)(g_base + RVA_ALLOW_ARRAY) = fh2;
    build_record();
    if (!sha256_file(FM5_EXE, hex, &size)) { test_skip(FM5_EXE " ausente"); return; }
    CHECK_STR(*(const char **)(fm5_rec + 0x018), hex);
    CHECK_EQ(*(uint64_t *)(fm5_rec + 0x020), size);
    if (sha256_file("game_binaries\\AppxManifest.xml", hex, &size))
        CHECK_STR(*(const char **)(fm5_rec + 0x010), hex);
    if (sha256_file("game_binaries\\xg_x.dll", hex, &size)) {
        CHECK_STR(*(const char **)(fm5_rec + 0x038), hex);
        CHECK_EQ(*(uint64_t *)(fm5_rec + 0x040), size);
    }
    REQUIRE((game = pe_map(FM5_EXE, &soi)));
    nt = (IMAGE_NT_HEADERS64 *)(game + ((IMAGE_DOS_HEADER *)game)->e_lfanew);
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x098), nt->OptionalHeader.SizeOfImage);
    CHECK_EQ(*(uint32_t *)(fm5_rec + 0x09c), nt->OptionalHeader.AddressOfEntryPoint);
    pe_unmap(game);
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
    /* allow list {fh2, fh2 f&f, fm5} */
    span = (void *)(az + RVA_ALLOW_SPAN);
    CHECK_EQ(span->count, 3);
    CHECK(span->ptr[0] == fh2_rec && span->ptr[2] == fm5_rec);
    CHECK(!wcscmp(*(const wchar_t **)fm5_rec, L"7b35d917"));
    CHECK(*(void **)(fm5_rec + 0x58) == *(void **)(fh2_rec + 0x58));
    /* bytes esperados escritos aqui de novo (nao lidos de exe_patches): PASSO 7, 09_proxy_winmm.txt */
    CHECK_EQ(sizeof exe_patches / sizeof *exe_patches, 6);
    CHECK_HEX(az + 0x85c8b, "725b");              /* template: jne -> jb */
    CHECK_HEX(az + 0xbe6e2, "0f87f1000000");      /* TLS tamanho: jne -> ja */
    CHECK_HEX(az + 0xbe6f9, "0f87da000000");      /* TLS Characteristics: jne -> ja */
    CHECK_HEX(az + 0xbe70d, "488b4608909090");    /* TLS zeros: mov rax,[rsi+8] */
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
