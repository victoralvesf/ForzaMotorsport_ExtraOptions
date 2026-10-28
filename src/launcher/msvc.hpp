// The MSVC standard library layouts that Azimuth's launcher hands over (x64, release): std::wstring, std::vector
// and std::function. What the mod builds here is later copied, moved and freed by Azimuth's own code, so the memory
// comes from the heap Azimuth's CRT uses (the process heap, 1.0.2) and every block stays under 4 KB (MSVC's
// operator delete only takes the plain path below that size).
#pragma once

#include <cstddef>
#include <cstdint>

namespace msvc {

constexpr size_t k_max_block = 0xfff;

// HeapAlloc/HeapFree on the process heap; null when n is 0 or above k_max_block.
void *alloc(size_t n);
void free(void *p);

// std::wstring: 8 characters inline (capacity 7) or a pointer.
struct WString {
    union {
        wchar_t buf[8];
        wchar_t *ptr;
    };
    size_t size;
    size_t cap;

    const wchar_t *data() const { return cap > 7 ? ptr : buf; }
};
static_assert(sizeof(WString) == 0x20);

// s is ASCII. false (w left empty) when it does not fit a block.
bool assign(WString &w, const char *s);
bool assign(WString &w, const wchar_t *s);
void destroy(WString &w);

template <typename T> struct Vector {
    T *first;
    T *last;
    T *end;

    size_t size() const { return static_cast<size_t>(last - first); }
};
static_assert(sizeof(Vector<int>) == 0x18);

// A buffer for n elements (first = last, end = first + n); false when it does not fit a block.
template <typename T> bool reserve(Vector<T> &v, size_t n)
{
    v.first = v.last = v.end = nullptr;
    if (n == 0) return true;
    if (n > k_max_block / sizeof(T)) return false;
    v.first = v.last = static_cast<T *>(alloc(n * sizeof(T)));
    if (!v.first) return false;
    v.end = v.first + n;
    return true;
}

// std::function<R(A...)>: 7 pointers of inline storage, then the pointer to the callable's _Func_base (the inline
// storage when the callable fits there). The _Func_base vtable, in order: _Copy(where), _Move(where),
// _Do_call(args...), _Target_type(), _Delete_this(bool deallocate), _Get().
struct Function {
    alignas(8) unsigned char storage[0x38];
    void *impl;
};
static_assert(sizeof(Function) == 0x40);

} // namespace msvc
