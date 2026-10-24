/*
 * Memoria do Azimuth comum aos jogos: heap implicito da APU, VirtualAlloc do Azimuth (protecao por jogo) e a faixa
 * de VA fixa do alocador dos jogos (FM5 e FM6).
 */
#include <windows.h>
#include <stdint.h>
#include "../proxy.h"

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

void implicit_apu_heap(const char *game_name)
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
    wlog("APU: heap created for %s: cached %p (%#x bytes, APU %#x), uncached %p (%#x bytes, APU %#x)",
         game_name, *(void **)h, *(uint32_t *)(h + 8), *(uint32_t *)(h + 0xc),
         *(void **)(h + 0x20), *(uint32_t *)(h + 0x28), *(uint32_t *)(h + 0x2c));
}

/* VirtualAlloc do Azimuth (0x1400aad30): solta a faixa de VA fixa quando o jogo reserva nela e, por jogo, traduz
   protecoes que o Azimuth nao conhece (FM6: 0x40404, fm6/fm6.c) */
#define RVA_AZ_VIRTUAL_ALLOC 0xaad30
typedef void *(*az_valloc_fn)(void *addr, size_t size, uint32_t type, uint32_t protect);
static az_valloc_fn orig_az_valloc;


/* Quedas de usuario 2026-10-23 (17_fm6.txt): com o NvPresent64 do driver NVIDIA o nvcuda64.dll entra no processo
   junto do device D3D12 e reserva 0x200000000..0xf00000000 (VA unificado do CUDA). O alocador do FM6 (e o do FM5)
   reserva faixas em endereco fixo a partir de 0x800000000 (game+0x2c1c18): a reserva falha, a faixa fica com
   tamanho 0 e o pool dela (game+0x2c0c60) com 0 entradas escreve sem fim -> CRASHED em game+0x2c0cf0 logo apos o
   "settings:". O proxy segura a faixa desde o DllMain (antes das DLLs do driver) e a solta no 1o VirtualAlloc do
   jogo que cai nela (ou no apply_patches de outro jogo). */
#define FIXED_VA_BASE 0x800000000ull
#define FIXED_VA_SIZE 0x201000000ull   /* FM6: 5 faixas, 0x800000000..0xa00c00000 */
static void *volatile g_fixed_va;

void fixed_va_hold(void)
{
    g_fixed_va = VirtualAlloc((void *)(uintptr_t)FIXED_VA_BASE, FIXED_VA_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (g_fixed_va) wlog("fixed VA: %#llx..%#llx held for the game's allocator", FIXED_VA_BASE, FIXED_VA_BASE + FIXED_VA_SIZE);
    else wlog("fixed VA: %#llx already taken (error %lu)", FIXED_VA_BASE, GetLastError());
}

void fixed_va_release(const char *why)
{
    void *p = InterlockedExchangePointer((void *volatile *)&g_fixed_va, NULL);
    if (p) wlog("fixed VA: released (%s): %d", why, VirtualFree(p, 0, MEM_RELEASE));
}

static uint32_t (*g_valloc_protect)(uint32_t type, uint32_t protect);   /* do jogo; NULL = sem traducao */

static void *hook_az_valloc(void *addr, size_t size, uint32_t type, uint32_t protect)
{
    uint32_t p = g_valloc_protect ? g_valloc_protect(type, protect) : protect;
    void *r;
    if (g_fixed_va && addr && (uintptr_t)addr < FIXED_VA_BASE + FIXED_VA_SIZE && (uintptr_t)addr + size > FIXED_VA_BASE)
        fixed_va_release("the game reserves there");
    r = orig_az_valloc(addr, size, type, p);
    if (p != protect) {
        static volatile LONG n;
        LONG k = InterlockedIncrement(&n);
        if (k <= 8 || !(k & (k - 1)))
            wlog("graphics VirtualAlloc #%ld %p size=%#zx type=%#x protection %#x -> %#x: %p", k, addr, size, type,
                 protect, p, r);
    }
    return r;
}

int az_valloc_install(uint32_t (*protect)(uint32_t type, uint32_t protect))
{
    g_valloc_protect = protect;
    if (!hook_function(RVA_AZ_VIRTUAL_ALLOC, "40555356574154415541564157488d6c24e1", 18, (void *)hook_az_valloc,
                       (void **)&orig_az_valloc)) {
        wlog("Azimuth VirtualAlloc (%#x) does not match; %s", RVA_AZ_VIRTUAL_ALLOC,
             protect ? "protection not translated" : "no hook");
        fixed_va_release("no VirtualAlloc hook");
        return 0;
    }
    return 1;
}
