#include "scan.hpp"

#include "pe.hpp"

#include <cstring>

namespace core::scan {

namespace {

struct Pattern {
    uint8_t value[k_max_bytes];
    bool any[k_max_bytes];
    size_t size;
};

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Same syntax as the sites (patch::parse), up to k_max_bytes.
bool parse(const char *hex, Pattern &out)
{
    out.size = 0;
    for (const char *p = hex; *p;) {
        if (*p == ' ') { p++; continue; }
        if (!p[1] || out.size == k_max_bytes) return false;
        if (p[0] == '?' && p[1] == '?') {
            out.value[out.size] = 0;
            out.any[out.size++] = true;
        } else {
            int hi = hex_digit(p[0]), lo = hex_digit(p[1]);
            if (hi < 0 || lo < 0) return false;
            out.value[out.size] = static_cast<uint8_t>(hi << 4 | lo);
            out.any[out.size++] = false;
        }
        p += 2;
    }
    return out.size > 0;
}

bool matches(const uint8_t *p, const Pattern &pat)
{
    for (size_t i = 0; i < pat.size; i++)
        if (!pat.any[i] && p[i] != pat.value[i]) return false;
    return true;
}

} // namespace

const uint8_t *find_unique(const uint8_t *base, const char *hex)
{
    Pattern pat;
    const IMAGE_NT_HEADERS64 *nt = pe::headers(base);
    if (!nt || !parse(hex, pat)) return nullptr;
    // the first fixed byte drives the search
    size_t lead = 0;
    while (lead < pat.size && pat.any[lead]) lead++;
    if (lead == pat.size) return nullptr;

    const uint8_t *found = nullptr;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; s++) {
        if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t *p = base + sec[s].VirtualAddress;
        size_t size = sec[s].Misc.VirtualSize;
        if (size < pat.size || !pe::readable(p, size)) continue;
        const uint8_t *last = p + size - pat.size;
        for (const uint8_t *q = p + lead; q <= last + lead;) {
            q = static_cast<const uint8_t *>(memchr(q, pat.value[lead], static_cast<size_t>(last + lead - q) + 1));
            if (!q) break;
            if (matches(q - lead, pat)) {
                if (found) return nullptr;
                found = q - lead;
            }
            q++;
        }
    }
    return found;
}

} // namespace core::scan
