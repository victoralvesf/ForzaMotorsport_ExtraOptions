#include "version.hpp"

#include <cstdint>

namespace core::version {

bool of_module(HMODULE mod, Version &out)
{
    HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW(1), reinterpret_cast<LPCWSTR>(RT_VERSION));
    HGLOBAL h = r ? LoadResource(mod, r) : nullptr;
    auto *p = h ? static_cast<const uint8_t *>(LockResource(h)) : nullptr;
    DWORD n = r ? SizeofResource(mod, r) : 0;
    // the fixed info sits after the VS_VERSIONINFO key, 32-bit aligned
    for (DWORD i = 0; p && i + sizeof(VS_FIXEDFILEINFO) <= n; i += 4) {
        auto *fi = reinterpret_cast<const VS_FIXEDFILEINFO *>(p + i);
        if (fi->dwSignature != 0xfeef04bd) continue;
        out = { HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS),
                LOWORD(fi->dwFileVersionLS) };
        return true;
    }
    return false;
}

bool at_least(const Version &v, unsigned major, unsigned minor, unsigned build)
{
    if (v.major != major) return v.major > major;
    if (v.minor != minor) return v.minor > minor;
    return v.build >= build;
}

} // namespace core::version
