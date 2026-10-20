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
 * A cada 10 s um resumo (media, p99, maximo, picos). Amostrador de CPU/RIP nos quadros parados: ver st_sampler.
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

extern uint8_t *g_base;
extern uint8_t *g_game;
extern uint8_t *g_fm6_game;
extern void wlog(const char *fmt, ...);
extern int dbg_int(const wchar_t *key, int def);
extern int is_fm6(void);

#define ST_AVG_N 64           /* media movel dos quadros */
#define ST_WAITS 512          /* anel de esperas longas */
#define ST_WAIT_MIN_US 4000
#define ST_SUM_FRAMES 4096    /* quadros guardados para o p99 do resumo */
#define ST_FRAMES_PER_WAIT 6

int st_on;
/* IIDs do DXGI aqui (o INITGUID do proxy so cobre o d3d12.h, debug12_fm5.c) */
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

static void st_label(char *out, void *p)
{
    uint8_t *a = p, *game = g_fm6_game ? g_fm6_game : g_game;
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
    uint8_t *game = g_fm6_game ? g_fm6_game : g_game;
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
        wlog("stutter: IAT %#x (%s) nao confere (%p; kernel32 %p, kernelbase %p) -> sem hook", rva, name, *slot, k32, kb);
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
    static const char *const kn[RK_N] = { "committed", "placed", "reserved", "heap", "tiles", "destruir" };
    static const char *const dn[] = { "?", "buf", "tex1d", "tex2d", "tex3d" };
    int len = 0, k, any = 0;
    for (k = 0; k < RK_N && len < cap - 200; k++) {
        rk_max *m = &rk_top[k];
        if (!rk_n[k]) continue;
        len += sprintf(buf + len, "%s %s %ld %.1f ms (max %.1f ms", any ? "," : " |", kn[k], rk_n[k], ms_of(rk_ticks[k]), ms_of(m->ticks));
        if (k == RK_HEAP) len += sprintf(buf + len, " %llu KB fl%#x", (unsigned long long)(m->size >> 10), m->flags);
        else if (k == RK_TILES) len += sprintf(buf + len, " %u regioes", m->flags);
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

/* Release das vtables de recurso/heap (uma entrada por vtable: o slot original de cada uma) */
#define RK_VT 6
static struct { void **vt; ULONG (STDMETHODCALLTYPE *orig)(IUnknown *); } rk_vts[RK_VT];
static volatile LONG rk_nvt;

/* DeferRelease (PASSO 65; padrao do FM6 desde o PASSO 70, dr_wanted): a ULTIMA referencia que a thread 'Azimuth
 * reaper' solta fica numa fila e uma thread propria solta aos poucos (<= DR_BUDGET_US a cada ~2 ms) - sem a
 * destruicao em rajada (ate 318 recursos reservados num quadro, PASSO 64). */
#define DR_QUEUE 16384
#define DR_BUDGET_US 500
int dr_on;
static IUnknown *dr_q[DR_QUEUE];
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
    InterlockedExchange(&dr_head, h + 1);    /* um so produtor (o reaper) */
    InterlockedIncrement(&dr_deferred);
    return 1;
}

static ULONG STDMETHODCALLTYPE rk_release(IUnknown *u);
static ULONG rk_call_orig(IUnknown *u);

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
                if (c == 2 && dr_push(u)) { rk_vts[i].orig(u); return 0; }
                rk_vts[i].orig(u);
            }
            t = qpc();
            r = rk_vts[i].orig(u);
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
    for (i = 0; i < n; i++) if (rk_vts[i].vt == vt) return rk_vts[i].orig(u);
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
static struct { ID3D12CommandQueue *q; uint8_t type; } sg_queues[SG_QUEUES];
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
    len = wsprintfA(buf, "stutter: quadro #%ld (pico %ld) ", n, k);
    len += sprintf(buf + len, "%.1f ms (media %.1f; %.1f no Present) | PSO %ld %.1f ms | recursos %ld %.1f ms %ld MB | "
                   "ECL %ld (%ld listas) %.1f ms | esperas>=4ms:", ms, mean, in_present, st_n[ST_PSO], ms_of(st_ticks[ST_PSO]),
                   st_n[ST_RES], ms_of(st_ticks[ST_RES]), (long)(st_bytes >> 20), st_n[ST_ECL], st_lists,
                   ms_of(st_ticks[ST_ECL]));
    if (!nt) len += wsprintfA(buf + len, " nenhuma");
    for (i = 0; i < nt && len < 1300; i++) {
        st_wait *w = &waits[top[i]];
        len += sprintf(buf + len, " [%s %.1f ms tid %u%s", kind[w->kind % 7], w->us / 1000.0, w->tid,
                       w->tid == present_tid ? " P" : "");
        for (j = 0; j < w->nframes; j++) { st_label(one, w->frames[j]); len += wsprintfA(buf + len, "%s", one); }
        len += wsprintfA(buf + len, "]");
    }
    len += rk_format(buf + len, (int)sizeof buf - len - 300);
    if (dr_on) len += sprintf(buf + len, " | adiados %ld soltos %ld fila %ld cheia %ld", dr_deferred, dr_released,
                              dr_head - dr_tail, dr_full);
    {
        static const char *const qn[] = { "direct", "bundle", "compute", "copy" };
        len += sprintf(buf + len, " | GPU: maior trecho %.1f ms (%s, valor %llu), soma %.1f ms, %ld concluidos, %ld pendentes, "
                       "%ld Wait de fila, %ld fora do anel", ms_of(gpu_max_ticks), qn[gpu_max_qt & 3],
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
        wlog("stutter: 10 s: %u quadros, media %.1f ms, p99 %.1f, max %.1f, %u picos", sum_n, mean10,
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
    wlog("stutter: Present/Present1 da swap chain com medicao");
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

static HRESULT STDMETHODCALLTYPE st_committed(ID3D12Device *d, const D3D12_HEAP_PROPERTIES *hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC *rd,
                                              D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE *c, REFIID r, void **o)
{
    HRESULT hr;
    LONG64 t = qpc(), dt;
    hr = real_committed(d, hp, hf, rd, s, c, r, o);
    dt = qpc() - t;
    st_count(ST_RES, dt);
    InterlockedAdd64(&st_bytes, (LONG64)st_res_bytes(rd));
    rk_note(RK_COMMITTED, dt, rd, 0, 0);
    if (SUCCEEDED(hr) && o) rk_hook_release(*o);
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
    if (SUCCEEDED(hr) && o) rk_hook_release(*o);
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
    if (SUCCEEDED(hr) && o) rk_hook_release(*o);
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
    if (SUCCEEDED(hr) && o) rk_hook_release(*o);
    return hr;
}

static void STDMETHODCALLTYPE st_ecl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l)
{
    LONG64 t = qpc();
    real_ecl(q, n, l);
    st_count(ST_ECL, qpc() - t);
    InterlockedAdd(&st_lists, (LONG)n);
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
    len = snprintf(buf, cap, "stutter: tiles 10 s: %u chamadas, %u regioes, %llu tiles, %u recursos distintos, %u com flags, "
                   "%u sem heap, %u faixas NULL; chamadores:", tl.calls, tl.regions,
                   (unsigned long long)tl.tiles, tl.distinct, tl.flagged, tl.unmaps, tl.nulls);
    for (k = 0; k < TL_CALLERS && tl.callers[k].ret && len < cap - 40; k++) {
        char a[48];
        st_label(a, tl.callers[k].ret);
        if (!a[0]) wsprintfA(a, " %p", tl.callers[k].ret);
        len += snprintf(buf + len, cap - len, "%s x%u", a, tl.callers[k].n);
    }
    if (tl.other_callers && len < cap - 24) len += snprintf(buf + len, cap - len, " outros x%u", tl.other_callers);
    return len;
}

static void STDMETHODCALLTYPE st_tiles(ID3D12CommandQueue *q, ID3D12Resource *res, UINT nr, const D3D12_TILED_RESOURCE_COORDINATE *co,
                                       const D3D12_TILE_REGION_SIZE *sz, ID3D12Heap *h, UINT ng, const D3D12_TILE_RANGE_FLAGS *rf,
                                       const UINT *hs, const UINT *rc, D3D12_TILE_MAPPING_FLAGS fl)
{
    LONG64 t;
    tl_note(res, nr, sz, h, ng, rf, rc, fl, __builtin_return_address(0));
    t = qpc();
    real_tiles(q, res, nr, co, sz, h, ng, rf, hs, rc, fl);
    rk_note(RK_TILES, qpc() - t, NULL, 0, nr);
}

static HRESULT (STDMETHODCALLTYPE *real_signal)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);
static HRESULT (STDMETHODCALLTYPE *real_qwait)(ID3D12CommandQueue *, ID3D12Fence *, UINT64);

static uint8_t sg_type(ID3D12CommandQueue *q)
{
    LONG i, n = sg_nqueues;
    for (i = 0; i < n && i < SG_QUEUES; i++) if (sg_queues[i].q == q) return sg_queues[i].type;
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
        if (k < SG_QUEUES) { sg_queues[k].q = q; sg_queues[k].type = qd ? (uint8_t)(qd->Type & 3) : 0; sg_nqueues = k + 1; }
        vt_swap((void **)&q->lpVtbl->ExecuteCommandLists, st_ecl, (void **)&real_ecl);
        vt_swap((void **)&q->lpVtbl->Signal, st_signal, (void **)&real_signal);
        vt_swap((void **)&q->lpVtbl->Wait, st_qwait, (void **)&real_qwait);
        vt_swap((void **)&q->lpVtbl->UpdateTileMappings, st_tiles, (void **)&real_tiles);
        q->lpVtbl->Release(q);
    }
    return hr;
}

/* DeferRelease (PASSO 70): no FM6 sempre (a rajada de destruicao do reaper para o quadro ~150 ms e, junto com o
 * NO_HAZARD dos tiles, derrubou a GPU nos boots sem ele); debug.ini DeferRelease=0/1 sobrescreve */
int dr_wanted(void) { return dbg_int(L"DeferRelease", is_fm6()) > 0; }

/* hook do apply_patches (fm5_patch.c): o device D3D12 nasce ANTES do registro do jogo (is_fm6 ainda falso no
 * stutter_device), entao a decisao e a thread de drenagem ficam para ca */
void defer_release_setup(void)
{
    static int started;
    dr_on = dr_wanted();
    if (!dr_on || started) return;
    started = 1;
    if (!st_freq.QuadPart) QueryPerformanceFrequency(&st_freq);
    CloseHandle(CreateThread(NULL, 0, dr_drain, NULL, 0, NULL));
    wlog("d3d: DeferRelease: a ultima Release do 'Azimuth reaper' em recurso/heap fica numa fila, solta aos poucos");
}

/* chamado pelo hook do D3D12CreateDevice (debug12_fm5.c), depois do gpuguard (estes hooks ficam por fora). Os hooks
 * de criacao de recurso/heap registram o Release das vtables (DeferRelease decide depois, no apply_patches; com
 * DeferRelease=0 no debug.ini e sem StutterLog nada e trocado); o resto so com StutterLog. */
void stutter_device(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device1 *dev1 = NULL;
    ID3D12Device2 *dev2 = NULL;
    static int done;
    if (done || (!st_on && !dbg_int(L"DeferRelease", 1)) ||
        FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) return;
    done = 1;
    if (!st_freq.QuadPart) QueryPerformanceFrequency(&st_freq);
    vt_swap((void **)&dev->lpVtbl->CreateCommittedResource, st_committed, (void **)&real_committed);
    vt_swap((void **)&dev->lpVtbl->CreatePlacedResource, st_placed, (void **)&real_placed);
    vt_swap((void **)&dev->lpVtbl->CreateReservedResource, st_reserved, (void **)&real_reserved);
    vt_swap((void **)&dev->lpVtbl->CreateHeap, st_heap, (void **)&real_heap);
    if (!st_on) { dev->lpVtbl->Release(dev); return; }
    vt_swap((void **)&dev->lpVtbl->CreateGraphicsPipelineState, st_gpso, (void **)&real_gpso);
    vt_swap((void **)&dev->lpVtbl->CreateComputePipelineState, st_cpso, (void **)&real_cpso);
    vt_swap((void **)&dev->lpVtbl->CreateCommandQueue, st_queue, (void **)&real_queue);
    dev->lpVtbl->Release(dev);
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device1, (void **)&dev1))) {
        vt_swap((void **)&dev1->lpVtbl->CreatePipelineLibrary, st_lib, (void **)&real_lib);
        dev1->lpVtbl->Release(dev1);
    }
    if (SUCCEEDED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device2, (void **)&dev2))) {
        vt_swap((void **)&dev2->lpVtbl->CreatePipelineState, st_spso, (void **)&real_spso);
        dev2->lpVtbl->Release(dev2);
    }
    wlog("stutter: device D3D12 com medicao (PSOs, recursos, ExecuteCommandLists, Signal/Wait das filas)");
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
static HMODULE self_mod;
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
    uint8_t *game = g_fm6_game ? g_fm6_game : g_game;
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
        len = sprintf(buf, "stutter:   bloqueada tid %lu inicio%s (%u de %u):", thr[tidx].tid, one, hist[top].n, nblocked_ticks);
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
    len = sprintf(buf, "stutter: amostras do quadro #%ld: %.1f ms parado (apos %.0f ms), %u amostras; CPU:", frame, stalled_ms,
                  limit_ms, nsamples);
    for (i = 0; i < nthr && i < 8 && len < 1700; i++) {
        double ms = (sorted[i].cprev - sorted[i].c0) / tsc_per_ms;
        if (ms < 1) break;
        st_label_any(one, sorted[i].start);
        len += sprintf(buf + len, " [tid %lu%s%s%s %.1f ms inicio%s]", sorted[i].tid, sorted[i].name[0] ? " '" : "",
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
        len = sprintf(buf, "stutter:   tid %lu (%u amostras):", thr[tidx].tid, hist[top].n);
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
        len = sprintf(buf, "stutter: fila %p dono vt%s: %.1f ms girando (%u voltas) desde o quadro #%ld; pendentes %u (max %u) "
                      "limite %u; candidatas:", (void *)q, one, ms, fq.spins, fq.frame, fq.pend0, fq.pend_max, fq.lim);
        for (i = 0; i < fq_ncand && len < 1700; i++) {
            fq_cand *c = &fq_cands[i];
            st_label_any(one, c->start);
            len += sprintf(buf + len, " [tid %lu inicio%s %s=%s x%u rip", c->tid, one, c->reg, c->owner ? "dono" : "fila", c->n);
            st_label_any(one, c->rip);
            len += sprintf(buf + len, "%s <", one);
            for (k = 0; k < ST_RETS && c->ret[k]; k++) { st_label_any(one, c->ret[k]); len += sprintf(buf + len, "%s", one); }
            len += sprintf(buf + len, " >]");
        }
        if (!fq_ncand) len += sprintf(buf + len, " nenhuma");
        wlog("%s", buf);
    }
    fq_ncand = 0;
}

/* thread do amostrador, depois do 1o Present (imports do jogo ja resolvidos): confere os bytes da chamada e o slot */
static void fq_install(void)
{
    static const uint8_t call[6] = { 0xff, 0x15, 0x72, 0x5b, 0xe8, 0x00 };   /* call [rip+0xe85b72] -> 0x101af88 */
    uint8_t *game = g_fm6_game;
    void **slot;
    DWORD old;
    if (!game) return;
    slot = (void **)(game + FQ_IAT);
    if (memcmp(game + FQ_CALL, call, sizeof call) || (uint8_t *)*slot < g_base || (uint8_t *)*slot >= g_base + 0x1000000) {
        wlog("stutter: limitador da fila (game+%#x / IAT %#x) nao confere -> sem medicao da fila", FQ_CALL, FQ_IAT);
        return;
    }
    fq_ret = (uintptr_t)game + FQ_CALL + 6;
    fq_real = (BOOL (WINAPI *)(void))*slot;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = (void *)st_switch_thunk;
    VirtualProtect(slot, sizeof *slot, old, &old);
    wlog("stutter: limitador da fila do FM6 medido (SwitchToThread game+%#x)", FQ_IAT);
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
    wlog("stutter: amostrador ligado (TSC %.0f MHz; quadro parado >= %d ms -> CPU por thread e RIPs a cada ~2 ms)",
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
        if (!ncode_ranges) {   /* depois do 1o Present o jogo ja esta carregado */
            uint8_t *game = g_fm6_game ? g_fm6_game : g_game;
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
        wlog("stutter: idade dos command allocators (azimuth+%#x) nao confere -> sem patch", CA_AGE_RVA);
        return 0;
    }
    VirtualProtect(at, sizeof orig, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at + 2, &v, 4);
    VirtualProtect(at, sizeof orig, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof orig);
    wlog("stutter: command allocators ociosos destruidos so depois de %u ms (era 4000)", v);
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
extern uint8_t *alloc_near(uint8_t *mod, size_t size);
extern void write_mem(void *dst, const void *src, size_t len);
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
        wlog("d3d: UpdateTileMappings do Azimuth (azimuth+%#x) nao confere -> sem NO_HAZARD", TN_SITE_RVA);
        return 0;
    }
    if (!(stub = alloc_near(g_base, 64))) { wlog("d3d: sem memoria perto do azimuth.exe para o NO_HAZARD"); return 0; }
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
    wlog("d3d: UpdateTileMappings do Azimuth com D3D12_TILE_MAPPING_FLAG_NO_HAZARD nos mapeamentos");
    return 1;
}

/* DllMain (fm5_patch.c install): fabrica DXGI e esperas pela IAT do azimuth.exe */
void stutter_install(void)
{
    int i, n = 0;
    st_on = dbg_int(L"StutterLog", 0) > 0;
    if (!st_on) return;
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
            wlog("stutter: IAT do dxgi (0x26f878/0x26f880) nao confere -> sem medicao do Present");
        }
    }
    for (i = 0; i < 7; i++) n += iat_swap(wait_iat[i], wait_names[i], wait_hooks[i], &wait_real[i]);
    CloseHandle(CreateThread(NULL, 0, st_sampler, NULL, 0, NULL));
    wlog("stutter: ligado (picos >= 25 ms e >= 1,8x a media; %d de 7 esperas do azimuth.exe medidas)", n);
}
