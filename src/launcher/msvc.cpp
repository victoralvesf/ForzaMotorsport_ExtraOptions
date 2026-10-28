#include "msvc.hpp"

#include <windows.h>
#include <cstring>
#include <cwchar>

namespace msvc {

void *alloc(size_t n)
{
    if (n == 0 || n > k_max_block) return nullptr;
    return HeapAlloc(GetProcessHeap(), 0, n);
}

void free(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

namespace {

// w with room for n characters (MSVC rounds the capacity up to 8k-1), contents not set.
wchar_t *prepare(WString &w, size_t n)
{
    w.size = 0;
    w.cap = 7;
    w.buf[0] = 0;
    if (n <= 7) return w.buf;
    size_t cap = n | 7;
    auto *p = static_cast<wchar_t *>(alloc((cap + 1) * sizeof(wchar_t)));
    if (!p) return nullptr;
    w.ptr = p;
    w.cap = cap;
    return p;
}

} // namespace

bool assign(WString &w, const char *s)
{
    size_t n = strlen(s);
    wchar_t *p = prepare(w, n);
    if (!p) return false;
    for (size_t i = 0; i < n; i++) p[i] = static_cast<unsigned char>(s[i]);
    p[n] = 0;
    w.size = n;
    return true;
}

bool assign(WString &w, const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *p = prepare(w, n);
    if (!p) return false;
    memcpy(p, s, (n + 1) * sizeof(wchar_t));
    w.size = n;
    return true;
}

void destroy(WString &w)
{
    if (w.cap > 7) free(w.ptr);
    w.size = 0;
    w.cap = 7;
    w.buf[0] = 0;
}

} // namespace msvc
