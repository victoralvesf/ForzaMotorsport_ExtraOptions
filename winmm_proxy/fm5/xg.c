/*
 * XG do FM5 (xg_x.dll do XDK nov-2012) sob o Azimuth: layout do desc, vtable do computer e contagens dos mips BC.
 * So roda com o FM5 carregado (fm5_game.xg_computer; hook do envio instalado no apply_patches do FM5).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"
#include "fm5.h"

/* ------------------------------------------------------------------------ */
/* XG_TEXTURE2D/3D_DESC: o Azimuth monta no layout do XDK novo (desc D3D11 + 2 dwords ESRAM +  */
/* TileMode + Pitch); o XG do FM5 (XDK nov-2012) tem 1 dword so: TileMode/Pitch 4 bytes antes. */
/* 2D: D3D11 = 11 dwords (TileMode em [0xc], Pitch [0xd]); 3D: 9 dwords ([10], [11]).          */
/* So as chamadas internas do Azimuth passam por aqui; o jogo chama o XG com o proprio layout. */
/* ------------------------------------------------------------------------ */
/* XGTextureAddressComputer: o XG do FM5 tem 6 metodos (0 AddRef, 1 Release, 2 GetResourceLayout,
   3 GetMipLevelOffsetBytes, 4 GetTexelElementOffsetBytes, 5 CopyIntoSubresource); o XDK novo tem
   6 = CopyFromSubresource (tiled -> linear), que o Azimuth usa em 0x1400a30ca para montar o mapa
   de enderecos (destino linear com 0xff, origem tiled com indices) e que nao existe no FM5 ->
   implementado aqui com o slot 4. Os computers do Azimuth recebem esta vtable; os do jogo nao.
   Campos internos do computer do FM5 (layout em +0x54b8, plano a cada 0x460, mip a cada 0x48):
     +0x54c8 MipLevels, +0x54f0 BytesPerElement, +0x5528/+0x552c/+0x5530 Width/Height/Depth em
     elementos do mip, +0x61f0 dimensao (4 = 3D). */
#define XG_NEW_SLOTS 16
static void *xg_vtbl[XG_NEW_SLOTS];
static void *const *xg_fm5_vtbl;

#define XG_BAD_SLOT(n) static long xg_bad_slot_##n(void *self) \
    { wlog("XG: computer %p called slot %d (no equivalent in the FM5 XG)", self, n); return (long)0x80004001; }
XG_BAD_SLOT(7) XG_BAD_SLOT(8) XG_BAD_SLOT(9) XG_BAD_SLOT(10) XG_BAD_SLOT(11)
XG_BAD_SLOT(12) XG_BAD_SLOT(13) XG_BAD_SLOT(14) XG_BAD_SLOT(15)

typedef uint64_t (*xg_texel_offset_fn)(void *self, uint32_t plane, uint32_t mip, uint64_t x,
                                       uint32_t y, uint32_t z_or_slice, uint32_t sample);

/* XG do FM5: os mips > 0 de formatos BC tem (elementos do mip 0) >> mip blocos por eixo (960x540 BC7: 120x67 no
   mip 1, certo 120x68; o XG do FH2/FM6 conta certo). O Azimuth monta o mapa de enderecos (0x1400a0090/0x1400a2e00)
   so com os Width/HeightElements do layout -> a ultima linha/coluna de blocos desses mips nunca era escrita na
   textura do D3D12 (lixo da GPU: a linha colorida do flare do sol na altura do velocimetro, lensflare_lenstexture =
   sujeira da lente 960x540 BC7 reduzida -> o trilinear mistura o mip 1). xg_fix_bc_mips corrige as contagens no
   proprio computer e na copia do layout que o 0x14009fba0 ja fez em out+0x10 (o mapa sai dela; so no computer a
   linha nova nunca era enviada e ficava com o que o heap do D3D12 tinha). Os enderecos ja contados nao mudam, os
   novos sao os mesmos do XG do FM6 e cabem no padrao 8x8 do mapa comprimido do Azimuth (tools/xg_layout_probe.c
   ... fix). O asset nao tem esses blocos: xg_fm5_zero_bc_tail os zera na memoria do jogo. Desc 2D e 3D:
   Width [0], Height [1], Format [4]. */
static int xg_is_bc(uint32_t f) { return (f >= 70 && f <= 84) || (f >= 94 && f <= 99); }

static int xg_fix_bc_mips(uint8_t *self, const uint32_t *desc)
{
    uint32_t mips, i, fixed = 0;
    if (!xg_is_bc(desc[4])) return 0;
    mips = *(uint32_t *)(self + 0x54c8);
    if (!mips || mips > 15) return 0;
    if (*(uint32_t *)(self + 0x5528) != (desc[0] + 3) / 4 || *(uint32_t *)(self + 0x552c) != (desc[1] + 3) / 4)
        return 0;   /* mip 0 fora do esperado: nao mexer */
    for (i = 1; i < mips; i++) {
        uint32_t *we = (uint32_t *)(self + 0x5528 + i * 0x48), *he = we + 1;
        uint32_t w = desc[0] >> i ? desc[0] >> i : 1, h = desc[1] >> i ? desc[1] >> i : 1;
        if (*we < (w + 3) / 4) { *we = (w + 3) / 4; fixed++; }
        if (*he < (h + 3) / 4) { *he = (h + 3) / 4; fixed++; }
    }
    return (int)fixed;
}

static long xg_copy_from_subresource(uint8_t *self, uint8_t *dst, uint32_t plane, uint32_t subres,
                                     const uint8_t *src, uint32_t row_pitch, uint32_t depth_pitch)
{
    xg_texel_offset_fn texel = (xg_texel_offset_fn)xg_fm5_vtbl[4];
    uint32_t mips = *(uint32_t *)(self + 0x54c8), mip, slice, w, h, d, x, y, z, bpe;
    const uint8_t *pl = self + plane * 0x460;
    if (!mips) return (long)0x80070057;
    mip = subres % mips; slice = subres / mips;
    bpe = *(uint32_t *)(pl + 0x54f0);
    w = *(uint32_t *)(pl + mip * 0x48 + 0x5528);
    h = *(uint32_t *)(pl + mip * 0x48 + 0x552c);
    d = *(uint32_t *)(self + 0x61f0) == 4 ? *(uint32_t *)(pl + mip * 0x48 + 0x5530) : 1;
    for (z = 0; z < d; z++)
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint64_t off = texel(self, plane, mip, x, y, d > 1 ? z : slice, 0);
                if (off == (uint64_t)-1) return (long)0x80070057;
                memcpy(dst + (size_t)z * depth_pitch + (size_t)y * row_pitch + (size_t)x * bpe, src + off, bpe);
            }
    return 0;
}

static void xg_wrap_computer(void **obj)
{
    if (!xg_fm5_vtbl) {
        void *bad[] = { xg_bad_slot_7, xg_bad_slot_8, xg_bad_slot_9, xg_bad_slot_10, xg_bad_slot_11,
                        xg_bad_slot_12, xg_bad_slot_13, xg_bad_slot_14, xg_bad_slot_15 };
        int i;
        xg_fm5_vtbl = *(void *const **)obj;
        for (i = 0; i < XG_NEW_SLOTS; i++)
            xg_vtbl[i] = i < 6 ? xg_fm5_vtbl[i] : i == 6 ? (void *)xg_copy_from_subresource : bad[i - 7];
        wlog("XG: computer %p vtable -> new layout (6 = own CopyFromSubresource)", (void *)xg_fm5_vtbl);
    }
    if (*(void *const **)obj == xg_fm5_vtbl) *obj = xg_vtbl;
}



/* O Azimuth tambem cria computers fora do 0x14009fba0: o TextureComputer::learn_later (0x1400a1370) monta o mapa de
   enderecos numa thread de trabalho com um computer novo, chamando o XG direto com o desc guardado -> contagens do
   XG do FM5 (linha 67 fora do mapa: a linha do flare continuava). Por isso o proprio XGCreateTexture2D/3DComputer
   do xg_x.dll do FM5 e enganchado (prologo de 16 bytes conferido) e corrige as contagens de todo computer. Os
   enderecos vem do mapa nome -> funcao do carregador de XG do Azimuth (std::map em [0x14037cff8]+0x28, criado no
   1o 0x14009fba0): no' [0] esq, [1] pai, [2] dir, +0x19 nulo, chave std::string em +0x20, funcao em +0x40. */
#define RVA_XG_LOADER 0x37cff8
typedef long (*xg_create_fn)(const uint32_t *desc, void **computer);
static xg_create_fn orig_xg_create[2];

static long xg_create_fixed(int k, const uint32_t *desc, void **computer)
{
    long hr = orig_xg_create[k](desc, computer);
    if (hr >= 0 && computer && *computer && xg_fix_bc_mips(*computer, desc)) {
        static LONG logged;
        if (InterlockedIncrement(&logged) <= 16)
            wlog("XG: BC %ux%u fmt %u: mip element counts rounded up in the FM5 XG", desc[0], desc[1], desc[4]);
    }
    return hr;
}
static long xg_create2d_fixed(const uint32_t *desc, void **computer) { return xg_create_fixed(0, desc, computer); }
static long xg_create3d_fixed(const uint32_t *desc, void **computer) { return xg_create_fixed(1, desc, computer); }

static void *xg_loader_find(const char *name)
{
    uint8_t *loader = *(uint8_t **)(g_base + RVA_XG_LOADER), *head, *stack[64], *nd;
    int sp = 0;
    size_t len = strlen(name);
    if (!loader || !(head = *(uint8_t **)(loader + 0x28))) return NULL;
    stack[sp++] = *(uint8_t **)(head + 8);
    while (sp) {
        nd = stack[--sp];
        if (!nd || nd == head || nd[0x19]) continue;
        {
            uint64_t sz = *(uint64_t *)(nd + 0x30), cap = *(uint64_t *)(nd + 0x38);
            const char *key = cap > 15 ? *(const char **)(nd + 0x20) : (const char *)(nd + 0x20);
            if (sz == len && !memcmp(key, name, len)) return *(void **)(nd + 0x40);
        }
        if (sp < 62) { stack[sp++] = *(uint8_t **)nd; stack[sp++] = *(uint8_t **)(nd + 0x10); }
    }
    return NULL;
}

static void xg_hook_fm5_create(void)
{
    static int done;
    static const char *const names[2] = { "XGCreateTexture2DComputer", "XGCreateTexture3DComputer" };
    void *const hooks[2] = { (void *)xg_create2d_fixed, (void *)xg_create3d_fixed };
    int k;
    if (done) return;
    done = 1;
    for (k = 0; k < 2; k++) {
        uint8_t *fn = xg_loader_find(names[k]);
        if (!fn || !hook_at(fn, "48896c2410574883ec20488bfa488be9", 16, hooks[k], (void **)&orig_xg_create[k]))
            wlog("XG: FM5 %s at %p not hooked (mip counts stay per Azimuth computer)", names[k], (void *)fn);
        else
            wlog("XG: FM5 %s at %p hooked (BC mip element counts rounded up)", names[k], (void *)fn);
    }
}


void fm5_xg_computer(void *out, const char *name, uint32_t *desc)
{
    {
        uint32_t n = !strcmp(name, "XGCreateTexture2DComputer") ? 11 :
                     !strcmp(name, "XGCreateTexture3DComputer") ? 9 : 0;
        if (n) {  /* desc e copia local do chamador: converter no lugar */
            desc[n + 1] = desc[n + 2];   /* TileMode */
            desc[n + 2] = desc[n + 3];   /* Pitch */
            desc[n + 3] = 0;
        }
        xg_computer_orig(out, name, desc);
        xg_hook_fm5_create();   /* carregador do XG ja existe apos o 1o original */
        if (*(void ***)out) {   /* out[0] = computer */
            if (n && xg_fix_bc_mips(*(uint8_t **)out, desc)) {
                /* o 0x14009fba0 ja copiou o XG_RESOURCE_LAYOUT do computer p/ out+0x10 (e dele que sai o mapa de
                   enderecos, 0x1400a0090): as contagens novas vao tambem para essa copia */
                const uint8_t *self = *(uint8_t **)out;
                uint8_t *lay = (uint8_t *)out + 0x10;
                uint32_t i, mips = *(const uint32_t *)(self + 0x54c8);
                for (i = 1; i < mips; i++) {
                    *(uint32_t *)(lay + 0x70 + i * 0x48) = *(const uint32_t *)(self + 0x5528 + i * 0x48);
                    *(uint32_t *)(lay + 0x74 + i * 0x48) = *(const uint32_t *)(self + 0x552c + i * 0x48);
                }
                static LONG logged;
                if (InterlockedIncrement(&logged) <= 16)
                    wlog("XG: BC %ux%u mips %u fmt %u tile %u: mip element counts rounded up", desc[0], desc[1],
                         desc[2], desc[4], desc[n + 1]);
            }
            xg_wrap_computer(*(void ***)out);
        }
    }
}


/* Os blocos que xg_fix_bc_mips acrescenta nao tem conteudo no asset (o XG do FM5 nunca os conta; o jogo nunca escreve
   ali): na memoria do jogo fica o que sobrou de outro uso do pool (0 numa sessao, blocos de outra textura em outra:
   captura flare7), no console a memoria nova vem zerada. CreatePlacementTexture2D do FM5 chama isto ANTES de criar a
   textura (o Azimuth ja envia o conteudo na criacao; zerado depois nao chegava, captura flare8): zera esses elementos
   nos enderecos do proprio XG (dentro de SizeBytes, +0x54b8). Devolve quantos elementos zerou. */
uint32_t xg_fm5_zero_bc_tail(const uint32_t *desc, uint32_t tile, uint32_t pitch, uint8_t *addr)
{
    uint32_t d[16] = {0}, mips, mip, slice, arr, w, h, ow, oh, bpe, x, y, n = 0;
    /* saida do 0x14009fba0: [0] = computer, +0x10 = XG_RESOURCE_LAYOUT (0xd40) copiado pelo proprio Azimuth */
    uint64_t out[(0x10 + 0xd40) / 8] = {0};
    uint8_t *self;
    uint64_t size;
    if (!addr || !xg_is_bc(desc[4])) return 0;
    memcpy(d, desc, 11 * sizeof *d);
    d[13] = tile;
    d[14] = pitch;
    fm5_xg_computer(out, "XGCreateTexture2DComputer", d);
    if (!(self = (uint8_t *)(uintptr_t)out[0])) return 0;
    mips = *(uint32_t *)(self + 0x54c8);
    size = *(uint64_t *)(self + 0x54b8);
    bpe = *(uint32_t *)(self + 0x54f0);
    arr = desc[3] ? desc[3] : 1;
    if (mips && mips <= 15 && bpe && bpe <= 16 && xg_fm5_vtbl)
        for (slice = 0; slice < arr; slice++)
            for (mip = 1; mip < mips; mip++) {
                w = *(uint32_t *)(self + mip * 0x48 + 0x5528);
                h = *(uint32_t *)(self + mip * 0x48 + 0x552c);
                ow = *(uint32_t *)(self + 0x5528) >> mip; if (!ow) ow = 1;
                oh = *(uint32_t *)(self + 0x552c) >> mip; if (!oh) oh = 1;
                for (y = 0; y < h; y++)
                    for (x = y < oh ? ow : 0; x < w; x++) {
                        uint64_t off = ((xg_texel_offset_fn)xg_fm5_vtbl[4])(self, 0, mip, x, y, slice, 0);
                        if (off == (uint64_t)-1 || off + bpe > size) continue;
                        memset(addr + off, 0, bpe);
                        n++;
                    }
            }
    ((unsigned long (*)(void *))(*(void ***)self)[1])(self);   /* Release (slot 1) */
    return n;
}

/* Envio por subrecurso das placement textures (0x14014c2f0; closure: [0] itens de 0x10 bytes {registro, ...}, [1]
   registros de 0x90 bytes {subrecurso, ..., [8] row pitch, [10] linhas de blocos, +0x60 mapa}). O ramo de GPU usa o
   mapa comprimido do Azimuth (0x1400a0090: +0 valido, +8/+0xc largura/altura em elementos, +0x14/+0x18 tiles 8x8,
   +0x1c padrao de 64, +0x120 bases por tile), que nas lentes do FM5 sai com a altura do XG do FM5 (mip 1: 120x67,
   tiles 15x9) apesar de o registro pedir 68 linhas -> a ultima linha de blocos dos mips BC nunca era escrita (a linha
   do flare do sol; diag "send" e dump da memoria x captura, PASSO FLARE cont. 8). Se os tiles ja cobrem as linhas do
   registro, a altura do mapa sobe para elas: os enderecos novos cabem no padrao 8x8 (tools/xg_layout_probe.c fix). */
#define RVA_XG_SEND 0x14c2f0
static void *(*orig_xg_send)(uint64_t *cl, uint64_t item);
static void *hook_xg_send(uint64_t *cl, uint64_t item)
{
    const uint32_t *it = (const uint32_t *)((item & 0xffffffff) * 0x10 + *(uint64_t *)cl[0]);
    const uint32_t *rec = (const uint32_t *)((uint64_t)it[0] * 0x90 + *(uint64_t *)cl[1]);
    uint8_t *map = *(uint8_t **)(rec + 0x18);
    if (map && map[0]) {
        uint32_t *h = (uint32_t *)(map + 0xc), rows = rec[10];
        if (*h < rows && *(const uint32_t *)(map + 0x18) * 8 >= rows && rows - *h < 8) {
            static LONG logged;
            if (InterlockedIncrement(&logged) <= 16)
                wlog("XG: address map %ux%u -> %u rows (subresource %u of an upload of %u rows)",
                     *(const uint32_t *)(map + 8), *h, rows, rec[0], rows);
            *h = rows;
        }
    }
    return orig_xg_send(cl, item);
}

/* apply_patches do FM5: o envio so e enganchado com o FM5 carregado (antes de o jogo rodar, nada o chama) */
void fm5_xg_send_install(void)
{
    if (hook_function(RVA_XG_SEND, "405355565741544155415641574883ec48", 17, (void *)hook_xg_send,
                      (void **)&orig_xg_send))
        wlog("patch %#x: placement texture upload hooked (FM5 BC mip map height)", RVA_XG_SEND);
}
