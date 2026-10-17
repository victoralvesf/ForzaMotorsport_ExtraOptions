/*
 * Runner dos testes do proxy: tests.exe [filtro] -> roda os testes cujo nome contem o filtro.
 * Utilidades: mapeamento de PE (secoes + relocacoes) e SHA-256 (bcrypt) para os testes de assinatura.
 */
#include "test.h"
#include <stdarg.h>
#include <stdlib.h>
#include <bcrypt.h>

#define MAX_TESTS 256
static struct { const char *name, *file; test_fn fn; } tests[MAX_TESTS];
static int ntests;
static const char *cur_test;
static int skipped;
int t_failed;

void test_register(const char *name, const char *file, test_fn fn)
{
    const char *s = strrchr(file, '/');
    const char *b = strrchr(file, '\\');
    if (b > s) s = b;
    if (ntests < MAX_TESTS) { tests[ntests].name = name; tests[ntests].file = s ? s + 1 : file; tests[ntests].fn = fn; ntests++; }
}

void test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    const char *s = strrchr(file, '/');
    if (!s) s = strrchr(file, '\\');
    if (t_failed++ < 20) {
        printf("    FALHA %s:%d: ", s ? s + 1 : file, line);
        va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
        printf("\n");
    }
}

void test_skip(const char *why) { skipped = 1; printf("    (pulado: %s)\n", why); }

int t_hex_eq(const uint8_t *p, const char *hex)
{
    size_t i, n = strlen(hex) / 2;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + i * 2, "%2x", &v) != 1 || p[i] != v) return 0;
    }
    return 1;
}

const char *t_hex(const uint8_t *p, size_t n)
{
    static char buf[2][257];
    static int k;
    char *o = buf[k ^= 1];
    size_t i;
    if (n > 128) n = 128;
    for (i = 0; i < n; i++) sprintf(o + i * 2, "%02x", p[i]);
    o[n * 2] = 0;
    return o;
}

const char *t_root(void)
{
    static char root[MAX_PATH];
    if (!root[0]) {
        /* <raiz>\winmm_proxy\build\tests.exe */
        int i;
        GetModuleFileNameA(NULL, root, MAX_PATH);
        for (i = 0; i < 3; i++) { char *s = strrchr(root, '\\'); if (s) *s = 0; }
    }
    return root;
}

void *t_rwx(size_t n)
{
    return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void *t_call_from(uint8_t *ret, void *fn)
{
    /* sub rsp,0x28 ; mov rax,fn ; call rax | ret: add rsp,0x28 ; ret  (ate 4 argumentos, em registradores) */
    uint8_t *p = ret - 16;
    memcpy(p, "\x48\x83\xec\x28\x48\xb8", 6);
    memcpy(p + 6, &fn, 8);
    memcpy(p + 14, "\xff\xd0", 2);
    memcpy(ret, "\x48\x83\xc4\x28\xc3", 5);
    FlushInstructionCache(GetCurrentProcess(), p, 21);
    return p;
}

static uint8_t *read_file(const char *rel_path, size_t *len)
{
    char path[MAX_PATH * 2];
    HANDLE h;
    LARGE_INTEGER sz;
    uint8_t *buf;
    DWORD got;
    snprintf(path, sizeof path, "%s\\%s", t_root(), rel_path);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    GetFileSizeEx(h, &sz);
    buf = VirtualAlloc(NULL, (size_t)sz.QuadPart, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buf && (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart)) {
        VirtualFree(buf, 0, MEM_RELEASE); buf = NULL;
    }
    CloseHandle(h);
    *len = (size_t)sz.QuadPart;
    return buf;
}

uint8_t *pe_map(const char *rel_path, uint32_t *size_of_image)
{
    size_t len;
    uint8_t *f = read_file(rel_path, &len), *img;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    IMAGE_DATA_DIRECTORY *rd;
    int64_t delta;
    unsigned i;
    if (!f) return NULL;
    nt = (IMAGE_NT_HEADERS64 *)(f + ((IMAGE_DOS_HEADER *)f)->e_lfanew);
    img = t_rwx(nt->OptionalHeader.SizeOfImage);
    if (!img) { VirtualFree(f, 0, MEM_RELEASE); return NULL; }
    memcpy(img, f, nt->OptionalHeader.SizeOfHeaders);
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD n = sec[i].SizeOfRawData;
        if (sec[i].Misc.VirtualSize && sec[i].Misc.VirtualSize < n) n = sec[i].Misc.VirtualSize;
        if (sec[i].PointerToRawData + (size_t)n <= len) memcpy(img + sec[i].VirtualAddress, f + sec[i].PointerToRawData, n);
    }
    delta = (int64_t)(uintptr_t)img - (int64_t)nt->OptionalHeader.ImageBase;
    rd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (i = 0; i < rd->Size;) {
        IMAGE_BASE_RELOCATION *b = (IMAGE_BASE_RELOCATION *)(img + rd->VirtualAddress + i);
        const WORD *e = (const WORD *)(b + 1);
        unsigned k, n = (b->SizeOfBlock - sizeof *b) / 2;
        if (!b->SizeOfBlock) break;
        for (k = 0; k < n; k++)
            if (e[k] >> 12 == IMAGE_REL_BASED_DIR64) *(int64_t *)(img + b->VirtualAddress + (e[k] & 0xfff)) += delta;
        i += b->SizeOfBlock;
    }
    if (size_of_image) *size_of_image = nt->OptionalHeader.SizeOfImage;
    VirtualFree(f, 0, MEM_RELEASE);
    return img;
}

void pe_unmap(uint8_t *base) { if (base) VirtualFree(base, 0, MEM_RELEASE); }

int sha256_file(const char *rel_path, char out_hex[65], uint64_t *size)
{
    size_t len;
    uint8_t *f = read_file(rel_path, &len), hash[32];
    BCRYPT_ALG_HANDLE alg;
    int i, ok;
    if (!f) return 0;
    ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0;
    if (ok) {
        BCRYPT_HASH_HANDLE h;
        ok = BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0;
        if (ok) {
            ok = BCryptHashData(h, f, (ULONG)len, 0) == 0 && BCryptFinishHash(h, hash, sizeof hash, 0) == 0;
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    for (i = 0; ok && i < 32; i++) sprintf(out_hex + i * 2, "%02x", hash[i]);
    if (size) *size = len;
    VirtualFree(f, 0, MEM_RELEASE);
    return ok;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *e)
{
    printf("    CRASH em %s: excecao %#lx em %p\n", cur_test ? cur_test : "?",
           e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    printf("\nRESULTADO: FALHOU (crash)\n");
    fflush(stdout);
    ExitProcess(2);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char **argv)
{
    int i, run = 0, failed = 0, nskip = 0;
    const char *filter = argc > 1 ? argv[1] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(on_crash);
    for (i = ntests - 1; i >= 0; i--) {    /* construtores rodam na ordem inversa: volta a ordem dos arquivos */
        if (filter && !strstr(tests[i].name, filter)) continue;
        cur_test = tests[i].name;
        printf("[%s] %s\n", tests[i].file, tests[i].name);
        t_failed = 0; skipped = 0;
        tests[i].fn();
        run++;
        if (t_failed) failed++;
        else if (skipped) nskip++;
    }
    printf("\nRESULT: %d tests, %d failed, %d skipped\n", run, failed, nskip);
    return failed ? 1 : 0;
}
