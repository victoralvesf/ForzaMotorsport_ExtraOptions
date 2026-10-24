#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <tlhelp32.h>
#include "../proxy.h"

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

void take_tls_template(uint8_t *game)
{
    IMAGE_TLS_DIRECTORY64 *t = tls_dir(game);
    uint8_t *start;
    size_t n, i;
    if (!t) return;
    start = (uint8_t *)(uintptr_t)t->StartAddressOfRawData;
    n = (size_t)(t->EndAddressOfRawData - t->StartAddressOfRawData);
    if (n > TLS_HELPER_SIZE) { wlog("TLS: template of %#zx bytes > helper block", n); return; }
    for (i = 0; i < n && !start[i]; i++) ;
    if (i == n) return;                              /* ja zerado: nada a fazer */
    memcpy(game_tls, start, n);
    game_tls_size = n;
    memset(start, 0, n);                             /* .tls do jogo e RW; so o loader le o template */
    game_tls_pending = 1;
    wlog("TLS: template of %#zx bytes saved and zeroed (1st non-zero byte at +%#zx)", n, i);
}

typedef LONG (WINAPI *nt_query_thread_fn)(HANDLE, int, void *, ULONG, ULONG *);
typedef struct { LONG status; PVOID teb; HANDLE pid, tid; ULONG_PTR affinity; LONG prio, base_prio; } THREAD_BASIC_INFO;


/* ------------------------------------------------------------------------ */
/* TLS no modelo "exe" do XDK: o FM6 (fm6/fm6.c, lista dos sites) le TEB.ThreadLocalStoragePointer[0] direto (no console o titulo */
/* e o exe, indice 0), sem _tls_index; no PC o slot 0 e o bloco do azimuth.exe (epoca dos statics   */
/* do MSVC em +0x10) -> o jogo pisava nele e o Azimuth pulava inits (WindowsDeleteString na init do */
/* CRT). Cada "mov T, gs:[0x58]" desses sites (tools/tls_slot0.py) vira "call stub_T; nop4"; o stub */
/* faz T = &ThreadLocalStoragePointer[indice do azimuth-tls.dll], entao [T] = bloco do jogo. Sem     */
/* efeito em flags; os 72 sites que usam _tls_index ja caem nesse bloco.                             */
/* ------------------------------------------------------------------------ */
static uint8_t *slot0_game;
static const uint32_t *slot0_sites;
static size_t slot0_n;

/* sites "mov T, gs:[0x58]" do jogo; aplicados (com o template) no 1o import resolvido */
void tls_slot0_sites(uint8_t *game, const uint32_t *sites, size_t n)
{
    slot0_game = game; slot0_sites = sites; slot0_n = n;
    game_tls_pending = 1;
}

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


static void tls_slot0_redirect(DWORD index)
{
    uint8_t *stubs, *stub[16] = { 0 };
    size_t i, used = 0;
    if (!slot0_n) return;
    for (i = 0; i < slot0_n; i++)
        if (slot0_reg(slot0_game + slot0_sites[i]) < 0) {
            wlog("TLS: slot 0 site at game+%#x does not match; none changed", slot0_sites[i]);
            return;
        }
    if (!(stubs = alloc_near(slot0_game, 16 * 32))) { wlog("TLS: no memory near the game for the stubs"); return; }
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
    wlog("TLS: %zu reads of the game's slot 0 -> index %lu (%zu stubs at %p)", slot0_n, index, used, (void *)stubs);
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
        wlog("TLS: azimuth-tls.dll does not match (module %p index %p); game template NOT applied", (void *)helper, (void *)index);
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
    wlog("TLS: game template in azimuth-tls.dll (index %lu) and in %d threads", *index, fixed);
}
