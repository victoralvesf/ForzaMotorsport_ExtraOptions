/*
 * Diagnostico de stutter (debug.ini [Debug] StutterLog=1; 17_fm6.txt PASSO 57). So registra, nada muda.
 * O quadro e o intervalo entre dois IDXGISwapChain::Present do host (fabrica DXGI do azimuth.exe pela IAT). Quadro
 * acima de 25 ms e de 1,8x a media dos ultimos 64 -> "stutter: quadro #N" com o que aconteceu no intervalo:
 *  - tempo dentro do Present (vsync/GPU) x fora dele (producao do quadro);
 *  - PSOs criados (Create*PipelineState / Load*Pipeline da library) e tempo gasto;
 *  - recursos D3D12 criados (committed/placed/reserved/heaps) e tempo gasto;
 *  - ExecuteCommandLists (chamadas, listas, tempo);
 *  - esperas longas (>= 4 ms) do azimuth.exe (WaitFor*, SleepConditionVariableSRW, WaitOnAddress, Sleep*) que
 *    terminaram no intervalo, as maiores com thread e pilha (game+/azimuth+); "P" = thread do Present.
 * A cada 10 s um resumo (media, p99, maximo, picos) e a vazao (vz_poll: GPU ocupada, listas, draws, CPU por thread).
 * Amostrador de CPU/RIP nos quadros parados: ver st_sampler.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <d3d12.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <dxgi1_2.h>
#include <psapi.h>
#include "../proxy.h"
#include "d3d.h"


#define ST_AVG_N 64           /* media movel dos quadros */
#define ST_WAITS 512          /* anel de esperas longas */
#define ST_WAIT_MIN_US 4000
#define ST_SUM_FRAMES 4096    /* quadros guardados para o p99 do resumo */
#define ST_FRAMES_PER_WAIT 6

int st_on;
/* IIDs do DXGI aqui (o INITGUID do proxy so cobre o d3d12.h, common/debug12.c) */
static const GUID st_iid_swapchain1 = { 0x790a45f7, 0x0d42, 0x4876, { 0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa } };
static const GUID st_iid_factory2 = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static LARGE_INTEGER st_freq;

/* ---- logica pura (tests/t_stutter.c) ---- */

/* pico: quadro >= 25 ms e >= 1,8x a media (com media 0 = sem historico, nao e pico) */
int st_is_spike(double ms, double avg)
{
    return avg > 0 && ms >= 25.0 && ms >= avg * 1.8;
}

typedef struct { double sum; uint32_t n, i; float v[ST_AVG_N]; } st_avg;

void st_avg_add(st_avg *a, double ms)
{
    if (a->n == ST_AVG_N) a->sum -= a->v[a->i];
    else a->n++;
    a->v[a->i] = (float)ms;
    a->sum += (float)ms;
    a->i = (a->i + 1) % ST_AVG_N;
}

double st_avg_get(const st_avg *a) { return a->n ? a->sum / a->n : 0; }

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* percentil (0..100) de n valores; ordena o vetor */
double st_percentile(float *v, uint32_t n, double pct)
{
    uint32_t k;
    if (!n) return 0;
    qsort(v, n, sizeof *v, cmp_float);
    k = (uint32_t)(pct / 100.0 * (n - 1) + 0.5);
    return v[k < n ? k : n - 1];
}

/* esperas longas: anel; o pico lista as maiores que terminaram em (t0, t1] */
typedef struct { volatile LONG64 end; uint32_t us, tid; void *frames[ST_FRAMES_PER_WAIT]; uint8_t nframes, kind; } st_wait;
static st_wait waits[ST_WAITS];
static volatile LONG wait_seq;

void st_wait_add(LONG64 end, uint32_t us, uint32_t tid, uint8_t kind, void *const *frames, uint32_t nframes)
{
    st_wait *w = &waits[(uint32_t)InterlockedIncrement(&wait_seq) % ST_WAITS];
    uint32_t i;
    w->end = 0;                       /* entrada em escrita: fora da busca */
    w->us = us; w->tid = tid; w->kind = kind;
    for (i = 0; i < nframes && i < ST_FRAMES_PER_WAIT; i++) w->frames[i] = frames[i];
    w->nframes = (uint8_t)i;
    w->end = end;
}

/* indices das ate `max` maiores esperas com fim em (t0, t1], da maior para a menor; devolve quantas */
uint32_t st_wait_top(LONG64 t0, LONG64 t1, uint32_t *idx, uint32_t max)
{
    uint32_t n = 0, i, j;
    for (i = 0; i < ST_WAITS; i++) {
        LONG64 e = waits[i].end;
        if (e <= t0 || e > t1) continue;
        if (n < max) n++;
        else if (waits[idx[max - 1]].us >= waits[i].us) continue;
        for (j = n - 1; j > 0 && waits[idx[j - 1]].us < waits[i].us; j--) idx[j] = idx[j - 1];
        idx[j] = i;
    }
    return n;
}

void st_wait_reset(void) { memset(waits, 0, sizeof waits); wait_seq = 0; }

/* ---- contadores do intervalo atual (zerados a cada Present) ---- */
enum { ST_PSO, ST_RES, ST_ECL, ST_NCOUNT };
static volatile LONG st_n[ST_NCOUNT], st_lists;
static volatile LONG64 st_ticks[ST_NCOUNT], st_bytes;

static void st_count(int which, LONG64 ticks) { InterlockedIncrement(&st_n[which]); InterlockedAdd64(&st_ticks[which], ticks); }
static LONG64 qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double ms_of(LONG64 ticks) { return ticks * 1000.0 / st_freq.QuadPart; }

/* ---- vazao (PASSO 71): janela de 10 s do amostrador (linha "stutter: vazao 10 s") - quadros, tempo no Present e no
 * limitador da fila, GPU ocupada por tipo de fila, ECL/listas, listas abertas (Reset/criacao, origem amostrada 1 em
 * VZ_EVERY), draws e CPU por thread. Contadores zerados pelo amostrador (InterlockedExchange). ---- */
#define VZ_EVERY 8
#define VZ_ORIGINS 16
#define VZ_KEY 3
#define VZ_TOP_THREADS 8
static volatile LONG vz_frames, vz_ecl, vz_lists, vz_lists_max, vz_opens, vz_draws;
static volatile LONG64 vz_frame_ticks, vz_present_ticks, vz_ecl_ticks, vz_lim_ticks;
static LONG64 vz_gpu[4];                         /* so o amostrador (sg_poll) */
static struct { uintptr_t k[VZ_KEY]; LONG n; } vz_orig[VZ_ORIGINS];
static LONG vz_orig_other;
static SRWLOCK vz_lock = SRWLOCK_INIT;

static void st_label(char *out, void *p)
{
    uint8_t *a = p, *game = g_game;
    if (game && a >= game && a < game + 0x8000000) wsprintfA(out, " game+%#x", (unsigned)(a - game));
    else if (a >= g_base && a < g_base + 0x1000000) wsprintfA(out, " azimuth+%#x", (unsigned)(a - g_base));
    else out[0] = 0;
}

/* ---- esperas do azimuth.exe (IAT) ---- */
static const char *const wait_names[] = { "WaitForSingleObject", "WaitForSingleObjectEx", "WaitForMultipleObjects",
                                          "SleepConditionVariableSRW", "WaitOnAddress", "Sleep", "SleepEx" };
static const uint32_t wait_iat[] = { 0x26f2b8, 0x26f600, 0x26f5b8, 0x26f5f8, 0x26f810, 0x26f320, 0x26f518 };
static void *wait_real[7];

static void wait_done(int kind, LONG64 t0)
{
    LONG64 t1 = qpc();
    uint32_t us = (uint32_t)((t1 - t0) * 1000000 / st_freq.QuadPart);
    void *f[24], *keep[ST_FRAMES_PER_WAIT];
    uint32_t n, i, k = 0;
    uint8_t *game = g_game;
    if (us < ST_WAIT_MIN_US) return;
    n = RtlCaptureStackBackTrace(2, 24, f, NULL);
    for (i = 0; i < n && k < ST_FRAMES_PER_WAIT; i++) {
        uint8_t *a = f[i];
        if ((game && a >= game && a < game + 0x8000000) || (a >= g_base && a < g_base + 0x1000000)) keep[k++] = a;
    }
    st_wait_add(t1, us, GetCurrentThreadId(), (uint8_t)kind, keep, k);
}

static DWORD WINAPI st_WFSO(HANDLE h, DWORD ms)
{ LONG64 t = qpc(); DWORD r = ((DWORD (WINAPI *)(HANDLE, DWORD))wait_real[0])(h, ms); wait_done(0, t); return r; }
static DWORD WINAPI st_WFSOEx(HANDLE h, DWORD ms, BOOL al)
{ LONG64 t = qpc(); DWORD r = ((DWORD (WINAPI *)(HANDLE, DWORD, BOOL))wait_real[1])(h, ms, al); wait_done(1, t); return r; }
static DWORD WINAPI st_WFMO(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{ LONG64 t = qpc(); DWORD r = ((DWORD (WINAPI *)(DWORD, const HANDLE *, BOOL, DWORD))wait_real[2])(n, h, all, ms); wait_done(2, t); return r; }
static BOOL WINAPI st_SCVSRW(PCONDITION_VARIABLE cv, PSRWLOCK l, DWORD ms, ULONG fl)
{ LONG64 t = qpc(); BOOL r = ((BOOL (WINAPI *)(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG))wait_real[3])(cv, l, ms, fl); wait_done(3, t); return r; }
static BOOL WINAPI st_WOA(volatile void *a, PVOID c, SIZE_T sz, DWORD ms)
{ LONG64 t = qpc(); BOOL r = ((BOOL (WINAPI *)(volatile void *, PVOID, SIZE_T, DWORD))wait_real[4])(a, c, sz, ms); wait_done(4, t); return r; }
static void WINAPI st_Sleep(DWORD ms)
{ LONG64 t = qpc(); ((void (WINAPI *)(DWORD))wait_real[5])(ms); wait_done(5, t); }
static DWORD WINAPI st_SleepEx(DWORD ms, BOOL al)
{ LONG64 t = qpc(); DWORD r = ((DWORD (WINAPI *)(DWORD, BOOL))wait_real[6])(ms, al); wait_done(6, t); return r; }

static void *const wait_hooks[] = { (void *)st_WFSO, (void *)st_WFSOEx, (void *)st_WFMO, (void *)st_SCVSRW, (void *)st_WOA,
                                    (void *)st_Sleep, (void *)st_SleepEx };

/* slot da IAT: so troca se apontar para a funcao exportada pelo kernel32/kernelbase (confere antes de alterar) */
static int iat_swap(uint32_t rva, const char *name, void *hook, void **orig)
{
    void **slot = (void **)(g_base + rva);
    void *k32 = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    void *kb = (void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), name);
    DWORD old;
    if (!*slot || (*slot != k32 && *slot != kb)) {
        wlog("stutter: IAT %#x (%s) does not match (%p; kernel32 %p, kernelbase %p) -> not hooked", rva, name, *slot, k32, kb);
        return 0;
    }
    *orig = *slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return 1;
}

static void vt_swap(void **slot, void *hook, void **orig)
{
    DWORD old;
    if (*slot == hook) return;
    *orig = *slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

/* ---- quadro (Present do host) ---- */
static volatile LONG64 frame_start;
static DWORD present_tid;
static st_avg avg;
static float sum_frames[ST_SUM_FRAMES];
static uint32_t sum_n, sum_spikes;
static LONG64 sum_t0;
static volatile LONG frame_no, spikes;
static float sum_max;

/* ---- recursos por tipo (PASSO 64): criacao (committed/placed/reserved/heap), UpdateTileMappings da fila e
 * destruicao (Release de ID3D12Resource/ID3D12Heap que leva >= 0,2 ms); a mais lenta de cada tipo com descricao. ---- */
enum { RK_COMMITTED, RK_PLACED, RK_RESERVED, RK_HEAP, RK_TILES, RK_RELEASE, RK_N };
typedef struct { LONG64 ticks; DWORD tid; uint32_t dim, w, h, depth, mips, fmt, flags; uint64_t size; } rk_max;
static volatile LONG rk_n[RK_N];
static volatile LONG64 rk_ticks[RK_N];
static rk_max rk_top[RK_N];
static SRWLOCK rk_lock = SRWLOCK_INIT;

uint64_t st_res_bytes(const D3D12_RESOURCE_DESC *d);
void tl_reset(void);
int tl_format(char *buf, int cap);

void rk_note(int k, LONG64 ticks, const D3D12_RESOURCE_DESC *rd, uint64_t size, uint32_t flags)
{
    InterlockedIncrement(&rk_n[k]);
    InterlockedAdd64(&rk_ticks[k], ticks);
    if (ticks <= rk_top[k].ticks) return;
    AcquireSRWLockExclusive(&rk_lock);
    if (ticks > rk_top[k].ticks) {
        rk_max *m = &rk_top[k];
        memset(m, 0, sizeof *m);
        m->ticks = ticks;
        m->tid = GetCurrentThreadId();
        m->size = size;
        m->flags = flags;
        if (rd) {
            m->dim = rd->Dimension; m->w = (uint32_t)rd->Width; m->h = rd->Height; m->depth = rd->DepthOrArraySize;
            m->mips = rd->MipLevels; m->fmt = rd->Format; m->flags = rd->Flags;
            if (!size) m->size = st_res_bytes(rd);
        }
    }
    ReleaseSRWLockExclusive(&rk_lock);
}

/* " | committed 3 12.0 ms (max 9.1 ms tex2d 2048x2048x1 m12 f71 fl0 16384 KB tid 123), ..." so tipos com chamadas */
int rk_format(char *buf, int cap)
{
    static const char *const kn[RK_N] = { "committed", "placed", "reserved", "heap", "tiles", "destroy" };
    static const char *const dn[] = { "?", "buf", "tex1d", "tex2d", "tex3d" };
    int len = 0, k, any = 0;
    for (k = 0; k < RK_N && len < cap - 200; k++) {
        rk_max *m = &rk_top[k];
        if (!rk_n[k]) continue;
        len += sprintf(buf + len, "%s %s %ld %.1f ms (max %.1f ms", any ? "," : " |", kn[k], rk_n[k], ms_of(rk_ticks[k]), ms_of(m->ticks));
        if (k == RK_HEAP) len += sprintf(buf + len, " %llu KB fl%#x", (unsigned long long)(m->size >> 10), m->flags);
        else if (k == RK_TILES) len += sprintf(buf + len, " %u regions", m->flags);
        else if (k < RK_HEAP)
            len += sprintf(buf + len, " %s %ux%ux%u m%u f%u fl%#x %llu KB", dn[m->dim <= 4 ? m->dim : 0], m->w, m->h, m->depth, m->mips,
                           m->fmt, m->flags, (unsigned long long)(m->size >> 10));
        len += sprintf(buf + len, " tid %lu)", m->tid);
        any = 1;
    }
    return len;
}

void rk_reset(void)
{
    int k;
    for (k = 0; k < RK_N; k++) { rk_n[k] = 0; rk_ticks[k] = 0; rk_top[k].ticks = 0; }
}

/* ---- caixa-preta do streaming (PASSO 76, so com StutterLog): a queda do fim da corrida do FM6 (0x887a0007 sem page
 * fault, DRED sem nenhuma lista, sempre no mesmo ponto) nao vem de comando de command list -> anel dos ultimos
 * eventos fora das listas: criacao de reservado/heap/committed/placed, ultima Release do reaper (adiada) e a
 * destruicao de fato, UpdateTileMappings. Cada mapeamento e conferido: recurso ou heap ja solto pelo reaper (ou
 * destruido), faixa do heap alem do tamanho. cx_dump (dred_watch, D3D12Debug>=3) grava o anel quando o device cai. */
enum { CX_RESERVED, CX_COMMITTED, CX_PLACED, CX_HEAP, CX_DEFER, CX_DESTROY, CX_TILES, CX_BAD, CX_REEXEC, CX_N };
static const char *const cx_names[CX_N] = { "reserved", "committed", "placed", "heap", "reaper release (deferred)",
                                            "destroyed", "UpdateTileMappings", "SUSPICIOUS MAPPING",
                                            "list re-executed (previous fence, completed, submitted)" };
typedef struct { LONG64 t; uint32_t kind, tid; void *a, *b; uint32_t x[8]; } cx_ev;
#define CX_RING 1024
static cx_ev cx_ring[CX_RING];
static volatile LONG cx_seq;

void cx_add(uint32_t kind, void *a, void *b, const uint32_t *x, int nx)
{
    LONG i = InterlockedIncrement(&cx_seq) - 1;
    cx_ev *e = &cx_ring[i % CX_RING];
    int k;
    e->t = qpc(); e->kind = kind; e->tid = GetCurrentThreadId(); e->a = a; e->b = b;
    for (k = 0; k < 8; k++) e->x[k] = k < nx ? x[k] : 0;
}

/* objetos vistos: estado (1 = vivo, 2 = ultima referencia com o reaper/fila, 3 = destruido) e, no heap, o tamanho em
   tiles de 64 KB. Tabela aberta; criacao no mesmo endereco volta a "vivo". */
#define CX_OBJS 65536
static struct { void *p; uint32_t state, tiles; } cx_obj[CX_OBJS];
static SRWLOCK cx_lock = SRWLOCK_INIT;
static volatile LONG cx_bad;

static uint32_t cx_slot(void *p)
{
    uint32_t s = (uint32_t)(((uintptr_t)p >> 4) * 2654435761u) % CX_OBJS, k;
    for (k = 0; k < CX_OBJS; k++, s = (s + 1) % CX_OBJS)
        if (cx_obj[s].p == p || !cx_obj[s].p) return s;
    return UINT32_MAX;
}

void cx_set(void *p, uint32_t state, uint32_t tiles)
{
    uint32_t s;
    if (!p) return;
    AcquireSRWLockExclusive(&cx_lock);
    if ((s = cx_slot(p)) != UINT32_MAX) {
        cx_obj[s].p = p;
        cx_obj[s].state = state;
        if (state == 1) cx_obj[s].tiles = tiles;
    }
    ReleaseSRWLockExclusive(&cx_lock);
}

/* 0 = desconhecido; *tiles = tamanho do heap em tiles (0 se nao e heap) */
uint32_t cx_state(void *p, uint32_t *tiles)
{
    uint32_t s, st = 0;
    if (tiles) *tiles = 0;
    if (!p) return 0;
    AcquireSRWLockShared(&cx_lock);
    if ((s = cx_slot(p)) != UINT32_MAX && cx_obj[s].p == p) { st = cx_obj[s].state; if (tiles) *tiles = cx_obj[s].tiles; }
    ReleaseSRWLockShared(&cx_lock);
    return st;
}

/* confere um UpdateTileMappings; devolve bits: 1 recurso solto/destruido, 2 heap solto/destruido, 4 faixa alem do heap */
uint32_t cx_check_tiles(void *res, void *heap, UINT ng, const D3D12_TILE_RANGE_FLAGS *rf, const UINT *hs, const UINT *rc,
                        uint32_t *first_bad)
{
    uint32_t bad = 0, size = 0, i;
    if (cx_state(res, NULL) >= 2) bad |= 1;
    if (heap && cx_state(heap, &size) >= 2) bad |= 2;
    for (i = 0; heap && size && hs && i < ng; i++) {
        UINT n = rc ? rc[i] : 1;
        if (rf && (rf[i] & (D3D12_TILE_RANGE_FLAG_NULL | D3D12_TILE_RANGE_FLAG_SKIP))) continue;
        if ((uint64_t)hs[i] + n > size) { if (!(bad & 4) && first_bad) *first_bad = i; bad |= 4; }
    }
    return bad;
}

static void cx_tiles(void *res, UINT nr, const D3D12_TILED_RESOURCE_COORDINATE *co, const D3D12_TILE_REGION_SIZE *sz,
                     void *heap, UINT ng, const D3D12_TILE_RANGE_FLAGS *rf, const UINT *hs, const UINT *rc, UINT fl)
{
    uint32_t x[8] = { nr, ng, fl, co ? co[0].X : 0, co ? co[0].Subresource : 0, sz ? sz[0].NumTiles : 0,
                      hs && ng ? hs[0] : 0, rc && ng ? rc[0] : 0 };
    uint32_t fb = 0, bad, size = 0;
    cx_add(CX_TILES, res, heap, x, 8);
    if (!(bad = cx_check_tiles(res, heap, ng, rf, hs, rc, &fb))) return;
    cx_state(heap, &size);
    {
        uint32_t y[5] = { bad, fb, hs && fb < ng ? hs[fb] : 0, rc && fb < ng ? rc[fb] : 1, size };
        LONG k = InterlockedIncrement(&cx_bad);
        cx_add(CX_BAD, res, heap, y, 5);
        if (k <= 32 || !(k & (k - 1)))
            wlog("stutter: black box: suspicious UpdateTileMappings #%ld: resource %p%s heap %p%s, range %u = tiles %u+%u of %u",
                 k, res, bad & 1 ? " (ALREADY RELEASED)" : "", heap, bad & 2 ? " (ALREADY RELEASED)" : "", y[1], y[2], y[3], size);
    }
}

void cx_dump(void)
{
    LONG n = cx_seq, i, first = n > CX_RING ? n - CX_RING : 0;
    LONG64 now = qpc();
    wlog("stutter: black box: %ld events, %ld suspicious mappings; last %ld (ms before the crash):", n, cx_bad, n - first);
    for (i = first; i < n; i++) {
        cx_ev *e = &cx_ring[i % CX_RING];
        wlog("stutter: cx %.1f ms tid %lu %s %p %p [%u %u %u %u %u %u %u %u]", (double)(now - e->t) * 1000.0 / st_freq.QuadPart,
             e->tid, e->kind < CX_N ? cx_names[e->kind] : "?", e->a, e->b, e->x[0], e->x[1], e->x[2], e->x[3], e->x[4],
             e->x[5], e->x[6], e->x[7]);
    }
}

/* criacao: descricao resumida (dim, largura, altura, profundidade/array, mips, formato, flags) */
static void cx_created(uint32_t kind, void *obj, const D3D12_RESOURCE_DESC *rd, uint64_t heap_size)
{
    uint32_t x[8] = { 0 };
    if (!obj) return;
    if (rd) {
        x[0] = rd->Dimension; x[1] = (uint32_t)rd->Width; x[2] = rd->Height; x[3] = rd->DepthOrArraySize;
        x[4] = rd->MipLevels; x[5] = rd->Format; x[6] = rd->Flags; x[7] = (uint32_t)(rd->Width >> 32);
    } else {
        x[0] = (uint32_t)(heap_size >> 16); x[1] = (uint32_t)heap_size;
    }
    cx_set(obj, 1, kind == CX_HEAP ? (uint32_t)(heap_size >> 16) : 0);
    cx_add(kind, obj, NULL, x, 8);
}

/* Release das vtables de recurso/heap (uma entrada por vtable: o slot original de cada uma) */
#define RK_VT 12
static struct { void **vt; ULONG (STDMETHODCALLTYPE *orig)(IUnknown *); } rk_vts[RK_VT];
static volatile LONG rk_nvt;

/* DeferRelease (PASSO 65; padrao do FM6 desde o PASSO 70, dr_wanted): a ULTIMA referencia que a thread 'Azimuth
 * reaper' solta fica numa fila e uma thread propria solta aos poucos (<= DR_BUDGET_US a cada ~2 ms) - sem a
 * destruicao em rajada (ate 318 recursos reservados num quadro, PASSO 64). */
#define DR_QUEUE 16384
#define DR_BUDGET_US 500
int dr_on;
int dr_hold_ms;                          /* PASSO 77: idade minima na fila antes de soltar (FM6 2000; DeferReleaseHoldMs) */
static IUnknown *dr_q[DR_QUEUE];
static LONG64 dr_t[DR_QUEUE];            /* qpc da entrada na fila */
static volatile LONG dr_head, dr_tail, dr_deferred, dr_released, dr_full;
static __thread int dr_is_reaper = -1;

static int dr_thread_is_reaper(void)
{
    typedef HRESULT (WINAPI *gtd_fn2)(HANDLE, PWSTR *);
    static gtd_fn2 gtd;
    PWSTR desc = NULL;
    if (dr_is_reaper >= 0) return dr_is_reaper;
    if (!gtd) gtd = (gtd_fn2)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    dr_is_reaper = 0;
    if (gtd && SUCCEEDED(gtd(GetCurrentThread(), &desc)) && desc) {
        dr_is_reaper = wcscmp(desc, L"Azimuth reaper") == 0;
        LocalFree(desc);
    }
    return dr_is_reaper;
}

/* devolve 1 se guardou o objeto (a referencia do chamador passa a ser da fila) */
static int dr_push(IUnknown *u)
{
    LONG h = dr_head;
    if (h - dr_tail >= DR_QUEUE) { InterlockedIncrement(&dr_full); return 0; }
    dr_q[h % DR_QUEUE] = u;
    dr_t[h % DR_QUEUE] = qpc();
    InterlockedExchange(&dr_head, h + 1);    /* um so produtor (o reaper) */
    InterlockedIncrement(&dr_deferred);
    return 1;
}

static ULONG STDMETHODCALLTYPE rk_release(IUnknown *u);
static ULONG rk_call_orig(IUnknown *u);
static void mr_del(const void *o);
static void pa_free(const void *res);
static int pa_format(char *out);
static void pa_live_report(const char *when);

/* entrou na fila em `t` e ja tem `hold_ms` (fila em ordem: o 1o novo demais para o resto) */
int dr_old_enough(LONG64 t, LONG64 now, int hold_ms, LONG64 freq)
{
    return hold_ms <= 0 || (now - t) * 1000 >= (LONG64)hold_ms * freq;
}

/* caixa-preta: o que o reaper solta (recurso: dim, largura, altura, prof., mips, fmt, flags; heap: tiles, bytes) */
static void cx_deferred(IUnknown *u)
{
    ID3D12Resource *r = NULL;
    ID3D12Heap *hp = NULL;
    uint32_t x[8] = { 0 };
    if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12Resource, (void **)&r))) {
        D3D12_RESOURCE_DESC rd;
        r->lpVtbl->GetDesc(r, &rd);
        x[0] = rd.Dimension; x[1] = (uint32_t)rd.Width; x[2] = rd.Height; x[3] = rd.DepthOrArraySize;
        x[4] = rd.MipLevels; x[5] = rd.Format; x[6] = rd.Flags; x[7] = (uint32_t)(rd.Width >> 32);
        r->lpVtbl->Release(r);
    } else if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12Heap, (void **)&hp))) {
        D3D12_HEAP_DESC hd;
        hp->lpVtbl->GetDesc(hp, &hd);
        x[0] = (uint32_t)(hd.SizeInBytes >> 16); x[1] = (uint32_t)hd.SizeInBytes; x[2] = 0xffffffffu;
        hp->lpVtbl->Release(hp);
    }
    cx_set(u, 2, 0);
    cx_add(CX_DEFER, u, NULL, x, 8);
}

static DWORD WINAPI dr_drain(void *arg)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    (void)arg;
    for (;;) {
        LARGE_INTEGER due;
        LONG64 t0;
        due.QuadPart = -20000;
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(2);
        t0 = qpc();
        while (dr_tail != dr_head && (qpc() - t0) * 1000000 < (LONG64)DR_BUDGET_US * st_freq.QuadPart) {
            IUnknown *u = dr_q[dr_tail % DR_QUEUE];
            if (!dr_old_enough(dr_t[dr_tail % DR_QUEUE], qpc(), dr_hold_ms, st_freq.QuadPart)) break;
            if (st_on) { cx_set(u, 3, 0); cx_add(CX_DESTROY, u, NULL, NULL, 0); }
            rk_call_orig(u);
            InterlockedIncrement(&dr_tail);
            InterlockedIncrement(&dr_released);
        }
    }
    return 0;
}

static ULONG STDMETHODCALLTYPE rk_release(IUnknown *u)
{
    void **vt = *(void ***)u;
    LONG i, n = rk_nvt;
    for (i = 0; i < n; i++)
        if (rk_vts[i].vt == vt) {
            LONG64 t, d;
            ULONG r;
            if (dr_on && dr_thread_is_reaper()) {
                ULONG c = u->lpVtbl->AddRef(u);       /* sonda: 2 = a referencia do reaper e a ultima */
                if (c == 2 && dr_push(u)) {
                    if (st_on) cx_deferred(u);
                    rk_vts[i].orig(u);
                    return 0;
                }
                rk_vts[i].orig(u);
            }
            t = qpc();
            r = rk_vts[i].orig(u);
            if (!r) { mr_del(u); pa_free(u); }
            d = qpc() - t;
            if (d * 5000 >= st_freq.QuadPart) rk_note(RK_RELEASE, d, NULL, 0, 0);   /* >= 0,2 ms */
            return r;
        }
    return 0;   /* nao acontece: so vtables registradas apontam para ca */
}

static ULONG rk_call_orig(IUnknown *u)
{
    void **vt = *(void ***)u;
    LONG i, n = rk_nvt;
    for (i = 0; i < n; i++)
        if (rk_vts[i].vt == vt) {
            ULONG r = rk_vts[i].orig(u);
            if (!r) { mr_del(u); pa_free(u); }
            return r;
        }
    return 0;
}

static SRWLOCK rk_vt_lock = SRWLOCK_INIT;
static void rk_hook_release(void *obj)
{
    void **vt;
    LONG i;
    DWORD old;
    if (!obj) return;
    vt = *(void ***)obj;
    if (vt[2] == (void *)rk_release) return;
    AcquireSRWLockExclusive(&rk_vt_lock);
    for (i = 0; i < rk_nvt && rk_vts[i].vt != vt; i++) {}
    if (i == rk_nvt && i < RK_VT && vt[2] != (void *)rk_release) {
        rk_vts[i].vt = vt;
        rk_vts[i].orig = (ULONG (STDMETHODCALLTYPE *)(IUnknown *))vt[2];
        rk_nvt = i + 1;
        VirtualProtect(&vt[2], sizeof vt[2], PAGE_READWRITE, &old);
        vt[2] = (void *)rk_release;
        VirtualProtect(&vt[2], sizeof vt[2], old, &old);
    }
    ReleaseSRWLockExclusive(&rk_vt_lock);
}

/* ---- GPU por envio (PASSO 63): Signal das filas -> anel; o amostrador consulta GetCompletedValue a cada ~2 ms.
 * Trecho de GPU = conclusao - max(Signal, conclusao anterior na mesma fila) ~ tempo que a GPU levou nesse trecho. */
#define SG_RING 2048
#define SG_QUEUES 8
typedef struct { ID3D12Fence *f; UINT64 v; LONG64 t_sig; uint8_t qt; volatile LONG state; } st_sig;   /* state: 0 livre, 1 escrevendo, 2 pendente */
static st_sig sigs[SG_RING];
static volatile LONG sig_seq, sig_dropped, sig_waits;
static uint32_t sig_tail;                        /* so o amostrador */
static LONG64 sig_last_done[4];                  /* por tipo de fila (0 direct, 1 bundle, 2 compute, 3 copy) */
static volatile LONG64 gpu_max_ticks;            /* maior trecho do intervalo (zerado no Present) */
static volatile LONG64 gpu_max_val, gpu_sum_ticks;
static volatile LONG gpu_max_qt, gpu_done, gpu_pend;
/* lf/lsent: fence do proxy sinalizado depois de cada ExecuteCommandLists da fila (pool de command lists, lp_ecl) */
typedef struct { ID3D12CommandQueue *q; uint8_t type; ID3D12Fence *lf; volatile UINT64 lsent; SRWLOCK llock; void *rq; } sg_queue;
static sg_queue sg_queues[SG_QUEUES];
static volatile LONG sg_nqueues;

/* duracao de um trecho de GPU: desde o Signal ou desde a conclusao anterior na fila, o que vier depois */
LONG64 st_gpu_segment(LONG64 t_sig, LONG64 prev_done, LONG64 t_done)
{
    LONG64 start = t_sig > prev_done ? t_sig : prev_done;
    return t_done > start ? t_done - start : 0;
}

static void log_spike(LONG n, double ms, double mean, double in_present, LONG64 t0, LONG64 t1)
{
    static const char *const kind[] = { "WFSO", "WFSOEx", "WFMO", "CondVar", "WaitOnAddress", "Sleep", "SleepEx" };
    char buf[2600], one[40];
    uint32_t top[6], nt = st_wait_top(t0, t1, top, 6), i, j;
    int len;
    LONG k = InterlockedIncrement(&spikes);
    if (k > 300 && (k & (k - 1))) return;
    len = wsprintfA(buf, "stutter: frame #%ld (spike %ld) ", n, k);
    len += sprintf(buf + len, "%.1f ms (average %.1f; %.1f in Present) | PSO %ld %.1f ms | resources %ld %.1f ms %ld MB | "
                   "ECL %ld (%ld lists) %.1f ms | waits>=4ms:", ms, mean, in_present, st_n[ST_PSO], ms_of(st_ticks[ST_PSO]),
                   st_n[ST_RES], ms_of(st_ticks[ST_RES]), (long)(st_bytes >> 20), st_n[ST_ECL], st_lists,
                   ms_of(st_ticks[ST_ECL]));
    if (!nt) len += wsprintfA(buf + len, " none");
    for (i = 0; i < nt && len < 1300; i++) {
        st_wait *w = &waits[top[i]];
        len += sprintf(buf + len, " [%s %.1f ms tid %u%s", kind[w->kind % 7], w->us / 1000.0, w->tid,
                       w->tid == present_tid ? " P" : "");
        for (j = 0; j < w->nframes; j++) { st_label(one, w->frames[j]); len += wsprintfA(buf + len, "%s", one); }
        len += wsprintfA(buf + len, "]");
    }
    len += rk_format(buf + len, (int)sizeof buf - len - 300);
    if (dr_on) len += sprintf(buf + len, " | deferred %ld released %ld queued %ld full %ld", dr_deferred, dr_released,
                              dr_head - dr_tail, dr_full);
    {
        static const char *const qn[] = { "direct", "bundle", "compute", "copy" };
        len += sprintf(buf + len, " | GPU: longest span %.1f ms (%s, value %llu), total %.1f ms, %ld completed, %ld pending, "
                       "%ld queue Waits, %ld outside the ring", ms_of(gpu_max_ticks), qn[gpu_max_qt & 3],
                       (unsigned long long)gpu_max_val, ms_of(gpu_sum_ticks), gpu_done, gpu_pend, sig_waits, sig_dropped);
    }
    wlog("%s", buf);
}

static void frame_tick(LONG64 before, LONG64 after)
{
    double ms, mean, in_p = ms_of(after - before);
    LONG n = ++frame_no;
    int i;
    present_tid = GetCurrentThreadId();
    if (frame_start) {
        ms = ms_of(after - frame_start);
        mean = st_avg_get(&avg);
        if (st_is_spike(ms, mean)) { log_spike(n, ms, mean, in_p, frame_start, after); sum_spikes++; }
        InterlockedIncrement(&vz_frames);
        InterlockedAdd64(&vz_frame_ticks, after - frame_start);
        InterlockedAdd64(&vz_present_ticks, after - before);
        if (st_lists > vz_lists_max) vz_lists_max = st_lists;
        st_avg_add(&avg, ms);
        if (sum_n < ST_SUM_FRAMES) sum_frames[sum_n++] = (float)ms;
        if (ms > sum_max) sum_max = (float)ms;
    }
    if (!sum_t0) sum_t0 = after;
    if (ms_of(after - sum_t0) >= 10000 && sum_n) {
        double mean10 = 0;
        uint32_t k;
        for (k = 0; k < sum_n; k++) mean10 += sum_frames[k];
        mean10 /= sum_n;
        wlog("stutter: 10 s: %u frames, average %.1f ms, p99 %.1f, max %.1f, %u spikes", sum_n, mean10,
             st_percentile(sum_frames, sum_n, 99), sum_max, sum_spikes);
        {
            char tb[512];
            if (tl_format(tb, sizeof tb)) wlog("%s", tb);
            tl_reset();
        }
        sum_n = sum_spikes = 0; sum_max = 0; sum_t0 = after;
    }
    frame_start = after;
    for (i = 0; i < ST_NCOUNT; i++) { st_n[i] = 0; st_ticks[i] = 0; }
    st_lists = 0; st_bytes = 0;
    rk_reset();
    gpu_max_ticks = 0; gpu_max_val = 0; gpu_sum_ticks = 0; gpu_done = 0; sig_waits = 0; sig_dropped = 0;
}

static HRESULT (STDMETHODCALLTYPE *real_present)(IDXGISwapChain *, UINT, UINT);
static HRESULT (STDMETHODCALLTYPE *real_present1)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);

static HRESULT STDMETHODCALLTYPE st_present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    LONG64 t = qpc();
    HRESULT hr = real_present(sc, sync, flags);
    if (!(flags & DXGI_PRESENT_TEST)) frame_tick(t, qpc());
    return hr;
}

static HRESULT STDMETHODCALLTYPE st_present1(IDXGISwapChain1 *sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *p)
{
    LONG64 t = qpc();
    HRESULT hr = real_present1(sc, sync, flags, p);
    if (!(flags & DXGI_PRESENT_TEST)) frame_tick(t, qpc());
    return hr;
}

static void hook_swapchain(IUnknown *u)
{
    IDXGISwapChain1 *sc = NULL;
    if (FAILED(u->lpVtbl->QueryInterface(u, &st_iid_swapchain1, (void **)&sc))) return;
    vt_swap((void **)&sc->lpVtbl->Present, st_present, (void **)&real_present);
    vt_swap((void **)&sc->lpVtbl->Present1, st_present1, (void **)&real_present1);
    sc->lpVtbl->Release(sc);
    wlog("stutter: swap chain Present/Present1 measured");
}

static HRESULT (STDMETHODCALLTYPE *real_csc)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
static HRESULT (STDMETHODCALLTYPE *real_csc_hwnd)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
static HRESULT (STDMETHODCALLTYPE *real_csc_core)(IDXGIFactory2 *, IUnknown *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *,
                                                  IDXGIOutput *, IDXGISwapChain1 **);

static HRESULT STDMETHODCALLTYPE st_csc(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **out)
{
    HRESULT hr = real_csc(f, dev, d, out);
    if (SUCCEEDED(hr) && out && *out) hook_swapchain((IUnknown *)*out);
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_csc_hwnd(IDXGIFactory2 *f, IUnknown *dev, HWND w, const DXGI_SWAP_CHAIN_DESC1 *d,
                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *o, IDXGISwapChain1 **out)
{
    HRESULT hr = real_csc_hwnd(f, dev, w, d, fs, o, out);
    if (SUCCEEDED(hr) && out && *out) hook_swapchain((IUnknown *)*out);
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_csc_core(IDXGIFactory2 *f, IUnknown *dev, IUnknown *win, const DXGI_SWAP_CHAIN_DESC1 *d,
                                             IDXGIOutput *o, IDXGISwapChain1 **out)
{
    HRESULT hr = real_csc_core(f, dev, win, d, o, out);
    if (SUCCEEDED(hr) && out && *out) hook_swapchain((IUnknown *)*out);
    return hr;
}

static void hook_factory(void *obj)
{
    IDXGIFactory2 *f = NULL;
    IUnknown *u = obj;
    if (FAILED(u->lpVtbl->QueryInterface(u, &st_iid_factory2, (void **)&f))) return;
    vt_swap((void **)&f->lpVtbl->CreateSwapChain, st_csc, (void **)&real_csc);
    vt_swap((void **)&f->lpVtbl->CreateSwapChainForHwnd, st_csc_hwnd, (void **)&real_csc_hwnd);
    vt_swap((void **)&f->lpVtbl->CreateSwapChainForCoreWindow, st_csc_core, (void **)&real_csc_core);
    f->lpVtbl->Release(f);
}

static HRESULT (WINAPI *real_factory1)(REFIID, void **);
static HRESULT (WINAPI *real_factory2)(UINT, REFIID, void **);
static HRESULT WINAPI st_factory1(REFIID riid, void **out)
{
    HRESULT hr = real_factory1(riid, out);
    if (SUCCEEDED(hr) && out && *out) hook_factory(*out);
    return hr;
}
static HRESULT WINAPI st_factory2(UINT flags, REFIID riid, void **out)
{
    HRESULT hr = real_factory2(flags, riid, out);
    if (SUCCEEDED(hr) && out && *out) hook_factory(*out);
    return hr;
}

/* ---- device D3D12: PSOs, recursos, filas ---- */
static HRESULT (STDMETHODCALLTYPE *real_gpso)(ID3D12Device *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_cpso)(ID3D12Device *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_spso)(ID3D12Device2 *, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib)(ID3D12Device1 *, const void *, SIZE_T, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib_gfx)(ID3D12PipelineLibrary *, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib_cs)(ID3D12PipelineLibrary *, LPCWSTR, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_lib_stream)(ID3D12PipelineLibrary1 *, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **);

#define TIMED(which, call) do { LONG64 t_ = qpc(); hr = call; st_count(which, qpc() - t_); } while (0)

static HRESULT STDMETHODCALLTYPE st_gpso(ID3D12Device *d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_gpso(d, p, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_cpso(ID3D12Device *d, const D3D12_COMPUTE_PIPELINE_STATE_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_cpso(d, p, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_spso(ID3D12Device2 *d, const D3D12_PIPELINE_STATE_STREAM_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_spso(d, p, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_lib_gfx(ID3D12PipelineLibrary *l, LPCWSTR n, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_lib_gfx(l, n, p, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_lib_cs(ID3D12PipelineLibrary *l, LPCWSTR n, const D3D12_COMPUTE_PIPELINE_STATE_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_lib_cs(l, n, p, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_lib_stream(ID3D12PipelineLibrary1 *l, LPCWSTR n, const D3D12_PIPELINE_STATE_STREAM_DESC *p, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_PSO, real_lib_stream(l, n, p, r, o)); return hr; }

static HRESULT STDMETHODCALLTYPE st_lib(ID3D12Device1 *d, const void *blob, SIZE_T len, REFIID r, void **o)
{
    HRESULT hr = real_lib(d, blob, len, r, o);
    if (SUCCEEDED(hr) && o && *o) {
        IUnknown *u = *o;
        ID3D12PipelineLibrary *l0 = NULL;
        ID3D12PipelineLibrary1 *l1 = NULL;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12PipelineLibrary, (void **)&l0))) {
            vt_swap((void **)&l0->lpVtbl->LoadGraphicsPipeline, st_lib_gfx, (void **)&real_lib_gfx);
            vt_swap((void **)&l0->lpVtbl->LoadComputePipeline, st_lib_cs, (void **)&real_lib_cs);
            l0->lpVtbl->Release(l0);
        }
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_ID3D12PipelineLibrary1, (void **)&l1))) {
            vt_swap((void **)&l1->lpVtbl->LoadPipeline, st_lib_stream, (void **)&real_lib_stream);
            l1->lpVtbl->Release(l1);
        }
    }
    return hr;
}

/* tamanho aproximado do recurso (so para o log: buffer = largura; textura = largura x altura x fatias x 4) */
uint64_t st_res_bytes(const D3D12_RESOURCE_DESC *d)
{
    if (!d) return 0;
    if (d->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return d->Width;
    return d->Width * d->Height * d->DepthOrArraySize * 4;
}

static HRESULT (STDMETHODCALLTYPE *real_committed)(ID3D12Device *, const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC *,
                                                   D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_placed)(ID3D12Device *, ID3D12Heap *, UINT64, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
                                                const D3D12_CLEAR_VALUE *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_reserved)(ID3D12Device *, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *,
                                                  REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_heap)(ID3D12Device *, const D3D12_HEAP_DESC *, REFIID, void **);
static HRESULT (STDMETHODCALLTYPE *real_queue)(ID3D12Device *, const D3D12_COMMAND_QUEUE_DESC *, REFIID, void **);
static void (STDMETHODCALLTYPE *real_ecl)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
static HRESULT (STDMETHODCALLTYPE *real_signal)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);
extern int lp_on;
/* listas unificadas (common/cmdrec.c) */
extern int rc_on;
extern int rc_any(UINT n, ID3D12CommandList *const *l);
extern UINT rc_merge(void **rqp, ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l, UINT64 fence, UINT64 done,
                     ID3D12CommandList ***outv);
extern int rc_wants(D3D12_COMMAND_LIST_TYPE type, const void *ret);
extern HRESULT rc_create(ID3D12Device *d, ID3D12PipelineState *ps, REFIID riid, void **out);
extern int rc_format(char *buf, int cap, double frames);
extern void rc_setup(void *inner);
static void lt_hook(ID3D12CommandList *cl);

static sg_queue *sg_find(ID3D12CommandQueue *q)
{
    LONG i, n = sg_nqueues;
    for (i = 0; i < n && i < SG_QUEUES; i++) if (sg_queues[i].q == q) return &sg_queues[i];
    return NULL;
}

/* ExecuteCommandLists + Signal do fence do proxy da fila, sob a trava da fila (valores em ordem). O pool de command
 * lists so devolve uma lista guardada depois que a GPU passou desse valor: reenviar uma lista cuja execucao
 * anterior ainda esta em voo e invalido (debug layer, erro 553: o envio e descartado) */
static void lp_ecl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l)
{
    int merge = rc_any(n, l);
    sg_queue *s = lp_on || merge ? sg_find(q) : NULL;
    HRESULT (STDMETHODCALLTYPE *sig)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);
    ID3D12CommandList **o = NULL;
    UINT k;
    if (!s || !s->lf) {
        if (merge) {   /* fila sem fence do proxy (nao acontece): listas reais nunca reusadas, mas nada de gravadora no D3D12 */
            static void *rq;
            static volatile LONG logged;
            if (!InterlockedExchange(&logged, 1)) wlog("d3d: unified lists: queue %p without a proxy fence", q);
            if ((k = rc_merge(&rq, q, n, l, UINT64_MAX, 0, &o)) != 0) real_ecl(q, k, o);
            return;
        }
        real_ecl(q, n, l);
        return;
    }
    sig = real_signal ? real_signal : q->lpVtbl->Signal;   /* o da fila, nao o st_signal (fora da medicao) */
    AcquireSRWLockExclusive(&s->llock);
    if (!merge) real_ecl(q, n, l);
    else if ((k = rc_merge(&s->rq, q, n, l, s->lsent + 1, s->lf->lpVtbl->GetCompletedValue(s->lf), &o)) != 0) {
        if (st_on) lt_hook(o[0]);
        real_ecl(q, k, o);
    }
    if (SUCCEEDED(sig(q, s->lf, s->lsent + 1))) s->lsent++;
    ReleaseSRWLockExclusive(&s->llock);
}

/* ---- objetos D3D12 vivos por tipo + mapa da memoria do processo (PASSO 94, com StutterLog): a queda do fim da
 * corrida vem com 10-11.6 GB privados ("Kernel memory failure") e o dump mostra ~5 GB em blocos WC de 32/64 MB e
 * ~2 GB em blocos RW com paginas de guarda, sem dizer de quem. Criacao (committed por tipo de heap, placed, reserved,
 * heap por tipo, allocator, descriptor heap) entra numa tabela {objeto, tipo, bytes}; a ultima Release (rk_release /
 * fila do DeferRelease) tira. A cada 10 s e na remocao do device: vivos/bytes/criados/destruidos por tipo +
 * VirtualQuery do processo (WC, alocacoes com guarda, RW, RO, mapeada). ---- */
enum { MR_C_DEFAULT, MR_C_UPLOAD, MR_C_READBACK, MR_C_CUSTOM, MR_PLACED, MR_RESERVED, MR_H_DEFAULT, MR_H_UPLOAD,
       MR_H_READBACK, MR_H_CUSTOM, MR_ALLOCATOR, MR_DESC_HEAP, MR_PA, MR_KINDS };
static const char *const mr_names[MR_KINDS] = { "committed DEFAULT", "committed UPLOAD", "committed READBACK",
    "committed CUSTOM", "placed", "reserved", "heap DEFAULT", "heap UPLOAD", "heap READBACK", "heap CUSTOM",
    "allocators", "descriptor heaps", "proxy placed" };
#define MR_BITS 20
#define MR_SIZE (1u << MR_BITS)
typedef struct { const void *o; uint32_t kind; uint64_t bytes; } mr_ent;
static mr_ent *mr_tab;
static LONG mr_used, mr_full;
static SRWLOCK mr_lock = SRWLOCK_INIT;
static volatile LONG64 mr_live[MR_KINDS], mr_live_bytes[MR_KINDS];
static volatile LONG mr_made[MR_KINDS], mr_gone[MR_KINDS];

static uint32_t mr_slot(const void *o) { return (uint32_t)((((uintptr_t)o >> 4) * 0x9E3779B97F4A7C15ull) >> (64 - MR_BITS)); }

/* bytes aproximados (so para o log): buffer = largura; textura = texels x bytes do formato (BC 0.5/1) x 4/3 com mips */
uint64_t mr_res_bytes(const D3D12_RESOURCE_DESC *d)
{
    uint64_t px, b;
    if (!d) return 0;
    if (d->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return d->Width;
    px = d->Width * d->Height * d->DepthOrArraySize * (d->SampleDesc.Count ? d->SampleDesc.Count : 1);
    switch (d->Format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: b = px / 2; break;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: b = px; break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT: b = px * 8; break;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: b = px * 16; break;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: b = px * 2; break;
    default: b = px * 4; break;
    }
    return d->MipLevels != 1 ? b * 4 / 3 : b;
}

uint32_t mr_heap_kind(D3D12_HEAP_TYPE t, int heap)
{
    uint32_t k = t == D3D12_HEAP_TYPE_UPLOAD ? 1 : t == D3D12_HEAP_TYPE_READBACK ? 2 : t == D3D12_HEAP_TYPE_CUSTOM ? 3 : 0;
    return (heap ? MR_H_DEFAULT : MR_C_DEFAULT) + k;
}

static void mr_add(const void *o, uint32_t kind, uint64_t bytes)
{
    uint32_t i;
    if (!st_on || !o || kind >= MR_KINDS) return;
    InterlockedIncrement(&mr_made[kind]);
    AcquireSRWLockExclusive(&mr_lock);
    if (!mr_tab) mr_tab = VirtualAlloc(NULL, MR_SIZE * sizeof *mr_tab, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mr_tab || mr_used >= (LONG)(MR_SIZE / 4 * 3)) { mr_full++; ReleaseSRWLockExclusive(&mr_lock); return; }
    for (i = mr_slot(o); mr_tab[i].o && mr_tab[i].o != o; i = (i + 1) & (MR_SIZE - 1)) {}
    if (mr_tab[i].o == o) {                           /* endereco reaproveitado sem passar pela Release */
        InterlockedDecrement64(&mr_live[mr_tab[i].kind]);
        InterlockedAdd64(&mr_live_bytes[mr_tab[i].kind], -(LONG64)mr_tab[i].bytes);
    } else mr_used++;
    mr_tab[i].o = o; mr_tab[i].kind = kind; mr_tab[i].bytes = bytes;
    InterlockedIncrement64(&mr_live[kind]);
    InterlockedAdd64(&mr_live_bytes[kind], (LONG64)bytes);
    ReleaseSRWLockExclusive(&mr_lock);
}

/* remocao com deslocamento para tras (sondagem linear sem lapides) */
static void mr_del(const void *o)
{
    uint32_t i, j, h;
    if (!o || !mr_tab) return;
    AcquireSRWLockExclusive(&mr_lock);
    for (i = mr_slot(o); mr_tab[i].o && mr_tab[i].o != o; i = (i + 1) & (MR_SIZE - 1)) {}
    if (mr_tab[i].o == o) {
        InterlockedDecrement64(&mr_live[mr_tab[i].kind]);
        InterlockedAdd64(&mr_live_bytes[mr_tab[i].kind], -(LONG64)mr_tab[i].bytes);
        InterlockedIncrement(&mr_gone[mr_tab[i].kind]);
        mr_used--;
        for (j = (i + 1) & (MR_SIZE - 1); mr_tab[j].o; j = (j + 1) & (MR_SIZE - 1)) {
            h = mr_slot(mr_tab[j].o);
            if (((j - h) & (MR_SIZE - 1)) >= ((j - i) & (MR_SIZE - 1))) { mr_tab[i] = mr_tab[j]; i = j; }
        }
        mr_tab[i].o = NULL;
    }
    ReleaseSRWLockExclusive(&mr_lock);
}

/* VirtualQuery do processo, por alocacao (AllocationBase): WC, com pagina NOACCESS comprometida (guarda), RW, RO,
 * mapeada; bytes comprometidos e numero de alocacoes (imagens fora) */
enum { MV_WC, MV_GUARD, MV_RW, MV_RO, MV_OTHER, MV_MAPPED, MV_KINDS };
static const char *const mv_names[MV_KINDS] = { "WC", "RW guarded", "RW", "RO", "other", "mapped" };
static void mv_close(uint64_t *bytes, uint32_t *allocs, uint64_t cur, int wc, int guard, int rw, int ro, DWORD type)
{
    int k;
    if (!cur) return;
    k = type == MEM_MAPPED ? MV_MAPPED : wc ? MV_WC : guard ? MV_GUARD : rw ? MV_RW : ro ? MV_RO : MV_OTHER;
    bytes[k] += cur; allocs[k]++;
}
void mv_scan(uint64_t *bytes, uint32_t *allocs)
{
    MEMORY_BASIC_INFORMATION m;
    uint8_t *p = NULL;
    void *base = NULL;
    uint64_t cur = 0;
    int wc = 0, guard = 0, rw = 0, ro = 0;
    DWORD type = 0;
    memset(bytes, 0, MV_KINDS * sizeof *bytes);
    memset(allocs, 0, MV_KINDS * sizeof *allocs);
    while (VirtualQuery(p, &m, sizeof m) == sizeof m) {
        if (m.State == MEM_COMMIT && m.Type != MEM_IMAGE) {
            if (m.AllocationBase != base) {
                mv_close(bytes, allocs, cur, wc, guard, rw, ro, type);
                base = m.AllocationBase; cur = 0; wc = guard = rw = ro = 0; type = m.Type;
            }
            cur += m.RegionSize;
            if (m.Protect & PAGE_WRITECOMBINE) wc = 1;
            if ((m.Protect & 0xff) == PAGE_NOACCESS) guard = 1;
            if ((m.Protect & 0xff) == PAGE_READWRITE) rw = 1;
            if ((m.Protect & 0xff) == PAGE_READONLY) ro = 1;
        }
        p = (uint8_t *)m.BaseAddress + m.RegionSize;
        if ((uintptr_t)p >= 0x7ffffffe0000ull) break;
    }
    mv_close(bytes, allocs, cur, wc, guard, rw, ro, type);
}

static HMODULE self_mod;
/* ---- de quem sao os blocos "RW guarded" (PASSO 95): crescem sem parar na corrida (714 MB -> 2.7 GB na queda,
 * nunca caem = vazamento; paginas NOACCESS entre blocos = heap). Le 1 pagina em MG_EVERY dessas alocacoes
 * (ReadProcessMemory: pagina solta no meio da leitura so falha) e conta os qwords que apontam para dentro de uma imagem
 * (vtables/codigo: azimuth, proxy, driver, d3d12, jogo) -> os valores mais frequentes dizem que objetos enchem. ---- */
#define MG_EVERY 16
#define MG_MODS 256
#define MG_BINS 8192
#define MG_TOP 16
typedef struct { uintptr_t lo, hi; char name[40]; uint64_t n; } mg_mod;
typedef struct { uintptr_t v; uint64_t n; } mg_bin;
static mg_mod mg_mods[MG_MODS];
static int mg_nmods;
static mg_bin mg_bins[MG_BINS];
static uint64_t mg_other;

static void mg_load_mods(void)
{
    HMODULE hm[MG_MODS];
    DWORD need = 0, i, n;
    uint8_t *game = g_game;
    MODULEINFO mi;
    mg_nmods = 0;
    if (game) {
        mg_mods[0].lo = (uintptr_t)game; mg_mods[0].hi = (uintptr_t)game + 0x8000000;
        strcpy(mg_mods[0].name, "game"); mg_nmods = 1;
    }
    if (!K32EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need)) return;
    n = need / sizeof *hm;
    for (i = 0; i < n && mg_nmods < MG_MODS; i++) {
        mg_mod *m = &mg_mods[mg_nmods];
        if (!K32GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi)) continue;
        m->lo = (uintptr_t)mi.lpBaseOfDll; m->hi = m->lo + mi.SizeOfImage;
        if ((uint8_t *)hm[i] == g_base) strcpy(m->name, "azimuth");
        else if (hm[i] == self_mod) strcpy(m->name, "winmm");
        else if (!K32GetModuleBaseNameA(GetCurrentProcess(), hm[i], m->name, sizeof m->name)) strcpy(m->name, "?");
        mg_nmods++;
    }
}

int mg_mod_of(uintptr_t v)
{
    int i;
    for (i = 0; i < mg_nmods; i++) if (v >= mg_mods[i].lo && v < mg_mods[i].hi) return i;
    return -1;
}

void mg_count(uintptr_t v)
{
    uint32_t h = (uint32_t)((v * 0x9E3779B97F4A7C15ull) >> 51), k;
    for (k = 0; k < 64; k++, h = (h + 1) & (MG_BINS - 1)) {
        if (mg_bins[h].v == v) { mg_bins[h].n++; return; }
        if (!mg_bins[h].v) { mg_bins[h].v = v; mg_bins[h].n = 1; return; }
    }
    mg_other++;
}

/* conta os ponteiros para imagens em uma pagina */
void mg_page(const uint64_t *q, size_t nq)
{
    size_t i;
    for (i = 0; i < nq; i++) {
        int m;
        if (q[i] < 0x10000 || (m = mg_mod_of((uintptr_t)q[i])) < 0) continue;
        mg_mods[m].n++;
        mg_count((uintptr_t)q[i]);
    }
}

static void mg_scan_alloc(uint8_t *base, uint8_t *end, uint64_t *pages)
{
    static uint64_t buf[4096 / 8];
    MEMORY_BASIC_INFORMATION m;
    uint8_t *p = base;
    while (p < end && VirtualQuery(p, &m, sizeof m) == sizeof m && m.AllocationBase == base) {
        if (m.State == MEM_COMMIT && (m.Protect & 0xff) == PAGE_READWRITE) {
            uint8_t *q;
            for (q = (uint8_t *)m.BaseAddress; q < (uint8_t *)m.BaseAddress + m.RegionSize; q += 4096 * MG_EVERY) {
                SIZE_T got = 0;
                if (ReadProcessMemory(GetCurrentProcess(), q, buf, sizeof buf, &got) && got == sizeof buf) {
                    mg_page(buf, sizeof buf / 8);
                    (*pages)++;
                }
            }
        }
        p = (uint8_t *)m.BaseAddress + m.RegionSize;
    }
}

void mg_report(const char *when)
{
    MEMORY_BASIC_INFORMATION m;
    uint8_t *p = NULL, *base = NULL, *start = NULL;
    int guard = 0, wc = 0, rw = 0, i, k, len;
    uint64_t pages = 0, total = 0;
    char buf[2048], one[96];
    uint32_t top[MG_TOP], nt = 0;
    mg_load_mods();
    memset(mg_bins, 0, sizeof mg_bins);
    mg_other = 0;
    for (i = 0; i < mg_nmods; i++) mg_mods[i].n = 0;
    for (;;) {
        int ok = VirtualQuery(p, &m, sizeof m) == sizeof m && (uintptr_t)p < 0x7ffffffe0000ull;
        if (!ok || m.AllocationBase != base) {               /* fecha a alocacao anterior */
            if (base && guard && rw && !wc) mg_scan_alloc(base, p, &pages);
            if (!ok) break;
            base = m.AllocationBase; start = p; guard = wc = rw = 0;
        }
        if (m.State == MEM_COMMIT && m.Type == MEM_PRIVATE) {
            if (m.Protect & PAGE_WRITECOMBINE) wc = 1;
            if ((m.Protect & 0xff) == PAGE_NOACCESS) guard = 1;
            if ((m.Protect & 0xff) == PAGE_READWRITE) rw = 1;
        }
        p = (uint8_t *)m.BaseAddress + m.RegionSize;
    }
    (void)start;
    for (i = 0; i < mg_nmods; i++) total += mg_mods[i].n;
    len = sprintf(buf, "stutter: guarded blocks %s: %llu pages read (1 in %d), %llu pointers into images; per module:",
                  when, (unsigned long long)pages, MG_EVERY, (unsigned long long)total);
    for (k = 0; k < 8; k++) {                                /* os 8 modulos com mais ponteiros */
        int best = -1;
        for (i = 0; i < mg_nmods; i++)
            if (mg_mods[i].n && (best < 0 || mg_mods[i].n > mg_mods[best].n)) best = i;
        if (best < 0) break;
        len += sprintf(buf + len, " %s %.0f%%", mg_mods[best].name, mg_mods[best].n * 100.0 / (total ? total : 1));
        mg_mods[best].n = 0;
    }
    wlog("%s", buf);
    for (k = 0; k < MG_TOP; k++) {                           /* os valores mais frequentes */
        uint32_t best = MG_BINS;
        for (i = 0; i < MG_BINS; i++) {
            uint32_t j;
            for (j = 0; j < nt && top[j] != (uint32_t)i; j++) {}
            if (j < nt || !mg_bins[i].v) continue;
            if (best == MG_BINS || mg_bins[i].n > mg_bins[best].n) best = (uint32_t)i;
        }
        if (best == MG_BINS) break;
        top[nt++] = best;
    }
    len = sprintf(buf, "stutter: guarded blocks %s: most frequent values:", when);
    for (k = 0; k < (int)nt && len < 1800; k++) {
        int mi = mg_mod_of(mg_bins[top[k]].v);
        sprintf(one, " %s+%#llx x%llu;", mi >= 0 ? mg_mods[mi].name : "?",
                (unsigned long long)(mg_bins[top[k]].v - (mi >= 0 ? mg_mods[mi].lo : 0)), (unsigned long long)mg_bins[top[k]].n);
        len += sprintf(buf + len, "%s", one);
    }
    wlog("%s", buf);
}

void mr_report(const char *when)
{
    char buf[2048];
    int len, k;
    uint64_t vb[MV_KINDS];
    uint32_t va[MV_KINDS];
    len = sprintf(buf, "stutter: memory %s: live D3D12:", when);
    for (k = 0; k < MR_KINDS && len < 1700; k++) {
        LONG made = InterlockedExchange(&mr_made[k], 0), gone = InterlockedExchange(&mr_gone[k], 0);
        if (!mr_live[k] && !made) continue;
        len += sprintf(buf + len, " [%s %lld, %llu MiB, +%ld -%ld]", mr_names[k], (long long)mr_live[k],
                       (unsigned long long)(mr_live_bytes[k] > 0 ? mr_live_bytes[k] : 0) >> 20, made, gone);
    }
    if (mr_full) len += sprintf(buf + len, " (table full: %ld left out)", mr_full);
    len += pa_format(buf + len);
    mv_scan(vb, va);
    len += sprintf(buf + len, " | process:");
    for (k = 0; k < MV_KINDS; k++)
        len += sprintf(buf + len, " %s %llu MiB/%u", mv_names[k], (unsigned long long)(vb[k] >> 20), va[k]);
    wlog("%s", buf);
    {   /* PASSO 99: placement textures vivas a cada 60 s e na remocao do device */
        static int ticks;
        if (!strcmp(when, "on device removal") || ++ticks % 6 == 0) pa_live_report(when);
    }
    {   /* conteudo dos blocos com guarda: a cada +256 MB deles e na remocao do device */
        static uint64_t scanned;
        int final = !strcmp(when, "on device removal");
        if (final || vb[MV_GUARD] >= scanned + (256ull << 20)) {
            scanned = vb[MV_GUARD];
            mg_report(when);
        }
    }
}

/* espera a remocao do device (a fence vai a UINT64_MAX) e grava o resumo */
static DWORD WINAPI mr_watch(void *param)
{
    ID3D12Device *dev = param;
    ID3D12Fence *fence = NULL;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ev || FAILED(dev->lpVtbl->CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence)) ||
        FAILED(fence->lpVtbl->SetEventOnCompletion(fence, UINT64_MAX, ev)))
        return 0;
    WaitForSingleObject(ev, INFINITE);
    mr_report("on device removal");
    return 0;
}

static HRESULT (STDMETHODCALLTYPE *real_alloc)(ID3D12Device *, D3D12_COMMAND_LIST_TYPE, REFIID, void **);
static HRESULT STDMETHODCALLTYPE st_alloc(ID3D12Device *d, D3D12_COMMAND_LIST_TYPE t, REFIID r, void **o)
{
    HRESULT hr = real_alloc(d, t, r, o);
    if (SUCCEEDED(hr) && o && *o) { rk_hook_release(*o); mr_add(*o, MR_ALLOCATOR, 0); }
    return hr;
}
static HRESULT (STDMETHODCALLTYPE *real_dheap)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *, REFIID, void **);
static HRESULT STDMETHODCALLTYPE st_dheap(ID3D12Device *d, const D3D12_DESCRIPTOR_HEAP_DESC *hd, REFIID r, void **o)
{
    HRESULT hr = real_dheap(d, hd, r, o);
    if (SUCCEEDED(hr) && o && *o) { rk_hook_release(*o); mr_add(*o, MR_DESC_HEAP, hd ? (uint64_t)hd->NumDescriptors * 32 : 0); }
    return hr;
}

/* ---- placement textures PLACED em heaps do proxy (PASSO 97, opcao A): com PlacementCommitted o Azimuth cria cada
 * placement texture do FM6 com CreateCommittedResource (0x1400e7720, chamada em azimuth+0xe77bd) -> ~49k recursos
 * committed vivos na corrida, ~10 KiB de memoria NONPAGED do kernel cada (tools\d3d12_kernel_probe.c; o FH2 tem 300).
 * Aqui essas chamadas (reconhecidas pelo retorno azimuth+0xe77c0; heap DEFAULT, textura sem RT/DS/MSAA, ate 16 MB)
 * viram CreatePlacedResource em heaps de 64 MB do proxy (so texturas nao RT/DS; 0.1 KiB nonpaged por recurso).
 * Paginas de 4 KiB num bitmap por heap; alinhamento de 4 KiB se o driver aceitar (small placement), senao 64 KiB.
 * A faixa volta ao heap na ultima Release do recurso (rk_release); heaps nunca sao destruidos. Qualquer falha cai no
 * committed original. debug.ini PlacementPlaced=0/1 (FM6 padrao 1). ---- */
#define PA_RET_RVA 0xe77c0
#define PA_HEAP_BYTES (64ull << 20)
#define PA_PAGE 4096u
#define PA_PAGES ((uint32_t)(PA_HEAP_BYTES / PA_PAGE))
#define PA_WORDS (PA_PAGES / 64)
#define PA_MAX_HEAPS 512
#define PA_MAX_RES (16ull << 20)
#define PA_BITS 18
#define PA_TAB (1u << PA_BITS)
typedef struct { ID3D12Heap *h; uint64_t used[PA_WORDS]; uint32_t free_pages; } pa_heap;
/* PASSO 99: tambem o objeto do Azimuth entregue ao jogo, o endereco no jogo, ordem/segundo de criacao, segundo em
   que o objeto chegou a 0 (+1; 0 = vivo), chamador (game+off), bytes estimados pelos texels e a forma da textura;
   PASSO 100: segundo do ultimo refresh_title_texture do objeto (+1; 0 = nunca);
   PASSO 101: lo = piso do tamanho no jogo (texels por mip, blocos BC), orphan = paginas ja devolvidas (sobreposta) */
typedef struct { const void *res, *obj; uint64_t addr; uint32_t seq, born, gone, caller, est, used, lo;
                 uint16_t heap, page_hi, page, n, w, h; uint8_t fmt, mips, orphan; } pa_ent;
static pa_heap *pa_heaps[PA_MAX_HEAPS];
static LONG pa_nheaps;
static pa_ent *pa_tab;
static LONG pa_ntab;
static SRWLOCK pa_lock = SRWLOCK_INIT;
static int pa_on;
static volatile LONG pa_made, pa_fallback, pa_freed;
static volatile LONG64 pa_used_pages;

static void pa_set(pa_heap *H, uint32_t p, uint32_t n, int on)
{
    uint32_t i;
    for (i = p; i < p + n; i++) {
        if (on) H->used[i >> 6] |= 1ull << (i & 63);
        else H->used[i >> 6] &= ~(1ull << (i & 63));
    }
    if (on) H->free_pages -= n; else H->free_pages += n;
}

/* 1a pagina >= p livre (used 0) ou ocupada (used 1); PA_PAGES se nao ha */
static uint32_t pa_next(const pa_heap *H, uint32_t p, int used)
{
    while (p < PA_PAGES) {
        uint64_t w = H->used[p >> 6];
        if (!used) w = ~w;
        w >>= p & 63;
        if (w) return p + (uint32_t)__builtin_ctzll(w);
        p = (p | 63) + 1;
    }
    return PA_PAGES;
}

/* 1a faixa livre de n paginas alinhada a `al` paginas; -1 se nao ha (PASSO 117: de 64 em 64 bits; era bit a bit) */
int pa_find(const pa_heap *H, uint32_t n, uint32_t al)
{
    uint32_t p = 0, u;
    if (!n || n > PA_PAGES || H->free_pages < n || !al) return -1;
    for (;;) {
        p = pa_next(H, p, 0);
        p = (p + al - 1) / al * al;
        if (p >= PA_PAGES || n > PA_PAGES - p) return -1;
        u = pa_next(H, p, 1);
        if (u >= p + n) return (int)p;
        p = u + 1;
    }
}

static uint32_t pa_slot(const void *r) { return (uint32_t)((((uintptr_t)r >> 4) * 0x9E3779B97F4A7C15ull) >> (64 - PA_BITS)); }

/* com a trava */
static pa_ent *pa_put(const void *res, uint32_t heap, uint32_t page, uint32_t n)
{
    uint32_t i;
    if (!pa_tab) pa_tab = VirtualAlloc(NULL, PA_TAB * sizeof *pa_tab, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pa_tab) return NULL;
    for (i = pa_slot(res); pa_tab[i].res && pa_tab[i].res != res; i = (i + 1) & (PA_TAB - 1)) {}
    if (!pa_tab[i].res) pa_ntab++;
    memset(&pa_tab[i], 0, sizeof pa_tab[i]);
    pa_tab[i].res = res; pa_tab[i].heap = (uint16_t)heap; pa_tab[i].page = (uint16_t)page;
    pa_tab[i].page_hi = (uint16_t)(page >> 16); pa_tab[i].n = (uint16_t)n;
    return &pa_tab[i];
}

static void po_del(const void *obj, const void *res);
static void pi_del(uint64_t addr, const void *res);
static void pi_add(uint64_t addr, const void *res, uint64_t len);
uint64_t pa_min_bytes(const D3D12_RESOURCE_DESC *d);

/* ultima Release de um recurso: se era nosso, devolve a faixa */
static void pa_free(const void *res)
{
    uint32_t i, j, h;
    if (!pa_tab || !res) return;
    AcquireSRWLockExclusive(&pa_lock);
    for (i = pa_slot(res); pa_tab[i].res && pa_tab[i].res != res; i = (i + 1) & (PA_TAB - 1)) {}
    if (pa_tab[i].res == res) {
        pa_heap *H = pa_heaps[pa_tab[i].heap];
        uint32_t page = pa_tab[i].page | (uint32_t)pa_tab[i].page_hi << 16;
        if (!pa_tab[i].orphan) {                       /* orfa: a faixa ja voltou (pa_reclaim) */
            pa_set(H, page, pa_tab[i].n, 0);
            InterlockedAdd64(&pa_used_pages, -(LONG64)pa_tab[i].n);
        }
        InterlockedIncrement(&pa_freed);
        if (pa_tab[i].obj && !pa_tab[i].gone) po_del(pa_tab[i].obj, res);
        if (pa_tab[i].addr) pi_del(pa_tab[i].addr, res);
        pa_ntab--;
        for (j = (i + 1) & (PA_TAB - 1); pa_tab[j].res; j = (j + 1) & (PA_TAB - 1)) {
            h = pa_slot(pa_tab[j].res);
            if (((j - h) & (PA_TAB - 1)) >= ((j - i) & (PA_TAB - 1))) { pa_tab[i] = pa_tab[j]; i = j; }
        }
        pa_tab[i].res = NULL;
    }
    ReleaseSRWLockExclusive(&pa_lock);
}

/* reserva n paginas alinhadas num heap existente; devolve o indice do heap e a pagina */
static int pa_reserve(uint32_t n, uint32_t al, uint32_t *page)
{
    LONG i, nh = pa_nheaps;
    for (i = 0; i < nh; i++) {
        int p = pa_find(pa_heaps[i], n, al);
        if (p >= 0) { pa_set(pa_heaps[i], (uint32_t)p, n, 1); *page = (uint32_t)p; return (int)i; }
    }
    return -1;
}

/* ---- PASSO 99 (opcao C): quem segura as placement textures placed ----
 * Na sessao de 1 h do PASSO 98 as vivas ficam em ~43-56k mas crescem de 0.6 para 8.5 GB, e nem as solturas em massa
 * (-31k) baixam os bytes. O wrapper do CreatePlacementTexture2D do FM6 (fm6/d3d.c) anota o endereco no jogo e o
 * chamador (pa_note_begin) e, na volta, o objeto do Azimuth entregue ao jogo (pa_note_end); o Release comum do
 * Azimuth (hook em common/d3d.c) avisa quando esse objeto chega a 0 (pa_obj_gone). A cada 60 s e na remocao do device
 * (pa_live_report): idade, tamanho, objeto do jogo vivo/solto, mesmo endereco ou sobreposta por uma mais nova (o jogo
 * reaproveitou a memoria e segura o objeto velho) e os chamadores/formas com mais MiB antigos. */
static __thread int pa_t_on;
static __thread uint64_t pa_t_addr;
static __thread uint32_t pa_t_caller;
static __thread const void *pa_t_res;
static volatile LONG pa_seq;
static ULONGLONG pa_t0;
typedef struct { const void *obj, *res; } po_ent;          /* objeto do Azimuth -> recurso placed */
static po_ent *po_tab;
static volatile LONG po_n;

static uint32_t pa_now(void)
{
    if (!pa_t0) pa_t0 = GetTickCount64();
    return (uint32_t)((GetTickCount64() - pa_t0) / 1000);
}

/* com a trava (leitura) */
static pa_ent *pa_get(const void *res)
{
    uint32_t i;
    if (!pa_tab || !res) return NULL;
    for (i = pa_slot(res); pa_tab[i].res && pa_tab[i].res != res; i = (i + 1) & (PA_TAB - 1)) {}
    return pa_tab[i].res == res ? &pa_tab[i] : NULL;
}

static uint32_t po_find(const void *obj)
{
    uint32_t i;
    for (i = pa_slot(obj); po_tab[i].obj && po_tab[i].obj != obj; i = (i + 1) & (PA_TAB - 1)) {}
    return i;
}

/* com a trava */
static void po_put(const void *obj, const void *res)
{
    uint32_t i;
    if (!po_tab) po_tab = VirtualAlloc(NULL, PA_TAB * sizeof *po_tab, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!po_tab || po_n >= (LONG)(PA_TAB - PA_TAB / 8)) return;
    i = po_find(obj);
    if (!po_tab[i].obj) po_n++;
    po_tab[i].obj = obj; po_tab[i].res = res;
}

static void po_del_at(uint32_t i)
{
    uint32_t j, h;
    po_n--;
    for (j = (i + 1) & (PA_TAB - 1); po_tab[j].obj; j = (j + 1) & (PA_TAB - 1)) {
        h = pa_slot(po_tab[j].obj);
        if (((j - h) & (PA_TAB - 1)) >= ((j - i) & (PA_TAB - 1))) { po_tab[i] = po_tab[j]; i = j; }
    }
    po_tab[i].obj = NULL; po_tab[i].res = NULL;
}

/* com a trava: o recurso placed morreu antes do objeto (so o par dele sai; outro objeto no mesmo endereco fica) */
static void po_del(const void *obj, const void *res)
{
    uint32_t i;
    if (!po_tab) return;
    i = po_find(obj);
    if (po_tab[i].obj == obj && po_tab[i].res == res) po_del_at(i);
}

/* com a trava: dados da textura recem criada; endereco/chamador se o wrapper do jogo anotou */
static void pa_describe(pa_ent *e, const void *res, const D3D12_RESOURCE_DESC *rd)
{
    uint64_t est = mr_res_bytes(rd);
    if (!e) return;
    e->seq = (uint32_t)InterlockedIncrement(&pa_seq);
    e->born = pa_now();
    e->est = est > 0xffffffffull ? 0xffffffffu : (uint32_t)est;
    e->w = rd->Width > 0xffff ? 0xffff : (uint16_t)rd->Width;
    e->h = rd->Height > 0xffff ? 0xffff : (uint16_t)rd->Height;
    e->fmt = (uint8_t)rd->Format; e->mips = (uint8_t)rd->MipLevels;
    est = pa_min_bytes(rd);
    e->lo = est > 0xffffffffull ? 0xffffffffu : (uint32_t)est;
    if (pa_t_on && !pa_t_res) {
        e->addr = pa_t_addr; e->caller = pa_t_caller; pa_t_res = res;
        if (e->addr) pi_add(e->addr, res, e->lo);
    }
}

/* ---- PASSO 101: DEVOLVER AS PAGINAS DAS SOBREPOSTAS ----
 * PASSOS 99/100: o jogo carrega texturas novas por cima da memoria de outras e segura os objetos velhos (no console
 * so o cabecalho; aqui uma copia D3D12 cada); em 2 sessoes NENHUMA sobreposta foi usada depois de coberta. Na criacao
 * de uma placement anotada (endereco no jogo), as vivas mais velhas (objeto do jogo vivo) cuja faixa no jogo cruza a
 * nova viram ORFAS: o recurso D3D12 continua (Azimuth/jogo seguram o objeto e as views), mas as paginas dele voltam ao
 * heap depois de pa_hold_ms (GPU em voo) e outras texturas passam a morar ali = alias, como a memoria do console. Se
 * o Azimuth for usar uma orfa (refresh_title_texture), o envio e PULADO (o envio escreveria por cima da dona atual) e
 * contado. Faixa no jogo = piso pelos texels (pa_min_bytes), para nao pegar vizinhas por engano.
 * debug.ini PlacementReclaim (padrao 1; precisa de PlacementTrack), PlacementReclaimHoldMs (padrao 2000). */
static int pa_reclaim_on;
int pa_hold_ms = 2000;
static uint64_t *pi_addr;                                  /* indice por endereco no jogo (ordem crescente) */
static uint64_t *pi_end;                                   /* fim da faixa (addr + lo): o pa_reclaim pula sem pa_get */
static const void **pi_res;
static uint32_t pi_n;
static uint64_t pi_maxlen;
typedef struct { uint64_t t; uint32_t page; uint16_t heap, n; } pq_ent;
#define PQ_RING 65536u
static pq_ent *pq_ring;
static uint32_t pq_head, pq_tail;
static volatile LONG pa_orphans, pa_orphan_uses;
static volatile LONG64 pa_orphan_pages;

/* piso do tamanho no jogo: soma dos mips (blocos 4x4 nos BC) x fatias */
uint64_t pa_min_bytes(const D3D12_RESOURCE_DESC *d)
{
    uint32_t bits, bc = 0, m, mips;
    uint64_t sum = 0, w, h, z;
    switch (d->Format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: bits = 4; bc = 1; break;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: bits = 8; bc = 1; break;
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_A8_UNORM: bits = 8; break;
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM: bits = 16; break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT: bits = 64; break;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: bits = 128; break;
    default: bits = 32; break;                        /* formatos de 32 bits; os raros maiores ficam por baixo (piso) */
    }
    mips = d->MipLevels ? d->MipLevels : 1;
    for (m = 0; m < mips; m++) {
        w = d->Width >> m; h = d->Height >> m;
        if (!w) w = 1;
        if (!h) h = 1;
        if (bc) { w = (w + 3) & ~3ull; h = (h + 3) & ~3ull; }
        z = d->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? (d->DepthOrArraySize >> m ? d->DepthOrArraySize >> m : 1) : 1;
        sum += w * h * z * bits / 8;
    }
    if (d->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D) sum *= d->DepthOrArraySize ? d->DepthOrArraySize : 1;
    return sum;
}

/* 1o indice com (endereco, recurso) >= o pedido */
static uint32_t pi_lower(uint64_t addr, const void *res)
{
    uint32_t lo = 0, hi = pi_n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (pi_addr[mid] < addr || (pi_addr[mid] == addr && (uintptr_t)pi_res[mid] < (uintptr_t)res)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* com a trava */
static void pi_add(uint64_t addr, const void *res, uint64_t len)
{
    uint32_t i;
    if (!pi_addr) {
        pi_addr = VirtualAlloc(NULL, PA_TAB * sizeof *pi_addr, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        pi_end = VirtualAlloc(NULL, PA_TAB * sizeof *pi_end, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        pi_res = VirtualAlloc(NULL, PA_TAB * sizeof *pi_res, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    if (!pi_addr || !pi_end || !pi_res || pi_n >= PA_TAB) return;
    i = pi_lower(addr, res);
    memmove(&pi_addr[i + 1], &pi_addr[i], (pi_n - i) * sizeof *pi_addr);
    memmove(&pi_end[i + 1], &pi_end[i], (pi_n - i) * sizeof *pi_end);
    memmove(&pi_res[i + 1], &pi_res[i], (pi_n - i) * sizeof *pi_res);
    pi_addr[i] = addr; pi_end[i] = addr + len; pi_res[i] = res; pi_n++;
    if (len > pi_maxlen) pi_maxlen = len;
}

/* com a trava */
static void pi_del(uint64_t addr, const void *res)
{
    uint32_t i;
    if (!pi_n) return;
    i = pi_lower(addr, res);
    if (i >= pi_n || pi_addr[i] != addr || pi_res[i] != res) return;
    memmove(&pi_addr[i], &pi_addr[i + 1], (pi_n - i - 1) * sizeof *pi_addr);
    memmove(&pi_end[i], &pi_end[i + 1], (pi_n - i - 1) * sizeof *pi_end);
    memmove(&pi_res[i], &pi_res[i + 1], (pi_n - i - 1) * sizeof *pi_res);
    pi_n--;
}

/* com a trava: devolve ao heap as faixas das orfas que ja passaram de pa_hold_ms (ou todas com force) */
static void pq_drain(uint64_t now_ms, int force)
{
    while (pq_tail != pq_head) {
        pq_ent *q = &pq_ring[pq_tail % PQ_RING];
        if (!force && now_ms - q->t < (uint64_t)(pa_hold_ms > 0 ? pa_hold_ms : 0)) break;
        pa_set(pa_heaps[q->heap], q->page, q->n, 0);
        pq_tail++;
    }
}

/* com a trava: a placement nova [addr, addr+len) no jogo deixa orfas as vivas mais velhas que ela cobre */
static void pa_reclaim(uint64_t addr, uint64_t len, uint64_t now_ms)
{
    uint32_t i;
    if (!pa_reclaim_on || !addr || !len || !pi_n) return;
    if (!pq_ring) pq_ring = VirtualAlloc(NULL, PQ_RING * sizeof *pq_ring, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pq_ring) return;
    for (i = pi_lower(addr > pi_maxlen ? addr - pi_maxlen : 0, NULL); i < pi_n && pi_addr[i] < addr + len; i++) {
        pa_ent *e;
        if (pi_end[i] <= addr) continue;               /* termina antes da nova: sem a tabela (falta de cache) */
        e = pa_get(pi_res[i]);
        if (!e || e->orphan || !e->obj || e->gone || !e->lo || e->addr + e->lo <= addr) continue;
        if (pq_head - pq_tail >= PQ_RING) break;      /* quarentena cheia: fica para a proxima */
        pq_ring[pq_head % PQ_RING].t = now_ms;
        pq_ring[pq_head % PQ_RING].heap = e->heap;
        pq_ring[pq_head % PQ_RING].page = e->page | (uint32_t)e->page_hi << 16;
        pq_ring[pq_head % PQ_RING].n = e->n;
        pq_head++;
        e->orphan = 1;
        InterlockedAdd64(&pa_used_pages, -(LONG64)e->n);
        InterlockedAdd64(&pa_orphan_pages, e->n);
        InterlockedIncrement(&pa_orphans);
    }
}

static int pa_track;                                      /* debug.ini PlacementTrack (padrao 1) */
int pa_tracking(void) { return pa_on && pa_track; }

void pa_note_begin(const void *addr, uintptr_t caller)
{
    pa_t_on = 1; pa_t_addr = (uint64_t)(uintptr_t)addr; pa_t_caller = (uint32_t)caller; pa_t_res = NULL;
}

/* volta do CreatePlacementTexture2D: liga o objeto entregue ao jogo ao recurso placed criado nessa chamada */
void pa_note_end(const void *obj)
{
    const void *res = pa_t_res;
    pa_ent *e;
    pa_t_on = 0; pa_t_res = NULL;
    if (!res || !obj) return;
    AcquireSRWLockExclusive(&pa_lock);
    if ((e = pa_get(res)) && !e->obj) { e->obj = obj; po_put(obj, res); }
    ReleaseSRWLockExclusive(&pa_lock);
}

/* Release comum do Azimuth chegou a 0 (qualquer objeto: so os anotados contam) */
void pa_obj_gone(const void *obj)
{
    uint32_t i;
    int hit;
    if (!po_n || !obj) return;
    AcquireSRWLockShared(&pa_lock);
    hit = po_tab && po_tab[po_find(obj)].obj == obj;
    ReleaseSRWLockShared(&pa_lock);
    if (!hit) return;
    AcquireSRWLockExclusive(&pa_lock);
    i = po_find(obj);
    if (po_tab[i].obj == obj) {
        pa_ent *e = pa_get(po_tab[i].res);
        if (e && e->obj == obj) e->gone = pa_now() + 1;
        po_del_at(i);
    }
    ReleaseSRWLockExclusive(&pa_lock);
}

/* PASSO 100: o Azimuth chama refresh_title_texture (0x14014aef0) para cada title texture usada numa lista
   (ExecuteCommandList e outros 5 sites) -> "ultimo uso" do objeto. Leitura com a trava compartilhada. */
#define PA_REFRESH_RVA 0x14aef0
static void (*pa_orig_refresh)(void *ctx, void *obj);
void pa_obj_used(const void *obj)
{
    pa_ent *e;
    uint32_t i;
    if (!po_n || !obj) return;
    AcquireSRWLockShared(&pa_lock);
    if (po_tab && po_tab[i = po_find(obj)].obj == obj && (e = pa_get(po_tab[i].res)) && e->obj == obj)
        e->used = pa_now() + 1;
    ReleaseSRWLockShared(&pa_lock);
}
/* 1 se o objeto e de uma orfa (PASSO 101): as paginas dela ja sao de outra textura */
int pa_obj_orphan(const void *obj)
{
    pa_ent *e;
    uint32_t i;
    int r = 0;
    if (!po_n || !obj) return 0;
    AcquireSRWLockShared(&pa_lock);
    if (po_tab && po_tab[i = po_find(obj)].obj == obj && (e = pa_get(po_tab[i].res)) && e->obj == obj) r = e->orphan;
    ReleaseSRWLockShared(&pa_lock);
    return r;
}
static void pa_refresh_hook(void *ctx, void *obj)
{
    if (pa_obj_orphan(obj)) {                         /* o envio escreveria na textura que mora ali agora */
        LONG k = InterlockedIncrement(&pa_orphan_uses);
        if (k <= 8 || (k & (k - 1)) == 0)
            wlog("d3d: PlacementReclaim: orphan placement refresh #%ld (obj %p) skipped", k, obj);
        pa_obj_used(obj);
        return;
    }
    pa_orig_refresh(ctx, obj);
    pa_obj_used(obj);
}

/* copia de uma entrada viva para o relatorio; len = tamanho aproximado no jogo (menor entre D3D12 e texels);
   covered = 1o segundo em que uma viva mais nova cruzou a faixa (+1; 0 = nao coberta) */
typedef struct { uint64_t addr, len, bytes; uint32_t seq, born, gone, caller, used, covered; uint16_t w, h;
                 uint8_t fmt, mips, flags; } pa_snap;
#define PA_OVER 1u        /* a faixa cruza a de uma textura viva mais nova */
#define PA_SAME 2u        /* mesmo endereco de uma textura viva mais nova */
#define PA_VIEW 4u        /* mesmo endereco e mesma largura/altura/mips de uma mais nova (outra view dos mesmos dados?) */
#define PA_OLD_S 300u     /* "antiga" = 5 min */
#define PA_GONE_S 10u

static int pa_snap_addr(const void *a, const void *b)
{
    const pa_snap *x = a, *y = b;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

typedef struct { uint64_t key, bytes; uint32_t n; } pa_tally;
static void pa_tally_add(pa_tally *t, int *nt, int cap, uint64_t key, uint64_t bytes)
{
    int i;
    for (i = 0; i < *nt && t[i].key != key; i++) {}
    if (i == *nt) { if (*nt == cap) return; t[i].key = key; t[i].bytes = 0; t[i].n = 0; (*nt)++; }
    t[i].bytes += bytes; t[i].n++;
}
static int pa_tally_top(const pa_tally *t, int nt)
{
    int i, b = -1;
    for (i = 0; i < nt; i++) if (t[i].n && (b < 0 || t[i].bytes > t[b].bytes)) b = i;
    return b;
}

/* marca sobrepostas/mesmo endereco (s em ordem de endereco) e devolve os bytes distintos cobertos */
uint64_t pa_mark_overlaps(pa_snap *s, uint32_t n)
{
    uint32_t i, j, k;
    uint64_t uni = 0, cs = 0, ce = 0;
    for (i = 0; i < n; i++) {
        uint64_t end;
        if (!s[i].addr || !s[i].len) continue;
        end = s[i].addr + s[i].len;
        for (j = i + 1, k = 0; j < n && s[j].addr < end && k < 4096; j++, k++) {
            pa_snap *older = s[j].seq < s[i].seq ? &s[j] : &s[i], *newer = older == &s[i] ? &s[j] : &s[i];
            older->flags |= PA_OVER;
            if (!older->covered || newer->born + 1 < older->covered) older->covered = newer->born + 1;
            if (s[j].addr == s[i].addr) {
                older->flags |= PA_SAME;
                if (s[j].w == s[i].w && s[j].h == s[i].h && s[j].mips == s[i].mips) older->flags |= PA_VIEW;
            }
        }
        if (s[i].addr >= ce) { uni += ce - cs; cs = s[i].addr; ce = end; }
        else if (end > ce) ce = end;
    }
    return uni + (ce - cs);
}

#define PA_MIB(b) ((double)(b) / 1048576.0)
/* duas linhas de texto (sem o prefixo) sobre as vivas; s e reordenado por endereco */
void pa_live_lines(pa_snap *s, uint32_t n, uint32_t now, char *l1, char *l2)
{
    static const uint32_t age_min[] = { 0, 1, 5, 15, 30, 60 };
    static const uint64_t sz_max[] = { 65536, 1u << 20, 4u << 20, ~0ull };
    static const char *const sz_name[] = { "<=64K", "<=1M", "<=4M", ">4M" };
    uint64_t age_b[6] = { 0 }, sz_b[4] = { 0 }, obj_b[4] = { 0 }, total = 0, over_b = 0, same_b = 0, held_b = 0, sum_len = 0, uni;
    uint32_t age_n[6] = { 0 }, sz_n[4] = { 0 }, obj_n[4] = { 0 }, over_n = 0, same_n = 0, held_n = 0, with_addr = 0, i, k;
    uint64_t use_b[3] = { 0 }, view_b = 0;
    uint32_t use_n[3] = { 0 }, view_n = 0;
    pa_tally callers[128], shapes[128];
    int nc = 0, ns = 0, len;
    qsort(s, n, sizeof *s, pa_snap_addr);
    uni = pa_mark_overlaps(s, n);
    for (i = 0; i < n; i++) {
        uint32_t age = now >= s[i].born ? now - s[i].born : 0, o;
        total += s[i].bytes;
        for (k = 5; k && age < age_min[k] * 60; k--) {}
        age_n[k]++; age_b[k] += s[i].bytes;
        for (k = 0; s[i].bytes > sz_max[k]; k++) {}
        sz_n[k]++; sz_b[k] += s[i].bytes;
        o = !s[i].addr ? 3 : !s[i].gone ? 0 : now + 1 - s[i].gone < PA_GONE_S ? 1 : 2;
        obj_n[o]++; obj_b[o] += s[i].bytes;
        if (s[i].addr) { with_addr++; sum_len += s[i].len; }
        if (s[i].flags & PA_OVER) { over_n++; over_b += s[i].bytes; }
        if (s[i].flags & PA_SAME) { same_n++; same_b += s[i].bytes; }
        if ((s[i].flags & PA_OVER) && !s[i].gone) {
            /* 0 usada depois de coberta, 1 usada so antes, 2 nunca usada */
            uint32_t u = !s[i].used ? 2 : s[i].used > s[i].covered ? 0 : 1;
            held_n++; held_b += s[i].bytes;
            use_n[u]++; use_b[u] += s[i].bytes;
            if (s[i].flags & PA_VIEW) { view_n++; view_b += s[i].bytes; }
        }
        if (age >= PA_OLD_S && s[i].addr && !s[i].gone) {
            pa_tally_add(callers, &nc, 128, s[i].caller, s[i].bytes);
            pa_tally_add(shapes, &ns, 128, (uint64_t)s[i].w << 32 | (uint64_t)s[i].h << 16 | (uint64_t)s[i].fmt << 8 | s[i].mips,
                         s[i].bytes);
        }
    }
    len = sprintf(l1, "%u, %.0f MiB | age:", n, PA_MIB(total));
    for (k = 0; k < 6; k++)
        len += sprintf(l1 + len, " %s%u min %u/%.0f MiB", k == 5 ? ">=" : "<", k == 5 ? 60 : age_min[k + 1], age_n[k], PA_MIB(age_b[k]));
    len += sprintf(l1 + len, " | size:");
    for (k = 0; k < 4; k++) len += sprintf(l1 + len, " %s %u/%.0f MiB", sz_name[k], sz_n[k], PA_MIB(sz_b[k]));
    sprintf(l1 + len, " | game object: alive %u/%.0f MiB, released <%us %u/%.0f MiB, released longer ago %u/%.0f MiB, not annotated %u/%.0f MiB",
            obj_n[0], PA_MIB(obj_b[0]), PA_GONE_S, obj_n[1], PA_MIB(obj_b[1]), obj_n[2], PA_MIB(obj_b[2]), obj_n[3], PA_MIB(obj_b[3]));
    len = sprintf(l2, "address: %u annotated, cover %.0f of %.0f MiB (approx.); overlapped by a newer one %u/%.0f MiB (object "
                  "alive %u/%.0f MiB: used after being covered %u/%.0f MiB, only before %u/%.0f MiB, never %u/%.0f MiB; same shape "
                  "at the same address %u/%.0f MiB), same address %u/%.0f MiB | old (>=%u min, object alive) per caller:",
                  with_addr, PA_MIB(uni), PA_MIB(sum_len), over_n, PA_MIB(over_b), held_n, PA_MIB(held_b), use_n[0],
                  PA_MIB(use_b[0]), use_n[1], PA_MIB(use_b[1]), use_n[2], PA_MIB(use_b[2]), view_n, PA_MIB(view_b), same_n,
                  PA_MIB(same_b), PA_OLD_S / 60);
    for (k = 0; k < 6; k++) {
        int b = pa_tally_top(callers, nc);
        if (b < 0) break;
        len += sprintf(l2 + len, " game+%#llx %u/%.0f MiB;", (unsigned long long)callers[b].key, callers[b].n, PA_MIB(callers[b].bytes));
        callers[b].n = 0;
    }
    len += sprintf(l2 + len, " shapes:");
    for (k = 0; k < 6; k++) {
        int b = pa_tally_top(shapes, ns);
        uint64_t x;
        if (b < 0) break;
        x = shapes[b].key;
        len += sprintf(l2 + len, " %ux%u f%u m%u %u/%.0f MiB;", (unsigned)(x >> 32), (unsigned)(x >> 16) & 0xffff,
                       (unsigned)(x >> 8) & 0xff, (unsigned)x & 0xff, shapes[b].n, PA_MIB(shapes[b].bytes));
        shapes[b].n = 0;
    }
}

static void pa_live_report(const char *when)
{
    static char l1[1024], l2[1536];
    pa_snap *s;
    uint32_t n = 0, cap, i;
    if (!pa_on || !pa_track || !pa_tab) return;
    AcquireSRWLockShared(&pa_lock);
    cap = (uint32_t)pa_ntab + 16;
    s = malloc(cap * sizeof *s);
    for (i = 0; s && i < PA_TAB && n < cap; i++) {
        const pa_ent *e = &pa_tab[i];
        uint64_t b = (uint64_t)e->n * PA_PAGE;
        if (!e->res) continue;
        memset(&s[n], 0, sizeof s[n]);
        s[n].addr = e->addr; s[n].bytes = b; s[n].len = e->est && e->est < b ? e->est : b;
        s[n].seq = e->seq; s[n].born = e->born; s[n].gone = e->gone; s[n].caller = e->caller; s[n].used = e->used;
        s[n].w = e->w; s[n].h = e->h; s[n].fmt = e->fmt; s[n].mips = e->mips;
        n++;
    }
    ReleaseSRWLockShared(&pa_lock);
    if (!s) return;
    pa_live_lines(s, n, pa_now(), l1, l2);
    free(s);
    wlog("stutter: live placements %s: %s", when, l1);
    wlog("stutter: live placements %s: %s", when, l2);
}

/* a textura cabe no caminho placed? */
int pa_wants(const D3D12_HEAP_PROPERTIES *hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC *rd, const D3D12_CLEAR_VALUE *c)
{
    return hp && hp->Type == D3D12_HEAP_TYPE_DEFAULT && hf == D3D12_HEAP_FLAG_NONE && rd && !c &&
           (rd->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) &&
           !(rd->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) &&
           rd->SampleDesc.Count <= 1;
}

/* resumo para a linha "stutter: memoria" */
static int pa_format(char *out)
{
    if (!pa_on) return 0;
    int len = sprintf(out, " | proxy placed: %ld heaps of 64 MiB, %lld MiB used, %ld alive, +%ld -%ld, %ld as committed",
                      pa_nheaps, (long long)(pa_used_pages * PA_PAGE >> 20), pa_ntab, InterlockedExchange(&pa_made, 0),
                      InterlockedExchange(&pa_freed, 0), InterlockedExchange(&pa_fallback, 0));
    if (pa_reclaim_on)
        len += sprintf(out + len, ", orphans %ld (%lld MiB returned in total, %u in quarantine), orphan refresh skipped %ld",
                       pa_orphans, (long long)(pa_orphan_pages * PA_PAGE >> 20), pq_head - pq_tail, pa_orphan_uses);
    return len;
}

/* chamada de CreateCommittedResource que vira placed: so a do title_storage do Azimuth (retorno azimuth+0xe77c0) */
int pa_redirect(const void *ret, const D3D12_HEAP_PROPERTIES *hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC *rd,
                const D3D12_CLEAR_VALUE *c)
{
    return pa_on && ret == (const void *)(g_base + PA_RET_RVA) && pa_wants(hp, hf, rd, c);
}

/* S_OK com *o = recurso placed; senao HRESULT de falha (o chamador cai no committed) */
static HRESULT pa_create(ID3D12Device *d, const D3D12_RESOURCE_DESC *rd, D3D12_RESOURCE_STATES s, REFIID r, void **o)
{
    D3D12_RESOURCE_DESC desc = *rd;
    D3D12_RESOURCE_ALLOCATION_INFO ai;
    uint32_t n, al, page = 0;
    int hi;
    HRESULT hr;
    desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
    d->lpVtbl->GetResourceAllocationInfo(d, &ai, 0, 1, &desc);
    if (ai.SizeInBytes == UINT64_MAX || ai.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
        desc.Alignment = 0;
        d->lpVtbl->GetResourceAllocationInfo(d, &ai, 0, 1, &desc);
    }
    if (ai.SizeInBytes == UINT64_MAX || !ai.SizeInBytes || ai.SizeInBytes > PA_MAX_RES ||
        ai.Alignment > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT || ai.Alignment < PA_PAGE)
        return E_NOTIMPL;
    n = (uint32_t)((ai.SizeInBytes + PA_PAGE - 1) / PA_PAGE);
    al = (uint32_t)(ai.Alignment / PA_PAGE);
    AcquireSRWLockExclusive(&pa_lock);
    if (pa_reclaim_on) {
        uint64_t now_ms = GetTickCount64();
        if (pa_t_on && !pa_t_res) pa_reclaim(pa_t_addr, pa_min_bytes(rd), now_ms);
        if (pq_ring) pq_drain(now_ms, 0);
    }
    hi = pa_reserve(n, al, &page);
    ReleaseSRWLockExclusive(&pa_lock);
    if (hi < 0) {                                     /* heap novo (fora da trava: criar leva ms) */
        D3D12_HEAP_DESC hd;
        ID3D12Heap *h = NULL;
        pa_heap *H;
        memset(&hd, 0, sizeof hd);
        hd.SizeInBytes = PA_HEAP_BYTES;
        hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
        if (pa_nheaps >= PA_MAX_HEAPS || !(H = calloc(1, sizeof *H))) return E_OUTOFMEMORY;
        hr = real_heap(d, &hd, &IID_ID3D12Heap, (void **)&h);
        if (FAILED(hr)) { free(H); return hr; }
        H->h = h; H->free_pages = PA_PAGES;
        AcquireSRWLockExclusive(&pa_lock);
        if (pa_nheaps < PA_MAX_HEAPS) { pa_heaps[pa_nheaps] = H; pa_nheaps++; H = NULL; }
        hi = pa_reserve(n, al, &page);
        ReleaseSRWLockExclusive(&pa_lock);
        if (H) { h->lpVtbl->Release(h); free(H); }
        if (hi < 0) return E_OUTOFMEMORY;
    }
    hr = real_placed(d, pa_heaps[hi]->h, (UINT64)page * PA_PAGE, &desc, s, NULL, r, o);
    AcquireSRWLockExclusive(&pa_lock);
    if (FAILED(hr) || !o || !*o) pa_set(pa_heaps[hi], page, n, 0);
    else pa_describe(pa_put(*o, (uint32_t)hi, page, n), *o, rd);
    ReleaseSRWLockExclusive(&pa_lock);
    if (FAILED(hr)) return hr;
    InterlockedAdd64(&pa_used_pages, n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE st_committed(ID3D12Device *d, const D3D12_HEAP_PROPERTIES *hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC *rd,
                                              D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE *c, REFIID r, void **o)
{
    HRESULT hr;
    LONG64 t, dt;
    if (o && pa_redirect(__builtin_return_address(0), hp, hf, rd, c)) {
        hr = pa_create(d, rd, s, r, o);
        if (SUCCEEDED(hr)) {
            InterlockedIncrement(&pa_made);
            rk_hook_release(*o);
            mr_add(*o, MR_PA, mr_res_bytes(rd));
            return hr;
        }
        InterlockedIncrement(&pa_fallback);
    }
    t = qpc();
    hr = real_committed(d, hp, hf, rd, s, c, r, o);
    dt = qpc() - t;
    st_count(ST_RES, dt);
    InterlockedAdd64(&st_bytes, (LONG64)st_res_bytes(rd));
    rk_note(RK_COMMITTED, dt, rd, 0, 0);
    if (SUCCEEDED(hr) && o) {
        rk_hook_release(*o);
        mr_add(*o, mr_heap_kind(hp ? hp->Type : D3D12_HEAP_TYPE_DEFAULT, 0), mr_res_bytes(rd));
        if (st_on) cx_created(CX_COMMITTED, *o, rd, 0);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_placed(ID3D12Device *d, ID3D12Heap *h, UINT64 off, const D3D12_RESOURCE_DESC *rd, D3D12_RESOURCE_STATES s,
                                           const D3D12_CLEAR_VALUE *c, REFIID r, void **o)
{
    HRESULT hr;
    LONG64 t = qpc(), dt;
    hr = real_placed(d, h, off, rd, s, c, r, o);
    dt = qpc() - t;
    st_count(ST_RES, dt);
    rk_note(RK_PLACED, dt, rd, 0, 0);
    if (SUCCEEDED(hr) && o) { rk_hook_release(*o); mr_add(*o, MR_PLACED, 0); if (st_on) cx_created(CX_PLACED, *o, rd, 0); }
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_reserved(ID3D12Device *d, const D3D12_RESOURCE_DESC *rd, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE *c,
                                             REFIID r, void **o)
{
    HRESULT hr;
    LONG64 t = qpc(), dt;
    hr = real_reserved(d, rd, s, c, r, o);
    dt = qpc() - t;
    st_count(ST_RES, dt);
    rk_note(RK_RESERVED, dt, rd, 0, 0);
    if (SUCCEEDED(hr) && o) { rk_hook_release(*o); mr_add(*o, MR_RESERVED, 0); if (st_on) cx_created(CX_RESERVED, *o, rd, 0); }
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_heap(ID3D12Device *d, const D3D12_HEAP_DESC *hd, REFIID r, void **o)
{
    HRESULT hr;
    LONG64 t = qpc(), dt;
    hr = real_heap(d, hd, r, o);
    dt = qpc() - t;
    st_count(ST_RES, dt);
    if (hd) InterlockedAdd64(&st_bytes, (LONG64)hd->SizeInBytes);
    rk_note(RK_HEAP, dt, NULL, hd ? hd->SizeInBytes : 0, hd ? (uint32_t)hd->Flags : 0);
    if (SUCCEEDED(hr) && o) {
        rk_hook_release(*o);
        mr_add(*o, mr_heap_kind(hd ? hd->Properties.Type : D3D12_HEAP_TYPE_DEFAULT, 1), hd ? hd->SizeInBytes : 0);
        if (st_on) cx_created(CX_HEAP, *o, NULL, hd ? hd->SizeInBytes : 0);
    }
    return hr;
}

/* ---- tempo por metodo da command list na thread 'Azimuth queue' (PASSO 73): a vtable da lista (a mesma de todas)
 * e trocada no 1o ExecuteCommandLists, por cima dos ganchos do gpuguard; so a thread da fila (vz_qtid, achada pelo
 * nome no qp_tick) soma. Linha "stutter: lista na fila 10 s" no vz_poll. ---- */
enum { LT_RESET, LT_CLOSE, LT_HEAPS, LT_DRAW, LT_DRAWIDX, LT_PSO, LT_BARRIER, LT_GRS, LT_GTAB, LT_OMRT, LT_VB, LT_GCBV, LT_COPYBUF,
       LT_COPYTEX, LT_CLEAR, LT_N };
static const char *const lt_names[LT_N] = { "Reset", "Close", "SetDescriptorHeaps", "DrawInstanced", "DrawIndexedInstanced",
                                            "SetPipelineState", "ResourceBarrier", "SetGraphicsRootSignature",
                                            "SetGraphicsRootDescriptorTable", "OMSetRenderTargets", "IASetVertexBuffers",
                                            "SetGraphicsRootConstantBufferView", "CopyBufferRegion", "CopyTextureRegion",
                                            "ClearRenderTargetView" };
static volatile DWORD vz_qtid;
static volatile LONG lt_n[LT_N];
static volatile LONG64 lt_ticks[LT_N];

static void lt_add(int k, LONG64 t0)
{
    if (GetCurrentThreadId() != vz_qtid) return;
    lt_ticks[k] += qpc() - t0;     /* uma thread so (a fila): sem Interlocked */
    lt_n[k]++;
}

#define LT_WRAP(name, k, params, args)     static void (STDMETHODCALLTYPE *lt_real_##name) params;     static void STDMETHODCALLTYPE lt_##name params { LONG64 t = qpc(); lt_real_##name args; lt_add(k, t); }
typedef ID3D12GraphicsCommandList GCL;
static HRESULT (STDMETHODCALLTYPE *lt_real_Reset)(GCL *, ID3D12CommandAllocator *, ID3D12PipelineState *);
static HRESULT STDMETHODCALLTYPE lt_Reset(GCL *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{ LONG64 t = qpc(); HRESULT hr = lt_real_Reset(l, a, ps); lt_add(LT_RESET, t); return hr; }
static HRESULT (STDMETHODCALLTYPE *lt_real_Close)(GCL *);
static HRESULT STDMETHODCALLTYPE lt_Close(GCL *l)
{ LONG64 t = qpc(); HRESULT hr = lt_real_Close(l); lt_add(LT_CLOSE, t); return hr; }
LT_WRAP(Heaps, LT_HEAPS, (GCL *l, UINT n, ID3D12DescriptorHeap *const *h), (l, n, h))
LT_WRAP(Draw, LT_DRAW, (GCL *l, UINT a, UINT b, UINT c, UINT d), (l, a, b, c, d))
LT_WRAP(DrawIdx, LT_DRAWIDX, (GCL *l, UINT a, UINT b, UINT c, INT d, UINT e), (l, a, b, c, d, e))
LT_WRAP(Pso, LT_PSO, (GCL *l, ID3D12PipelineState *p), (l, p))
LT_WRAP(Barrier, LT_BARRIER, (GCL *l, UINT n, const D3D12_RESOURCE_BARRIER *b), (l, n, b))
LT_WRAP(Grs, LT_GRS, (GCL *l, ID3D12RootSignature *r), (l, r))
LT_WRAP(Gtab, LT_GTAB, (GCL *l, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h), (l, i, h))
LT_WRAP(OmRt, LT_OMRT, (GCL *l, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE *r, BOOL s, const D3D12_CPU_DESCRIPTOR_HANDLE *d), (l, n, r, s, d))
LT_WRAP(Vb, LT_VB, (GCL *l, UINT s, UINT n, const D3D12_VERTEX_BUFFER_VIEW *v), (l, s, n, v))
LT_WRAP(Gcbv, LT_GCBV, (GCL *l, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a), (l, i, a))
LT_WRAP(CopyBuf, LT_COPYBUF, (GCL *l, ID3D12Resource *d, UINT64 o, ID3D12Resource *s, UINT64 so, UINT64 n), (l, d, o, s, so, n))
LT_WRAP(CopyTex, LT_COPYTEX, (GCL *l, const D3D12_TEXTURE_COPY_LOCATION *d, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION *s,
                              const D3D12_BOX *b), (l, d, x, y, z, s, b))
LT_WRAP(Clear, LT_CLEAR, (GCL *l, D3D12_CPU_DESCRIPTOR_HANDLE r, const FLOAT c[4], UINT n, const D3D12_RECT *rc), (l, r, c, n, rc))

static void lt_hook(ID3D12CommandList *cl)
{
    static volatile LONG done;
    GCL *l = NULL;
    ID3D12GraphicsCommandListVtbl *v;
    if (done || !cl || rc_any(1, &cl) || InterlockedCompareExchange(&done, 1, 0)) return;   /* gravadora: vtable do proxy */
    if (FAILED(cl->lpVtbl->QueryInterface(cl, &IID_ID3D12GraphicsCommandList, (void **)&l))) return;
    v = l->lpVtbl;
    vt_swap((void **)&v->Reset, lt_Reset, (void **)&lt_real_Reset);
    vt_swap((void **)&v->Close, lt_Close, (void **)&lt_real_Close);
    vt_swap((void **)&v->SetDescriptorHeaps, lt_Heaps, (void **)&lt_real_Heaps);
    vt_swap((void **)&v->DrawInstanced, lt_Draw, (void **)&lt_real_Draw);
    vt_swap((void **)&v->DrawIndexedInstanced, lt_DrawIdx, (void **)&lt_real_DrawIdx);
    vt_swap((void **)&v->SetPipelineState, lt_Pso, (void **)&lt_real_Pso);
    vt_swap((void **)&v->ResourceBarrier, lt_Barrier, (void **)&lt_real_Barrier);
    vt_swap((void **)&v->SetGraphicsRootSignature, lt_Grs, (void **)&lt_real_Grs);
    vt_swap((void **)&v->SetGraphicsRootDescriptorTable, lt_Gtab, (void **)&lt_real_Gtab);
    vt_swap((void **)&v->OMSetRenderTargets, lt_OmRt, (void **)&lt_real_OmRt);
    vt_swap((void **)&v->IASetVertexBuffers, lt_Vb, (void **)&lt_real_Vb);
    vt_swap((void **)&v->SetGraphicsRootConstantBufferView, lt_Gcbv, (void **)&lt_real_Gcbv);
    vt_swap((void **)&v->CopyBufferRegion, lt_CopyBuf, (void **)&lt_real_CopyBuf);
    vt_swap((void **)&v->CopyTextureRegion, lt_CopyTex, (void **)&lt_real_CopyTex);
    vt_swap((void **)&v->ClearRenderTargetView, lt_Clear, (void **)&lt_real_Clear);
    l->lpVtbl->Release(l);
    wlog("stutter: command list method timing measured on the 'Azimuth queue' thread");
}

uint32_t vz_top(const uint64_t *v, uint32_t n, uint32_t *idx, uint32_t max);

/* " Reset 690 4.1 ms (5.9 us)," por quadro, so metodos chamados, do mais caro ao mais barato */
int lt_format(char *buf, int cap, double frames)
{
    uint64_t t[LT_N];
    uint32_t idx[LT_N], n, i;
    int len = 0;
    double total = 0;
    for (i = 0; i < LT_N; i++) { t[i] = (uint64_t)lt_ticks[i]; total += ms_of(lt_ticks[i]); }
    n = vz_top(t, LT_N, idx, LT_N);
    len += snprintf(buf + len, cap - len, "stutter: list on the queue 10 s (per frame): measured %.2f ms;", total / frames);
    for (i = 0; i < n && len < cap - 100; i++) {
        uint32_t k = idx[i];
        len += snprintf(buf + len, cap - len, " %s %.0f %.2f ms (%.1f us);", lt_names[k], lt_n[k] / frames, ms_of(lt_ticks[k]) / frames,
                        lt_n[k] ? ms_of(lt_ticks[k]) * 1000 / lt_n[k] : 0);
    }
    for (i = 0; i < LT_N; i++) { lt_ticks[i] = 0; lt_n[i] = 0; }
    return n ? len : 0;
}

/* ---- tempo de GPU por grupo de listas na fila direct (PASSO 83, com StutterLog; debug.ini GpuSlowMs, padrao 50,
 * 0 = desligado). No trecho da queda do fim da corrida UMA submissao leva 1-1.6 s (passando de ~2 s: device reset).
 * Cada ExecuteCommandLists da fila direct vira, na mesma chamada, [ts, grupo 1, ts, grupo 2, ..., ts]: ate GT_GROUPS
 * grupos de listas seguidas, cada ts uma lista pre-gravada (EndQuery TIMESTAMP + ResolveQueryData do seu indice no
 * buffer de leitura). Concluido (o ultimo ts do ECL escrito), loga o grupo que passou de GpuSlowMs com o resumo das
 * listas tirado na hora do envio (gg_trace_brief: draws e pontos com mais ocorrencias, hash do PSO) e o buraco da
 * GPU entre o fim do ECL anterior e o inicio deste (Wait de fila / fila vazia). ---- */
static int sg_is_direct(ID3D12CommandQueue *q);
#define GT_TS     1024       /* listas de timestamp / indices da query heap */
#define GT_GROUPS 32
#define GT_ENT    256
#define GT_TOP    6
typedef struct { void *cl0; uint32_t nl, nops, ntop, top[GT_TOP][5]; } gt_group;
typedef struct { uint32_t ts0, ng, seq, n; LONG64 qpc; gt_group g[GT_GROUPS]; } gt_ent;
extern uint32_t gg_trace_brief(void *cl, uint32_t *nops, uint32_t (*top)[5], uint32_t max);
extern const char *gg_op_str(uint32_t op);
int gt_slow_ms;
static int gt_state;                         /* 0 = a criar, 1 = pronto, -1 = falhou */
static ID3D12GraphicsCommandList *gt_lists[GT_TS];
static volatile UINT64 *gt_rb;               /* buffer de leitura mapeado: GT_TS valores */
static uint8_t gt_busy[GT_TS];
static UINT64 gt_freq, gt_prev_end;
static gt_ent gt_ring[GT_ENT];
static uint32_t gt_head, gt_tail, gt_next_ts, gt_seq;
static SRWLOCK gt_lock = SRWLOCK_INIT;
static ID3D12CommandList *gt_buf[4096 + GT_GROUPS + 1];

static int gt_init(ID3D12CommandQueue *q)
{
    ID3D12Device *dev = NULL;
    ID3D12QueryHeap *qh = NULL;
    ID3D12Resource *rb = NULL;
    ID3D12CommandAllocator *al = NULL;
    D3D12_QUERY_HEAP_DESC qd = { D3D12_QUERY_HEAP_TYPE_TIMESTAMP, GT_TS, 0 };
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, GT_TS * 8, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 },
                               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    void *p = NULL;
    int i;
    if (FAILED(q->lpVtbl->GetTimestampFrequency(q, &gt_freq)) || !gt_freq ||
        FAILED(q->lpVtbl->GetDevice(q, &IID_ID3D12Device, (void **)&dev))) return -1;
    if (FAILED(dev->lpVtbl->CreateQueryHeap(dev, &qd, &IID_ID3D12QueryHeap, (void **)&qh)) ||
        FAILED(dev->lpVtbl->CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                    &IID_ID3D12Resource, (void **)&rb)) ||
        FAILED(rb->lpVtbl->Map(rb, 0, NULL, &p)) ||
        FAILED(dev->lpVtbl->CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&al))) {
        dev->lpVtbl->Release(dev);
        return -1;
    }
    gt_rb = p;
    for (i = 0; i < GT_TS; i++) {
        ID3D12GraphicsCommandList *l = NULL;
        if (FAILED(dev->lpVtbl->CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, NULL,
                                                  &IID_ID3D12GraphicsCommandList, (void **)&l))) {
            dev->lpVtbl->Release(dev);
            return -1;
        }
        l->lpVtbl->EndQuery(l, qh, D3D12_QUERY_TYPE_TIMESTAMP, i);
        l->lpVtbl->ResolveQueryData(l, qh, D3D12_QUERY_TYPE_TIMESTAMP, i, 1, rb, (UINT64)i * 8);
        l->lpVtbl->Close(l);
        gt_lists[i] = l;
        gt_rb[i] = 0;
    }
    dev->lpVtbl->Release(dev);   /* query heap, buffer, allocator e listas ficam ate o fim */
    wlog("stutter: GPU time per list group on the direct queue (logs above %d ms; %d timestamps)", gt_slow_ms, GT_TS);
    return 1;
}

static double gt_ms(UINT64 a, UINT64 b) { return b > a ? (double)(b - a) * 1000.0 / (double)gt_freq : 0.0; }

/* sob gt_lock: consome os ECLs concluidos em ordem e loga os lentos */
static void gt_poll(void)
{
    while (gt_tail != gt_head) {
        gt_ent *e = &gt_ring[gt_tail % GT_ENT];
        uint32_t i, k;
        UINT64 t[GT_GROUPS + 1];
        if (!gt_rb[(e->ts0 + e->ng) % GT_TS]) return;
        for (i = 0; i <= e->ng; i++) t[i] = gt_rb[(e->ts0 + i) % GT_TS];
        if (gt_prev_end && gt_ms(gt_prev_end, t[0]) >= gt_slow_ms)
            wlog("stutter: GPU idle %.1f ms BEFORE ECL #%u (direct queue; queue Wait or nothing submitted)",
                 gt_ms(gt_prev_end, t[0]), e->seq);
        for (i = 0; i < e->ng; i++) {
            const gt_group *g = &e->g[i];
            char buf[1024];
            int len;
            double ms = gt_ms(t[i], t[i + 1]);
            if (ms < gt_slow_ms) continue;
            len = snprintf(buf, sizeof buf, "stutter: GPU slow %.1f ms in ECL #%u (%u lists; group %u of %u: %u lists starting "
                           "at %p, %u operations, whole ECL %.1f ms, submitted %.0f ms ago):", ms, e->seq, e->n, i + 1, e->ng,
                           g->nl, g->cl0, g->nops, gt_ms(t[0], t[e->ng]), ms_of(qpc() - e->qpc));
            for (k = 0; k < g->ntop && len < (int)sizeof buf - 90; k++)
                len += snprintf(buf + len, sizeof buf - len, " [pso %08x x%u %s (%u,%u)]", g->top[k][0], g->top[k][1],
                                gg_op_str(g->top[k][2]), g->top[k][3], g->top[k][4]);
            wlog("%s", buf);
        }
        gt_prev_end = t[e->ng];
        for (i = 0; i <= e->ng; i++) { gt_rb[(e->ts0 + i) % GT_TS] = 0; gt_busy[(e->ts0 + i) % GT_TS] = 0; }
        gt_tail++;
    }
}

static int gt_group_str(char *buf, int cap, const gt_group *g)
{
    int len = snprintf(buf, cap, "%u lists starting at %p, %u operations:", g->nl, g->cl0, g->nops);
    uint32_t k;
    for (k = 0; k < g->ntop && len < cap - 90; k++)
        len += snprintf(buf + len, cap - len, " [pso %08x x%u %s (%u,%u)]", g->top[k][0], g->top[k][1],
                        gg_op_str(g->top[k][2]), g->top[k][3], g->top[k][4]);
    return len;
}

/* na queda (dred_watch, depois do DRED): os ECLs medidos ainda em voo - ate onde a GPU chegou em cada um. O grupo
   com o timestamp de inicio e sem o de fim e o que travou; sem nenhum timestamp do 1o ECL pendente, a GPU parou
   antes dele (espera de fila/fence) */
void gt_dump(void)
{
    uint32_t e0, shown = 0;
    int locked = TryAcquireSRWLockExclusive(&gt_lock);
    if (gt_state <= 0) { if (locked) ReleaseSRWLockExclusive(&gt_lock); return; }
    wlog("stutter: GPU time: %u measured ECLs in flight at the crash (end of the last completed: tick %llu)", gt_head - gt_tail,
         (unsigned long long)gt_prev_end);
    for (e0 = gt_tail; e0 != gt_head && shown < 8; e0++, shown++) {
        const gt_ent *e = &gt_ring[e0 % GT_ENT];
        char buf[1024];
        uint32_t i;
        UINT64 t0 = gt_rb[e->ts0 % GT_TS];
        if (!t0) {
            wlog("stutter:  ECL #%u (%u lists in %u groups, submitted %.0f ms ago): the GPU DID NOT START%s", e->seq, e->n, e->ng,
                 ms_of(qpc() - e->qpc), e0 == gt_tail ? " -> stopped BEFORE it (queue/fence wait or the previous one)" : "");
            if (e0 == gt_tail) {
                gt_group_str(buf, sizeof buf, &e->g[0]);
                wlog("stutter:   1st group: %s", buf);
            }
            continue;
        }
        for (i = 0; i < e->ng; i++) {
            UINT64 a = gt_rb[(e->ts0 + i) % GT_TS], b = gt_rb[(e->ts0 + i + 1) % GT_TS];
            if (a && b) continue;
            gt_group_str(buf, sizeof buf, &e->g[i]);
            wlog("stutter:  ECL #%u (%u lists, submitted %.0f ms ago): group %u of %u STARTED (%.1f ms after the end of the previous one) "
                 "AND DID NOT FINISH: %s", e->seq, e->n, ms_of(qpc() - e->qpc), i + 1, e->ng, gt_prev_end ? gt_ms(gt_prev_end, a) : 0.0,
                 buf);
            break;
        }
    }
    if (locked) ReleaseSRWLockExclusive(&gt_lock);
}

/* resume as listas [a, b) no grupo: soma das operacoes e os GT_TOP pontos com mais ocorrencias entre elas */
static void gt_group_fill(gt_group *g, ID3D12CommandList *const *l, UINT a, UINT b)
{
    UINT i;
    g->cl0 = l[a]; g->nl = b - a; g->nops = 0; g->ntop = 0;
    for (i = a; i < b; i++) {
        uint32_t top[GT_TOP][5], nops, n = gg_trace_brief(l[i], &nops, top, GT_TOP), j, k, m;
        g->nops += nops;
        for (j = 0; j < n; j++) {
            for (k = 0; k < g->ntop && g->top[k][1] >= top[j][1]; k++) {}
            if (k >= GT_TOP) continue;
            if (g->ntop < GT_TOP) g->ntop++;
            for (m = g->ntop - 1; m > k; m--) memcpy(g->top[m], g->top[m - 1], sizeof *g->top);
            memcpy(g->top[k], top[j], sizeof *top);
        }
    }
}

/* ECL da fila direct com timestamps; 0 = nao medido (o chamador envia normal) */
static int gt_ecl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l)
{
    uint32_t ng, per, i, k = 0;
    gt_ent *e;
    if (!n || n > 4096) return 0;
    AcquireSRWLockExclusive(&gt_lock);
    if (!gt_state) gt_state = gt_init(q);
    if (gt_state < 0) { ReleaseSRWLockExclusive(&gt_lock); return 0; }
    gt_poll();
    per = (n + GT_GROUPS - 1) / GT_GROUPS;
    ng = (n + per - 1) / per;
    for (i = 0; i <= ng; i++)
        if (gt_busy[(gt_next_ts + i) % GT_TS]) break;
    if (i <= ng || gt_head - gt_tail >= GT_ENT) { ReleaseSRWLockExclusive(&gt_lock); return 0; }   /* anel cheio: sem medir */
    e = &gt_ring[gt_head % GT_ENT];
    e->ts0 = gt_next_ts; e->ng = ng; e->n = n; e->seq = ++gt_seq; e->qpc = qpc();
    for (i = 0; i < ng; i++) {
        UINT a = i * per, b = a + per < n ? a + per : n;
        gt_busy[(e->ts0 + i) % GT_TS] = 1;
        gt_buf[k++] = (ID3D12CommandList *)gt_lists[(e->ts0 + i) % GT_TS];
        memcpy(gt_buf + k, l + a, (b - a) * sizeof *l);
        k += b - a;
        gt_group_fill(&e->g[i], l, a, b);
    }
    gt_busy[(e->ts0 + ng) % GT_TS] = 1;
    gt_buf[k++] = (ID3D12CommandList *)gt_lists[(e->ts0 + ng) % GT_TS];
    gt_next_ts = (gt_next_ts + ng + 1) % GT_TS;
    gt_head++;
    lp_ecl(q, k, gt_buf);         /* sob a trava: a ordem do anel e a ordem na GPU */
    ReleaseSRWLockExclusive(&gt_lock);
    return 1;
}

static void STDMETHODCALLTYPE st_ecl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l)
{
    LONG64 t, dt;
    if (!st_on) { lp_ecl(q, n, l); return; }   /* so o fence do pool (ListPool sem StutterLog) */
    if (n) lt_hook(l[0]);
    t = qpc();
    if (!(gt_slow_ms > 0 && sg_is_direct(q) && gt_ecl(q, n, l)))
        lp_ecl(q, n, l);
    dt = qpc() - t;
    st_count(ST_ECL, dt);
    InterlockedAdd(&st_lists, (LONG)n);
    InterlockedIncrement(&vz_ecl);
    InterlockedAdd(&vz_lists, (LONG)n);
    InterlockedAdd64(&vz_ecl_ticks, dt);
}

/* frames de codigo (intervalos [lo, hi)) fora de [skip_lo, skip_hi), ate max: a chave da origem de uma lista aberta */
uint32_t vz_key(void *const *f, uint32_t n, const uintptr_t (*ranges)[2], uint32_t nranges, uintptr_t skip_lo,
                uintptr_t skip_hi, uintptr_t *out, uint32_t max)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < n && k < max; i++) {
        uintptr_t a = (uintptr_t)f[i];
        if (a >= skip_lo && a < skip_hi) continue;
        for (j = 0; j < nranges; j++)
            if (a >= ranges[j][0] && a < ranges[j][1]) { out[k++] = a; break; }
    }
    for (i = k; i < max; i++) out[i] = 0;
    return k;
}

/* gpuguard (Reset e criacao de command list): conta; 1 em VZ_EVERY com a origem (pilha sem o 0x1400dee90 do Azimuth,
 * que e quem abre todas) */
#define VZ_OPEN_LO 0xdee90
#define VZ_OPEN_HI 0xdf220
void st_list_open(void)
{
    void *f[10];
    uintptr_t key[VZ_KEY], ranges[2][2];
    uint8_t *game = g_game;
    uint32_t n, i, nr = 0;
    if (!st_on || InterlockedIncrement(&vz_opens) % VZ_EVERY) return;
    n = RtlCaptureStackBackTrace(2, 10, f, NULL);
    if (game) { ranges[nr][0] = (uintptr_t)game; ranges[nr++][1] = (uintptr_t)game + 0x8000000; }
    ranges[nr][0] = (uintptr_t)g_base; ranges[nr++][1] = (uintptr_t)g_base + 0x1000000;
    vz_key(f, n, (const uintptr_t (*)[2])ranges, nr, (uintptr_t)g_base + VZ_OPEN_LO, (uintptr_t)g_base + VZ_OPEN_HI, key, VZ_KEY);
    AcquireSRWLockExclusive(&vz_lock);
    for (i = 0; i < VZ_ORIGINS && vz_orig[i].n && memcmp(vz_orig[i].k, key, sizeof key); i++) {}
    if (i == VZ_ORIGINS) vz_orig_other++;
    else { if (!vz_orig[i].n) memcpy(vz_orig[i].k, key, sizeof key); vz_orig[i].n++; }
    ReleaseSRWLockExclusive(&vz_lock);
}

/* gpuguard (draws e ExecuteIndirect): contador local da thread, somado de 64 em 64 (sem disputa por draw) */
void st_draw(void)
{
    static __thread LONG local;
    if (!st_on || ++local < 64) return;
    InterlockedAdd(&vz_draws, local);
    local = 0;
}

static void (STDMETHODCALLTYPE *real_tiles)(ID3D12CommandQueue *, ID3D12Resource *, UINT, const D3D12_TILED_RESOURCE_COORDINATE *,
                                            const D3D12_TILE_REGION_SIZE *, ID3D12Heap *, UINT, const D3D12_TILE_RANGE_FLAGS *,
                                            const UINT *, const UINT *, D3D12_TILE_MAPPING_FLAGS);
/* PASSO 68: perfil das UpdateTileMappings em janelas de 10 s (linha "stutter: tiles 10 s" junto do resumo) - flags
 * que chegam (com o tile_nohazard_patch: NO_HAZARD), desmapeamentos, tiles, recursos distintos e quem chama. */
#define TL_SEEN 16384
#define TL_CALLERS 4
static struct {
    uint32_t calls, flagged, unmaps, nulls, regions, distinct;
    uint64_t tiles;
    void *seen[TL_SEEN];
    struct { void *ret; uint32_t n; } callers[TL_CALLERS];
    uint32_t other_callers;
} tl;
static SRWLOCK tl_lock = SRWLOCK_INIT;

void tl_reset(void)
{
    AcquireSRWLockExclusive(&tl_lock);
    memset(&tl, 0, sizeof tl);
    ReleaseSRWLockExclusive(&tl_lock);
}

void tl_note(void *res, UINT nr, const D3D12_TILE_REGION_SIZE *sz, ID3D12Heap *h, UINT ng,
                                 const D3D12_TILE_RANGE_FLAGS *rf, const UINT *rc, D3D12_TILE_MAPPING_FLAGS fl, void *ret)
{
    UINT i;
    uint32_t slot = (uint32_t)(((uintptr_t)res >> 4) * 2654435761u) % TL_SEEN, k;
    AcquireSRWLockExclusive(&tl_lock);
    tl.calls++;
    tl.regions += nr;
    if (fl) tl.flagged++;
    if (!h) tl.unmaps++;
    for (i = 0; i < ng; i++) {
        if (rf && (rf[i] & D3D12_TILE_RANGE_FLAG_NULL)) tl.nulls++;
        tl.tiles += rc ? rc[i] : 1;
    }
    if (!ng) for (i = 0; i < nr; i++) tl.tiles += sz ? sz[i].NumTiles : 1;
    for (k = 0; k < TL_SEEN; k++, slot = (slot + 1) % TL_SEEN) {
        if (tl.seen[slot] == res) break;
        if (!tl.seen[slot]) { tl.seen[slot] = res; tl.distinct++; break; }
    }
    for (k = 0; k < TL_CALLERS; k++) {
        if (tl.callers[k].ret == ret) { tl.callers[k].n++; break; }
        if (!tl.callers[k].ret) { tl.callers[k].ret = ret; tl.callers[k].n = 1; break; }
    }
    if (k == TL_CALLERS) tl.other_callers++;
    ReleaseSRWLockExclusive(&tl_lock);
}

/* "stutter: tiles 10 s: 812 chamadas, 1300 regioes, 5120 tiles, 640 recursos distintos, flags 0 em 812, ..." */
int tl_format(char *buf, int cap)
{
    int len, k;
    if (!tl.calls) return 0;
    len = snprintf(buf, cap, "stutter: tiles 10 s: %u calls, %u regions, %llu tiles, %u distinct resources, %u with flags, "
                   "%u without heap, %u NULL ranges; callers:", tl.calls, tl.regions,
                   (unsigned long long)tl.tiles, tl.distinct, tl.flagged, tl.unmaps, tl.nulls);
    for (k = 0; k < TL_CALLERS && tl.callers[k].ret && len < cap - 40; k++) {
        char a[48];
        st_label(a, tl.callers[k].ret);
        if (!a[0]) wsprintfA(a, " %p", tl.callers[k].ret);
        len += snprintf(buf + len, cap - len, "%s x%u", a, tl.callers[k].n);
    }
    if (tl.other_callers && len < cap - 24) len += snprintf(buf + len, cap - len, " others x%u", tl.other_callers);
    return len;
}

static void STDMETHODCALLTYPE st_tiles(ID3D12CommandQueue *q, ID3D12Resource *res, UINT nr, const D3D12_TILED_RESOURCE_COORDINATE *co,
                                       const D3D12_TILE_REGION_SIZE *sz, ID3D12Heap *h, UINT ng, const D3D12_TILE_RANGE_FLAGS *rf,
                                       const UINT *hs, const UINT *rc, D3D12_TILE_MAPPING_FLAGS fl)
{
    LONG64 t;
    tl_note(res, nr, sz, h, ng, rf, rc, fl, __builtin_return_address(0));
    cx_tiles(res, nr, co, sz, h, ng, rf, hs, rc, fl);
    t = qpc();
    real_tiles(q, res, nr, co, sz, h, ng, rf, hs, rc, fl);
    rk_note(RK_TILES, qpc() - t, NULL, 0, nr);
}

static HRESULT (STDMETHODCALLTYPE *real_qwait)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);

static uint8_t sg_type(ID3D12CommandQueue *q)
{
    LONG i, n = sg_nqueues;
    for (i = 0; i < n && i < SG_QUEUES; i++) if (sg_queues[i].q == q) return sg_queues[i].type;
    return 0;
}

/* fila registrada no CreateCommandQueue como direct (uma desconhecida nao conta) */
static int sg_is_direct(ID3D12CommandQueue *q)
{
    LONG i, n = sg_nqueues;
    for (i = 0; i < n && i < SG_QUEUES; i++) if (sg_queues[i].q == q) return sg_queues[i].type == D3D12_COMMAND_LIST_TYPE_DIRECT;
    return 0;
}

static HRESULT STDMETHODCALLTYPE st_signal(ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 v)
{
    HRESULT hr = real_signal(q, f, v);
    if (SUCCEEDED(hr) && f) {
        st_sig *e = &sigs[(uint32_t)InterlockedIncrement(&sig_seq) % SG_RING];
        if (InterlockedCompareExchange(&e->state, 1, 0) == 0) {
            f->lpVtbl->AddRef(f);                 /* solto pelo amostrador quando concluir */
            e->f = f; e->v = v; e->t_sig = qpc(); e->qt = sg_type(q);
            InterlockedExchange(&e->state, 2);
        } else {
            InterlockedIncrement(&sig_dropped);
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE st_qwait(ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 v)
{
    InterlockedIncrement(&sig_waits);
    return real_qwait(q, f, v);
}

/* thread do amostrador: conclusoes desde a ultima volta (anel em ordem de Signal, varrido inteiro a partir do mais
 * antigo pendente; a cauda so anda enquanto nao houver pendente antes) */
static void sg_poll(LONG64 now)
{
    uint32_t i, k, pend = 0;
    ID3D12Fence *lastf = NULL;
    UINT64 lastv = 0;
    for (k = 0, i = sig_tail; k < SG_RING; k++, i = (i + 1) % SG_RING) {
        st_sig *e = &sigs[i];
        UINT64 done;
        if (e->state != 2) {
            if (!pend && e->state == 0) sig_tail = (i + 1) % SG_RING;
            continue;
        }
        if (e->f != lastf) { lastf = e->f; lastv = e->f->lpVtbl->GetCompletedValue(e->f); }
        done = lastv;
        if (done != UINT64_MAX && done < e->v) { pend++; continue; }
        {
            LONG64 seg = st_gpu_segment(e->t_sig, sig_last_done[e->qt & 3], now);
            sig_last_done[e->qt & 3] = now;
            gpu_sum_ticks += seg;
            vz_gpu[e->qt & 3] += seg;
            gpu_done++;
            if (seg > gpu_max_ticks) { gpu_max_ticks = seg; gpu_max_val = (LONG64)e->v; gpu_max_qt = e->qt; }
        }
        e->f->lpVtbl->Release(e->f);
        e->f = NULL;
        InterlockedExchange(&e->state, 0);
        if (!pend) sig_tail = (i + 1) % SG_RING;
    }
    gpu_pend = (LONG)pend;
}

static HRESULT STDMETHODCALLTYPE st_queue(ID3D12Device *d, const D3D12_COMMAND_QUEUE_DESC *qd, REFIID r, void **o)
{
    HRESULT hr = real_queue(d, qd, r, o);
    ID3D12CommandQueue *q = NULL;
    if (SUCCEEDED(hr) && o && *o && SUCCEEDED(((IUnknown *)*o)->lpVtbl->QueryInterface(*o, &IID_ID3D12CommandQueue, (void **)&q))) {
        LONG k = sg_nqueues;
        if (k < SG_QUEUES) {
            sg_queue *s = &sg_queues[k];
            s->q = q; s->type = qd ? (uint8_t)(qd->Type & 3) : 0;
            InitializeSRWLock(&s->llock);
            if (FAILED(d->lpVtbl->CreateFence(d, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s->lf))) s->lf = NULL;
            sg_nqueues = k + 1;
        }
        vt_swap((void **)&q->lpVtbl->ExecuteCommandLists, st_ecl, (void **)&real_ecl);
        if (st_on) {
            vt_swap((void **)&q->lpVtbl->Signal, st_signal, (void **)&real_signal);
            vt_swap((void **)&q->lpVtbl->Wait, st_qwait, (void **)&real_qwait);
            vt_swap((void **)&q->lpVtbl->UpdateTileMappings, st_tiles, (void **)&real_tiles);
        }
        q->lpVtbl->Release(q);
    }
    return hr;
}

/* ---- pool de command lists (PASSO 74): o Azimuth abre cada gravacao (contexto diferido / execucao na fila) com um
 * slot de lista vazio e 0x1400dee90 chama CreateCommandList - 524288 listas criadas numa sessao (553 vivas), o custo
 * por lista do trecho denso (driver AMD). Com o pool a ultima Release de uma command list a guarda (a do pool e a
 * unica referencia que sobra) e o CreateCommandList seguinte do mesmo tipo devolve uma guardada com Reset(allocator,
 * PSO). O Reset logo depois do ExecuteCommandLists e legal, mas a lista nao pode ser ENVIADA de novo antes da GPU
 * terminar a execucao anterior (debug layer: erro 553, envio descartado): a guardada leva os valores enviados do
 * fence do proxy de cada fila (lp_ecl) e so volta quando todos passaram; fila FIFO (a mais antiga primeiro).
 * Ganchos no device sempre (o device nasce antes do apply_patches); lp_on decidido em list_pool_setup: FM6 sim,
 * debug.ini ListPool=0/1 sobrescreve. ---- */
#define LP_MAX 1024
int lp_on;
typedef struct { IUnknown *u; UINT64 sent[SG_QUEUES]; } lp_ent;
static lp_ent lp_pool[4][LP_MAX];
static LONG lp_n[4], lp_head[4];                 /* lp_n guardadas a partir de lp_head (anel) */
static SRWLOCK lp_lock = SRWLOCK_INIT;
static ULONG (STDMETHODCALLTYPE *lp_real_release)(IUnknown *);
static HRESULT (STDMETHODCALLTYPE *lp_real_create)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
                                                   ID3D12PipelineState *, REFIID, void **);
static volatile LONG lp_made, lp_reused, lp_kept, lp_failed;
static volatile LONG64 lp_made_ticks;
extern int gg_cl_forget(void *cl);
extern int gg_ours(const void *ret);

/* guarda u (ja com so a referencia do pool) se houver lugar; devolve 1 se guardou */
int lp_put(IUnknown *u, unsigned type)
{
    int ok = 0;
    LONG i, nq = sg_nqueues;
    if (type >= 4) return 0;
    AcquireSRWLockExclusive(&lp_lock);
    if (lp_n[type] < LP_MAX) {
        lp_ent *e = &lp_pool[type][(lp_head[type] + lp_n[type]++) % LP_MAX];
        e->u = u;
        for (i = 0; i < SG_QUEUES; i++) e->sent[i] = i < nq ? sg_queues[i].lsent : 0;
        ok = 1;
    }
    ReleaseSRWLockExclusive(&lp_lock);
    return ok;
}

/* a mais antiga, se a GPU ja passou de todos os envios feitos ate ela ser guardada; senao NULL (lista nova) */
IUnknown *lp_take(unsigned type)
{
    IUnknown *u = NULL;
    LONG i, nq = sg_nqueues;
    if (type >= 4) return NULL;
    AcquireSRWLockExclusive(&lp_lock);
    if (lp_n[type]) {
        lp_ent *e = &lp_pool[type][lp_head[type]];
        for (i = 0; i < nq && i < SG_QUEUES; i++)
            if (e->sent[i] && sg_queues[i].lf && sg_queues[i].lf->lpVtbl->GetCompletedValue(sg_queues[i].lf) < e->sent[i]) break;
        if (i >= nq || i >= SG_QUEUES) {
            u = e->u;
            lp_head[type] = (lp_head[type] + 1) % LP_MAX;
            lp_n[type]--;
        }
    }
    ReleaseSRWLockExclusive(&lp_lock);
    return u;
}

/* listas de outro modulo (gg_ours, common/gpuguard.c: o overlay do RTSS via d3d11on12) nao entram nem saem do pool */
static ULONG STDMETHODCALLTYPE lp_release(IUnknown *u)
{
    if (lp_on && gg_ours(__builtin_return_address(0))) {
        ULONG c = u->lpVtbl->AddRef(u);              /* sonda: 2 = a referencia do chamador e a ultima */
        if (c == 2 && lp_put(u, (unsigned)((ID3D12CommandList *)u)->lpVtbl->GetType((ID3D12CommandList *)u))) {
            lp_real_release(u);                       /* fica a do pool */
            InterlockedIncrement(&lp_kept);
            return 0;
        }
        lp_real_release(u);
    }
    return lp_real_release(u);
}

/* allocators compartilhados (abaixo) */
static void as_bind(void *l, void *a);
static HRESULT STDMETHODCALLTYPE as_close(ID3D12GraphicsCommandList *l);
static HRESULT STDMETHODCALLTYPE as_reset(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps);
static HRESULT (STDMETHODCALLTYPE *as_real_close)(ID3D12GraphicsCommandList *);
static HRESULT (STDMETHODCALLTYPE *as_real_reset)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *, ID3D12PipelineState *);

static HRESULT STDMETHODCALLTYPE lp_create(ID3D12Device *d, UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *a,
                                           ID3D12PipelineState *ps, REFIID riid, void **out)
{
    static volatile LONG hooked;
    IUnknown *u;
    LONG64 t;
    HRESULT hr;
    if (rc_wants(type, __builtin_return_address(0))) return rc_create(d, ps, riid, out);   /* gravadora (common/cmdrec.c) */
    u = lp_on && out && gg_ours(__builtin_return_address(0)) ? lp_take((unsigned)type) : NULL;
    if (u) {
        ID3D12GraphicsCommandList *l = (ID3D12GraphicsCommandList *)u;
        gg_cl_forget(u);                              /* estado por lista do gpuguard: como uma lista nova */
        hr = l->lpVtbl->Reset(l, a, ps);
        if (FAILED(hr)) { l->lpVtbl->Close(l); hr = l->lpVtbl->Reset(l, a, ps); }
        if (SUCCEEDED(hr)) hr = u->lpVtbl->QueryInterface(u, riid, out);
        lp_real_release(u);                           /* a do pool: sobra a do chamador (ou nenhuma -> destruida) */
        if (SUCCEEDED(hr)) { InterlockedIncrement(&lp_reused); return hr; }
        InterlockedIncrement(&lp_failed);
    }
    t = qpc();
    hr = lp_real_create(d, mask, type, a, ps, riid, out);
    InterlockedAdd64(&lp_made_ticks, qpc() - t);
    InterlockedIncrement(&lp_made);
    if (SUCCEEDED(hr) && out && *out) {
        as_bind(*out, a);
        if (!hooked && !InterlockedCompareExchange(&hooked, 1, 0)) {
            void **vt = *(void ***)*out;              /* a mesma vtable para todos os tipos (por cima do gpuguard) */
            vt_swap(&vt[2], (void *)lp_release, (void **)&lp_real_release);
            vt_swap(&vt[9], (void *)as_close, (void **)&as_real_close);
            vt_swap(&vt[10], (void *)as_reset, (void **)&as_real_reset);
        }
    }
    return hr;
}

/* ---- allocators compartilhados (PASSO 91): o Azimuth tira um command allocator do pool (0x1400dea50, com Reset)
 * para CADA abertura de command list e o devolve (0x1400ded10, com o fence do envio) quando a lista e enviada ou a
 * gravacao destruida. O FM6 abre 400-1200 listas por quadro -> milhares de allocators vivos com as listas em voo, e
 * no driver AMD cada allocator usado guarda ~1.1 MB (tools\d3d12_mem_probe.c) -> ~11 GB privados na rajada do fim da
 * 1a corrida e DEVICE_RESET ("Kernel memory failure"). Aqui as aberturas da MESMA thread reusam o mesmo allocator
 * (sem Reset) enquanto nenhuma lista dele estiver gravando (D3D12: uma lista gravando por allocator; listas fechadas
 * ou em voo podem conviver). Ele volta ao pool do Azimuth uma vez so, com o MAIOR fence das devolucoes, quando foi
 * aposentado (N aberturas, ou devolvido com a lista ainda gravando) e todas as aberturas devolveram.
 * debug.ini AllocatorShare=N (aberturas por allocator; 0/1 = desligado, padrao). PASSO 92: ligado no FM6 derrubou
 * o carregamento da corrida 2x (logo apos reexecucao de lista aninhada) e os allocators nao eram o grosso da memoria. ---- */
#define AS_ACQ_RVA 0xdea50
#define AS_RET_RVA 0xded10
#define AS_MAX 1024
#define AS_PER_THREAD 8
typedef struct {
    IUnknown *a;            /* referencia propria; NULL = livre */
    void *dev, *list;       /* list: a lista gravando com ele (Reset/CreateCommandList ate o Close) */
    DWORD tid;              /* so a thread dona o reusa */
    LONG users, uses;       /* entregues e nao devolvidos / aberturas desde que saiu do pool */
    UINT64 fence;
    int busy, retired;
} as_ent;
typedef void **(*as_acq_fn)(void **out, void *dev);
typedef void (*as_ret_fn)(void *dev, UINT64 fence, void **alloc);
static as_ent as_tab[AS_MAX];
static LONG as_hi;          /* entradas usadas abaixo deste indice */
static SRWLOCK as_lock = SRWLOCK_INIT;
static int as_on, as_budget;
static as_acq_fn as_orig_acq;
static as_ret_fn as_orig_ret;
static volatile LONG as_shared, as_fresh, as_pushed, as_live;

static as_ent *as_find(const void *a)
{
    LONG i;
    for (i = 0; i < as_hi; i++) if (as_tab[i].a == a) return &as_tab[i];
    return NULL;
}

/* tira a entrada (com a trava); devolve a referencia propria para o pool do Azimuth fora da trava */
static IUnknown *as_take(as_ent *e, void **dev, UINT64 *fence)
{
    IUnknown *a = e->a;
    *dev = e->dev; *fence = e->fence;
    memset(e, 0, sizeof *e);
    while (as_hi && !as_tab[as_hi - 1].a) as_hi--;
    InterlockedDecrement(&as_live);
    return a;
}

static void as_push(IUnknown *a, void *dev, UINT64 fence)
{
    void *p = a;
    if (!a) return;
    InterlockedIncrement(&as_pushed);
    as_orig_ret(dev, fence, &p);
}

static void **as_acquire(void **out, void *dev)
{
    DWORD tid = GetCurrentThreadId();
    as_ent *e, *cand = NULL, *oldest = NULL, *slot = NULL;
    IUnknown *old = NULL;
    void *odev = NULL;
    UINT64 ofence = 0;
    LONG i, mine = 0;
    if (!as_on) return as_orig_acq(out, dev);
    AcquireSRWLockExclusive(&as_lock);
    for (i = 0; i < as_hi; i++) {
        e = &as_tab[i];
        if (!e->a || e->retired || e->tid != tid || e->dev != dev) continue;
        mine++;
        if (!e->busy && !cand) cand = e;
        if (!oldest || e->uses > oldest->uses) oldest = e;
    }
    if (cand) {
        cand->busy = 1; cand->list = NULL;
        cand->users++;
        if (++cand->uses >= as_budget) cand->retired = 1;
        cand->a->lpVtbl->AddRef(cand->a);
        *out = cand->a;
        ReleaseSRWLockExclusive(&as_lock);
        InterlockedIncrement(&as_shared);
        return out;
    }
    ReleaseSRWLockExclusive(&as_lock);
    as_orig_acq(out, dev);                            /* pool do Azimuth (com Reset) ou CreateCommandAllocator */
    if (!*out) return out;
    InterlockedIncrement(&as_fresh);
    AcquireSRWLockExclusive(&as_lock);
    if (mine >= AS_PER_THREAD && oldest && oldest->a && !oldest->retired && oldest->tid == tid) {
        oldest->retired = 1;                          /* todas gravando: a mais usada sai quando as listas voltarem */
        if (!oldest->users) old = as_take(oldest, &odev, &ofence);
    }
    for (i = 0; i < AS_MAX; i++) if (!as_tab[i].a) { slot = &as_tab[i]; break; }
    if (slot) {                                       /* sem lugar: allocator comum, so do Azimuth */
        IUnknown *a = *out;
        a->lpVtbl->AddRef(a);
        slot->a = a; slot->dev = dev; slot->tid = tid;
        slot->users = 1; slot->uses = 1; slot->fence = 0; slot->busy = 1; slot->list = NULL;
        slot->retired = as_budget <= 1;
        if (slot - as_tab >= as_hi) as_hi = (LONG)(slot - as_tab) + 1;
        InterlockedIncrement(&as_live);
    }
    ReleaseSRWLockExclusive(&as_lock);
    as_push(old, odev, ofence);
    return out;
}

static void as_return(void *dev, UINT64 fence, void **alloc)
{
    IUnknown *a = alloc ? *alloc : NULL, *done = NULL;
    as_ent *e;
    void *pdev = NULL;
    UINT64 pfence = 0;
    if (!a) { as_orig_ret(dev, fence, alloc); return; }
    AcquireSRWLockExclusive(&as_lock);
    e = as_find(a);
    if (!e) { ReleaseSRWLockExclusive(&as_lock); as_orig_ret(dev, fence, alloc); return; }
    if (fence > e->fence) e->fence = fence;
    if (--e->users <= 0) {
        if (e->busy) e->retired = 1;                  /* a gravacao em curso foi devolvida sem Close: nao reusa */
        if (e->retired) done = as_take(e, &pdev, &pfence);
    }
    ReleaseSRWLockExclusive(&as_lock);
    *alloc = NULL;
    a->lpVtbl->Release(a);                            /* a do chamador; fica a propria (ou a do pool) */
    as_push(done, pdev, pfence);
}

/* a lista l passa a gravar com o allocator a */
static void as_bind(void *l, void *a)
{
    as_ent *e;
    if (!as_on || !a) return;
    AcquireSRWLockExclusive(&as_lock);
    if ((e = as_find(a))) e->list = l;
    ReleaseSRWLockExclusive(&as_lock);
}

static HRESULT STDMETHODCALLTYPE as_reset(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a, ID3D12PipelineState *ps)
{
    HRESULT hr = as_real_reset(l, a, ps);
    if (SUCCEEDED(hr)) as_bind(l, a);
    return hr;
}

static HRESULT STDMETHODCALLTYPE as_close(ID3D12GraphicsCommandList *l)
{
    HRESULT hr = as_real_close(l);
    LONG i;
    if (!as_on) return hr;
    AcquireSRWLockExclusive(&as_lock);
    for (i = 0; i < as_hi; i++)
        if (as_tab[i].a && as_tab[i].list == l) { as_tab[i].list = NULL; as_tab[i].busy = 0; }
    ReleaseSRWLockExclusive(&as_lock);
    return hr;
}

/* hook do apply_patches (azimuth.c): placement textures placed (PASSO 97) */
int pa_wanted(void) { return dbg_int(L"PlacementPlaced", GAME_DEFAULT(placement_placed)) > 0; }
void placed_textures_setup(void)
{
    pa_on = pa_wanted() && real_placed && real_heap && real_committed;
    pa_now();                                         /* idade das placement textures conta daqui */
    pa_track = dbg_int(L"PlacementTrack", 1) > 0;
    pa_reclaim_on = pa_on && pa_track && dbg_int(L"PlacementReclaim", 1) > 0;
    pa_hold_ms = dbg_int(L"PlacementReclaimHoldMs", 2000);
    if (pa_on && pa_track && !pa_orig_refresh &&
        !hook_function(PA_REFRESH_RVA, "48895c24185556574154415541564157", 16, (void *)pa_refresh_hook, (void **)&pa_orig_refresh))
        wlog("d3d: PlacementPlaced: refresh_title_texture (azimuth+%#x) with different bytes, no usage tracking", PA_REFRESH_RVA);
    if (pa_on) wlog("d3d: PlacementPlaced: Azimuth committed placement textures (azimuth+%#x) become placed in 64 MiB proxy heaps",
                    PA_RET_RVA);
    if (pa_reclaim_on && pa_orig_refresh)
        wlog("d3d: PlacementReclaim: a placement covered by a newer one returns its pages after %d ms", pa_hold_ms);
    else pa_reclaim_on = 0;
}

/* hook do apply_patches (azimuth.c) */
int as_wanted(void) { return dbg_int(L"AllocatorShare", 0); }   /* PASSO 92: no FM6 derrubou o carregamento */
void alloc_share_setup(void)
{
    int n = as_wanted();
    if (n <= 1 || as_on) return;
    if (!hook_function(AS_ACQ_RVA, "48895c24185556574154415541564157", 16, (void *)as_acquire, (void **)&as_orig_acq)) {
        wlog("d3d: Azimuth command allocator pool (azimuth+%#x) does not match -> no AllocatorShare", AS_ACQ_RVA);
        return;
    }
    if (!hook_function(AS_RET_RVA, "48895c24104c894424185556574154", 15, (void *)as_return, (void **)&as_orig_ret)) {
        wlog("d3d: Azimuth command allocator return (azimuth+%#x) does not match -> no AllocatorShare", AS_RET_RVA);
        return;   /* o gancho de aquisicao sem o de devolucao e inofensivo: as_on fica 0 e nada e compartilhado */
    }
    as_budget = n;
    as_on = 1;
    wlog("d3d: AllocatorShare: command list opens on the same thread reuse the allocator (up to %d per allocator)", n);
}

/* hook do apply_patches (azimuth.c), como o DeferRelease */
int lp_wanted(void) { return dbg_int(L"ListPool", GAME_DEFAULT(list_pool)) > 0; }
void list_pool_setup(void)
{
    lp_on = lp_real_create && lp_wanted();
    if (lp_on) wlog("d3d: command list pool: the last Release keeps the list, the next CreateCommandList reuses it (Reset)");
}

/* hook do apply_patches (azimuth.c), depois do pool: listas unificadas (common/cmdrec.c; debug.ini ListMerge, padrao FM6).
 * As listas reais delas sao criadas por dentro do pool/gravadoras (gpuguard -> D3D12) */
void list_merge_setup(void)
{
    rc_setup((void *)lp_real_create);
}

/* DeferRelease (PASSO 70): no FM6 sempre (a rajada de destruicao do reaper para o quadro ~150 ms e, junto com o
 * NO_HAZARD dos tiles, derrubou a GPU nos boots sem ele); debug.ini DeferRelease=0/1 sobrescreve */
int dr_wanted(void) { return dbg_int(L"DeferRelease", GAME_DEFAULT(defer_release)) > 0; }

/* hook do apply_patches (azimuth.c): o device D3D12 nasce ANTES do registro do jogo (is_fm6 ainda falso no
 * stutter_device), entao a decisao e a thread de drenagem ficam para ca */
void defer_release_setup(void)
{
    static int started;
    dr_on = dr_wanted();
    dr_hold_ms = dbg_int(L"DeferReleaseHoldMs", GAME_DEFAULT(defer_release_hold_ms));
    if (!dr_on || started) return;
    started = 1;
    if (!st_freq.QuadPart) QueryPerformanceFrequency(&st_freq);
    CloseHandle(CreateThread(NULL, 0, dr_drain, NULL, 0, NULL));
    wlog("d3d: DeferRelease: the 'Azimuth reaper' last Release on a resource/heap is queued and released gradually after %d ms",
         dr_hold_ms);
}

/* chamado pelo hook do D3D12CreateDevice (common/debug12.c), depois do gpuguard (estes hooks ficam por fora). Os hooks
 * de criacao de recurso/heap registram o Release das vtables (DeferRelease decide depois, no apply_patches; com
 * DeferRelease=0 no debug.ini e sem StutterLog nada e trocado); o resto so com StutterLog. */
void stutter_device(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device1 *dev1 = NULL;
    ID3D12Device2 *dev2 = NULL;
    static int done;
    if (done || (!st_on && !dbg_int(L"DeferRelease", 1) && !dbg_int(L"ListPool", 1)) ||
        FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) return;
    done = 1;
    if (!st_freq.QuadPart) QueryPerformanceFrequency(&st_freq);
    vt_swap((void **)&dev->lpVtbl->CreateCommittedResource, st_committed, (void **)&real_committed);
    vt_swap((void **)&dev->lpVtbl->CreatePlacedResource, st_placed, (void **)&real_placed);
    vt_swap((void **)&dev->lpVtbl->CreateReservedResource, st_reserved, (void **)&real_reserved);
    vt_swap((void **)&dev->lpVtbl->CreateHeap, st_heap, (void **)&real_heap);
    vt_swap((void **)&dev->lpVtbl->CreateCommandList, lp_create, (void **)&lp_real_create);
    vt_swap((void **)&dev->lpVtbl->CreateCommandQueue, st_queue, (void **)&real_queue);   /* fence do pool */
    if (!st_on) { dev->lpVtbl->Release(dev); return; }
    vt_swap((void **)&dev->lpVtbl->CreateGraphicsPipelineState, st_gpso, (void **)&real_gpso);
    vt_swap((void **)&dev->lpVtbl->CreateComputePipelineState, st_cpso, (void **)&real_cpso);
    vt_swap((void **)&dev->lpVtbl->CreateCommandAllocator, st_alloc, (void **)&real_alloc);
    vt_swap((void **)&dev->lpVtbl->CreateDescriptorHeap, st_dheap, (void **)&real_dheap);
    dev->lpVtbl->AddRef(dev);                         /* a do mr_watch */
    CloseHandle(CreateThread(NULL, 0, mr_watch, dev, 0, NULL));
    dev->lpVtbl->Release(dev);
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device1, (void **)&dev1))) {
        vt_swap((void **)&dev1->lpVtbl->CreatePipelineLibrary, st_lib, (void **)&real_lib);
        dev1->lpVtbl->Release(dev1);
    }
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device2, (void **)&dev2))) {
        vt_swap((void **)&dev2->lpVtbl->CreatePipelineState, st_spso, (void **)&real_spso);
        dev2->lpVtbl->Release(dev2);
    }
    wlog("stutter: D3D12 device measured (PSOs, resources, ExecuteCommandLists, queue Signal/Wait)");
}

/* ---- amostrador (PASSO 58): quadro parado ha >= st_stall_ms(media) sem Present -> a cada ~2 ms o tempo de CPU de cada
 * thread (QueryThreadCycleTime) e, nas que rodaram desde a ultima volta, o RIP (SuspendThread/GetThreadContext) e
 * os valores da pilha que caem em codigo do jogo/azimuth/proxy (retornos provaveis; o jogo nao tem unwind); a cada
 * ST_BLOCKED_EVERY voltas tambem as threads do jogo que nao rodaram ("bloqueada": onde esperam). Com a
 * thread suspensa nada que pegue lock e chamado (so GetThreadContext e a copia da pilha). No Present seguinte:
 * "stutter: amostras do quadro #N" com as threads de mais CPU e os RIPs mais frequentes (so paradas de
 * >= ST_REPORT_MIN_MS alem do limiar; ST_REPORTS primeiros, depois potencias de 2). ---- */
#define ST_STALL_MS 30
#define ST_REPORT_MIN_MS 8    /* paradas mais curtas que isso alem do limiar nao viram relatorio */
#define ST_REPORTS 1000       /* relatorios completos; depois potencias de 2 */
#define ST_THREADS 256
#define ST_SAMPLES 32768
#define ST_BLOCKED_EVERY 5   /* a cada 5 voltas (~10 ms) tambem as threads do jogo que nao rodaram (onde estao bloqueadas) */
#define ST_RETS 4
#define ST_BUCKET 0x10        /* RIPs agrupados de 16 em 16 bytes */

typedef struct { uintptr_t key; uint32_t n; } st_bin;

/* histograma pequeno: incrementa a chave (ou entra se houver lugar); devolve 0 se cheio */
int st_hist_add(st_bin *h, uint32_t cap, uint32_t *used, uintptr_t key)
{
    uint32_t i;
    for (i = 0; i < *used; i++) if (h[i].key == key) { h[i].n++; return 1; }
    if (*used >= cap) return 0;
    h[*used].key = key; h[*used].n = 1; (*used)++;
    return 1;
}

static int cmp_bin(const void *a, const void *b)
{
    const st_bin *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}
void st_hist_sort(st_bin *h, uint32_t used) { qsort(h, used, sizeof *h, cmp_bin); }

/* limiar da parada: 30 ms ou 1,5x a media (a 30 fps todo quadro passava de 30 ms e gastava os relatorios no menu) */
double st_stall_ms(double avg_ms)
{
    return avg_ms * 1.5 > ST_STALL_MS ? avg_ms * 1.5 : ST_STALL_MS;
}

/* valores de uma copia da pilha que caem em algum intervalo [lo, hi) (retornos provaveis), ate max */
uint32_t st_scan_returns(const uint64_t *stack, uint32_t n, const uintptr_t (*ranges)[2], uint32_t nranges, uintptr_t *out, uint32_t max)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < n && k < max; i++)
        for (j = 0; j < nranges; j++)
            if (stack[i] >= ranges[j][0] && stack[i] < ranges[j][1]) { out[k++] = (uintptr_t)stack[i]; break; }
    return k;
}

typedef struct {
    DWORD tid;
    HANDLE h;
    uintptr_t start;
    char name[32];
    ULONG64 c0, cprev;        /* ciclos no inicio da parada / na ultima volta */
} st_thread;

typedef struct { uint32_t t; uint8_t blocked; uintptr_t rip, ret[ST_RETS]; } st_sample;

static st_thread thr[ST_THREADS];
static uint32_t nthr;
static st_sample samples[ST_SAMPLES];
static uint32_t nsamples, nblocked_ticks;
static double tsc_per_ms;
static uintptr_t code_ranges[3][2];
static uint32_t ncode_ranges;

typedef LONG (WINAPI *nqit_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef HRESULT (WINAPI *gtd_fn)(HANDLE, PWSTR *);

static void st_refresh_threads(void)
{
    static nqit_fn nqit;
    static gtd_fn gtd;
    HANDLE snap;
    THREADENTRY32 te;
    BOOL ok;
    DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    uint32_t i;
    if (!nqit) nqit = (nqit_fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    if (!gtd) gtd = (gtd_fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    for (i = 0; i < nthr; i++) CloseHandle(thr[i].h);
    nthr = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof te;
    for (ok = Thread32First(snap, &te); ok && nthr < ST_THREADS; ok = Thread32Next(snap, &te)) {
        st_thread *t = &thr[nthr];
        PWSTR desc = NULL;
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        t->h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t->h) continue;
        t->tid = te.th32ThreadID;
        t->start = 0;
        if (nqit) nqit(t->h, 9 /* ThreadQuerySetWin32StartAddress */, &t->start, sizeof t->start, NULL);
        t->name[0] = 0;
        if (gtd && SUCCEEDED(gtd(t->h, &desc)) && desc) {
            int k;
            for (k = 0; desc[k] && k < 31; k++) t->name[k] = desc[k] < 128 ? (char)desc[k] : '?';
            t->name[k] = 0;
            LocalFree(desc);
        }
        nthr++;
    }
    CloseHandle(snap);
}

/* rotulo de qualquer endereco: game+/azimuth+/winmm+ ou <modulo>+ (so fora da amostragem: pega o loader lock) */
static void st_label_any(char *out, uintptr_t a)
{
    HMODULE m = NULL;
    char path[MAX_PATH], *base;
    st_label(out, (void *)a);
    if (out[0]) return;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)a, &m) && m) {
        if (m == self_mod) { wsprintfA(out, " winmm+%#x", (unsigned)(a - (uintptr_t)m)); return; }
        GetModuleFileNameA(m, path, sizeof path);
        base = strrchr(path, '\\');
        wsprintfA(out, " %s+%#x", base ? base + 1 : path, (unsigned)(a - (uintptr_t)m));
        return;
    }
    sprintf(out, " %#llx", (unsigned long long)a);
}

static int st_in_game(uintptr_t a)
{
    uint8_t *game = g_game;
    return game && a >= (uintptr_t)game && a < (uintptr_t)game + 0x8000000;
}

/* ---- limitador da fila de blocos do FM6 (17_fm6.txt RESULTADO PASSO 59-61) ----
 * game+0x195400: com pendentes [q+0xc0] > 2x limite [q+0xc4] a thread principal gira em kernelx!SwitchToThread
 * (IAT do jogo 0x101af88, chamada em 0x195410, retorno 0x195416; rbx = fila) ate pendentes <= limite ou 1 s.
 * Com StutterLog o slot vai para st_switch_thunk (rcx = rbx, rdx = retorno). Na thread do jogo so contadores
 * (leituras diretas da fila, sem syscall nem lock); as candidatas a consumidora (threads do jogo com a fila ou
 * fila-0x28 = dono em algum registrador) saem das amostras do amostrador (st_capture) e o log "stutter: fila"
 * tambem e da thread do amostrador, quando o episodio (voltas com < 5 ms entre si) acaba e durou >= 20 ms.
 * PASSO 61 fazia a captura na thread do jogo (Toolhelp + suspensoes > 5 ms por volta): a principal passava a
 * parada nisso e a sessao terminou em device removed. */
#define FQ_IAT 0x101af88
#define FQ_CALL 0x195410
#define FQ_GAP_MS 5
#define FQ_MIN_MS 20
#define FQ_CAND 6

typedef struct { DWORD tid; uintptr_t start, rip, ret[ST_RETS]; const char *reg; uint8_t owner; uint32_t n; } fq_cand;
static BOOL (WINAPI *fq_real)(void);
static uintptr_t fq_ret;
/* escrito so pela thread do jogo no limitador; lido pelo amostrador (diagnostico: leitura sem lock) */
static volatile struct {
    uintptr_t q;
    LONG64 t0, last;
    LONG frame, id;
    uint32_t spins, pend0, pend_max, lim;
} fq;
static fq_cand fq_cands[FQ_CAND];      /* so a thread do amostrador */
static uint32_t fq_ncand;
static LONG fq_done = 0, fq_reports;

/* registradores da amostra: fila do episodio ativo (ou o dono, fila-0x28) -> candidata */
static void fq_note(const st_thread *t, const CONTEXT *ctx, const uintptr_t *ret)
{
    static const char *const names[] = { "rbx", "rsi", "rdi", "rbp", "r12", "r13", "r14", "r15" };
    DWORD64 regs[8];
    uintptr_t q = fq.q;
    uint32_t k, j;
    if (!q || fq.id == fq_done || ms_of(qpc() - fq.last) > FQ_GAP_MS) return;
    regs[0] = ctx->Rbx; regs[1] = ctx->Rsi; regs[2] = ctx->Rdi; regs[3] = ctx->Rbp;
    regs[4] = ctx->R12; regs[5] = ctx->R13; regs[6] = ctx->R14; regs[7] = ctx->R15;
    for (k = 0; k < 8; k++) {
        if (regs[k] != q && regs[k] != q - 0x28) continue;
        for (j = 0; j < fq_ncand && fq_cands[j].tid != t->tid; j++) {}
        if (j == fq_ncand) {
            if (fq_ncand == FQ_CAND) return;
            fq_ncand++;
            fq_cands[j].tid = t->tid;
            fq_cands[j].start = t->start;
            fq_cands[j].n = 0;
            fq_cands[j].rip = (uintptr_t)ctx->Rip;      /* 1a amostra */
            memcpy(fq_cands[j].ret, ret, sizeof fq_cands[j].ret);
            fq_cands[j].reg = names[k];
            fq_cands[j].owner = regs[k] != q;
        }
        fq_cands[j].n++;
        return;
    }
}

/* copia da pilha a partir de rsp: 4 KB (a pilha do driver passa de 512 bytes, PASSO 64); perto da base da pilha
 * a leitura grande falha -> 1 KB, 512 B. Devolve quantos qwords. */
#define ST_STACK_QW 512
static uint32_t st_read_stack(uintptr_t rsp, uint64_t *out)
{
    static const uint32_t tries[] = { ST_STACK_QW, 128, 64 };
    uint32_t i;
    for (i = 0; i < 3; i++) {
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (void *)rsp, out, tries[i] * 8, &got) && got == tries[i] * 8) return tries[i];
    }
    return 0;
}

/* RIP + pilha da thread i (suspensa so durante GetThreadContext e a copia) */
static void st_capture(uint32_t i, uint8_t blocked)
{
    st_thread *t = &thr[i];
    CONTEXT ctx;
    static uint64_t stack[ST_STACK_QW];      /* so a thread do amostrador */
    uint32_t nq = 0;
    int have;
    if (SuspendThread(t->h) == (DWORD)-1) return;
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    have = GetThreadContext(t->h, &ctx) && (nq = st_read_stack((uintptr_t)ctx.Rsp, stack)) != 0;
    ResumeThread(t->h);
    if (!have) return;
    {
        st_sample *s = &samples[nsamples++];
        s->t = i;
        s->blocked = blocked;
        s->rip = (uintptr_t)ctx.Rip;
        memset(s->ret, 0, sizeof s->ret);
        st_scan_returns(stack, nq, (const uintptr_t (*)[2])code_ranges, ncode_ranges, s->ret, ST_RETS);
        fq_note(t, &ctx, s->ret);
    }
}

static void st_sample_tick(int with_blocked)
{
    uint32_t i;
    double tick_cycles = tsc_per_ms * 2;
    if (with_blocked) nblocked_ticks++;
    for (i = 0; i < nthr && nsamples < ST_SAMPLES; i++) {
        st_thread *t = &thr[i];
        ULONG64 c = 0;
        int ran;
        if (!QueryThreadCycleTime(t->h, &c)) continue;
        ran = (double)(c - t->cprev) >= tick_cycles * 0.3;   /* senao quase nao rodou */
        t->cprev = c;
        if (ran) st_capture(i, 0);
        else if (with_blocked && st_in_game(t->start)) st_capture(i, 1);
    }
}

static int cmp_cycles(const void *a, const void *b)
{
    const st_thread *x = a, *y = b;
    ULONG64 dx = x->cprev - x->c0, dy = y->cprev - y->c0;
    return dx < dy ? 1 : dx > dy ? -1 : 0;
}

/* threads do jogo bloqueadas na maior parte da parada: onde (1o retorno em game/azimuth/proxy; RIP = syscall) */
static void st_report_blocked(void)
{
    static st_bin hist[ST_THREADS], sites[256];
    char buf[2000], one[96];
    uint32_t i, j, used = 0, top, shown = 0;
    int len;
    if (!nblocked_ticks) return;
    for (i = 0; i < nsamples; i++) if (samples[i].blocked) st_hist_add(hist, ST_THREADS, &used, samples[i].t);
    st_hist_sort(hist, used);
    for (top = 0; top < used && shown < 12; top++) {
        uint32_t ns = 0, tidx = (uint32_t)hist[top].key;
        if (hist[top].n * 2 < nblocked_ticks) break;          /* bloqueada menos da metade da parada */
        for (i = 0; i < nsamples; i++)
            if (samples[i].t == tidx && samples[i].blocked) st_hist_add(sites, 256, &ns, samples[i].ret[0] ? samples[i].ret[0] : samples[i].rip);
        st_hist_sort(sites, ns);
        st_label_any(one, thr[tidx].start);
        len = sprintf(buf, "stutter:   blocked tid %lu start%s (%u of %u):", thr[tidx].tid, one, hist[top].n, nblocked_ticks);
        for (j = 0; j < ns && j < 3 && len < 1700; j++) {
            uint32_t q;
            for (i = 0; i < nsamples; i++)
                if (samples[i].t == tidx && samples[i].blocked && (samples[i].ret[0] ? samples[i].ret[0] : samples[i].rip) == sites[j].key) break;
            st_label_any(one, samples[i].rip);
            len += sprintf(buf + len, "%s x%u <", one, sites[j].n);
            for (q = 0; q < ST_RETS && samples[i].ret[q]; q++) {
                st_label_any(one, samples[i].ret[q]);
                len += sprintf(buf + len, "%s", one);
            }
            len += sprintf(buf + len, " >;");
        }
        wlog("%s", buf);
        shown++;
    }
}

static void st_report(LONG frame, double stalled_ms, double limit_ms)
{
    static LONG reports;
    static st_bin hist[ST_THREADS], rips[512];
    static st_thread sorted[ST_THREADS];
    char buf[2000], one[96];
    uint32_t i, j, used = 0, top;
    int len;
    LONG k;
    if (stalled_ms < ST_REPORT_MIN_MS) return;
    k = ++reports;
    if (k > ST_REPORTS && (k & (k - 1))) return;
    for (i = 0; i < nthr; i++) QueryThreadCycleTime(thr[i].h, &thr[i].cprev);
    memcpy(sorted, thr, nthr * sizeof *thr);   /* as amostras guardam o indice em thr: ordenar uma copia */
    qsort(sorted, nthr, sizeof *sorted, cmp_cycles);
    len = sprintf(buf, "stutter: samples of frame #%ld: %.1f ms stalled (after %.0f ms), %u samples; CPU:", frame, stalled_ms,
                  limit_ms, nsamples);
    for (i = 0; i < nthr && i < 8 && len < 1700; i++) {
        double ms = (sorted[i].cprev - sorted[i].c0) / tsc_per_ms;
        if (ms < 1) break;
        st_label_any(one, sorted[i].start);
        len += sprintf(buf + len, " [tid %lu%s%s%s %.1f ms start%s]", sorted[i].tid, sorted[i].name[0] ? " '" : "",
                       sorted[i].name, sorted[i].name[0] ? "'" : "", ms, one);
    }
    wlog("%s", buf);
    for (i = 0; i < nsamples; i++) if (!samples[i].blocked) st_hist_add(hist, ST_THREADS, &used, samples[i].t);
    st_hist_sort(hist, used);
    for (top = 0; top < used && top < 4; top++) {
        uint32_t nr = 0, tidx = (uint32_t)hist[top].key;
        for (i = 0; i < nsamples; i++)
            if (samples[i].t == tidx && !samples[i].blocked) st_hist_add(rips, 512, &nr, samples[i].rip & ~(uintptr_t)(ST_BUCKET - 1));
        st_hist_sort(rips, nr);
        len = sprintf(buf, "stutter:   tid %lu (%u samples):", thr[tidx].tid, hist[top].n);
        for (j = 0; j < nr && j < 10 && len < 1700; j++) {
            st_label_any(one, rips[j].key);
            len += sprintf(buf + len, "%s x%u", one, rips[j].n);
            for (i = 0; i < nsamples; i++)          /* retornos provaveis da 1a amostra desse RIP */
                if (samples[i].t == tidx && !samples[i].blocked && (samples[i].rip & ~(uintptr_t)(ST_BUCKET - 1)) == rips[j].key) break;
            if (i < nsamples && samples[i].ret[0]) {
                uint32_t q;
                len += sprintf(buf + len, " <");
                for (q = 0; q < ST_RETS && samples[i].ret[q]; q++) {
                    st_label_any(one, samples[i].ret[q]);
                    len += sprintf(buf + len, "%s", one);
                }
                len += sprintf(buf + len, " >");
            }
            len += sprintf(buf + len, ";");
        }
        wlog("%s", buf);
    }
    st_report_blocked();
}

extern BOOL WINAPI st_switch_thunk(void);
__asm__(
    ".text\n"
    ".globl st_switch_thunk\n"
    "st_switch_thunk:\n"
    "    movq %rbx, %rcx\n"
    "    movq (%rsp), %rdx\n"
    "    jmp st_switch_c\n");

/* thread do jogo: so contadores; a fila e um objeto vivo do jogo (rbx do limitador), lida direto */
BOOL WINAPI st_switch_c(uintptr_t q, uintptr_t ret)
{
    BOOL r;
    if (ret == fq_ret) {
        LONG64 now = qpc();
        uint32_t pend = *(volatile uint32_t *)(q + 0xc0);
        if (q != fq.q || ms_of(now - fq.last) > FQ_GAP_MS) {
            fq.q = q;
            fq.t0 = now;
            fq.frame = frame_no + 1;
            fq.spins = 0;
            fq.pend0 = fq.pend_max = pend;
            fq.lim = *(volatile uint32_t *)(q + 0xc4);
            fq.id++;
        }
        if (pend > fq.pend_max) fq.pend_max = pend;
        fq.spins++;
        r = fq_real();
        fq.last = qpc();
        InterlockedAdd64(&vz_lim_ticks, fq.last - now);
        return r;
    }
    return fq_real();
}

/* thread do amostrador, a cada volta: episodio terminado -> log (>= 20 ms) */
static void fq_poll(LONG64 now)
{
    char buf[2000], one[96];
    uint32_t i, k;
    int len;
    uintptr_t vt = 0, q = fq.q;
    LONG id = fq.id;
    double ms;
    SIZE_T got = 0;
    if (!q || id == fq_done || ms_of(now - fq.last) <= FQ_GAP_MS) return;
    ms = ms_of(fq.last - fq.t0);
    fq_done = id;
    if (ms >= FQ_MIN_MS && (++fq_reports <= 2000 || !(fq_reports & (fq_reports - 1)))) {
        ReadProcessMemory(GetCurrentProcess(), (void *)(q - 0x28), &vt, sizeof vt, &got);
        st_label_any(one, vt);
        len = sprintf(buf, "stutter: queue %p owner vt%s: %.1f ms spinning (%u spins) since frame #%ld; pending %u (max %u) "
                      "limit %u; candidates:", (void *)q, one, ms, fq.spins, fq.frame, fq.pend0, fq.pend_max, fq.lim);
        for (i = 0; i < fq_ncand && len < 1700; i++) {
            fq_cand *c = &fq_cands[i];
            st_label_any(one, c->start);
            len += sprintf(buf + len, " [tid %lu start%s %s=%s x%u rip", c->tid, one, c->reg, c->owner ? "owner" : "queue", c->n);
            st_label_any(one, c->rip);
            len += sprintf(buf + len, "%s <", one);
            for (k = 0; k < ST_RETS && c->ret[k]; k++) { st_label_any(one, c->ret[k]); len += sprintf(buf + len, "%s", one); }
            len += sprintf(buf + len, " >]");
        }
        if (!fq_ncand) len += sprintf(buf + len, " none");
        wlog("%s", buf);
    }
    fq_ncand = 0;
}

/* thread do amostrador, depois do 1o Present (imports do jogo ja resolvidos): confere os bytes da chamada e o slot */
static void fq_install(void)
{
    static const uint8_t call[6] = { 0xff, 0x15, 0x72, 0x5b, 0xe8, 0x00 };   /* call [rip+0xe85b72] -> 0x101af88 */
    uint8_t *game = g_trace_game;   /* sites do exe do FM6 (so com trace_game_frames) */
    void **slot;
    DWORD old;
    if (!game) return;
    slot = (void **)(game + FQ_IAT);
    if (memcmp(game + FQ_CALL, call, sizeof call) || (uint8_t *)*slot < g_base || (uint8_t *)*slot >= g_base + 0x1000000) {
        wlog("stutter: queue limiter (game+%#x / IAT %#x) does not match -> queue not measured", FQ_CALL, FQ_IAT);
        return;
    }
    fq_ret = (uintptr_t)game + FQ_CALL + 6;
    fq_real = (BOOL (WINAPI *)(void))*slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = (void *)st_switch_thunk;
    VirtualProtect(slot, sizeof *slot, old, &old);
    wlog("stutter: FM6 queue limiter measured (SwitchToThread game+%#x)", FQ_IAT);
}

/* ---- perfil continuo (PASSO 72; PASSO 117: tambem as threads de mais CPU): as threads do perfil sao amostradas a
 * cada volta do amostrador (~2 ms; so se rodaram >= 30% da volta): RIP em baldes de 16 bytes e o 1o retorno em codigo do
 * jogo/azimuth/proxy (quem chamou o driver). Slots 0..QP_NAMED-1 = threads do Azimuth por nome; os demais = as threads
 * de mais CPU da janela anterior (qp_pick no vz_poll). A cada 10 s "stutter: profile '<rotulo>'": % por modulo, RIPs e
 * chamadores. */
#define QP_N 8
#define QP_NAMED 2
#define QP_RIPS 1024
#define QP_CALLERS 256
static const char *const QP_NAMES[QP_NAMED] = { "Azimuth queue", "Azimuth worker" };
static struct { DWORD tid; ULONG64 c; } qp_prev[ST_THREADS];   /* por indice em thr (conferido pelo tid) */
static struct {
    DWORD tid;                /* 0 = slot por nome ainda sem thread / slot livre */
    char label[64];
    uint32_t nrip, ncall, n, lost;
    st_bin rips[QP_RIPS], callers[QP_CALLERS];
} qp[QP_N];

static void qp_tick(void)
{
    static uint64_t stack[ST_STACK_QW];
    uint32_t i, k;
    for (i = 0; i < nthr; i++) {
        st_thread *t = &thr[i];
        CONTEXT ctx;
        ULONG64 c = 0;
        uintptr_t ret[1] = { 0 };
        uint32_t nq = 0;
        int have;
        for (k = 0; k < QP_NAMED && strcmp(t->name, QP_NAMES[k]); k++) {}
        if (k < QP_NAMED) {
            if (k == 0) vz_qtid = t->tid;
            if (qp[k].tid != t->tid) { qp[k].tid = t->tid; strcpy(qp[k].label, QP_NAMES[k]); }
        } else {
            for (k = QP_NAMED; k < QP_N && qp[k].tid != t->tid; k++) {}
            if (k == QP_N) continue;
        }
        if (!QueryThreadCycleTime(t->h, &c)) continue;
        if (qp_prev[i].tid != t->tid || (double)(c - qp_prev[i].c) < tsc_per_ms * 2 * 0.3) {   /* nova ou quase nao rodou */
            qp_prev[i].tid = t->tid;
            qp_prev[i].c = c;
            continue;
        }
        qp_prev[i].c = c;
        if (SuspendThread(t->h) == (DWORD)-1) continue;
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        have = GetThreadContext(t->h, &ctx) && (nq = st_read_stack((uintptr_t)ctx.Rsp, stack)) != 0;
        ResumeThread(t->h);
        if (!have) continue;
        st_scan_returns(stack, nq, (const uintptr_t (*)[2])code_ranges, ncode_ranges, ret, 1);
        qp[k].n++;
        if (!st_hist_add(qp[k].rips, QP_RIPS, &qp[k].nrip, (uintptr_t)ctx.Rip & ~(uintptr_t)(ST_BUCKET - 1))) qp[k].lost++;
        st_hist_add(qp[k].callers, QP_CALLERS, &qp[k].ncall, ret[0]);
    }
}

/* modulo de um endereco: base (0 = fora de modulo) */
static uintptr_t qp_module(uintptr_t a)
{
    HMODULE m = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m))
        return (uintptr_t)m;
    return 0;
}

/* threads de mais CPU da janela (fora as do perfil por nome) passam a ser perfiladas na proxima: idx = vz_top */
static void qp_pick(const uint32_t *idx, uint32_t nt)
{
    uint32_t i, k = QP_NAMED, j;
    for (j = QP_NAMED; j < QP_N; j++) qp[j].tid = 0;
    for (i = 0; i < nt && k < QP_N; i++) {
        st_thread *t = &thr[idx[i]];
        for (j = 0; j < QP_NAMED && strcmp(t->name, QP_NAMES[j]); j++) {}
        if (j < QP_NAMED) continue;
        qp[k].tid = t->tid;
        if (t->name[0]) wsprintfA(qp[k].label, "%s tid %lu", t->name, t->tid);
        else {
            char one[96];
            st_label_any(one, t->start);
            wsprintfA(qp[k].label, "%s tid %lu", one[0] ? one + 1 : "?", t->tid);
        }
        k++;
    }
}

static void qp_report(void)
{
    char buf[2400], one[96], path[MAX_PATH], *base;
    uint32_t k, i, nm;
    st_bin mods[32];
    int len;
    for (k = 0; k < QP_N; k++) {
        double n = qp[k].n;
        if (!qp[k].n) continue;
        nm = 0;
        for (i = 0; i < qp[k].nrip; i++) {
            uintptr_t m = qp_module(qp[k].rips[i].key);
            uint32_t j;
            for (j = 0; j < nm && mods[j].key != m; j++) {}
            if (j == nm) { if (nm == 32) continue; mods[nm].key = m; mods[nm++].n = 0; }
            mods[j].n += qp[k].rips[i].n;
        }
        st_hist_sort(mods, nm);
        len = sprintf(buf, "stutter: profile '%s' 10 s: %u samples (%u outside the table); modules:", qp[k].label, qp[k].n, qp[k].lost);
        for (i = 0; i < nm && i < 8; i++) {
            if (!mods[i].key) strcpy(path, "?");
            else if ((HMODULE)mods[i].key == self_mod) strcpy(path, "winmm");
            else if ((uint8_t *)mods[i].key == g_base) strcpy(path, "azimuth");
            else GetModuleFileNameA((HMODULE)mods[i].key, path, sizeof path);
            base = strrchr(path, '\\');
            len += sprintf(buf + len, " %s %.0f%%", base ? base + 1 : path, mods[i].n * 100 / n);
        }
        wlog("%s", buf);
        st_hist_sort(qp[k].rips, qp[k].nrip);
        len = sprintf(buf, "stutter: profile '%s' RIPs:", qp[k].label);
        for (i = 0; i < qp[k].nrip && i < 16 && len < 2200; i++) {
            st_label_any(one, qp[k].rips[i].key);
            len += sprintf(buf + len, "%s %.1f%%;", one, qp[k].rips[i].n * 100 / n);
        }
        wlog("%s", buf);
        st_hist_sort(qp[k].callers, qp[k].ncall);
        len = sprintf(buf, "stutter: profile '%s' 1st return into game/azimuth/proxy:", qp[k].label);
        for (i = 0; i < qp[k].ncall && i < 16 && len < 2200; i++) {
            if (qp[k].callers[i].key) st_label_any(one, qp[k].callers[i].key);
            else strcpy(one, " none");
            len += sprintf(buf + len, "%s %.1f%%;", one, qp[k].callers[i].n * 100 / n);
        }
        wlog("%s", buf);
        qp[k].n = qp[k].nrip = qp[k].ncall = qp[k].lost = 0;
    }
}

/* indices dos ate max maiores valores, do maior para o menor (zeros fora); devolve quantos */
uint32_t vz_top(const uint64_t *v, uint32_t n, uint32_t *idx, uint32_t max)
{
    uint32_t k = 0, i, j;
    for (i = 0; i < n; i++) {
        if (!v[i]) continue;
        if (k < max) k++;
        else if (v[idx[max - 1]] >= v[i]) continue;
        for (j = k - 1; j > 0 && v[idx[j - 1]] < v[i]; j--) idx[j] = idx[j - 1];
        idx[j] = i;
    }
    return k;
}

/* thread do amostrador: a cada 10 s as linhas "stutter: vazao" (ciclos por thread desde a janela anterior) */
static void vz_poll(LONG64 now)
{
    static LONG64 t0;
    static struct { DWORD tid; ULONG64 c; } prev[ST_THREADS];
    static uint32_t nprev;
    static const char *const qn[] = { "direct", "bundle", "compute", "copy" };
    uint64_t cpu[ST_THREADS];
    ULONG64 cur[ST_THREADS];
    uint32_t top[VZ_ORIGINS], i, j, k, n = nthr;
    LONG frames, opens, other;
    double win, f;
    char buf[3000], one[96];
    int len;
    if (t0 && ms_of(now - t0) < 10000) return;
    for (i = 0; i < n; i++) {
        cur[i] = 0;
        QueryThreadCycleTime(thr[i].h, &cur[i]);
        for (j = 0; j < nprev && prev[j].tid != thr[i].tid; j++) {}
        cpu[i] = j < nprev && cur[i] > prev[j].c ? cur[i] - prev[j].c : 0;
    }
    for (i = 0; i < n; i++) { prev[i].tid = thr[i].tid; prev[i].c = cur[i]; }
    nprev = n;
    if (!t0) { t0 = now; return; }
    win = ms_of(now - t0);
    t0 = now;
    frames = InterlockedExchange(&vz_frames, 0);
    f = frames ? frames : 1;
    len = sprintf(buf, "stutter: throughput 10 s: %ld frames, %.1f ms/frame (Present %.1f, queue limiter %.1f) | GPU ms/frame:",
                  frames, ms_of(InterlockedExchange64(&vz_frame_ticks, 0)) / f, ms_of(InterlockedExchange64(&vz_present_ticks, 0)) / f,
                  ms_of(InterlockedExchange64(&vz_lim_ticks, 0)) / f);
    for (k = 0; k < 4; k++) {
        if (k == 1) continue;
        len += sprintf(buf + len, " %s %.1f (%.0f%%)", qn[k], ms_of(vz_gpu[k]) / f, ms_of(vz_gpu[k]) * 100 / win);
        vz_gpu[k] = 0;
    }
    opens = InterlockedExchange(&vz_opens, 0);
    len += sprintf(buf + len, " | ECL %.1f/frame, lists %.0f/frame (max %ld), %.2f ms/frame | opened %.0f/frame, draws %.0f/frame", InterlockedExchange(&vz_ecl, 0) / f, InterlockedExchange(&vz_lists, 0) / f,
                   InterlockedExchange(&vz_lists_max, 0), ms_of(InterlockedExchange64(&vz_ecl_ticks, 0)) / f, opens / f,
                   InterlockedExchange(&vz_draws, 0) / f);
    len += sprintf(buf + len, " | CreateCommandList %.1f/frame %.2f ms, reused %.1f/frame, kept %.1f/frame, failures %ld",
                   InterlockedExchange(&lp_made, 0) / f, ms_of(InterlockedExchange64(&lp_made_ticks, 0)) / f,
                   InterlockedExchange(&lp_reused, 0) / f, InterlockedExchange(&lp_kept, 0) / f, InterlockedExchange(&lp_failed, 0));
    if (as_on)
        len += sprintf(buf + len, " | allocators: from pool %.1f/frame, reused %.1f/frame, returned %.1f/frame, %ld shared alive",
                       InterlockedExchange(&as_fresh, 0) / f, InterlockedExchange(&as_shared, 0) / f,
                       InterlockedExchange(&as_pushed, 0) / f, as_live);
    len += rc_format(buf + len, (int)sizeof buf - len, f);
    {   /* memoria do processo (PASSO 90: a queda do fim da corrida e "Kernel memory failure" com ~11 GB privados) */
        PROCESS_MEMORY_COUNTERS_EX pm;
        pm.cb = sizeof pm;
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof pm))
            len += sprintf(buf + len, " | private memory %llu MiB (peak %llu)", (unsigned long long)(pm.PrivateUsage >> 20),
                           (unsigned long long)(pm.PeakPagefileUsage >> 20));
    }
    {   /* commit do SISTEMA (PASSO 92: as quedas vem com a soma jogo + outros programas perto do limite) */
        PERFORMANCE_INFORMATION pi;
        pi.cb = sizeof pi;
        if (K32GetPerformanceInfo(&pi, sizeof pi))
            len += sprintf(buf + len, ", system: commit %llu of %llu MiB, free RAM %llu MiB, kernel paged %llu nonpaged %llu MiB",
                           (unsigned long long)pi.CommitTotal * pi.PageSize >> 20,
                           (unsigned long long)pi.CommitLimit * pi.PageSize >> 20,
                           (unsigned long long)pi.PhysicalAvailable * pi.PageSize >> 20,
                           (unsigned long long)pi.KernelPaged * pi.PageSize >> 20,
                           (unsigned long long)pi.KernelNonpaged * pi.PageSize >> 20);
    }
    {
        DWORD handles = 0;
        if (GetProcessHandleCount(GetCurrentProcess(), &handles)) len += sprintf(buf + len, ", handles %lu", handles);
    }
    len += sprintf(buf + len, " | CPU ms/frame:");
    if (tsc_per_ms > 0) {
        uint32_t idx[VZ_TOP_THREADS], nt = vz_top(cpu, n, idx, VZ_TOP_THREADS);
        uint64_t total = 0;
        for (i = 0; i < n; i++) total += cpu[i];
        len += sprintf(buf + len, " all threads %.1f (%.2f cores)", total / tsc_per_ms / f, total / tsc_per_ms / win);
        for (i = 0; i < nt && len < 1800; i++) {
            st_thread *t = &thr[idx[i]];
            double ms = cpu[idx[i]] / tsc_per_ms;
            if (t->name[0]) wsprintfA(one, " %s", t->name);
            else st_label_any(one, t->start);
            len += sprintf(buf + len, " [%s tid %lu %.1f (%.0f%%)]", one[0] ? one + 1 : "?", t->tid, ms / f, ms * 100 / win);
        }
        wlog("%s", buf);
        if (lt_format(buf, sizeof buf, f)) wlog("%s", buf);
        qp_report();
        qp_pick(idx, nt);
    } else {
        wlog("%s", buf);
        if (lt_format(buf, sizeof buf, f)) wlog("%s", buf);
        qp_report();
    }
    mr_report("10 s");
    if (!opens) return;
    {
        uint64_t cnt[VZ_ORIGINS];
        uint32_t nt;
        AcquireSRWLockExclusive(&vz_lock);
        for (i = 0; i < VZ_ORIGINS; i++) cnt[i] = (uint64_t)vz_orig[i].n;
        nt = vz_top(cnt, VZ_ORIGINS, top, 6);
        len = sprintf(buf, "stutter: throughput: origin of opened lists (1 in %d):", VZ_EVERY);
        for (i = 0; i < nt; i++) {
            len += sprintf(buf + len, " [%.0f%%", cnt[top[i]] * 100.0 * VZ_EVERY / opens);
            for (k = 0; k < VZ_KEY && vz_orig[top[i]].k[k]; k++) { st_label(one, (void *)vz_orig[top[i]].k[k]); len += sprintf(buf + len, "%s", one); }
            len += sprintf(buf + len, "]");
        }
        other = vz_orig_other;
        memset(vz_orig, 0, sizeof vz_orig);
        vz_orig_other = 0;
        ReleaseSRWLockExclusive(&vz_lock);
        if (other) sprintf(buf + len, " others %ld", other);
    }
    wlog("%s", buf);
}

static DWORD WINAPI st_sampler(void *arg)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    LONG64 last_refresh = 0, stall_frame_start = 0;
    LONG stall_frame = 0;
    int stalled = 0;
    double limit = ST_STALL_MS;
    uint32_t ticks = 0;
    (void)arg;
    {   /* ciclos do TSC por ms (QueryThreadCycleTime conta ciclos do TSC) */
        LONG64 q0 = qpc();
        unsigned long long c0 = __rdtsc();
        Sleep(50);
        tsc_per_ms = (double)(__rdtsc() - c0) / ms_of(qpc() - q0);
    }
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)st_sampler, &self_mod);
    wlog("stutter: sampler on (TSC %.0f MHz; frame stalled >= %d ms -> CPU per thread and RIPs every ~2 ms)",
         tsc_per_ms / 1000, ST_STALL_MS);
    for (;;) {
        LARGE_INTEGER due;
        LONG64 now, fs;
        due.QuadPart = -20000;   /* 2 ms */
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(2);
        now = qpc();
        fs = frame_start;
        if (!fs) continue;
        fq_poll(now);
        sg_poll(now);
        vz_poll(now);
        if (ncode_ranges) qp_tick();
        if (!ncode_ranges) {   /* depois do 1o Present o jogo ja esta carregado */
            uint8_t *game = g_game;
            if (game) { code_ranges[ncode_ranges][0] = (uintptr_t)game; code_ranges[ncode_ranges++][1] = (uintptr_t)game + 0x8000000; }
            code_ranges[ncode_ranges][0] = (uintptr_t)g_base; code_ranges[ncode_ranges++][1] = (uintptr_t)g_base + 0x1000000;
            code_ranges[ncode_ranges][0] = (uintptr_t)self_mod; code_ranges[ncode_ranges++][1] = (uintptr_t)self_mod + 0x200000;
            fq_install();
        }
        if (stalled) {
            if (fs != stall_frame_start) {   /* Present chegou: fim da parada */
                st_report(stall_frame, ms_of(now - stall_frame_start) - limit, limit);
                stalled = 0;
            } else {
                st_sample_tick(++ticks % ST_BLOCKED_EVERY == 0);
            }
            continue;
        }
        limit = st_stall_ms(st_avg_get(&avg));
        if (ms_of(now - fs) >= limit && nthr) {
            uint32_t i;
            stalled = 1;
            stall_frame_start = fs;
            stall_frame = frame_no + 1;
            nsamples = 0;
            nblocked_ticks = 0;
            ticks = 0;
            for (i = 0; i < nthr; i++) { QueryThreadCycleTime(thr[i].h, &thr[i].c0); thr[i].cprev = thr[i].c0; }
            continue;
        }
        if (ms_of(now - last_refresh) >= 1000) { st_refresh_threads(); last_refresh = qpc(); }
    }
    return 0;
}

/* o pool de command allocators do Azimuth (0x1400dea50, em toda abertura de command list) destroi o allocator mais
 * antigo do pool se ele esta ocioso ha mais de 4000 ms - um por abertura, nas threads do jogo e da fila
 * (DestroyAllocation2 nos picos do PASSO 65). Troca o imediato de "cmp rax, 0xfa0" (azimuth+0xdeadd) por ms (0 = nao
 * mexe; < 0 = nunca destroi). Confere os 6 bytes originais antes. Chamado no apply_patches: FM6 = nunca (PASSO 67),
 * debug.ini AllocatorMaxAgeMs muda o valor (FM5/FH2: so com o ini). */
#define CA_AGE_RVA 0xdeadd
int cmdalloc_age_patch(int ms)
{
    static const uint8_t orig[6] = { 0x48, 0x3d, 0xa0, 0x0f, 0x00, 0x00 };
    uint8_t *at = g_base + CA_AGE_RVA;
    uint32_t v = ms < 0 ? 0x7fffffff : (uint32_t)ms;
    DWORD old;
    if (!ms) return 0;
    if (memcmp(at, orig, sizeof orig)) {
        wlog("stutter: command allocator age (azimuth+%#x) does not match -> not patched", CA_AGE_RVA);
        return 0;
    }
    VirtualProtect(at, sizeof orig, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at + 2, &v, 4);
    VirtualProtect(at, sizeof orig, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof orig);
    wlog("stutter: idle command allocators destroyed only after %u ms (was 4000)", v);
    return 1;
}

/* EXPERIMENTO RetireMargin (PASSO 81, debug.ini; padrao 0): 0x1400d9a40 poe na fila do reaper (ordenada pelo valor
 * de fence) um shared_ptr {objeto, control block}; 0x1400d9d10 so o passa ao reaper quando o concluido chega no valor.
 * Somar N so aos SLOTS DE DESCRITOR (control block _Ref_count_resource do lambda de retire_descriptor, vtable
 * azimuth+0x2ae020, montado em 0x1400d9c70) atrasa a volta do slot ao pool por ~N envios (~13 por quadro).
 * Atrasar tudo (1a versao) ou tudo de 0x1400d9b10 (81b: recursos, Storage/Slice/Block) derrubou o carregamento. */
#define RM_ENQUEUE_RVA 0xd9a40
#define RM_DESC_CB_VT  0x2ae020
typedef void (*rm_enqueue_fn)(void *dev, UINT64 fence, void *sp);
static rm_enqueue_fn rm_orig;
static UINT64 rm_margin;

/* sp = {objeto, control block}; o control block comeca pela vtable */
static UINT64 rm_fence(void *const *sp, UINT64 fence)
{
    void *const *cb = sp ? (void *const *)sp[1] : NULL;
    return cb && *cb == g_base + RM_DESC_CB_VT ? fence + rm_margin : fence;
}

static void rm_enqueue(void *dev, UINT64 fence, void *sp)
{
    rm_orig(dev, rm_fence(sp, fence), sp);
}

int retire_margin_patch(int n)
{
    if (n <= 0) return 0;
    rm_margin = (UINT64)n;
    if (!hook_function(RM_ENQUEUE_RVA, "48895c24084c894424184889542410", 15, (void *)rm_enqueue, (void **)&rm_orig)) {
        wlog("d3d: Azimuth reaper queue (azimuth+%#x) does not match -> no RetireMargin", RM_ENQUEUE_RVA);
        return 0;
    }
    wlog("d3d: RetireMargin: return of Azimuth descriptor slots to the pool delayed by %d submissions", n);
    return 1;
}

/* PASSO 69: o Azimuth mapeia os tiles de cada textura reservada (streaming) com uma UpdateTileMappings na fila de
 * render (laco em azimuth+0xe0160, call r10 em 0xe01e8) com Flags = 0 -> o driver sincroniza a fila a cada chamada e
 * a GPU para no quadro seguinte a uma rajada (150-270 chamadas). Na copia das flags (mov [rsp+0x50], ebp em 0xe017d,
 * antes do test do heap) entra um jmp para um stub: com heap (mapeamento) as flags viram NO_HAZARD (as texturas sao
 * novas: nada na GPU usa esses tiles por elas); sem heap (desmapeamento) segue igual. Confere os 9 bytes antes. */
#define TN_SITE_RVA 0xe017d
#define TN_MAP_RVA 0xe0186
#define TN_UNMAP_RVA 0xe01a9
int tile_nohazard_patch(int on)
{
    static const uint8_t orig[9] = { 0x89, 0x6c, 0x24, 0x50, 0x48, 0x85, 0xc9, 0x74, 0x23 };
    static const uint8_t body[0x11] = {
        0x89, 0x6c, 0x24, 0x50,                                /* mov [rsp+0x50], ebp */
        0x48, 0x85, 0xc9,                                      /* test rcx, rcx (heap) */
        0x74, 0x16,                                            /* je -> jmp do desmapeamento */
        0xc7, 0x44, 0x24, 0x50, 0x01, 0x00, 0x00, 0x00,        /* mov dword [rsp+0x50], NO_HAZARD */
    };
    uint8_t *at = g_base + TN_SITE_RVA, *stub, jmp[9] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    uint8_t *back[2] = { g_base + TN_MAP_RVA, g_base + TN_UNMAP_RVA };
    int32_t rel;
    int i;
    if (!on) return 0;
    if (memcmp(at, orig, sizeof orig) || memcmp(g_base + TN_MAP_RVA, "\x48\x8b\x43\x58", 4)) {
        wlog("d3d: Azimuth UpdateTileMappings (azimuth+%#x) does not match -> no NO_HAZARD", TN_SITE_RVA);
        return 0;
    }
    if (!(stub = alloc_near(g_base, 64))) { wlog("d3d: no memory near azimuth.exe for NO_HAZARD"); return 0; }
    memcpy(stub, body, sizeof body);
    for (i = 0; i < 2; i++) {
        memcpy(stub + 0x11 + i * 14, "\xff\x25\0\0\0\0", 6);
        memcpy(stub + 0x17 + i * 14, &back[i], 8);
    }
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    rel = (int32_t)(stub - (at + 5));
    memcpy(jmp + 1, &rel, 4);
    write_mem(at, jmp, sizeof jmp);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof jmp);
    wlog("d3d: Azimuth UpdateTileMappings with D3D12_TILE_MAPPING_FLAG_NO_HAZARD on mappings");
    return 1;
}

/* DllMain (azimuth.c azimuth_install): fabrica DXGI e esperas pela IAT do azimuth.exe */
void stutter_install(void)
{
    int i, n = 0;
    st_on = dbg_int(L"StutterLog", 0) > 0;
    if (!st_on) return;
    gt_slow_ms = dbg_int(L"GpuSlowMs", 50);
    QueryPerformanceFrequency(&st_freq);
    {
        void **f1 = (void **)(g_base + 0x26f878), **f2 = (void **)(g_base + 0x26f880);
        HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
        DWORD old;
        if (dxgi && *f1 == (void *)GetProcAddress(dxgi, "CreateDXGIFactory1") && *f2 == (void *)GetProcAddress(dxgi, "CreateDXGIFactory2")) {
            real_factory1 = (HRESULT (WINAPI *)(REFIID, void **))*f1;
            real_factory2 = (HRESULT (WINAPI *)(UINT, REFIID, void **))*f2;
            VirtualProtect(f1, 16, PAGE_READWRITE, &old);
            *f1 = (void *)st_factory1;
            *f2 = (void *)st_factory2;
            VirtualProtect(f1, 16, old, &old);
        } else {
            wlog("stutter: dxgi IAT (0x26f878/0x26f880) does not match -> Present not measured");
        }
    }
    for (i = 0; i < 7; i++) n += iat_swap(wait_iat[i], wait_names[i], wait_hooks[i], &wait_real[i]);
    CloseHandle(CreateThread(NULL, 0, st_sampler, NULL, 0, NULL));
    wlog("stutter: on (spikes >= 25 ms and >= 1.8x the average; %d of 7 azimuth.exe waits measured)", n);
}
