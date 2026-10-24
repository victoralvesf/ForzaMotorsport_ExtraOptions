/* common/ui.c: resolucao/viewport da UI e modo de present por jogo */
#ifndef COMMON_UI_H
#define COMMON_UI_H
#include <stdint.h>

typedef struct {
    const char *tag;
    uint32_t height;                 /* mov dword [rbx+X], 0x438 (opcao defaultverticalresolution) */
    const char *height_orig;
    uint32_t width;                  /* vmulss ; vmulss ; vcvttss2si r64,xmm2 (+0x10) */
    const char *width_orig;
    uint8_t width_mov;               /* opcode de mov r32, imm32 no registro do vcvttss2si */
    uint32_t ui_size;                /* float([r+8]), float([r+0xc]) -> os 2 floats passados ao setter da UI */
    const char *ui_size_orig;
    const uint8_t *ui_size_code;
    uint32_t ui_size_len;
    uint32_t vp_call;                /* call [rax+off] do viewport da UI */
    const char *vp_call_orig;
    uint32_t vp_off;
    uint32_t vp_slot;                /* 8 bytes cc de padding (ponteiro p/ fm5_ui_vcall) */
    uint32_t chart_vp;               /* mov rax,[rcx] ; 2 stores do retangulo ; call [rax+chart_off] (graficos da UI) */
    uint8_t chart_off;               /* SetViewport do renderizador dos graficos */
    uint32_t chart_slot;             /* 8 bytes cc de padding (ponteiro p/ ui_chart_vcall) */
} ResSites;

void game_resolution(uint8_t *game, const ResSites *rs, uint32_t height, uint32_t width);

/* modo de present: o setter do Frame (slot do jogo) aponta para fm5_frame_set_sync_stub */
extern uint8_t *fps_game;
extern int fps_force;              /* frame_rate >= 60: modo 1 (30 fps) vira 0 */
extern const char *fps_tag;
extern uintptr_t fps_queue_ret;    /* retorno da chamada do executor da fila (FM5); 0 = nao mapeado */
void fm5_frame_set_sync_stub(void);

const char *gfx_level_name(int l);
#endif
