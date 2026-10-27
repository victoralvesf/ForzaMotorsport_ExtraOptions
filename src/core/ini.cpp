#include "ini.hpp"

#include <windows.h>

namespace core::ini {

namespace {

// section and key names are ASCII
void widen(const char *s, wchar_t *out, size_t room)
{
    size_t i = 0;
    for (; s[i] && i + 1 < room; i++) out[i] = static_cast<unsigned char>(s[i]);
    out[i] = 0;
}

} // namespace

int get_int(const wchar_t *path, const char *section, const char *key, int def)
{
    wchar_t s[64], k[64];
    widen(section, s, 64);
    widen(key, k, 64);
    return static_cast<int>(GetPrivateProfileIntW(s, k, def, path));
}

} // namespace core::ini
