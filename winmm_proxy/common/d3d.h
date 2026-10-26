/* common/d3d.c: utilidades e hooks D3D11 do Azimuth comuns aos jogos */
#ifndef COMMON_D3D_H
#define COMMON_D3D_H
#include <windows.h>
#include <stdint.h>

#define RVA_CREATE_XAUDIO2 0x181b10   /* CreateXAudio2Object do Azimuth */

const char *msvc_str(const uint8_t *s, size_t *len);   /* std::string do MSVC */
int str_is(const uint8_t *s, const char *want);
/* ImportEntry {dll, nome}: o resolve_import de cada jogo termina com import_finish(entry, p) */
FARPROC import_finish(const uint8_t *entry, FARPROC p);
/* XAudio2Create do XDK -> CreateXAudio2Object do Azimuth com processador -1 (FM5 e FM6) */
long WINAPI xaudio2_create_bridge(void **pp, UINT flags, UINT processor);
void *az_release_tramp(void);   /* trampolim do Release comum (0x1400da9e0) */
void trace_skips(void);

/* common/shader.c: DXBC */
uint8_t *shader_console_rewrite(const void *code, size_t size, size_t *out_size);   /* opcodes do console -> PC */
uint8_t *shader_a2c(const void *code, size_t size, unsigned samples, size_t *out_size);  /* A2C no pixel shader */

/* common/stutter.c (PASSO 99): rastreio das placement textures placed */
int pa_tracking(void);
void pa_note_begin(const void *addr, uintptr_t caller);
void pa_note_end(const void *obj);
void pa_obj_gone(const void *obj);
/* ---- common/d3d11.c: D3D11.X do Azimuth comum as pontes do FM5 e do FM6 ---- */
#define RVA_CREATE_DEVICEX 0x58090   /* HLE de D3D11XCreateDeviceX(desc, ppDevice, ppContext) */
#define SLOTS        0x10e           /* tabelas de metodos do Azimuth: 0x870 bytes */
typedef void (*set_fn)(void *ctx, UINT start, UINT n, void *const *pp);
typedef long (*defctx_fn)(void *dev, UINT flags, void **out);
typedef long (*tex2d_fn)(void *dev, const UINT *desc, const void *init, void **out);
typedef void (*tex2d_getdesc_fn)(void *tex, UINT *d);
typedef long (*srv_fn)(void *dev, void *res, const UINT *desc, void **out);
typedef long (*ptex2d_fn)(void *dev, const UINT *desc, UINT tile, UINT pitch, void *addr, void **out);
typedef long (*state_fn)(void *dev, const UINT *desc, void **out);
typedef long (*rtv_fn)(void *dev, void *res, const UINT *desc, void **out);
typedef long (*createx_fn)(const void *desc, void **dev, void **ctx);
#define RVA_VTBL_TABLES   0x37d1e0   /* tipo t: +t*0x870 (montadas por 0x1400f36d0) */
#define RVA_VTBL_GET      0xf47a0    /* garante a montagem e devolve a tabela do tipo */
#define NTYPES            0x12

/* SetFast* (X) sobre os Set* padrao, contexto real em rcx */
#define D3D_FAST_DECL(name) void ctx_##name(void *real, UINT slot, void *obj);
D3D_FAST_DECL(PSSetFastConstantBuffer) D3D_FAST_DECL(PSSetFastShaderResource)
D3D_FAST_DECL(VSSetFastConstantBuffer) D3D_FAST_DECL(VSSetFastShaderResource) D3D_FAST_DECL(VSSetFastSampler)
D3D_FAST_DECL(GSSetFastConstantBuffer) D3D_FAST_DECL(GSSetFastShaderResource) D3D_FAST_DECL(GSSetFastSampler)
D3D_FAST_DECL(CSSetFastConstantBuffer) D3D_FAST_DECL(CSSetFastShaderResource) D3D_FAST_DECL(CSSetFastSampler)
D3D_FAST_DECL(HSSetFastConstantBuffer) D3D_FAST_DECL(HSSetFastShaderResource) D3D_FAST_DECL(HSSetFastSampler)
D3D_FAST_DECL(DSSetFastConstantBuffer) D3D_FAST_DECL(DSSetFastShaderResource) D3D_FAST_DECL(DSSetFastSampler)
void ctx_IASetFastVertexBuffer(void *real, UINT slot, void *buf, UINT stride);
void ctx_IASetFastIndexBuffer(void *real, UINT hwfmt, void *buf);

/* aliasing da ESRAM (cena MSAA -> profundidade/cor 1x lidas pelo pos) */
extern int alias_test;
extern SRWLOCK alias_lock;
extern void *al_color2x, *al_depth_a, *al_depth_b, *al_color1x;
extern UINT al_w, al_h;
extern volatile LONG al_pending, al_count;
void alias_track_ex(const UINT *desc, void *tex, int esram);
void alias_track(const UINT *desc, void *tex);
void alias_rtv_note(void *rtv, void *res);
typedef void *(*alias_fn_lookup)(void *ctx, int slot);
int alias_fm6_dispatch(void *ctx, UINT x, UINT y, UINT z, int bundle, alias_fn_lookup fn);
void alias_fm6_leave_scene(void *ctx, UINT n, void *const *rtvs, int clear, alias_fn_lookup fn);

/* Quality MSAA pedido pelo jogo (o D3D12 recebe o padrao) */
void msaa_q_set(void *tex, UINT q);
void msaa_q_set_desc(void *tex, UINT count, UINT q);
UINT msaa_q_count(void *tex);
int msaa_q_get(void *tex, UINT *q);
void msaa_q_fix(void *tex, UINT *d, const uint8_t *ret, const uint8_t *game, size_t size);

/* FMASK emulada (2x R8_UINT, 4x R32_UINT) */
UINT host_samples(UINT samples);
UINT host_msaa_mode(void);
UINT fmask_value(UINT samples, UINT host, UINT fmt);
void *fmask_emul(void *dev, tex2d_fn create, UINT w, UINT h, UINT samples, UINT fmt);
long srv_fmask(void *dev, void *res, const UINT *desc, void **out, tex2d_fn create, srv_fn srv);

/* tabelas de metodos por tipo de objeto do Azimuth */
enum { K_NONE, K_CHILD, K_RES, K_DXGI, K_CTX };
extern const uint8_t type_kind[NTYPES];
void drop_slot(void **t, int first);
#endif
