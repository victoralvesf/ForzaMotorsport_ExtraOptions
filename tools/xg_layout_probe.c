/* xg_layout_probe.c - carrega um xg_x.dll de jogo e imprime Width/HeightElements de cada mip de uma textura 2D
 * (XG_RESOURCE_LAYOUT: plano em +0x18, Mip[15] de 0x48 em +0x28, Width/HeightElements em +0x30/+0x34).
 * Uso: xg_layout_probe.exe <xg_x.dll> <fm5|new> <largura> <altura> <mips> <formato DXGI> <tilemode>
 *   fm5 = desc do XDK nov-2012 (TileMode/Pitch logo apos o desc D3D11); new = FH2/FM6 (2 dwords ESRAM antes).
 * gcc -O2 -o build\xg_layout_probe.exe tools\xg_layout_probe.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef long (*create_fn)(const uint32_t *desc, void **computer);
typedef long (*layout_fn)(void *self, void *layout);

int main(int argc, char **argv)
{
    HMODULE m;
    create_fn create;
    uint32_t desc[16] = {0};
    void *comp = NULL;
    static uint8_t layout[0x2000];
    int fm5, mips, i, n;
    long hr;
    if (argc < 8) { fprintf(stderr, "uso: %s xg_x.dll fm5|new w h mips fmt tilemode\n", argv[0]); return 2; }
    m = LoadLibraryA(argv[1]);
    if (!m) { fprintf(stderr, "LoadLibrary %lu\n", GetLastError()); return 1; }
    create = (create_fn)GetProcAddress(m, "XGCreateTexture2DComputer");
    if (!create) { fprintf(stderr, "sem XGCreateTexture2DComputer\n"); return 1; }
    fm5 = !strcmp(argv[2], "fm5");
    mips = atoi(argv[5]);
    desc[0] = atoi(argv[3]); desc[1] = atoi(argv[4]); desc[2] = mips; desc[3] = 1; desc[4] = atoi(argv[6]);
    desc[5] = 1; desc[6] = 0; desc[7] = 0; desc[8] = 8; desc[9] = 0; desc[10] = 0;
    n = fm5 ? 11 : 13;
    desc[n] = atoi(argv[7]); desc[n + 1] = 0;
    hr = create(desc, &comp);
    if (hr < 0 || !comp) { printf("create hr=%#lx\n", hr); return 1; }
    hr = ((layout_fn)(*(void ***)comp)[2])(comp, layout);
    if (hr < 0) { printf("layout hr=%#lx\n", hr); return 1; }
    printf("size %#llx bpe %u\n", (unsigned long long)*(uint64_t *)(layout + 0x18 + 8), *(uint32_t *)(layout + 0x18 + 0x20));
    /* campos internos do computer (= layout copiado de +0x54b8): mips +0x54c8, bpe +0x54f0, mip 1 +0x5570/+0x5574 */
    printf("interno: mips %u bpe %u mip1 %ux%u\n", *(uint32_t *)((uint8_t *)comp + 0x54c8),
           *(uint32_t *)((uint8_t *)comp + 0x54f0), *(uint32_t *)((uint8_t *)comp + 0x5528 + 0x48),
           *(uint32_t *)((uint8_t *)comp + 0x552c + 0x48));
    for (i = 0; i < mips; i++) {
        uint8_t *mp = layout + 0x18 + 0x28 + i * 0x48;
        printf("mip %d: %u texels -> %u x %u elements\n", i, (unsigned)(atoi(argv[4]) >> i),
               *(uint32_t *)(mp + 0x30), *(uint32_t *)(mp + 0x34));
    }
    {   /* GetTexelElementOffsetBytes (slot 4 no XG do FM5, 6 no do FM6) na ultima linha de blocos de cada mip */
        typedef uint64_t (*texel_fn)(void *, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t, uint32_t);
        texel_fn texel = (texel_fn)(*(void ***)comp)[fm5 ? 4 : (argc > 8 ? atoi(argv[8]) : 4)];
        for (i = 0; i < mips; i++) {
            uint32_t h = *(uint32_t *)(layout + 0x18 + 0x28 + i * 0x48 + 0x34);
            printf("mip %d: y=%u -> %#llx, y=%u -> %#llx, y=%u -> %#llx\n", i, h - 1,
                   (unsigned long long)texel(comp, 0, i, 0, h - 1, 0, 0), h, (unsigned long long)texel(comp, 0, i, 0, h, 0, 0),
                   h + 1, (unsigned long long)texel(comp, 0, i, 0, h + 1, 0, 0));
        }
    }
    if (fm5 && argc > 8 && !strcmp(argv[8], "fix")) {
        /* corrige Width/HeightElements no proprio computer (+0x5528/+0x552c por mip) e confere se os enderecos
           de todos os elementos ja contados pelo XG continuam iguais */
        typedef uint64_t (*texel_fn)(void *, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t, uint32_t);
        texel_fn texel = (texel_fn)(*(void ***)comp)[4];
        uint8_t *c = comp;
        int diff = 0;
        for (i = 0; i < mips; i++) {
            uint32_t ow = *(uint32_t *)(c + 0x5528 + i * 0x48), oh = *(uint32_t *)(c + 0x552c + i * 0x48), x, y;
            uint32_t tw = desc[0] >> i ? desc[0] >> i : 1, th = desc[1] >> i ? desc[1] >> i : 1;
            uint64_t *before = malloc(sizeof(uint64_t) * ow * oh);
            for (y = 0; y < oh; y++) for (x = 0; x < ow; x++) before[y * ow + x] = texel(comp, 0, i, x, y, 0, 0);
            *(uint32_t *)(c + 0x5528 + i * 0x48) = (tw + 3) / 4;
            *(uint32_t *)(c + 0x552c + i * 0x48) = (th + 3) / 4;
            for (y = 0; y < oh; y++) for (x = 0; x < ow; x++) diff += before[y * ow + x] != texel(comp, 0, i, x, y, 0, 0);
            printf("fix mip %d: %ux%u -> %ux%u\n", i, ow, oh, (tw + 3) / 4, (th + 3) / 4);
            {   /* enderecos da grade corrigida: unicos e dentro de [inicio do mip, inicio do proximo) */
                uint32_t nw = (tw + 3) / 4, nh = (th + 3) / 4, k, j, dup = 0, out = 0;
                typedef uint64_t (*mipoff_fn)(void *, uint32_t, uint32_t);
                mipoff_fn mipoff = (mipoff_fn)(*(void ***)comp)[3];   /* GetMipLevelOffsetBytes */
                uint64_t lo = mipoff(comp, 0, i), hi = i + 1 < mips ? mipoff(comp, 0, i + 1) :
                    *(uint64_t *)(layout + 0x18 + 8);
                uint64_t *a = malloc(sizeof(uint64_t) * nw * nh);
                for (y = 0; y < nh; y++) for (x = 0; x < nw; x++) {
                    a[y * nw + x] = texel(comp, 0, i, x, y, 0, 0);
                    out += a[y * nw + x] < lo || a[y * nw + x] + 16 > hi;
                }
                for (k = 0; k < nw * nh; k++) for (j = k + 1; j < nw * nh; j++) dup += a[k] == a[j];
                printf("  [%#llx, %#llx) fora %u repetidos %u\n", (unsigned long long)lo, (unsigned long long)hi, out, dup);
                free(a);
            }
            free(before);
        }
        printf("enderecos diferentes apos a correcao: %d\n", diff);
    }
    return 0;
}
