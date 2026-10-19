/* Diagnostico do audio do FM6 (17_fm6.txt PASSO 53): jogo sem som, sem STOP.                 */
/* O XAudio2 do FM6 (game+0x517470) so serve o player de video (com SkipIntro nem e criado); o   */
/* som do jogo sai pelo WASAPI: MMDevAPI!ActivateAudioInterfaceAsync -> FmodAudioClient do       */
/* Azimuth (vtable 0x1402a1da0, 18 slots: IUnknown + IAudioClient + IAudioClient2), que repassa  */
/* ao IAudioClient real do Windows ([this+0x10]); GetService devolve o IAudioRenderClient real.  */
/* Aqui: Initialize/Start/Stop/SetEventHandle/GetService logados e, no render client,            */
/* GetBuffer/ReleaseBuffer contados com as amostras escritas (silencio x som). So loga no FM6;   */
/* nos outros jogos os wrappers so repassam.                                                     */
#include <windows.h>
#include <stdint.h>

extern uint8_t *g_base;
extern void wlog(const char *fmt, ...);
extern int is_fm6(void);
extern void write_mem(void *dst, const void *src, size_t len);

#define RVA_FMOD_CLIENT_VT 0x2a1da0

enum { AC_INITIALIZE = 3, AC_GET_PADDING = 6, AC_START = 10, AC_STOP = 11, AC_SET_EVENT = 13, AC_GET_SERVICE = 14 };
enum { RC_GET_BUFFER = 3, RC_RELEASE_BUFFER = 4 };

/* slots trocados e o metodo do Azimuth esperado em cada um (conferido antes de trocar) */
static const struct { int slot; uint32_t rva; } client_slots[] = {
    { AC_INITIALIZE, 0x82a00 }, { AC_GET_PADDING, 0x83200 }, { AC_START, 0x83340 },
    { AC_STOP, 0x83360 }, { AC_SET_EVENT, 0x833a0 }, { AC_GET_SERVICE, 0x833c0 },
};

static const GUID iid_render_client =
    { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };

typedef long (*init_fn)(void *, int, DWORD, int64_t, int64_t, const WAVEFORMATEX *, const GUID *);
typedef long (*padding_fn)(void *, UINT32 *);
typedef long (*noarg_fn)(void *);
typedef long (*set_event_fn)(void *, HANDLE);
typedef long (*get_service_fn)(void *, const GUID *, void **);
typedef long (*get_buffer_fn)(void *, UINT32, BYTE **);
typedef long (*release_buffer_fn)(void *, UINT32, DWORD);

static init_fn orig_init;
static padding_fn orig_padding;
static noarg_fn orig_start, orig_stop;
static set_event_fn orig_set_event;
static get_service_fn orig_get_service;
static get_buffer_fn orig_get_buffer;
static release_buffer_fn orig_release_buffer;
static void **render_vt;
static void *game_rc[4];   /* render clients devolvidos ao jogo (o vtable do AudioSes e compartilhado) */

static int is_game_rc(void *rc)
{
    int i;
    for (i = 0; i < 4; i++) if (game_rc[i] == rc) return 1;
    return 0;
}

/* formato do stream (Initialize) */
static unsigned fmt_block, fmt_bits;
static int fmt_float;

typedef struct {
    LONG get_calls, get_fail, release_calls, padding_calls;
    uint64_t frames, frames_silent_flag, frames_sound;
    float peak;
} AudioStats;
static AudioStats st;
static BYTE *last_buffer;
static UINT32 last_frames;

static int log_point(LONG n) { return n <= 4 || !(n & (n - 1)); }

static int fmt_is_float(const WAVEFORMATEX *f)
{
    if (f->wFormatTag == 3) return 1;   /* WAVE_FORMAT_IEEE_FLOAT */
    return f->wFormatTag == 0xfffe && f->cbSize >= 22 && *(const uint32_t *)((const uint8_t *)f + 24) == 3;
}

/* pico absoluto de um buffer escrito pelo jogo (float ou PCM inteiro de 16/32 bits) */
static float buffer_peak(const BYTE *p, UINT32 frames, unsigned block, unsigned bits, int is_float)
{
    size_t i, n = (size_t)frames * block;
    float peak = 0;
    if (is_float && bits == 32) {
        for (i = 0; i + 4 <= n; i += 4) {
            float v = *(const float *)(p + i);
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
    } else if (bits == 16) {
        for (i = 0; i + 2 <= n; i += 2) {
            int v = *(const int16_t *)(p + i);
            float a = (float)(v < 0 ? -v : v) / 32768.0f;
            if (a > peak) peak = a;
        }
    } else if (bits == 32) {
        for (i = 0; i + 4 <= n; i += 4) {
            int64_t v = *(const int32_t *)(p + i);
            float a = (float)(v < 0 ? -v : v) / 2147483648.0f;
            if (a > peak) peak = a;
        }
    }
    return peak;
}

static void stats_release(UINT32 frames, DWORD flags, const BYTE *buf)
{
    LONG n = InterlockedIncrement(&st.release_calls);
    st.frames += frames;
    if (flags & 2) st.frames_silent_flag += frames;   /* AUDCLNT_BUFFERFLAGS_SILENT */
    else if (buf && frames && fmt_block) {
        float pk = buffer_peak(buf, frames, fmt_block, fmt_bits, fmt_float);
        if (pk > 0) st.frames_sound += frames;
        if (pk > st.peak) st.peak = pk;
    }
    if (log_point(n))
        wlog("audio FM6: ReleaseBuffer #%ld (%u quadros, flags %#lx): total %llu quadros, %llu com flag SILENT, "
             "%llu com amostra != 0, pico %.4f; GetBuffer %ld (%ld falhas), GetCurrentPadding %ld",
             n, frames, flags, (unsigned long long)st.frames, (unsigned long long)st.frames_silent_flag,
             (unsigned long long)st.frames_sound, st.peak, st.get_calls, st.get_fail, st.padding_calls);
}

static long rc_get_buffer(void *self, UINT32 frames, BYTE **data)
{
    long hr = orig_get_buffer(self, frames, data);
    if (!is_fm6() || !is_game_rc(self)) return hr;
    InterlockedIncrement(&st.get_calls);
    if (hr < 0) {
        LONG k = InterlockedIncrement(&st.get_fail);
        if (log_point(k)) wlog("audio FM6: GetBuffer(%u) falhou #%ld hr=%#lx", frames, k, hr);
    }
    last_buffer = hr >= 0 && data ? *data : NULL;
    last_frames = frames;
    return hr;
}

static long rc_release_buffer(void *self, UINT32 frames, DWORD flags)
{
    if (is_fm6() && is_game_rc(self)) {
        stats_release(frames, flags, frames <= last_frames ? last_buffer : NULL);
        last_buffer = NULL;
    }
    return orig_release_buffer(self, frames, flags);
}

static void swap_slot(void **vt, int slot, void *hook, void **orig)
{
    *orig = vt[slot];
    write_mem(&vt[slot], &hook, sizeof hook);
}

static void hook_render_client(void *rc)
{
    void **vt = *(void ***)rc;
    int i;
    if (!is_game_rc(rc))
        for (i = 0; i < 4; i++) if (!game_rc[i]) { game_rc[i] = rc; break; }
    if (vt == render_vt) return;
    if (render_vt) { wlog("audio FM6: outro vtable de render client %p (ja trocado %p), ignorado", vt, render_vt); return; }
    render_vt = vt;
    swap_slot(vt, RC_GET_BUFFER, (void *)rc_get_buffer, (void **)&orig_get_buffer);
    swap_slot(vt, RC_RELEASE_BUFFER, (void *)rc_release_buffer, (void **)&orig_release_buffer);
    wlog("audio FM6: IAudioRenderClient %p (vtable %p): GetBuffer/ReleaseBuffer com contagem", rc, vt);
}

static long ac_init(void *self, int share, DWORD flags, int64_t dur, int64_t period, const WAVEFORMATEX *f,
                    const GUID *session)
{
    long hr = orig_init(self, share, flags, dur, period, f, session);
    if (!is_fm6()) return hr;
    if (f) {
        fmt_block = f->nBlockAlign;
        fmt_bits = f->wBitsPerSample;
        fmt_float = fmt_is_float(f);
    }
    wlog("audio FM6: Initialize(%p modo %d flags %#lx duracao %lld periodo %lld fmt tag %#x %u canais %lu Hz %u bits "
         "bloco %u%s) hr=%#lx", self, share, flags, (long long)dur, (long long)period, f ? f->wFormatTag : 0,
         f ? f->nChannels : 0, f ? f->nSamplesPerSec : 0, f ? f->wBitsPerSample : 0, f ? f->nBlockAlign : 0,
         fmt_float ? " float" : "", hr);
    return hr;
}

static long ac_padding(void *self, UINT32 *pad)
{
    if (is_fm6()) InterlockedIncrement(&st.padding_calls);
    return orig_padding(self, pad);
}

static long ac_start(void *self)
{
    long hr = orig_start(self);
    if (is_fm6()) wlog("audio FM6: Start(%p) hr=%#lx", self, hr);
    return hr;
}

static long ac_stop(void *self)
{
    long hr = orig_stop(self);
    if (is_fm6()) wlog("audio FM6: Stop(%p) hr=%#lx", self, hr);
    return hr;
}

static long ac_set_event(void *self, HANDLE ev)
{
    long hr = orig_set_event(self, ev);
    if (is_fm6()) wlog("audio FM6: SetEventHandle(%p, %p) hr=%#lx", self, ev, hr);
    return hr;
}

static long ac_get_service(void *self, const GUID *iid, void **out)
{
    long hr = orig_get_service(self, iid, out);
    if (!is_fm6()) return hr;
    if (iid && hr >= 0 && out && *out && !memcmp(iid, &iid_render_client, sizeof *iid)) hook_render_client(*out);
    else wlog("audio FM6: GetService(%p, %08lx-...) hr=%#lx", self, iid ? iid->Data1 : 0, hr);
    return hr;
}

int audio_fm6_install(void)
{
    void **vt = (void **)(g_base + RVA_FMOD_CLIENT_VT);
    static void *const hooks[] = { (void *)ac_init, (void *)ac_padding, (void *)ac_start, (void *)ac_stop,
                                   (void *)ac_set_event, (void *)ac_get_service };
    void **origs[] = { (void **)&orig_init, (void **)&orig_padding, (void **)&orig_start, (void **)&orig_stop,
                       (void **)&orig_set_event, (void **)&orig_get_service };
    size_t i, n = sizeof client_slots / sizeof *client_slots;
    for (i = 0; i < n; i++)
        if ((uint8_t *)vt[client_slots[i].slot] != g_base + client_slots[i].rva) {
            wlog("audio: vtable do FmodAudioClient slot %d = %p (esperado azimuth+%#x), diagnostico nao instalado",
                 client_slots[i].slot, vt[client_slots[i].slot], client_slots[i].rva);
            return 0;
        }
    for (i = 0; i < n; i++) swap_slot(vt, client_slots[i].slot, hooks[i], origs[i]);
    wlog("audio: FmodAudioClient (vtable %#x) com diagnostico do FM6", RVA_FMOD_CLIENT_VT);
    return 1;
}
