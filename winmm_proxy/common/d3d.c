/*
 * D3D11 do Azimuth comum aos jogos: resolvedor de imports (repassa ao jogo carregado), ponte do XAudio2Create e
 * diagnostico de draws pulados / busca de pipeline (FM5 e FM6).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../proxy.h"
#include "d3d.h"

#define RVA_RESOLVER       0x3a290   /* FARPROC resolve(ImportEntry*) : {std::string dll; std::string nome} */
#define RESOLVER_STOLEN    15        /* 3 x mov [rsp+..], reg */
#define RVA_RELEASE        0xda9e0   /* Release comum dos objetos do Azimuth */
#define RELEASE_HEX        "48895c241048896c2418488974242057"
#define RELEASE_STOLEN     16

/* std::string do MSVC (x64): buf/ptr em +0, tamanho em +0x10, capacidade em +0x18 */
const char *msvc_str(const uint8_t *s, size_t *len)
{
    *len = *(const size_t *)(s + 0x10);
    return *(const size_t *)(s + 0x18) > 15 ? *(const char *const *)s : (const char *)s;
}

int str_is(const uint8_t *s, const char *want)
{
    size_t n, w = strlen(want);
    const char *p = msvc_str(s, &n);
    return n == w && memcmp(p, want, n) == 0;
}

/* ------------------------------------------------------------------------ */
/* XAudio2Create                                                              */
/* ------------------------------------------------------------------------ */
/* O FM5 importa XAudio2_9.dll ordinal 1 = XAudio2Create(ppXAudio2, Flags, XAudio2Processor). O FH2 nao importa:
 * faz LoadLibraryExW("xaudio2_9.dll") + GetProcAddress("CreateXAudio2Object") (HLE 0x14009a0c0 / 0x14009a450)
 * -> 0x140181b10(pp, flags, processor, x): exige flags & ~0x400 == 0, processor == -1 e x == 0 ("alternate
 * profile"); ignora o processador (XAudio2Create do desktop com (0, 0)) e embrulha no Engine do Azimuth. */
#define RVA_XA2_GUARD      0x38741c  /* guard do static "HMODULE do xaudio2_9.dll" (_Init_thread_header) */
#define RVA_XA2_HMOD       0x387420
typedef long (*cxa2_fn)(void **pp, UINT flags, UINT processor, uintptr_t x);

/* O static do 0x140181b10 so e inicializado se a epoca da thread ([TLS[0]+0x10], o exe assume _tls_index 0)
 * for MENOR que o guard (0 = nao inicializado; epocas comecam em INT_MIN). Na thread do jogo TLS[0] e o bloco
 * TLS do FM5 (VC11, sem statics thread-safe): +0x10 e variavel do jogo (>= 0) -> o init era pulado, HMODULE nulo
 * -> "CreateXAudio2Object requires the installed desktop xaudio2_9.dll". Aqui o static e preenchido e o guard
 * marcado como inicializado (INT_MIN: nenhuma epoca e menor). */
long WINAPI xaudio2_create_bridge(void **pp, UINT flags, UINT processor)
{
    volatile LONG *guard = (volatile LONG *)(g_base + RVA_XA2_GUARD);
    HMODULE *hmod = (HMODULE *)(g_base + RVA_XA2_HMOD);
    long hr;
    if (*guard == 0) {
        HMODULE m = LoadLibraryExW(L"xaudio2_9.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m && InterlockedCompareExchangePointer((void **)hmod, m, NULL) == NULL)
            InterlockedCompareExchange(guard, (LONG)0x80000000, 0);
        wlog("xaudio2: xaudio2_9.dll = %p (thread epoch %d)", m,
             *(int *)(*(uint8_t **)__readgsqword(0x58) + 0x10));
    }
    hr = ((cxa2_fn)(g_base + RVA_CREATE_XAUDIO2))(pp, flags & 0x400, 0xffffffff, 0);
    wlog("xaudio2: XAudio2Create(flags=%#x processor=%#x) hr=%#lx engine=%p", flags, processor, hr, pp ? *pp : NULL);
    return hr;
}

/* ------------------------------------------------------------------------ */
/* Resolvedor de imports                                                      */
/* ------------------------------------------------------------------------ */
typedef FARPROC (*resolver_fn)(const uint8_t *entry);
static resolver_fn orig_resolver;

/* imports sem entrada no HLE do Azimuth (o FH2 nao usa), com a mesma assinatura do Win32 -> DLL real:
   GlobalMemoryStatusEx = thread de liberacao de memoria do FM5 (game+0x683b30), ullAvailPhys;
   GetThreadId (game+0x563ce5) e DuplicateHandle = FM6; StringFromGUID2/GetLocalTime = Xbox.Services.dll do FM6. */
static const struct { const char *dll, *name, *real; } import_fwd[] = {
    { "kernelx.dll", "GlobalMemoryStatusEx", "kernel32.dll" },
    { "kernelx.dll", "GetThreadId", "kernel32.dll" },
    { "kernelx.dll", "DuplicateHandle", "kernel32.dll" },
    { "kernelx.dll", "GetLocalTime", "kernel32.dll" },
    { "combase.dll", "StringFromGUID2", "combase.dll" },
};

static FARPROC import_fallback(const uint8_t *entry)
{
    size_t i;
    for (i = 0; i < sizeof import_fwd / sizeof *import_fwd; i++)
        if (str_is(entry, import_fwd[i].dll) && str_is(entry + 0x20, import_fwd[i].name)) {
            HMODULE m = LoadLibraryA(import_fwd[i].real);
            wlog("%s!%s -> %s", import_fwd[i].dll, import_fwd[i].name, import_fwd[i].real);
            return m ? GetProcAddress(m, import_fwd[i].name) : NULL;
        }
    return NULL;
}

/* fim de todo resolve_import: import sem HLE -> DLL real (import_fwd) ou log (thunk fatal se chamado) */
FARPROC import_finish(const uint8_t *entry, FARPROC p)
{
    if (!p) p = import_fallback(entry);
    if (!p) {
        size_t nd, nn;
        const char *d = msvc_str(entry, &nd), *n = msvc_str(entry + 0x20, &nn);
        wlog("unresolved import (fatal thunk if called): %.*s!%.*s", (int)nd, d, (int)nn, n);
    }
    return p;
}

static FARPROC hook_resolver(const uint8_t *entry)
{
    const game *g = game_current();
    FARPROC p = orig_resolver(entry);
    game_tls_apply();   /* 1o import: azimuth-tls.dll ja carregado, jogo ainda nao rodou */
    if (!g || !g->resolve_import) return p;   /* FH2: exatamente o Azimuth */
    return g->resolve_import(entry, p);
}

/* ------------------------------------------------------------------------ */
/* Release comum dos objetos do Azimuth (0x1400da9e0)                         */
/* ------------------------------------------------------------------------ */
static ULONG (*orig_release)(void *obj);
void *az_release_tramp(void) { return (void *)orig_release; }

/* ao chegar a 0: placement textures rastreadas (stutter.c, PASSO 99) e os dados do jogo para o objeto */
static ULONG hook_release(void *obj)
{
    ULONG n = orig_release(obj);
    if (n == 0) {
        const game *g = game_current();
        pa_obj_gone(obj);
        if (g && g->object_released) g->object_released(obj, __builtin_return_address(0));
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/* Diagnostico: draws pulados (0x14012b780) e busca de pipeline (0x1401338e0)   */
/* ------------------------------------------------------------------------ */
#define RVA_DRAW_SKIP   0x12b780   /* draw descartado: 0x14012a6d0 nao achou pipeline */
#define RVA_PSO_LOOKUP  0x1338e0   /* (ctx, esperar) -> pipeline ou 0 */
static void (*orig_draw_skip)(uint8_t *ctx);
static void *(*orig_pso_lookup)(uint8_t *ctx, int wait);
volatile LONG skip_counts[64], pso_null_counts[64];

static void hook_draw_skip(uint8_t *ctx)
{
    const game *g = game_current();
    int i = g && g->context_index ? g->context_index(ctx) : -1;
    InterlockedIncrement(&skip_counts[i < 0 ? 63 : i]);
    if (g && g->note_draw_skip) g->note_draw_skip(i);
    orig_draw_skip(ctx);
}

static void *hook_pso_lookup(uint8_t *ctx, int wait)
{
    const game *g = game_current();
    void *p = orig_pso_lookup(ctx, wait);
    int i = g && g->context_index ? g->context_index(ctx) : -1;
    if (g && g->note_pso) g->note_pso(p, wait, i);
    if (!p) {
        LONG n = InterlockedIncrement(&pso_null_counts[i < 0 ? 63 : i]);
        if (n <= 5 || (n & (n - 1)) == 0)
            wlog("d3d: null pipeline #%ld on context c%d (wait=%d) vs=%p ps=%p", n, i, wait,
                 *(void **)(ctx + 0x40d0), *(void **)(ctx + 0x40f0));
    }
    return p;
}

void trace_skips(void)
{
    char line[512];
    int i, len = 0;
    for (i = 0; i < 64; i++)
        if (skip_counts[i] || pso_null_counts[i])
            len += _snprintf(line + len, sizeof line - len, " c%d: skipped=%ld null=%ld", i == 63 ? -1 : i,
                             skip_counts[i], pso_null_counts[i]);
    if (len) wlog("trace skipped draws (cumulative):%s", line);
}

int d3d_install(void)
{
    if (!hook_function(RVA_DRAW_SKIP, "80b9480c000000488b91400c0000", 14, (void *)hook_draw_skip,
                       (void **)&orig_draw_skip)) return 0;
    if (!hook_function(RVA_PSO_LOOKUP, "48895c241848897c24205541564157", 15, (void *)hook_pso_lookup,
                       (void **)&orig_pso_lookup)) return 0;
    if (!hook_function(RVA_RELEASE, RELEASE_HEX, RELEASE_STOLEN, (void *)hook_release,
                       (void **)&orig_release)) return 0;
    return hook_function(RVA_RESOLVER, "48895c2410488974241848897c2420", RESOLVER_STOLEN,
                         (void *)hook_resolver, (void **)&orig_resolver);
}

