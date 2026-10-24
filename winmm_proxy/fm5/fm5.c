/*
 * Forza Motorsport 5 (7b35d917, XDK nov-2012): registro na allow list, patches do apply_patches e opcoes do
 * azimuth_fm5.ini [FM5]. Tudo aqui so roda com o FM5 carregado (fm5_game); enderecos em docs\contexto\ (PASSOS 4-11).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../proxy.h"
#include "../common/ui.h"
#include "fm5.h"


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

/* ---- azimuth_fm5.ini (pasta do azimuth.exe), secao [FM5] ---- */
static wchar_t g_ini[MAX_PATH];

int cfg_int(const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(L"FM5", key, def, g_ini);
}

/* Registro do FM5 (layout no PASSO 4, valores de docs\fm5_record.json)       */
/* ------------------------------------------------------------------------ */
static const LangEntry fm5_langs[] = {
    { L"en", 0x0, 1 },  { L"cs", 0x0, 0 },  { L"de", 0x4, 1 },  { L"en-GB", 0x2, 0 },
    { L"en-AU", 0x0, 0 }, { L"en-CA", 0x0, 0 }, { L"en-IE", 0x1, 0 }, { L"en-NZ", 0x1, 0 },
    { L"es", 0x7, 1 },  { L"es-MX", 0x6, 1 }, { L"fr", 0x5, 1 },  { L"fr-CA", 0x5, 0 },
    { L"hu", 0x0, 0 },  { L"it", 0x8, 1 },  { L"ja", 0x3, 1 },  { L"ko", 0x9, 0 },
    { L"nl", 0x10, 1 }, { L"pl", 0x0, 0 },  { L"pt", 0xb, 1 },  { L"ru", 0xf, 1 },
    { L"zh-Hant", 0xa, 0 },
};
static uint8_t __attribute__((aligned(16))) fm5_rec[GAME_RECORD_SIZE];
#define SET_PTR(off, v) (*(const void **)(rec + (off)) = (const void *)(v))
#define SET_U64(off, v) (*(uint64_t *)(rec + (off)) = (uint64_t)(v))
#define SET_U32(off, v) (*(uint32_t *)(rec + (off)) = (uint32_t)(v))

static void fm5_build_record(uint8_t *rec, const uint8_t *fh2)
{
    int i;

    memset(rec, 0, GAME_RECORD_SIZE);
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
    SET_PTR(0x318, record_unknown_hash);
    SET_PTR(0x330, fm5_langs);
    SET_U64(0x338, sizeof fm5_langs / sizeof fm5_langs[0]);
    rec[0x340] = 0;
    rec[0x341] = 1;              /* EXPERIMENTAL */
}

int is_fm5(void) { return game_current() == &fm5_game; }

#define RVA_LINKS_END_A    0x15772c  /* lea r14,[0x1402bc918] (fim da lista de links da Live table) */
#define RVA_LINKS_END_B    0x1586ae  /* lea rdi,[0x1402bc918] */
#define LINK_SIZE          0x38      /* ultimo link = Windows.Xbox.Multiplayer.PartyConfig */
/* Live table: o runtime do console do FM5 (xdk-nov-2012) nao tem a classe
   Windows.Xbox.Multiplayer.PartyConfig -> tirar o ultimo link (os dois lacos usam o fim) */
static void drop_partyconfig_link(void)
{
    static const struct { uint32_t rva; const char *orig; } sites[] = {
        { RVA_LINKS_END_A, "4c8d35e5511600" }, { RVA_LINKS_END_B, "488d3d63421600" } };
    size_t i;
    for (i = 0; i < 2; i++)
        if (!bytes_match(g_base + sites[i].rva, sites[i].orig, 7)) {
            wlog("Live table: end-of-links lea does not match at %#x", sites[i].rva);
            return;
        }
    for (i = 0; i < 2; i++) {
        int32_t disp = *(int32_t *)(g_base + sites[i].rva + 3) - LINK_SIZE;
        write_mem(g_base + sites[i].rva + 3, &disp, 4);
    }
    wlog("Live table: PartyConfig link removed");
}

#define FM5_UI_VP_CALL     0xb60db8  /* ff 90 88 01 00 00  call [rax+0x188] (viewport) */
#define FM5_UI_SLOT        0xb60c98  /* padding cc entre 0xb60c91 e 0xb60ca0 */
#define FM5_CHART_VP       0x52175b  /* graficos da UI: mov rax,[rcx] ; 2 stores ; call [rax+0x28] (ver ui_chart_vcall) */
#define FM5_CHART_SLOT     0x32a948  /* padding cc depois de um ret (15 bytes) */

static const uint8_t fm5_ui_size_code[36] = {
    0xb8, 0x00, 0x00, 0xf0, 0x44,  0x89, 0x45, 0x6f,       /* mov eax, 1920.0f ; mov [rbp+0x6f], eax */
    0xb8, 0x00, 0x00, 0x87, 0x44,  0x89, 0x45, 0x73,       /* mov eax, 1080.0f ; mov [rbp+0x73], eax */
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
    0x0f, 0x1f, 0x40, 0x00 };                              /* nop (4) */

static const ResSites fm5_res_sites = {
    "FM5", FM5_RES_HEIGHT, "c7836025000038040000",
    FM5_RES_WIDTH, "c5fa590d0b2aba01c5f259153f22ba01c4e1fa2cea", 0xbd,           /* rbp -> mov ebp */
    FM5_RES_UI_SIZE, "418b4708c5f857c0c4e1fa2ac0c5fa11456f418b470cc5f057c9c4e1f22ac8c5fa114d73",
    fm5_ui_size_code, sizeof fm5_ui_size_code, FM5_UI_VP_CALL, "ff9088010000", 0x188, FM5_UI_SLOT,
    FM5_CHART_VP, 0x28, FM5_CHART_SLOT };

static void fm5_resolution(uint8_t *game, uint32_t height, uint32_t width)
{
    game_resolution(game, &fm5_res_sites, height, width);
}

#define FM5_FRAME_VTBL     0x22c3350  /* .?AVFrame@@ */
#define FM5_FRAME11_VTBL   0x22c2f10  /* .?AVFrame11@@ */
#define FM5_FRAME_SET_SYNC 0x711620

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
        wlog("FM5: fps: 60 limiter / fixed step do not match, fps > 60 ignored");
        return;
    }
    fps_step = 1.0 / fps;
    write_mem(game + FM5_FRAME11_VTBL + 0xe8, &fn, 8);
    write_mem(game + FM5_STEP_BRANCH, &eb, 1);
    wlog("FM5: fps: game limiter at %u fps (step %.3f ms), simulation with measured dt", fps, fps_step * 1000);
}

static void fm5_frame_rate(uint8_t *game, uint32_t fps)
{
    static const uint32_t vtbls[] = { FM5_FRAME_VTBL, FM5_FRAME11_VTBL };
    void *fn = (void *)fm5_frame_set_sync_stub;
    size_t i;
    if (!bytes_match(game + FM5_FRAME_SET_SYNC, "8991a0000000c3", 7)) {
        wlog("FM5: fps: setter %#x does not match, ignored", FM5_FRAME_SET_SYNC);
        return;
    }
    for (i = 0; i < 2; i++)
        if (*(uint8_t **)(game + vtbls[i] + 0x180) != game + FM5_FRAME_SET_SYNC) {
            wlog("FM5: fps: vtable %#x slot 0x180 does not match, ignored", vtbls[i]);
            return;
        }
    fps_game = game;
    fps_tag = "FM5";
    fps_queue_ret = 0x6828d0;
    fps_force = fps >= 60;
    for (i = 0; i < 2; i++) write_mem(game + vtbls[i] + 0x180, &fn, 8);
    wlog("FM5: fps: frame_rate %u -> %s", fps, fps_force ? "present always with interval 1" : "original");
    if (fps > 60) fm5_frame_unlock(game, fps);
}

static void fm5_patches(uint8_t *game, const uint8_t *options)
{
    const LangEntry *speech = *(const LangEntry *const *)(options + 0x30);

    drop_partyconfig_link();
    implicit_apu_heap("FM5");

    if (cfg_int(L"SkipIntro", 0)) {
        /* cmp [rsi+0x309f],0 -> cmp [rsi+0x309f],0xff: o jogo ve a opcao ligada (so je/jne depois) */
        static const uint8_t ff = 0xff;
        size_t i, n = sizeof fm5_splash_intro_cmps / sizeof *fm5_splash_intro_cmps;
        for (i = 0; i < n; i++)
            if (!bytes_match(game + fm5_splash_intro_cmps[i], "80be9f30000000", 7)) break;
        if (i < n) wlog("FM5: SkipIntro: site %#x does not match, ignored", fm5_splash_intro_cmps[i]);
        else {
            for (i = 0; i < n; i++) write_mem(game + fm5_splash_intro_cmps[i] + 6, &ff, 1);
            wlog("FM5: SkipIntro=1 (disablesplashintro forced)");
        }
    }

    if (cfg_int(L"SkipPresentationVideo", 0)) {
        /* 0x4ab060: jmp 0x4b2f50 -> chama direto o fim do video (mesma pilha, nenhum argumento usado) */
        uint8_t *p = game + FM5_IE_VIDEO_PLAY;
        uint8_t code[5] = { 0xe9 };
        int32_t rel = (int32_t)(FM5_IE_VIDEO_DONE - (FM5_IE_VIDEO_PLAY + 5));
        if (!bytes_match(p, "4883ec7848c7442420feffffff", 13) ||
            !bytes_match(game + FM5_IE_VIDEO_DONE, "4883ec48488d15851ac200", 11))
            wlog("FM5: SkipPresentationVideo: site does not match, ignored");
        else {
            memcpy(code + 1, &rel, 4);
            write_mem(p, code, sizeof code);
            wlog("FM5: SkipPresentationVideo=1 (IE_Blockbuster.bk2 skipped)");
        }
    }

    if (cfg_int(L"UnlockDLC", 0)) {
        /* 0x203050 -> mov eax,1 ; ret: todo produto conta como comprado (refresh 0x174d00 -> lambda 0x19e690 ->
           IsPurchased=1 na arvore de ofertas; UI do marketplace 0x367ef0/0x369ef0). Sem inventario (ptr nulo)
           o refresh zera tudo via 0x1745b0(raiz, 0) -> mov dl,1. */
        static const uint8_t ret1[6] = { 0xb8, 1, 0, 0, 0, 0xc3 }, dl1[2] = { 0xb2, 0x01 };
        if (!bytes_match(game + FM5_OFFER_OWNED, "48895c241048896c2418565741564883", 16) ||
            !bytes_match(game + FM5_OFFER_NOINV - 7, "488dbe8800000033d2488bcfe8abf7ffff", 17))
            wlog("FM5: UnlockDLC: sites do not match, ignored");
        else {
            write_mem(game + FM5_OFFER_OWNED, ret1, sizeof ret1);
            write_mem(game + FM5_OFFER_NOINV, dl1, sizeof dl1);
            wlog("FM5: UnlockDLC=1 (all DLC offers count as purchased)");
        }
    }

    if (cfg_int(L"EnableMarketplace", 0)) {
        /* padrao do enablemarketplace (normalmente ligado so pela config do servidor, Main/IsMarketplaceEnabled
           em 0x1a13d0): imediato 0x01000000 -> 0x01010000 */
        static const uint8_t one = 1;
        if (!bytes_match(game + FM5_MARKET_DEFAULT, "c783bc30000000000001", 10))
            wlog("FM5: EnableMarketplace: site does not match, ignored");
        else {
            write_mem(game + FM5_MARKET_DEFAULT + 8, &one, 1);
            wlog("FM5: EnableMarketplace=1 (enablemarketplace on)");
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
            wlog("FM5: speech site does not match, ignored");
            return;
        }
        memcpy(code + 1, &speech->index, 4);
        write_mem(p, code, sizeof code);
        wlog("FM5: speech forced to %ls (index %u)", speech->locale, speech->index);
    }
}

static void fm5_load_config(void)
{
    proxy_path(L"azimuth_fm5.ini", g_ini);
    wlog("config: %ls (SkipIntro=%d SkipPresentationVideo=%d UnlockDLC=%d EnableMarketplace=%d)", g_ini,
         cfg_int(L"SkipIntro", 0), cfg_int(L"SkipPresentationVideo", 0), cfg_int(L"UnlockDLC", 0),
         cfg_int(L"EnableMarketplace", 0));
}

/* alpha-to-coverage sem o xadrez do driver (common/gpuguard.c): ligado no FM5 desde o FM6 PASSO 112 (era o padrao
   de todo jogo) */
static int fm5_a2c_no_dither(void) { return 1; }

static void fm5_apply_patches(uint8_t *game, const uint8_t *options)
{
    az_valloc_install(NULL);   /* so a faixa de VA fixa (sem traducao de protecao) */
    fm5_xg_send_install();
    fm5_patches(game, options);
}

const game fm5_game = {
    .tag = "FM5",
    .record = fm5_rec,
    .build_record = fm5_build_record,
    .defaults = { 0 },   /* camada D3D12/audio do Azimuth como no FH2 (o debug.ini liga cada uma) */
    .load_config = fm5_load_config,
    .apply_patches = fm5_apply_patches,
    .resolve_import = fm5_resolve_import,
    .xg_computer = fm5_xg_computer,
    .shape_graph = fm5_shape_graph,
    .winrt_factory = fm5_winrt_factory,
    .winrt_template = fm5_winrt_template,
    .object_released = fm5_object_released,
    .context_index = fm5_context_index,
    .a2c_no_dither = fm5_a2c_no_dither,
};
