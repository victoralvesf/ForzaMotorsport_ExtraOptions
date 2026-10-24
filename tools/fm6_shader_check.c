/*
 * fm6_shader_check DIR - passa cada DXBC de DIR (ex. extraidos de um dump) pelo shader_console_rewrite do proxy e
 * confere com o d3dcompiler_47 do Windows (D3DDisassemble rejeita opcodes que o PC nao conhece, como o parser do
 * D3D12; nos opcodes do console ele LANCA excecao C++ e o processo morre, por isso o original so e conferido quando
 * o nome nao comeca com 'x' = extraido com 0x11a): original aceito?, convertido aceito?. -v grava o .asm do 1o
 * convertido. Antes de cada conversao imprime o nome (se morrer, o ultimo nome e o culpado).
 * Build: gcc -O1 -o build\fm6_shader_check.exe ..\tools\fm6_shader_check.c common/shader.c (de winmm_proxy)
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

uint8_t *shader_console_rewrite(const void *code, size_t size, size_t *out_size);
void wlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

typedef struct blob { struct blob_vt *vt; } blob;
struct blob_vt { void *qi, *addref; ULONG (__stdcall *Release)(blob *); void *(__stdcall *Ptr)(blob *);
                 SIZE_T (__stdcall *Size)(blob *); };
typedef HRESULT (WINAPI *disasm_fn)(const void *, SIZE_T, UINT, const char *, blob **);

static int accepted(disasm_fn dis, const void *p, size_t n, const char *save)
{
    blob *b = NULL;
    HRESULT hr = dis(p, n, 0, NULL, &b);
    if (SUCCEEDED(hr) && save) {
        FILE *f = fopen(save, "wb");
        if (f) { fwrite(b->vt->Ptr(b), 1, b->vt->Size(b) - 1, f); fclose(f); }
    }
    if (b) b->vt->Release(b);
    return SUCCEEDED(hr);
}

int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    disasm_fn dis = m ? (disasm_fn)GetProcAddress(m, "D3DDisassemble") : NULL;
    char pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int verbose = argc > 2 && !strcmp(argv[2], "-v"), n = 0, orig_ok = 0, conv = 0, conv_ok = 0, saved = 0;
    if (!dis || argc < 2) { puts("uso: fm6_shader_check DIR [-v] (precisa do d3dcompiler_47.dll)"); return 1; }
    snprintf(pat, sizeof pat, "%s\\*.bin", argv[1]);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return 1;
    do {
        FILE *f;
        long sz;
        uint8_t *d, *r;
        size_t rn = 0;
        snprintf(path, sizeof path, "%s\\%s", argv[1], fd.cFileName);
        if (!(f = fopen(path, "rb"))) continue;
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        d = malloc(sz);
        fread(d, 1, sz, f); fclose(f);
        n++;
        if (fd.cFileName[0] != 'x') orig_ok += accepted(dis, d, sz, NULL);
        if ((r = shader_console_rewrite(d, sz, &rn))) {
            int ok;
            char asmp[MAX_PATH];
            snprintf(asmp, sizeof asmp, "%s\\%s.asm", argv[1], fd.cFileName);
            conv++;
            printf("convertendo %s\n", fd.cFileName);
            fflush(stdout);
            ok = accepted(dis, r, rn, verbose && !saved ? asmp : NULL);
            saved |= ok && verbose;
            conv_ok += ok;
            if (!ok) printf("REJEITADO depois da conversao: %s\n", fd.cFileName);
            free(r);
        }
        free(d);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    printf("%d shaders: %d aceitos como vieram (sem os 'x'); %d convertidos, %d aceitos depois\n", n, orig_ok, conv, conv_ok);
    return conv != conv_ok;
}
