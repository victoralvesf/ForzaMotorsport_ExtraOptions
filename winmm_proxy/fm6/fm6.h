/* Forza Motorsport 6: declaracoes internas de fm6\ (so fm6\*.c incluem; a raiz/common usam so fm6_game) */
#ifndef FM6_H
#define FM6_H
#include <windows.h>
#include <stdint.h>

/* fm6.c */
int is_fm6(void);
int cfg6_int(const wchar_t *key, int def);   /* azimuth_fm6.ini [FM6] */

/* d3d.c */
FARPROC fm6_resolve_import(const uint8_t *entry, FARPROC az);
int fm6_batch_install(uint8_t *game);
int fm6_reexec_install(void);
int fm6_imm_srv_hold_install(void);
void fm6_vline_rate(uint32_t hz);

/* xg (fm6.c) */
void fm6_xg_computer(void *out, const char *name, uint32_t *desc);

/* gfx.c */
void fm6_car_detail(uint8_t *game, int level);
void fm6_shadows(uint8_t *game, int level);
/* font.c */
void fm6_dash_font_fix(uint8_t *game);

/* audio.c */
int audio_fm6_install(void);
void audio_fm6_game_hooks(uint8_t *game);
uint64_t fm6_shape_graph(uint8_t *proc, uint8_t *graph);

/* winrt.c */
long fm6_winrt_factory(const wchar_t *cls, const GUID *iid, void **out, int *handled);
uint64_t fm6_winrt_table_call(uint32_t slot, uint64_t *args, void *ret, double *fargs, int *handled);

/* livery.c (debug.ini LiveryLog) */
void fm6_livery_log_install(uint8_t *game);
void liv_note_pso(void *pso, int wait, int ctx);
void liv_note_skip(int ctx);
void liv_note_resolve(int ctx, UINT sw, UINT sh, UINT sfmt, UINT ss, UINT dw, UINT dh, UINT dfmt, UINT ds, UINT fmt);
void liv_note_dispatch(UINT x, UINT y, UINT z);
void liv_note_texture(const UINT *d, long hr);
void liv_note_placement(const UINT *d, UINT tile, UINT pitch, const void *addr, uintptr_t caller);
int liv_in_window(void);
extern int liv_log;
#endif
