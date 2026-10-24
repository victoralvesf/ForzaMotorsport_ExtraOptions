/*
 * xma_decode.c - decodifica pacotes XMA2 crus (gravados pelo AudioDump, extraidos por tools\audio_dump.py --xma)
 * com o FFmpeg do Azimuth (data\bin\avcodec-62.dll), abrindo o decodificador como o Azimuth abre (0x140150cd0:
 * xma2, canais/taxa, block_align 0x800, extradata de 0x22 bytes montada em 0x14018ed10), e grava o PCM float
 * intercalado. Mostra quantas amostras saem de cada pacote e as primeiras amostras (o FFmpeg descarta o 1o quadro?).
 *
 * Build (de E:\Azimuth For FM5): gcc -O1 -o winmm_proxy\build\xma_decode.exe tools\xma_decode.c -static-libgcc -static
 * Uso: xma_decode.exe ENTRADA.xma SAIDA.f32 [DLLDIR]   (ENTRADA = u32 canais, u32 taxa, pacotes de 0x800)
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* offsets do FFmpeg 8.1 usados pelo Azimuth (AVCodecContext / AVFrame / AVPacket) */
#define CTX_EXTRADATA      0x48
#define CTX_EXTRADATA_SIZE 0x50
#define CTX_SAMPLE_RATE    0x158
#define CTX_CH_LAYOUT      0x160
#define CTX_BLOCK_ALIGN    0x17c
#define FRAME_EXT_DATA     0x60
#define FRAME_NB_SAMPLES   0x70
#define FRAME_FORMAT       0x74
#define FRAME_CHANNELS     0x184
#define PKT_DATA           0x18
#define AV_CODEC_ID_XMA2   0x15050

typedef void *(*find_fn)(int);
typedef void *(*alloc_ctx_fn)(void *);
typedef int (*open_fn)(void *, void *, void *);
typedef void *(*alloc_fn)(void);
typedef int (*new_packet_fn)(void *, int);
typedef int (*send_fn)(void *, void *);
typedef int (*recv_fn)(void *, void *);
typedef void (*unref_fn)(void *);
typedef void (*layout_fn)(void *, int);
typedef void *(*mallocz_fn)(size_t);

int main(int argc, char **argv)
{
    char dir[MAX_PATH], path[MAX_PATH];
    HMODULE avu, avc;
    FILE *in, *out;
    uint32_t ch, rate;
    uint8_t pkt_in[0x800];
    void *codec, *ctx, *pkt, *frame;
    uint8_t ext[0x22] = { 0 };
    long total = 0;
    int npkt = 0, shown = 0;
    if (argc < 3) { fprintf(stderr, "uso: xma_decode ENTRADA.xma SAIDA.f32 [DLLDIR]\n"); return 2; }
    snprintf(dir, sizeof dir, "%s", argc > 3 ? argv[3] : "data\\bin");
    snprintf(path, sizeof path, "%s\\avutil-60.dll", dir);
    avu = LoadLibraryA(path);
    snprintf(path, sizeof path, "%s\\avcodec-62.dll", dir);
    avc = LoadLibraryA(path);
    if (!avu || !avc) { fprintf(stderr, "FFmpeg nao carregou de %s\n", dir); return 1; }
#define AVC(t, n) t n = (t)GetProcAddress(avc, #n)
#define AVU(t, n) t n = (t)GetProcAddress(avu, #n)
    AVC(find_fn, avcodec_find_decoder); AVC(alloc_ctx_fn, avcodec_alloc_context3); AVC(open_fn, avcodec_open2);
    AVC(alloc_fn, av_packet_alloc); AVC(new_packet_fn, av_new_packet); AVC(send_fn, avcodec_send_packet);
    AVC(recv_fn, avcodec_receive_frame); AVC(unref_fn, av_packet_unref);
    AVU(alloc_fn, av_frame_alloc); AVU(unref_fn, av_frame_unref); AVU(layout_fn, av_channel_layout_default);
    AVU(mallocz_fn, av_mallocz);
    in = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (!in || !out || fread(&ch, 4, 1, in) != 1 || fread(&rate, 4, 1, in) != 1) { fprintf(stderr, "arquivos\n"); return 1; }
    /* extradata do Azimuth: u16 1, u16 (canais != 2) + 3, ..., +0xb = 0x80, +0x1f = 4 */
    ext[0] = 1; ext[2] = (uint8_t)((ch != 2) + 3); ext[0xb] = 0x80; ext[0x1f] = 4;
    codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    ctx = avcodec_alloc_context3(codec);
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    av_channel_layout_default((uint8_t *)ctx + CTX_CH_LAYOUT, (int)ch);
    *(int *)((uint8_t *)ctx + CTX_SAMPLE_RATE) = (int)rate;
    *(int *)((uint8_t *)ctx + CTX_BLOCK_ALIGN) = 0x800;
    *(int *)((uint8_t *)ctx + CTX_EXTRADATA_SIZE) = 0x22;
    *(void **)((uint8_t *)ctx + CTX_EXTRADATA) = av_mallocz(0x62);
    memcpy(*(void **)((uint8_t *)ctx + CTX_EXTRADATA), ext, 0x22);
    if (avcodec_open2(ctx, codec, NULL) < 0) { fprintf(stderr, "avcodec_open2\n"); return 1; }
    printf("%u canais %u Hz\n", ch, rate);
    while (fread(pkt_in, 1, sizeof pkt_in, in) == sizeof pkt_in) {
        long got = 0;
        int r;
        av_new_packet(pkt, 0x800);
        memcpy(*(uint8_t **)((uint8_t *)pkt + PKT_DATA), pkt_in, 0x800);
        r = avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);
        if (r < 0) printf("pacote %d: send %d\n", npkt, r);
        while (avcodec_receive_frame(ctx, frame) >= 0) {
            int n = *(int *)((uint8_t *)frame + FRAME_NB_SAMPLES), c, i;
            float **pl = *(float ***)((uint8_t *)frame + FRAME_EXT_DATA);
            if (*(int *)((uint8_t *)frame + FRAME_FORMAT) != 8) { printf("formato %d\n", *(int *)((uint8_t *)frame + FRAME_FORMAT)); return 1; }
            for (i = 0; i < n; i++)
                for (c = 0; c < (int)ch; c++) fwrite(&pl[c][i], 4, 1, out);
            if (shown < 1 && n) {
                printf("1o quadro de saida (amostra %ld): ", total);
                for (i = 0; i < 8 && i < n; i++) printf("%.4f ", pl[0][i]);
                printf("\n");
                shown++;
            }
            got += n;
            total += n;
            av_frame_unref(frame);
        }
        printf("pacote %d: %d quadros no cabecalho, %ld amostras saem\n", npkt, pkt_in[0] >> 2, got);
        npkt++;
    }
    /* o Azimuth nunca drena; aqui so conta o que ficaria preso no decodificador */
    avcodec_send_packet(ctx, NULL);
    {
        long rest = 0;
        while (avcodec_receive_frame(ctx, frame) >= 0) {
            rest += *(int *)((uint8_t *)frame + FRAME_NB_SAMPLES);
            av_frame_unref(frame);
        }
        printf("dreno no fim: %ld amostras (nao gravadas)\n", rest);
    }
    printf("total %ld amostras por canal\n", total);
    fclose(out);
    return 0;
}
