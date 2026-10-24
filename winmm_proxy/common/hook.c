/*
 * Escrita na memoria com conferencia dos bytes originais e hooks com trampolim (FM5 e FM6).
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void unhex(const char *s, uint8_t *out) { while (*s) { *out++ = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1])); s += 2; } }

int bytes_match(const uint8_t *p, const char *hex, size_t len)
{
    uint8_t buf[64];
    unhex(hex, buf);
    return memcmp(p, buf, len) == 0;
}

void write_mem(void *dst, const void *src, size_t len)
{
    DWORD old;
    VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dst, src, len);
    VirtualProtect(dst, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, len);
}

void abs_jmp(uint8_t *at, const void *to, size_t len)
{
    uint8_t buf[32];
    memset(buf, 0xcc, sizeof buf);
    buf[0] = 0xff; buf[1] = 0x25; memset(buf + 2, 0, 4);
    memcpy(buf + 6, &to, 8);
    write_mem(at, buf, len);
}

/* memoria executavel a menos de 2 GB da imagem (para call rel32) */
uint8_t *alloc_near(uint8_t *mod, size_t size)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(mod + ((IMAGE_DOS_HEADER *)mod)->e_lfanew);
    uintptr_t a = ((uintptr_t)mod + nt->OptionalHeader.SizeOfImage + 0xffff) & ~(uintptr_t)0xffff;
    uintptr_t end = (uintptr_t)mod + 0x70000000;
    for (; a < end; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

/* hook com trampolim: os bytes roubados nao podem ter enderecos relativos a RIP */
int hook_at(uint8_t *at, const char *orig_hex, size_t stolen, void *hook, void **tramp_out)
{
    uint8_t *tramp;
    const void *back = at + stolen;
    if (!bytes_match(at, orig_hex, strlen(orig_hex) / 2)) { wlog("hook %p: bytes do not match", (void *)at); return 0; }
    tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { wlog("hook %p: VirtualAlloc failed", (void *)at); return 0; }
    memcpy(tramp, at, stolen);
    tramp[stolen] = 0xff; tramp[stolen + 1] = 0x25;
    memset(tramp + stolen + 2, 0, 4);
    memcpy(tramp + stolen + 6, &back, 8);
    *tramp_out = tramp;
    abs_jmp(at, hook, stolen);
    return 1;
}

int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp_out)
{
    if (!hook_at(g_base + rva, orig_hex, stolen, hook, tramp_out)) return 0;
    wlog("hook %#x installed", rva);
    return 1;
}
