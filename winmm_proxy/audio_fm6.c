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
extern int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp_out);
extern int dbg_int(const wchar_t *key, int def);
extern int bytes_match(const uint8_t *p, const char *hex, size_t len);
extern uint8_t *alloc_near(uint8_t *mod, size_t size);
static void dump_output(const BYTE *buf, UINT32 frames, DWORD flags);
static void dump_xma_command(const uint32_t *msg);

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
static unsigned fmt_block, fmt_bits, fmt_channels;
static DWORD fmt_rate;
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
        wlog("audio FM6: ReleaseBuffer #%ld (%u frames, flags %#lx): total %llu frames, %llu with SILENT flag, "
             "%llu with sample != 0, peak %.4f; GetBuffer %ld (%ld failures), GetCurrentPadding %ld",
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
        if (log_point(k)) wlog("audio FM6: GetBuffer(%u) failed #%ld hr=%#lx", frames, k, hr);
    }
    last_buffer = hr >= 0 && data ? *data : NULL;
    last_frames = frames;
    return hr;
}

static long rc_release_buffer(void *self, UINT32 frames, DWORD flags)
{
    if (is_fm6() && is_game_rc(self)) {
        stats_release(frames, flags, frames <= last_frames ? last_buffer : NULL);
        dump_output(frames <= last_frames ? last_buffer : NULL, frames, flags);
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
    if (render_vt) { wlog("audio FM6: another render client vtable %p (already swapped %p), ignored", vt, render_vt); return; }
    render_vt = vt;
    swap_slot(vt, RC_GET_BUFFER, (void *)rc_get_buffer, (void **)&orig_get_buffer);
    swap_slot(vt, RC_RELEASE_BUFFER, (void *)rc_release_buffer, (void **)&orig_release_buffer);
    wlog("audio FM6: IAudioRenderClient %p (vtable %p): GetBuffer/ReleaseBuffer counted", rc, vt);
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
        fmt_rate = f->nSamplesPerSec;
        fmt_channels = f->nChannels;
    }
    wlog("audio FM6: Initialize(%p mode %d flags %#lx duration %lld period %lld fmt tag %#x %u channels %lu Hz %u bits "
         "block %u%s) hr=%#lx", self, share, flags, (long long)dur, (long long)period, f ? f->wFormatTag : 0,
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

/* ---- caminho que cria o ACP no FM6 (PASSO 53, parte 2) ----
   game+0x79e0b0 (init do som; chama 0x64d6b0 2x) -> 0x64d6b0 -> 0x666470 -> 0x656000 -> 0x646780 -> 0x6553b0
   (AllocateShapeContexts + AcpHalCreate). Mesma cadeia no FM5: 0xe0c710 -> 0xcb8510 -> ... -> 0xcba8d0.
   Mensagens ACP chegam ao Azimuth em 0x1400b0150(proc, msg): tipo = msg[0]. */
#define FM6_SOUND_INIT  0x79e0b0
#define FM6_SOUND_SUB   0x64d6b0
#define FM6_ACP_SETUP   0x6553b0
#define RVA_ACP_MESSAGE 0xb0150

typedef uint64_t (*fn8)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static fn8 orig_sound_init, orig_sound_sub, orig_acp_setup;
static void (*orig_acp_message)(uint8_t *proc, uint32_t *msg);
static volatile LONG acp_types[0x20];

/* repassa 8 argumentos inteiros (os de pilha nas mesmas posicoes) e so loga depois da chamada */
static void log_ret(const char *what, LONG n, uint64_t ret, void *caller)
{
    if (log_point(n)) wlog("audio FM6: %s #%ld -> %#llx (caller %p)", what, n, (unsigned long long)ret, caller);
}

static uint64_t fm6_sound_init(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g,
                               uint64_t h)
{
    static volatile LONG n;
    uint64_t r = orig_sound_init(a, b, c, d, e, f, g, h);
    log_ret("sound init (game+0x79e0b0)", InterlockedIncrement(&n), r, __builtin_return_address(0));
    return r;
}

static uint64_t fm6_sound_sub(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g,
                              uint64_t h)
{
    static volatile LONG n;
    uint64_t r = orig_sound_sub(a, b, c, d, e, f, g, h);
    log_ret("game+0x64d6b0", InterlockedIncrement(&n), r, __builtin_return_address(0));
    return r;
}

static uint64_t fm6_acp_setup(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g,
                              uint64_t h)
{
    static volatile LONG n;
    uint64_t r = orig_acp_setup(a, b, c, d, e, f, g, h);
    log_ret("ACP created (game+0x6553b0)", InterlockedIncrement(&n), r, __builtin_return_address(0));
    return r;
}

static void acp_message(uint8_t *proc, uint32_t *msg)
{
    if (is_fm6()) {
        uint32_t t = msg[0] < 0x1f ? msg[0] : 0x1f;
        LONG k = InterlockedIncrement(&acp_types[t]);
        if (log_point(k)) wlog("audio FM6: ACP message type %u #%ld", msg[0], k);
        if (msg[0] >= 5 && msg[0] <= 8) dump_xma_command(msg);
    }
    orig_acp_message(proc, msg);
}

/* ---- mensagem ACP 0x10 (conclusao de comando com tag) no layout do FM6 (PASSO 53 parte 3) ----
   O Azimuth (layout do FH2/FM5) poe o tag u64 em +0xc e um contador u32 em +0x14; o laco ACP do FM6
   (game+0x655c97) le o tag u64 em +0x10. Com o valor errado, [engine+0x244] nunca zera e [engine+0x240]
   (ultimo tag concluido) nao anda: o FMOD fica esperando (game+0x6553a0) o comando XMA terminar -> so
   silencio. PopMessage do IAcpHal do Azimuth (slot 3, 0x1400b16d0): no FM6 copia o tag para +0x10. */
#define RVA_ACP_POP_MESSAGE 0xb16d0
#define ACP_MSG_COMPLETION  0x10

static uint64_t (*orig_acp_pop)(void *hal, uint8_t *msg);
static volatile LONG acp_fixed;

static void fm6_acp_message_layout(uint8_t *msg)
{
    uint64_t tag;
    LONG k;
    if (*(uint32_t *)msg != ACP_MSG_COMPLETION) return;
    memcpy(&tag, msg + 0xc, sizeof tag);
    memcpy(msg + 0x10, &tag, sizeof tag);
    k = InterlockedIncrement(&acp_fixed);
    if (log_point(k))
        wlog("audio FM6: ACP message 0x10 #%ld (command type %u): tag %#llx at +0x10", k, *(uint32_t *)(msg + 8),
             (unsigned long long)tag);
}

static uint64_t acp_pop(void *hal, uint8_t *msg)
{
    uint64_t r = orig_acp_pop(hal, msg);
    if ((uint8_t)r && msg && is_fm6()) fm6_acp_message_layout(msg);
    return r;
}

/* ---- gravacao do audio (debug.ini AudioDump=N segundos; clique do som de navegacao do menu, PASSO 114) ----
   Comeca quando o usuario aperta F9 (o boot ate o menu leva mais que N segundos).
   audio_fm6_out.wav: saida do WASAPI do jogo (o que vai para o Windows), N segundos.
   audio_fm6_shape.bin: por grafo SHAPE consumido, as entradas, os contextos SRC (0x20) e XMA (0x40) de cada SRC
   antes e depois do interpretador, e os 128 floats de cada mix buffer levado por DMA ao jogo; e os comandos de
   contexto XMA (ACP tipos 5..8); na 1a SRC de cada voz (contexto SRC ainda nao renderizado) os pacotes XMA crus
   dos 2 buffers de entrada (decodificados fora do jogo por tools\xma_decode.c). Registro = u32 tipo, u32 tamanho,
   dados. Analise: tools\audio_dump.py.
   Tudo do lado do grafo roda na thread "Azimuth audio processor"; o WAV na thread de audio do jogo. */
#define RVA_SRC_COUNT 0x333790   /* contextos SRC: u32 quantidade, base em 0x3337a8 (0x20 cada) */
#define RVA_SRC_BASE  0x3337a8
#define RVA_XMA_COUNT 0x3337a0   /* contextos XMA: u32 quantidade, base em 0x3337c8 (0x40 cada) */
#define RVA_XMA_BASE  0x3337c8
#define DUMP_MAX_SRC  64
#define RVA_APU_PTR   0xaf3c0    /* (endereco APU u32, tamanho) -> ponteiro do processo ou 0 */

enum { DUMP_GRAPH = 1, DUMP_SRC = 2, DUMP_MIX = 3, DUMP_XMA_CMD = 4, DUMP_XMA_INPUT = 5 };

static void *(*apu_ptr)(uint32_t apu, uint64_t size);

typedef struct {
    HANDLE f;
    uint8_t *buf;
    DWORD used;
    uint64_t data_bytes;   /* WAV: bytes de amostras escritos */
} DumpFile;

static int dump_secs;                       /* AudioDump do debug.ini (0 = desligado) */
static wchar_t dump_dir[MAX_PATH];
static DumpFile dump_out, dump_shape;
static int dump_out_done, dump_shape_done;
static volatile LONG dump_armed;            /* F9 apertado */
static uint32_t dump_seq;
static struct { uint32_t ctx; uint8_t src[0x20], xma[0x40]; } dump_pre[DUMP_MAX_SRC];
static unsigned dump_pre_n;

#define DUMP_BUF (1u << 20)

static int dump_started(void)
{
    if (dump_armed) return 1;
    if (!(GetAsyncKeyState(VK_F9) & 0x8000)) return 0;
    if (!InterlockedExchange(&dump_armed, 1)) wlog("audio dump: F9, recording %d s", dump_secs);
    return 1;
}

static int dump_open(DumpFile *d, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    if (d->f) return 1;
    wcscpy(path, dump_dir);
    wcscat(path, name);
    d->f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (d->f == INVALID_HANDLE_VALUE) { d->f = NULL; wlog("audio dump: cannot create %ls", path); return 0; }
    d->buf = VirtualAlloc(NULL, DUMP_BUF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    d->used = 0;
    d->data_bytes = 0;
    wlog("audio dump: %ls", path);
    return d->buf != NULL;
}

static void dump_flush(DumpFile *d)
{
    DWORD w;
    if (d->f && d->used) WriteFile(d->f, d->buf, d->used, &w, NULL);
    d->used = 0;
}

static void dump_write(DumpFile *d, const void *p, DWORD n)
{
    if (!d->f) return;
    if (d->used + n > DUMP_BUF) dump_flush(d);
    if (n > DUMP_BUF) return;
    memcpy(d->buf + d->used, p, n);
    d->used += n;
}

static void dump_record(uint32_t type, const void *a, uint32_t na, const void *b, uint32_t nb)
{
    uint32_t h[2] = { type, na + nb };
    dump_write(&dump_shape, h, sizeof h);
    dump_write(&dump_shape, a, na);
    if (nb) dump_write(&dump_shape, b, nb);
}

/* cabecalho WAV (float 32 = WAVE_FORMAT_IEEE_FLOAT, senao PCM) com o tamanho dos dados ja escritos */
static void wav_header(uint8_t h[44], uint64_t data_bytes)
{
    uint32_t v, block = fmt_block;
    uint16_t s;
    memcpy(h, "RIFF", 4); v = (uint32_t)(36 + data_bytes); memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8); v = 16; memcpy(h + 16, &v, 4);
    s = fmt_float ? 3 : 1; memcpy(h + 20, &s, 2);
    s = (uint16_t)fmt_channels; memcpy(h + 22, &s, 2);
    memcpy(h + 24, &fmt_rate, 4);
    v = fmt_rate * block; memcpy(h + 28, &v, 4);
    s = (uint16_t)block; memcpy(h + 32, &s, 2);
    s = (uint16_t)fmt_bits; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4); v = (uint32_t)data_bytes; memcpy(h + 40, &v, 4);
}

static void dump_close(DumpFile *d, int wav)
{
    if (!d->f) return;
    dump_flush(d);
    if (wav) {
        uint8_t h[44];
        DWORD w;
        wav_header(h, d->data_bytes);
        SetFilePointer(d->f, 0, NULL, FILE_BEGIN);
        WriteFile(d->f, h, sizeof h, &w, NULL);
    }
    CloseHandle(d->f);
    VirtualFree(d->buf, 0, MEM_RELEASE);
    d->f = NULL;
    d->buf = NULL;
}

/* ReleaseBuffer do render client do jogo: os quadros vao para o WAV (SILENT = zeros) */
static void dump_output(const BYTE *buf, UINT32 frames, DWORD flags)
{
    static const uint8_t zero[4096];
    uint64_t n = (uint64_t)frames * fmt_block, limit = (uint64_t)dump_secs * fmt_rate * fmt_block;
    if (!dump_secs || dump_out_done || !fmt_block || !fmt_rate || !fmt_channels || !dump_started()) return;
    if (!dump_out.f) {
        uint8_t h[44];
        if (!dump_open(&dump_out, L"audio_fm6_out.wav")) { dump_out_done = 1; return; }
        wav_header(h, 0);
        dump_write(&dump_out, h, sizeof h);
    }
    if (dump_out.data_bytes + n > limit) n = limit - dump_out.data_bytes;
    if ((flags & 2) || !buf) {
        uint64_t k;
        for (k = 0; k < n; k += sizeof zero) dump_write(&dump_out, zero, (DWORD)(n - k < sizeof zero ? n - k : sizeof zero));
    } else dump_write(&dump_out, buf, (DWORD)n);
    dump_out.data_bytes += n;
    if (dump_out.data_bytes >= limit) {
        dump_close(&dump_out, 1);
        dump_out_done = 1;
        wlog("audio dump: audio_fm6_out.wav done (%d s)", dump_secs);
    }
}

int audio_dump_active(void) { return dump_secs > 0 && !dump_shape_done; }

static uint8_t *src_ctx(uint32_t i)
{
    uint8_t *base = *(uint8_t **)(g_base + RVA_SRC_BASE);
    return base && i < *(uint32_t *)(g_base + RVA_SRC_COUNT) ? base + (size_t)i * 0x20 : NULL;
}

static uint8_t *xma_ctx(uint32_t i)
{
    uint8_t *base = *(uint8_t **)(g_base + RVA_XMA_BASE);
    return base && i < *(uint32_t *)(g_base + RVA_XMA_COUNT) ? base + (size_t)i * 0x40 : NULL;
}

/* voz nova (SRC sem nada renderizado: +8 e +0xc zerados): os 2 buffers de entrada do contexto XMA
   (+0/+4 bits 0..11 = pacotes de 0x800, bits 20/21 de +0 = buffer valido, endereco APU em +0x14/+0x18) */
static void dump_xma_input(uint32_t ctx, const uint8_t *src, const uint8_t *xma)
{
    struct { uint32_t type, len, seq, ctx; uint8_t xma[0x40]; uint32_t size[2]; } h;
    const uint32_t *w = (const uint32_t *)xma;
    const void *data[2] = { NULL, NULL };
    int b;
    if (!apu_ptr || !dump_shape.f || *(const uint32_t *)(src + 8) || (*(const uint32_t *)(src + 0xc) & 0x1ffffff))
        return;
    for (b = 0; b < 2; b++) {
        h.size[b] = (w[0] >> (20 + b) & 1) ? (w[b] & 0xfff) * 0x800 : 0;
        if (h.size[b]) data[b] = apu_ptr(w[5 + b], h.size[b]);
        if (!data[b]) h.size[b] = 0;
    }
    h.type = DUMP_XMA_INPUT; h.len = sizeof h - 8 + h.size[0] + h.size[1];
    h.seq = dump_seq; h.ctx = ctx;
    memcpy(h.xma, xma, 0x40);
    dump_write(&dump_shape, &h, sizeof h);
    for (b = 0; b < 2; b++) if (h.size[b]) dump_write(&dump_shape, data[b], h.size[b]);
}

/* antes do interpretador: contextos SRC/XMA de cada entrada SRC (op 2; ctx = w>>13 & 0xfff) */
void audio_dump_graph_pre(uint8_t *graph)
{
    uint64_t *p = *(uint64_t **)(graph + 0x48), *e = *(uint64_t **)(graph + 0x50);
    dump_pre_n = 0;
    for (; p != e && dump_pre_n < DUMP_MAX_SRC; p++) {
        uint32_t ctx = (uint32_t)(*p >> 13) & 0xfff;
        uint8_t *s = src_ctx(ctx), *x = xma_ctx(ctx);
        if ((*p & 0x1f) != 2) continue;
        dump_pre[dump_pre_n].ctx = ctx;
        if (s) memcpy(dump_pre[dump_pre_n].src, s, 0x20); else memset(dump_pre[dump_pre_n].src, 0, 0x20);
        if (x) memcpy(dump_pre[dump_pre_n].xma, x, 0x40); else memset(dump_pre[dump_pre_n].xma, 0, 0x40);
        if (s && x) dump_xma_input(ctx, s, x);
        dump_pre_n++;
    }
}

/* mix buffer id -> 128 floats (proc+0x1238: u16 indice+1 por id; proc+0x1220: base dos blocos de 0x200) */
static const float *mix_buffer(uint8_t *proc, uint32_t id)
{
    uint16_t k = *(uint16_t *)(proc + 0x1238 + (size_t)id * 2);
    uint8_t *base = *(uint8_t **)(proc + 0x1220);
    return k && base ? (const float *)(base + (size_t)(k - 1) * 0x200) : NULL;
}

/* depois do interpretador, so se o grafo foi consumido (devolvido 0 = sera repassado igual) */
void audio_dump_graph_post(uint8_t *proc, uint8_t *graph, uint64_t ret)
{
    uint64_t *b = *(uint64_t **)(graph + 0x48), *e = *(uint64_t **)(graph + 0x50), *p;
    uint32_t n = (uint32_t)(e - b), i = 0;
    struct { uint32_t seq, id; uint64_t stamp; uint32_t n, pad; } g;
    if (!(uint8_t)ret || dump_shape_done || !dump_started()) return;
    if (!dump_shape.f) {
        if (!dump_open(&dump_shape, L"audio_fm6_shape.bin")) { dump_shape_done = 1; return; }
        dump_write(&dump_shape, "AZSH\1\0\0\0", 8);
    }
    g.seq = dump_seq; g.id = *(uint32_t *)(graph + 0x40); g.stamp = *(uint64_t *)(graph + 0x30); g.n = n; g.pad = 0;
    dump_record(DUMP_GRAPH, &g, sizeof g, b, n * 8);
    for (p = b; p != e; p++) {
        unsigned op = (unsigned)(*p & 0x1f);
        if (op == 2 && i < dump_pre_n) {
            struct { uint32_t seq, ctx; uint64_t reset; uint8_t src_pre[0x20], xma_pre[0x40], src[0x20], xma[0x40]; } s;
            uint8_t *sc, *xc;
            s.seq = dump_seq; s.ctx = dump_pre[i].ctx;
            s.reset = *(uint64_t *)(proc + 0x1e8 + (size_t)s.ctx * 8);
            memcpy(s.src_pre, dump_pre[i].src, 0x20); memcpy(s.xma_pre, dump_pre[i].xma, 0x40);
            sc = src_ctx(s.ctx); xc = xma_ctx(s.ctx);
            if (sc) memcpy(s.src, sc, 0x20); else memset(s.src, 0, 0x20);
            if (xc) memcpy(s.xma, xc, 0x40); else memset(s.xma, 0, 0x40);
            dump_record(DUMP_SRC, &s, sizeof s, NULL, 0);
            i++;
        } else if (op == 0x10) {
            struct { uint32_t seq, mix, dma, pad; } m;
            const float *f;
            static const float silence[128];
            m.seq = dump_seq; m.mix = (uint32_t)(*p >> 14) & 0x1fff; m.dma = (uint32_t)(*p >> 27) & 0xfff; m.pad = 0;
            f = mix_buffer(proc, m.mix);
            dump_record(DUMP_MIX, &m, sizeof m, f ? f : silence, 0x200);
        }
    }
    if (++dump_seq >= (uint32_t)dump_secs * 375) {   /* 48000 / 128 grafos por segundo */
        dump_close(&dump_shape, 0);
        dump_shape_done = 1;
        wlog("audio dump: audio_fm6_shape.bin done (%u graphs)", dump_seq);
    }
}

/* comandos de contexto XMA (5/7 = um contexto em *payload; 6/8 = mascara de 0x40 bytes), stamp em msg+0x30 */
static void dump_xma_command(const uint32_t *msg)
{
    struct { uint32_t seq, type; uint64_t stamp; uint8_t mask[0x40]; } r;
    const uint32_t *pl = *(const uint32_t *const *)(msg + 6);
    if (!dump_shape.f || dump_shape_done || !pl) return;
    r.seq = dump_seq; r.type = msg[0];
    memcpy(&r.stamp, msg + 0xc, 8);
    memset(r.mask, 0, sizeof r.mask);
    if (msg[0] == 5 || msg[0] == 7) { if (pl[0] < 0x200) r.mask[pl[0] >> 3] = (uint8_t)(1 << (pl[0] & 7)); }
    else memcpy(r.mask, pl, sizeof r.mask);
    dump_record(DUMP_XMA_CMD, &r, sizeof r, NULL, 0);
}

static void audio_dump_init(void)
{
    wchar_t *slash;
    dump_secs = dbg_int(L"AudioDump", 0);
    if (dump_secs <= 0) { dump_secs = 0; return; }
    if (!dump_dir[0]) {
        GetModuleFileNameW(NULL, dump_dir, MAX_PATH);
        slash = wcsrchr(dump_dir, L'\\');
        if (slash) slash[1] = 0; else dump_dir[0] = 0;
    }
    if (bytes_match(g_base + RVA_APU_PTR, "48895c2408574883ec208bd9488bfa", 15))
        apu_ptr = (void *(*)(uint32_t, uint64_t))(g_base + RVA_APU_PTR);
    else wlog("audio dump: APU address lookup (%#x) does not match, XMA input not recorded", RVA_APU_PTR);
    wlog("audio dump: AudioDump=%d s from F9 (FM6: audio_fm6_out.wav + audio_fm6_shape.bin)", dump_secs);
}

/* ---- inicio das vozes XMA (PASSO 114: clique do som de navegacao do menu do FM6) ----
   Ao criar a voz (0x14018ed10) o Azimuth descarta da saida do FFmpeg (subquadros a pular do contexto XMA, +7
   bits 0..2, + 4 x quadros antes do bit de inicio) x 128 amostras, como o hardware faz com a saida do decodificador.
   Mas o FFmpeg (xma2/wmapro) ja joga fora o 1o quadro (512 amostras) de cada decodificador novo: a saida dele
   comeca 512 amostras depois da do hardware. Com os 3 subquadros (384) que o FM6 pede em todo som, o ataque some
   e o som comeca no meio da onda (0 -> 0.46 numa amostra = clique). Aqui: descarte = max(0, conta - 512). */
#define RVA_XMA_DISCARD 0x18f28c   /* lea ecx,[rax+r12*4]; shl ecx,7; mov [r14+0x30],ecx */
#define RVA_XMA_DISCARD_BACK 0x18f297

int xma_start_patch(int on)
{
    static const uint8_t body[] = {
        0x42, 0x8d, 0x0c, 0xa0,                 /* lea ecx, [rax+r12*4] */
        0xc1, 0xe1, 0x07,                       /* shl ecx, 7 */
        0x81, 0xe9, 0x00, 0x02, 0x00, 0x00,     /* sub ecx, 512 (1o quadro que o FFmpeg ja descartou) */
        0x7d, 0x02,                             /* jge +2 */
        0x31, 0xc9,                             /* xor ecx, ecx */
        0x41, 0x89, 0x4e, 0x30,                 /* mov [r14+0x30], ecx */
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,     /* jmp [rip] -> volta */
    };
    uint8_t *at = g_base + RVA_XMA_DISCARD, *stub, *back = g_base + RVA_XMA_DISCARD_BACK;
    uint8_t jmp[11] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    int32_t rel;
    if (!on) return 0;
    if (!bytes_match(at - 8, "410fb6450783e007" "428d0ca0c1e10741894e30" "41816508000000fc", 27)) {
        wlog("audio: XMA voice start (azimuth+%#x) does not match, start discard unchanged", RVA_XMA_DISCARD);
        return 0;
    }
    if (!(stub = alloc_near(g_base, 64))) { wlog("audio: no memory near azimuth.exe for the XMA voice start"); return 0; }
    memcpy(stub, body, sizeof body);
    memcpy(stub + sizeof body, &back, 8);
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    rel = (int32_t)(stub - (at + 5));
    memcpy(jmp + 1, &rel, 4);
    write_mem(at, jmp, sizeof jmp);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof jmp);
    wlog("audio: XMA voices start 512 samples earlier (FFmpeg already drops the first frame)");
    return 1;
}

void audio_fm6_game_hooks(uint8_t *game)
{
    int ok = hook_at(game + FM6_SOUND_INIT, "4489442418895424105556574154", 14, (void *)fm6_sound_init,
                     (void **)&orig_sound_init);
    ok &= hook_at(game + FM6_SOUND_SUB, "40535556415641574881ec60010000", 15, (void *)fm6_sound_sub,
                  (void **)&orig_sound_sub);
    ok &= hook_at(game + FM6_ACP_SETUP, "40534881ecc0000000488bd933d2", 14, (void *)fm6_acp_setup,
                  (void **)&orig_acp_setup);
    wlog("audio FM6: sound init / ACP hooks %s", ok ? "installed" : "INCOMPLETE (bytes do not match)");
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
            wlog("audio: FmodAudioClient vtable slot %d = %p (expected azimuth+%#x), diagnostics not installed",
                 client_slots[i].slot, vt[client_slots[i].slot], client_slots[i].rva);
            return 0;
        }
    if (!hook_at(g_base + RVA_ACP_MESSAGE, "48895c241856574154415641574881ecd0000000", 20, (void *)acp_message,
                 (void **)&orig_acp_message))
        wlog("audio: ACP message processing (%#x) does not match, not counted", RVA_ACP_MESSAGE);
    if (!hook_at(g_base + RVA_ACP_POP_MESSAGE, "48895c24104889742418574883ec20", 15, (void *)acp_pop,
                 (void **)&orig_acp_pop))
        wlog("audio: ACP PopMessage (%#x) does not match, FM6 message 0x10 not fixed", RVA_ACP_POP_MESSAGE);
    for (i = 0; i < n; i++) swap_slot(vt, client_slots[i].slot, hooks[i], origs[i]);
    audio_dump_init();
    wlog("audio: FmodAudioClient (vtable %#x) with FM6 diagnostics", RVA_FMOD_CLIENT_VT);
    return 1;
}
