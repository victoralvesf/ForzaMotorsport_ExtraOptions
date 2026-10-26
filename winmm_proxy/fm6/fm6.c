/*
 * Forza Motorsport 6 (64f9397e, Monument 1.0.0.44, XDK "era"): registro na allow list, patches do apply_patches e
 * opcoes do azimuth_fm6.ini [FM6]. Tudo aqui so roda com o FM6 carregado (fm6_game); historico em docs\contexto\fm6\.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../proxy.h"
#include "../common/ui.h"
#include "fm6.h"


/* ---- RVAs no exe do FM6 (Monument 1.0.0.44) ---- */
/* resolucao (mesmos sites do FM5, ver fm6_resolution) */
#define FM6_RES_HEIGHT     0x552dff  /* mov dword [rbx+0x2578], 0x438 */
#define FM6_RES_WIDTH      0x55c690  /* vmulss ; vmulss ; vcvttss2si rdx,xmm2 (+0x10) */
#define FM6_RES_UI_SIZE    0x9faf52  /* float([rbx+8]), float([rbx+0xc]) -> [rbp+0x38/0x3c] (34 bytes) */
#define FM6_UI_VP_CALL     0x127ea5  /* ff 90 38 05 00 00  call [rax+0x538] (viewport) */
#define FM6_UI_SLOT        0x1287d8  /* padding cc entre 0x1287d8 e 0x1287e0 */
#define FM6_CHART_VP       0xcff5c5  /* graficos da UI: mov rax,[rcx] ; 2 stores ; call [rax+0x30] */
#define FM6_CHART_SLOT     0x7520    /* padding cc depois de um ret (18 bytes) */
/* SkipIntro: opcao "disablesplashintro" = byte [opcoes+0x30d7] (registrada em 0xa3ae3b). Sem imediato para trocar:
   no init 0xa06964 e "cmp [r12+0x30d7],sil ; je" (esi = 0 desde 0xa06a46) -> je vira nop (= opcao ligada);
   no logo parade 0xa08e78 (Health_Disclaimer/forzatech) e "cmp [rax+0x30d7],r15b ; jne" (r15 = 0) -> jmp */
static const uint32_t fm6_splash_intro_cmps[] = { 0xa06b33, 0xa06c4d, 0xa07e0c };
#define FM6_LOGO_PARADE_CMP 0xa08eb6
/* SkipPresentationVideo: o video vem do GameDB (CareerRaceIntros.IntroVideo; IE_Blockbuster.bk2 na intro 100, a da
   1a corrida; Porsche_IE/Nascar_Intro nas dos DLCs). Os dois leitores da coluna passam a devolver vazio, o caso
   normal de todas as outras corridas: 0xce1a04 preenche a intro (wstring em +0x28 via move-assign 0x14f490 -> nop)
   e 0xce1e50 so devolve o caminho (jne da linha achada -> nop = L"") */
#define FM6_INTRO_VIDEO_SET  0xce1c55  /* lea rcx,[rbx+0x28] ; call 0x14f490 */
#define FM6_INTRO_VIDEO_PATH 0xce1eb4  /* test eax,eax ; jne */
/* UnlockDLC: mesmo esquema do FM5 (CContentManager, refresh 0x43bd8c; inventario = estatico game+0x1dded30) */
#define FM6_OFFER_OWNED      0xa8ad80  /* EntitlementManager: produto no inventario? (al); lambda 0xa40f40, UI, expansoes */
#define FM6_OFFER_NOINV      0x43be08  /* refresh sem inventario: xor edx,edx -> 0x43be7c(raiz, 0) */

/* ---- azimuth_fm6.ini (mesma pasta), secao [FM6] ---- */
static wchar_t g_ini6[MAX_PATH];

int cfg6_int(const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(L"FM6", key, def, g_ini6);
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

static uint8_t __attribute__((aligned(16))) fm6_rec[GAME_RECORD_SIZE];

#define SET_PTR(off, v) (*(const void **)(rec + (off)) = (const void *)(v))
#define SET_U64(off, v) (*(uint64_t *)(rec + (off)) = (uint64_t)(v))
#define SET_U32(off, v) (*(uint32_t *)(rec + (off)) = (uint32_t)(v))

static void fm6_build_record(uint8_t *rec, const uint8_t *fh2)
{
    int i;

    memset(rec, 0, GAME_RECORD_SIZE);
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
    SET_PTR(0x318, record_unknown_hash);
    SET_PTR(0x330, fm6_langs);
    SET_U64(0x338, sizeof fm6_langs / sizeof fm6_langs[0]);
    rec[0x341] = 1;              /* EXPERIMENTAL */
}

int is_fm6(void) { return game_current() == &fm6_game; }

static const uint8_t fm6_ui_size_code[34] = {
    0xb8, 0x00, 0x00, 0xf0, 0x44,  0x89, 0x45, 0x38,       /* mov eax, 1920.0f ; mov [rbp+0x38], eax */
    0xb8, 0x00, 0x00, 0x87, 0x44,  0x89, 0x45, 0x3c,       /* mov eax, 1080.0f ; mov [rbp+0x3c], eax */
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,        /* nop (8) */
    0x66, 0x90 };                                          /* nop (2) */

static const ResSites fm6_res_sites = {
    "FM6", FM6_RES_HEIGHT, "c7837825000038040000",
    FM6_RES_WIDTH, "c5fa590d48cedb00c5f25915b0c8db00c4e1fa2cd2", 0xba,           /* rdx -> mov edx */
    FM6_RES_UI_SIZE, "8b4308c5f857c0c4e1fa2ac0c5fa1145388b430cc5f057c9c4e1f22ac8c5fa114d3c",
    fm6_ui_size_code, sizeof fm6_ui_size_code, FM6_UI_VP_CALL, "ff9038050000", 0x538, FM6_UI_SLOT, 0x708,
    FM6_CHART_VP, 0x30, FM6_CHART_SLOT };

/* FM6: mesmos sites. Altura no construtor das opcoes ([rbx+0x2578] = defaultverticalresolution); largura = rdx do
   [r9+0x30] que cria o render (0x55c654); setter do tamanho da UI = objeto global 0x1d86d30 slot 0x80 (getter 0x88,
   lido pelo renderizador da UI 0x127d90). */
static void fm6_resolution(uint8_t *game, uint32_t height, uint32_t width)
{
    game_resolution(game, &fm6_res_sites, height, width);
}

/* FPS no FM6: mesma cadeia do FM5 com outros enderecos. O Present (Frame11 slot 0x158 = 0x207868) le [frame+0xa0]:
   0 -> SyncInterval 1, 1 -> 2 (30 fps), outro -> 0; setter Frame/Frame11 slot 0x1a0 = 0x849940 (mesma funcao
   dobrada nas vtables de PresentationView slot 0x98: so os slots do Frame sao trocados). Nao ha o limitador de
   espera ativa do FM5 no relogio do Frame (frame+0x4d8 so e gravado depois do Present) -> > 60 sem patch. */
#define FM6_FRAME_VTBL     0x19f4008  /* .?AVFrame@@ */
#define FM6_FRAME11_VTBL   0x19f38e8  /* .?AVFrame11@@ */
#define FM6_FRAME_SET_SYNC 0x849940
#define FM6_FRAME_PRESENT  0x207868

/* Os cenarios de render (Mount\media\renderscenarios\*.xml) tem <FrameRate value="30"/> em PreRace, 321, PostRace,
   Replay, PhotoMode, FrontEnd e CarSelect (Race/Global 60). O valor (controle id 1, settings+0x58) e lido pelo getter
   0x3fe908 (mgr, idx): override [mgr+0xb0], 3 camadas de cenario, padrao [mgr+0x678]. Consumidores (0x124171,
   0x15a787, 0x15ad48) so testam == 30; a animacao pre-corrida fica a 30 mesmo com o modo de present 0. Com
   frame_rate >= 60 o hook devolve 60 no lugar de 30 (o mesmo que o cenario Race). */
#define FM6_GET_FRAME_RATE 0x3fe908
static uint32_t (*fm6_get_frame_rate_orig)(void *mgr, int idx);

static uint32_t fm6_get_frame_rate(void *mgr, int idx)
{
    static volatile LONG last = -1;
    uint32_t v = fm6_get_frame_rate_orig(mgr, idx), r = (fps_force && v == 30) ? 60 : v;
    if (InterlockedExchange(&last, (LONG)v) != (LONG)v) wlog("FM6: fps: scenario FrameRate %u -> %u", v, r);
    return r;
}

/* > 60 fps no FM6 (cont. 4): o loop espera a cada quadro o evento de vline (Frame11 slot 0x108 = 0x25cc90, chamado
   por 0x25cc5c se !unlockframerate e framesperrender == 1), que o proxy sinaliza a 60 Hz -> passa a ser frame_rate.
   O dt da simulacao (0x25cfbc) e medido (limitado a [0, 0.2 s]) mas, com lockupdatetimeto60fps (opcoes +0x2571,
   ligado pelo carregador em 0xa3a9f0), e o passo fixo 1/59.94 (0x15d61b0) -> jne em 0x25d018 vira nop (sempre dt
   medido, como o EB em 0x5d130a do FM5). */
#define FM6_STEP_BRANCH    0x25d011

static void fm6_frame_unlock(uint8_t *game, uint32_t fps)
{
    static const uint8_t nop6[6] = { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 };
    if (!bytes_match(game + FM6_STEP_BRANCH, "80bb71250000000f85f0287000", 13)) {
        wlog("FM6: fps: fixed step %#x does not match, fps > 60 ignored", FM6_STEP_BRANCH);
        return;
    }
    write_mem(game + FM6_STEP_BRANCH + 7, nop6, sizeof nop6);
    fm6_vline_rate(fps);
    wlog("FM6: fps: vline at %u Hz, simulation with measured dt", fps);
}

/* Medicao de fps do FM6 (diagnostico, sempre ligado com o fps instalado): o FM6 chama o Present do host direto (sem o
   fm5_DXGIXPresentArray). Frame11 slot 0x158 (Present, render thread) e slot 0x108 (espera do vline, thread do loop)
   passam por wrappers que somam o tempo; a cada 1 s, se o fps muda > 3, loga quadro/Present/vline em ms. */
#define FM6_FRAME_VLINE_WAIT 0x25cc90
typedef uintptr_t (*fm6_vcall4)(void *, void *, void *, void *);
static fm6_vcall4 fm6_present_orig, fm6_vline_wait_orig;
static LARGE_INTEGER fm6_qpf;
static volatile LONG64 fm6_wait_sum;

static uintptr_t fm6_vline_wait_hook(void *a, void *b, void *c, void *d)
{
    LARGE_INTEGER t0, t1;
    uintptr_t r;
    QueryPerformanceCounter(&t0);
    r = fm6_vline_wait_orig(a, b, c, d);
    QueryPerformanceCounter(&t1);
    InterlockedAdd64(&fm6_wait_sum, t1.QuadPart - t0.QuadPart);
    return r;
}

/* uma amostra por Present: [t0, t1] = dentro do Present; t1 do anterior -> t0 = resto do quadro */
static void fm6_fps_sample(LONGLONG t0, LONGLONG t1)
{
    static LONGLONG win0, prev1, in_sum;
    static LONG n, last_fps;
    if (!fm6_qpf.QuadPart) QueryPerformanceFrequency(&fm6_qpf);
    if (!win0) { win0 = prev1 = t1; return; }
    in_sum += t1 - t0;
    n++;
    prev1 = t1;
    if (t1 - win0 >= fm6_qpf.QuadPart) {
        double ms = 1000.0 / fm6_qpf.QuadPart;
        LONG fps = (LONG)(n * (double)fm6_qpf.QuadPart / (t1 - win0) + 0.5);
        LONG64 w = InterlockedExchange64(&fm6_wait_sum, 0);
        if (fps < last_fps - 3 || fps > last_fps + 3) {
            wlog("FM6: fps %ld (frame %.2f ms: Present %.2f ms, vline wait %.2f ms)", fps,
                 (t1 - win0) * ms / n, in_sum * ms / n, w * ms / n);
            last_fps = fps;
        }
        win0 = t1; n = 0; in_sum = 0;
    }
}

static uintptr_t fm6_present_hook(void *a, void *b, void *c, void *d)
{
    LARGE_INTEGER t0, t1;
    uintptr_t r;
    QueryPerformanceCounter(&t0);
    r = fm6_present_orig(a, b, c, d);
    QueryPerformanceCounter(&t1);
    fm6_fps_sample(t0.QuadPart, t1.QuadPart);
    return r;
}

static void fm6_fps_meter(uint8_t *game)
{
    void *p = (void *)fm6_present_hook, *w = (void *)fm6_vline_wait_hook;
    if (*(uint8_t **)(game + FM6_FRAME11_VTBL + 0x108) != game + FM6_FRAME_VLINE_WAIT ||
        !bytes_match(game + FM6_FRAME_VLINE_WAIT, "4053", 2)) {
        wlog("FM6: fps: slot 0x108 (vline) does not match, not measured");
        return;
    }
    fm6_present_orig = (fm6_vcall4)(game + FM6_FRAME_PRESENT);
    fm6_vline_wait_orig = (fm6_vcall4)(game + FM6_FRAME_VLINE_WAIT);
    write_mem(game + FM6_FRAME11_VTBL + 0x158, &p, 8);
    write_mem(game + FM6_FRAME11_VTBL + 0x108, &w, 8);
}

static void fm6_frame_rate(uint8_t *game, uint32_t fps)
{
    static const uint32_t vtbls[] = { FM6_FRAME_VTBL, FM6_FRAME11_VTBL };
    void *fn = (void *)fm5_frame_set_sync_stub;
    size_t i;
    if (!bytes_match(game + FM6_FRAME_SET_SYNC, "8991a0000000c3", 7) ||
        !bytes_match(game + FM6_FRAME_PRESENT + 0x1b, "8b89a000000085c90f84", 10) ||
        *(uint8_t **)(game + FM6_FRAME11_VTBL + 0x158) != game + FM6_FRAME_PRESENT) {
        wlog("FM6: fps: setter/Present do not match, ignored");
        return;
    }
    for (i = 0; i < 2; i++)
        if (*(uint8_t **)(game + vtbls[i] + 0x1a0) != game + FM6_FRAME_SET_SYNC) {
            wlog("FM6: fps: vtable %#x slot 0x1a0 does not match, ignored", vtbls[i]);
            return;
        }
    fps_game = game;
    fps_tag = "FM6";
    fps_queue_ret = 0;
    fps_force = fps >= 60;
    for (i = 0; i < 2; i++) write_mem(game + vtbls[i] + 0x1a0, &fn, 8);
    wlog("FM6: fps: frame_rate %u -> %s", fps, fps_force ? "present always with interval 1" : "original");
    if (fps_force && !hook_at(game + FM6_GET_FRAME_RATE, "40574883ec4048c7442420feffffff48895c2458", 20,
                              (void *)fm6_get_frame_rate, (void **)&fm6_get_frame_rate_orig))
        wlog("FM6: fps: FrameRate getter %#x does not match, 30 fps scenarios stay", FM6_GET_FRAME_RATE);
    if (fps > 60) fm6_frame_unlock(game, fps);
    fm6_fps_meter(game);
}

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

/* EXPERIMENTO D3D12Debug=5 (17_fm6.txt PASSO 24): o criterio 0x14013bae0 de title_storage (so chamado por
   CreatePlacementTexture2D/3D) escolhe recurso reservado (tiled) em vez de committed; com tiled tier 4 vale p/
   todo Texture2D/3D. Forcado a 0 (xor eax,eax ; ret) -> placement textures do FM6 viram committed, o mesmo
   caminho do Azimuth p/ arrays em GPUs com tier < 4. */
#define RVA_TITLE_STORAGE_RESERVED 0x13bae0

static void fm6_committed_placement_textures(void)
{
    static const uint8_t ret0[3] = { 0x31, 0xc0, 0xc3 };
    if (!bytes_match(g_base + RVA_TITLE_STORAGE_RESERVED, "48895c241055565748", 9)) {
        wlog("FM6: placement textures committed: site %#x does not match, ignored", RVA_TITLE_STORAGE_RESERVED);
        return;
    }
    write_mem(g_base + RVA_TITLE_STORAGE_RESERVED, ret0, 3);
    wlog("FM6: placement textures as committed resources (no reserved resource)");
}

/* EXPERIMENTO D3D12Debug=6 (17_fm6.txt PASSO 25): o destroy comum 0x1400da270 (chamado pela fila de Release
   apos o ticket do worker e pelo present_release) aposenta storage/descritores na fence da GPU. Pulado para
   recursos (5, 0xc, 0xd, 0x10), views (0xb) e 0x11 -> nada disso e destruido (vaza). Se o page fault sumir, a
   causa e uso de objeto ja solto pelo jogo (no console soltar um placement resource nao libera a memoria). */
#define RVA_GFX_DESTROY 0xda270
static void (*orig_gfx_destroy)(uint8_t *obj);
static volatile LONG leaked[0x12];

static int leak_type(uint32_t t)
{
    return t == 5 || t == 0xb || t == 0xc || t == 0xd || t == 0x10 || t == 0x11;
}

static void fm6_leak_destroy(uint8_t *obj)
{
    uint32_t t = *(uint32_t *)(obj + 0x38);
    LONG n;
    if (!leak_type(t)) { orig_gfx_destroy(obj); return; }
    n = InterlockedIncrement(&leaked[t]);
    if (!(n & (n - 1))) wlog("FM6: D3D12Debug=6: destroy skipped type %#x (%ld)", t, n);
}

static void fm6_leak_gfx_objects(void)
{
    if (!hook_function(RVA_GFX_DESTROY, "48895c241855565741564157488bec", 15, (void *)fm6_leak_destroy,
                       (void **)&orig_gfx_destroy)) {
        wlog("FM6: D3D12Debug=6: site %#x does not match, ignored", RVA_GFX_DESTROY);
        return;
    }
    wlog("FM6: D3D12Debug=6: resources/views never destroyed (they leak)");
}

/* 17_fm6.txt PASSO 47: o VirtualAlloc do Azimuth (0x1400aad30) so traduz as protecoes Xbox 0x10004/0x30004/0x20204
   de memoria grafica (MEM_GRAPHICS 0x10000000) para PAGE_READWRITE; o FM6 pede 0x40404 (game+0x5f0306 pool das
   texturas do mundo, game+0xe66322/0xe66492 heap grafico), que vai cru ao Windows, falha com 0x57 e volta NULL sem
   STOP -> o jogo cria as placement textures na base 0 (offsets puros, fora do rastreador = texturas zeradas).
   Traduzida para PAGE_READWRITE como as outras (write-combine descartado, igual ao 0x20204). */
#define XBOX_MEM_GRAPHICS 0x10000000u
#define XBOX_PROTECT_GPU_WC 0x40404u
static uint32_t fm6_graphics_protect(uint32_t type, uint32_t protect)
{
    return (type & XBOX_MEM_GRAPHICS) && protect == XBOX_PROTECT_GPU_WC ? PAGE_READWRITE : protect;
}

/* opcoes do azimuth_fm6.ini */
static void fm6_patches(uint8_t *game)
{
    /* PASSO 85: com tiled tier 4 o Azimuth faz de TODA placement texture 2D/3D do FM6 um recurso reservado (create +
       GetResourceTiling + UpdateTileMappings na fila de render; a destruicao desmapeia no kernel). O streaming do FM6
       cria/destroi centenas por segundo e a fila de paginacao (atualizacao de tabela de paginas) trava: a fila de
       render espera por ela, CreateReservedResource bloqueia ~1 s e o device cai (DEVICE_RESET sem lista em voo,
       sem page fault) no fim da 1a corrida. O caminho committed e o que o Azimuth usa com tier < 4.
       debug.ini PlacementCommitted=0 volta ao reservado; D3D12Debug=5 (antigo) tambem liga. */
    if (dbg_int(L"PlacementCommitted", 1) > 0 || dbg_int(L"D3D12Debug", 0) == 5) fm6_committed_placement_textures();
    if (dbg_int(L"D3D12Debug", 0) == 6) fm6_leak_gfx_objects();
    fm6_dash_font_fix(game);   /* PASSO 119: apostrofos do tempo em pt-BR nas fontes dos paineis */
    if (cfg6_int(L"SkipIntro", 0)) {
        static const uint8_t nop2[2] = { 0x66, 0x90 }, jmp8 = 0xeb;
        size_t i, n = sizeof fm6_splash_intro_cmps / sizeof *fm6_splash_intro_cmps;
        for (i = 0; i < n; i++)
            if (!bytes_match(game + fm6_splash_intro_cmps[i], "4138b424d730000074", 9)) break;
        if (i < n) wlog("FM6: SkipIntro: site %#x does not match, ignored", fm6_splash_intro_cmps[i]);
        else if (!bytes_match(game + FM6_LOGO_PARADE_CMP, "4438b8d730000075", 8))
            wlog("FM6: SkipIntro: site %#x does not match, ignored", FM6_LOGO_PARADE_CMP);
        else {
            for (i = 0; i < n; i++) write_mem(game + fm6_splash_intro_cmps[i] + 8, nop2, 2);
            write_mem(game + FM6_LOGO_PARADE_CMP + 7, &jmp8, 1);
            wlog("FM6: SkipIntro=1 (disablesplashintro forced: T10_MS_Combined.bk2 and logo parade skipped)");
        }
    }
    if (cfg6_int(L"SkipPresentationVideo", 0)) {
        static const uint8_t nop5[5] = { 0x0f, 0x1f, 0x44, 0x00, 0x00 }, nop2[2] = { 0x66, 0x90 };
        if (!bytes_match(game + FM6_INTRO_VIDEO_SET, "488d4b28e832d846ff", 9) ||
            !bytes_match(game + FM6_INTRO_VIDEO_PATH, "85c07519", 4))
            wlog("FM6: SkipPresentationVideo: site does not match, ignored");
        else {
            write_mem(game + FM6_INTRO_VIDEO_SET + 4, nop5, 5);
            write_mem(game + FM6_INTRO_VIDEO_PATH + 2, nop2, 2);
            wlog("FM6: SkipPresentationVideo=1 (career intro IntroVideo empty: IE_Blockbuster.bk2 skipped)");
        }
    }
    if (cfg6_int(L"UnlockDLC", 0)) {
        /* 0xa8ad80 -> mov eax,1 ; ret: todo produto conta como comprado (lambda 0xa40f40 marca node+0x29 e o
           IsPurchased; UI 0xbda77c/0xbe4404; expansoes Porsche/NASCAR 0xd04210/0xd04298). Sem inventario o
           refresh zera tudo via 0x43be7c(raiz, 0) -> mov dl,1. */
        static const uint8_t ret1[6] = { 0xb8, 1, 0, 0, 0, 0xc3 }, dl1[2] = { 0xb2, 0x01 };
        if (!bytes_match(game + FM6_OFFER_OWNED, "48895c2410565741564883ec20488db9", 16) ||
            !bytes_match(game + FM6_OFFER_NOINV - 7, "488dbba000000033d2488bcfe86a000000", 17))
            wlog("FM6: UnlockDLC: sites do not match, ignored");
        else {
            write_mem(game + FM6_OFFER_OWNED, ret1, sizeof ret1);
            write_mem(game + FM6_OFFER_NOINV, dl1, sizeof dl1);
            wlog("FM6: UnlockDLC=1 (all DLC offers count as purchased)");
        }
    }
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
            if (!logged++) wlog("XG: FM6 computer %p with unexpected vtable %p, not converted", (void *)obj, (void *)vt);
            return;
        }
        for (i = 0; i < XG_FH2_SLOTS; i++) xg_fm6_new[i] = vt[xg_fm6_slot[i]];
        xg_fm6_vtbl = vt;
        wlog("XG: FM6 computer vtable %p -> FH2 layout (12 -> 8 slots)", (void *)vt);
    }
    if (vt == xg_fm6_vtbl) *obj = xg_fm6_new;
}

/* XG texture computer do Azimuth (azimuth.c) com o FM6: computer do XG do FM6 na vtable do FH2 */
void fm6_xg_computer(void *out, const char *name, uint32_t *desc)
{
    xg_computer_orig(out, name, desc);
    if (*(void ***)out) xg_wrap_computer_fm6(*(void ***)out);
}

static void fm6_load_config(void)
{
    proxy_path(L"azimuth_fm6.ini", g_ini6);
    wlog("config: %ls (SkipIntro=%d SkipPresentationVideo=%d UnlockDLC=%d)", g_ini6, cfg6_int(L"SkipIntro", 0),
         cfg6_int(L"SkipPresentationVideo", 0), cfg6_int(L"UnlockDLC", 0));
}

/* pre-jogo: diagnostico do WASAPI do FM6 (audio.c; os hooks conferem is_fm6); se o vtable nao conferir, so loga */
static void fm6_early_install(void)
{
    audio_fm6_install();
}

static void fm6_apply_patches(uint8_t *game, const uint8_t *options)
{
    /* os sites do registro sao do FH2: TLS, heap da APU e batches */
    take_tls_template(game);
    implicit_apu_heap("FM6");
    fm6_batch_install(game);
    fm6_reexec_install();
    fm6_imm_srv_hold_install();
    az_valloc_install(fm6_graphics_protect);
    /* audio_fm6_game_hooks(game): desligado - suspeito de travar o carregamento (17_fm6.txt PASSO 53) */
    fm6_patches(game);
    /* diagnostico da plotagem (16_opcoes_graficas.txt PASSO 12) */
    if (dbg_int(L"LiveryLog", 0) > 0) fm6_livery_log_install(game);
    fm6_resolution(game, *(const uint32_t *)(options + 4), *(const uint32_t *)(options + 8));
    fm6_frame_rate(game, *(const uint32_t *)(options + 0xc));
    fm6_car_detail(game, ((const int32_t *)(options + 0x14))[2]);
    fm6_shadows(game, ((const int32_t *)(options + 0x14))[0]);
    fm6_reflections(game, ((const int32_t *)(options + 0x14))[1]);
    tls_slot0_sites(game, fm6_tls_slot0, FM6_TLS_SLOT0_N);
}

/* alpha-to-coverage sem o xadrez do driver (common/gpuguard.c; fm6 PASSO 112) */
static int fm6_a2c_no_dither(void) { return cfg6_int(L"AlphaToCoverageNoDither", 1) > 0; }

const game fm6_game = {
    .tag = "FM6",
    .record = fm6_rec,
    .build_record = fm6_build_record,
    .defaults = {
        .allocator_max_age_ms = -1,     /* PASSO 67 */
        .tile_no_hazard = 1,            /* PASSO 69 */
        .xma_start_fix = 1,             /* PASSO 114 */
        .defer_release = 1,             /* PASSO 70 */
        .defer_release_hold_ms = 2000,
        .list_pool = 1,                 /* PASSO 74 */
        .list_merge = 1,                /* PASSO 98 */
        .placement_placed = 1,          /* PASSO 97 */
        .trace_game_frames = 1,
    },
    .load_config = fm6_load_config,
    .early_install = fm6_early_install,
    .apply_patches = fm6_apply_patches,
    .resolve_import = fm6_resolve_import,
    .xg_computer = fm6_xg_computer,
    .shape_graph = fm6_shape_graph,
    .winrt_factory = fm6_winrt_factory,
    .winrt_table_call = fm6_winrt_table_call,
    .a2c_no_dither = fm6_a2c_no_dither,
    .note_pso = liv_note_pso,
    .note_draw_skip = liv_note_skip,
};
