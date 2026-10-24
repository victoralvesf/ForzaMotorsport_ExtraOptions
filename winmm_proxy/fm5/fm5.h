/* Forza Motorsport 5: declaracoes internas de fm5\ (so fm5\*.c incluem; a raiz/common usam so fm5_game) */
#ifndef FM5_H
#define FM5_H
#include <windows.h>
#include <stdint.h>

/* fm5.c */
int is_fm5(void);
int cfg_int(const wchar_t *key, int def);   /* azimuth_fm5.ini [FM5] */

/* d3d.c */
FARPROC fm5_resolve_import(const uint8_t *entry, FARPROC az);
void fm5_object_released(void *obj, void *caller);
int fm5_context_index(void *ctx);

/* xg.c */
void fm5_xg_computer(void *out, const char *name, uint32_t *desc);
uint32_t xg_fm5_zero_bc_tail(const uint32_t *desc, uint32_t tile, uint32_t pitch, uint8_t *addr);
void fm5_xg_send_install(void);

/* gfx.c */
void fm5_car_detail(uint8_t *game, int level);
void fm5_shadows(uint8_t *game, int level);
void fm5_reflections(uint8_t *game, int level);

/* shape.c, winrt.c */
uint64_t fm5_shape_graph(uint8_t *proc, uint8_t *graph);
long fm5_winrt_factory(const wchar_t *cls, const GUID *iid, void **out, int *handled);
long fm5_winrt_template(void *self, const void *name, void **out, int *handled);
#endif
