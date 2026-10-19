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

static void log_spike(LONG n, double ms, double mean, double in_present, LONG64 t0, LONG64 t1)
{
    static const char *const kind[] = { "WFSO", "WFSOEx", "WFMO", "CondVar", "WaitOnAddress", "Sleep", "SleepEx" };
    char buf[1600], one[40];
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
    for (i = 0; i < nt && len < 1400; i++) {
        st_wait *w = &waits[top[i]];
        len += sprintf(buf + len, " [%s %.1f ms tid %u%s", kind[w->kind % 7], w->us / 1000.0, w->tid,
                       w->tid == present_tid ? " P" : "");
        for (j = 0; j < w->nframes; j++) { st_label(one, w->frames[j]); len += wsprintfA(buf + len, "%s", one); }
        len += wsprintfA(buf + len, "]");
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
        sum_n = sum_spikes = 0; sum_max = 0; sum_t0 = after;
    }
    frame_start = after;
    for (i = 0; i < ST_NCOUNT; i++) { st_n[i] = 0; st_ticks[i] = 0; }
    st_lists = 0; st_bytes = 0;
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
{ HRESULT hr; TIMED(ST_RES, real_committed(d, hp, hf, rd, s, c, r, o)); InterlockedAdd64(&st_bytes, (LONG64)st_res_bytes(rd)); return hr; }
static HRESULT STDMETHODCALLTYPE st_placed(ID3D12Device *d, ID3D12Heap *h, UINT64 off, const D3D12_RESOURCE_DESC *rd, D3D12_RESOURCE_STATES s,
                                           const D3D12_CLEAR_VALUE *c, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_RES, real_placed(d, h, off, rd, s, c, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_reserved(ID3D12Device *d, const D3D12_RESOURCE_DESC *rd, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE *c,
                                             REFIID r, void **o)
{ HRESULT hr; TIMED(ST_RES, real_reserved(d, rd, s, c, r, o)); return hr; }
static HRESULT STDMETHODCALLTYPE st_heap(ID3D12Device *d, const D3D12_HEAP_DESC *hd, REFIID r, void **o)
{ HRESULT hr; TIMED(ST_RES, real_heap(d, hd, r, o)); if (hd) InterlockedAdd64(&st_bytes, (LONG64)hd->SizeInBytes); return hr; }

static void STDMETHODCALLTYPE st_ecl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *l)
{
    LONG64 t = qpc();
    real_ecl(q, n, l);
    st_count(ST_ECL, qpc() - t);
    InterlockedAdd(&st_lists, (LONG)n);
}

static HRESULT STDMETHODCALLTYPE st_queue(ID3D12Device *d, const D3D12_COMMAND_QUEUE_DESC *qd, REFIID r, void **o)
{
    HRESULT hr = real_queue(d, qd, r, o);
    ID3D12CommandQueue *q = NULL;
    if (SUCCEEDED(hr) && o && *o && SUCCEEDED(((IUnknown *)*o)->lpVtbl->QueryInterface(*o, &IID_ID3D12CommandQueue, (void **)&q))) {
        vt_swap((void **)&q->lpVtbl->ExecuteCommandLists, st_ecl, (void **)&real_ecl);
        q->lpVtbl->Release(q);
    }
    return hr;
}

/* chamado pelo hook do D3D12CreateDevice (debug12_fm5.c), depois do gpuguard (estes hooks ficam por fora) */
void stutter_device(IUnknown *obj)
{
    ID3D12Device *dev = NULL;
    ID3D12Device1 *dev1 = NULL;
    ID3D12Device2 *dev2 = NULL;
    static int done;
    if (!st_on || done || FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ID3D12Device, (void **)&dev))) return;
    done = 1;
    vt_swap((void **)&dev->lpVtbl->CreateGraphicsPipelineState, st_gpso, (void **)&real_gpso);
    vt_swap((void **)&dev->lpVtbl->CreateComputePipelineState, st_cpso, (void **)&real_cpso);
    vt_swap((void **)&dev->lpVtbl->CreateCommittedResource, st_committed, (void **)&real_committed);
    vt_swap((void **)&dev->lpVtbl->CreatePlacedResource, st_placed, (void **)&real_placed);
    vt_swap((void **)&dev->lpVtbl->CreateReservedResource, st_reserved, (void **)&real_reserved);
    vt_swap((void **)&dev->lpVtbl->CreateHeap, st_heap, (void **)&real_heap);
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
    wlog("stutter: device D3D12 com medicao (PSOs, recursos, ExecuteCommandLists)");
}

/* ---- amostrador (PASSO 58): quadro parado ha >= ST_STALL_MS sem Present -> a cada ~2 ms o tempo de CPU de cada
 * thread (QueryThreadCycleTime) e, nas que rodaram desde a ultima volta, o RIP (SuspendThread/GetThreadContext) e
 * os valores da pilha que caem em codigo do jogo/azimuth/proxy (retornos provaveis; o jogo nao tem unwind). Com a
 * thread suspensa nada que pegue lock e chamado (so GetThreadContext e a copia da pilha). No Present seguinte:
 * "stutter: amostras do quadro #N" com as threads de mais CPU e os RIPs mais frequentes. ---- */
#define ST_STALL_MS 30
#define ST_THREADS 256
#define ST_SAMPLES 8192
#define ST_RETS 3
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

typedef struct { uint32_t t; uintptr_t rip, ret[ST_RETS]; } st_sample;

static st_thread thr[ST_THREADS];
static uint32_t nthr;
static st_sample samples[ST_SAMPLES];
static uint32_t nsamples;
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

static void st_sample_tick(void)
{
    uint32_t i;
    double tick_cycles = tsc_per_ms * 2;
    for (i = 0; i < nthr && nsamples < ST_SAMPLES; i++) {
        st_thread *t = &thr[i];
        ULONG64 c = 0;
        CONTEXT ctx;
        uint64_t stack[64];
        SIZE_T got = 0;
        int have;
        if (!QueryThreadCycleTime(t->h, &c)) continue;
        if ((double)(c - t->cprev) < tick_cycles * 0.3) { t->cprev = c; continue; }   /* quase nao rodou */
        t->cprev = c;
        if (SuspendThread(t->h) == (DWORD)-1) continue;
        ctx.ContextFlags = CONTEXT_CONTROL;
        have = GetThreadContext(t->h, &ctx) &&
               ReadProcessMemory(GetCurrentProcess(), (void *)ctx.Rsp, stack, sizeof stack, &got) && got == sizeof stack;
        ResumeThread(t->h);
        if (!have) continue;
        {
            st_sample *s = &samples[nsamples++];
            s->t = i;
            s->rip = (uintptr_t)ctx.Rip;
            memset(s->ret, 0, sizeof s->ret);
            st_scan_returns(stack, 64, (const uintptr_t (*)[2])code_ranges, ncode_ranges, s->ret, ST_RETS);
        }
    }
}

static int cmp_cycles(const void *a, const void *b)
{
    const st_thread *x = a, *y = b;
    ULONG64 dx = x->cprev - x->c0, dy = y->cprev - y->c0;
    return dx < dy ? 1 : dx > dy ? -1 : 0;
}

static void st_report(LONG frame, double stalled_ms)
{
    static volatile LONG reports;
    static st_bin hist[ST_THREADS], rips[512];
    static st_thread sorted[ST_THREADS];
    char buf[2000], one[96];
    uint32_t i, j, used = 0, top;
    int len;
    LONG k = InterlockedIncrement(&reports);
    if (k > 300 && (k & (k - 1))) return;
    for (i = 0; i < nthr; i++) QueryThreadCycleTime(thr[i].h, &thr[i].cprev);
    memcpy(sorted, thr, nthr * sizeof *thr);   /* as amostras guardam o indice em thr: ordenar uma copia */
    qsort(sorted, nthr, sizeof *sorted, cmp_cycles);
    len = sprintf(buf, "stutter: amostras do quadro #%ld: %.1f ms parado (apos %d ms), %u amostras; CPU:", frame, stalled_ms,
                  ST_STALL_MS, nsamples);
    for (i = 0; i < nthr && i < 8 && len < 1700; i++) {
        double ms = (sorted[i].cprev - sorted[i].c0) / tsc_per_ms;
        if (ms < 1) break;
        st_label_any(one, sorted[i].start);
        len += sprintf(buf + len, " [tid %lu%s%s%s %.1f ms inicio%s]", sorted[i].tid, sorted[i].name[0] ? " '" : "",
                       sorted[i].name, sorted[i].name[0] ? "'" : "", ms, one);
    }
    wlog("%s", buf);
    for (i = 0; i < nsamples; i++) st_hist_add(hist, ST_THREADS, &used, samples[i].t);
    st_hist_sort(hist, used);
    for (top = 0; top < used && top < 4; top++) {
        uint32_t nr = 0, tidx = (uint32_t)hist[top].key;
        for (i = 0; i < nsamples; i++)
            if (samples[i].t == tidx) st_hist_add(rips, 512, &nr, samples[i].rip & ~(uintptr_t)(ST_BUCKET - 1));
        st_hist_sort(rips, nr);
        len = sprintf(buf, "stutter:   tid %lu (%u amostras):", thr[tidx].tid, hist[top].n);
        for (j = 0; j < nr && j < 10 && len < 1700; j++) {
            st_label_any(one, rips[j].key);
            len += sprintf(buf + len, "%s x%u", one, rips[j].n);
            for (i = 0; i < nsamples; i++)          /* retornos provaveis da 1a amostra desse RIP */
                if (samples[i].t == tidx && (samples[i].rip & ~(uintptr_t)(ST_BUCKET - 1)) == rips[j].key) break;
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
}

static DWORD WINAPI st_sampler(void *arg)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    LONG64 last_refresh = 0, stall_frame_start = 0;
    LONG stall_frame = 0;
    int stalled = 0;
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
        if (!ncode_ranges) {   /* depois do 1o Present o jogo ja esta carregado */
            uint8_t *game = g_fm6_game ? g_fm6_game : g_game;
            if (game) { code_ranges[ncode_ranges][0] = (uintptr_t)game; code_ranges[ncode_ranges++][1] = (uintptr_t)game + 0x8000000; }
            code_ranges[ncode_ranges][0] = (uintptr_t)g_base; code_ranges[ncode_ranges++][1] = (uintptr_t)g_base + 0x1000000;
            code_ranges[ncode_ranges][0] = (uintptr_t)self_mod; code_ranges[ncode_ranges++][1] = (uintptr_t)self_mod + 0x200000;
        }
        if (stalled) {
            if (fs != stall_frame_start) {   /* Present chegou: fim da parada */
                st_report(stall_frame, ms_of(now - stall_frame_start) - ST_STALL_MS);
                stalled = 0;
            } else {
                st_sample_tick();
            }
            continue;
        }
        if (ms_of(now - fs) >= ST_STALL_MS && nthr) {
            uint32_t i;
            stalled = 1;
            stall_frame_start = fs;
            stall_frame = frame_no + 1;
            nsamples = 0;
            for (i = 0; i < nthr; i++) { QueryThreadCycleTime(thr[i].h, &thr[i].c0); thr[i].cprev = thr[i].c0; }
            continue;
        }
        if (ms_of(now - last_refresh) >= 1000) { st_refresh_threads(); last_refresh = qpc(); }
    }
    return 0;
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
