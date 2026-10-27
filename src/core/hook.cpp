#include "hook.hpp"

#include "patch.hpp"

#include <windows.h>
#include <cstring>

namespace core::hook {

namespace {

void abs_jmp(uint8_t *out, const void *to)
{
    out[0] = 0xff;
    out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &to, 8);
}

} // namespace

bool install(uint8_t *at, const char *stolen_hex, void *target, void **tramp)
{
    patch::Bytes stolen;
    if (!patch::parse(stolen_hex, strlen(stolen_hex), stolen, false) || stolen.size < k_jmp_size) return false;
    if (memcmp(at, stolen.value, stolen.size) != 0) return false;

    auto *t = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!t) return false;
    memcpy(t, at, stolen.size);
    abs_jmp(t + stolen.size, at + stolen.size);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    *tramp = t;

    uint8_t jmp[patch::k_max_bytes];
    memset(jmp, 0xcc, sizeof jmp); // leftover of the stolen instructions: never reached
    abs_jmp(jmp, target);
    return patch::write_code(at, jmp, stolen.size);
}

} // namespace core::hook
