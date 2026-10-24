/*
 * Audio do Azimuth comum aos jogos: inicio das vozes XMA (padrao por jogo, game_defaults.xma_start_fix).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"

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
