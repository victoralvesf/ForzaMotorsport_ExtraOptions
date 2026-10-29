#include "image.hpp"

#include "expected.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace expected {

std::vector<uint8_t> hex(const char *s)
{
    std::vector<uint8_t> out;
    for (; *s; s++) {
        if (*s == ' ') continue;
        unsigned v;
        if (!s[1] || sscanf(s, "%2x", &v) != 1) throw std::runtime_error("bad hex in a test");
        out.push_back(static_cast<uint8_t>(v));
        s++;
    }
    return out;
}

} // namespace expected

namespace test_image {

Fake::Fake(size_t size) : size(size)
{
    base = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) throw std::runtime_error("VirtualAlloc reserve");
}

Fake::~Fake() { VirtualFree(base, 0, MEM_RELEASE); }

void Fake::put(uint32_t rva, std::span<const uint8_t> b)
{
    uint8_t *p = base + rva;
    DWORD old;
    if (!VirtualAlloc(p, b.size() + 32, MEM_COMMIT, PAGE_EXECUTE_READ) ||
        !VirtualProtect(p, b.size(), PAGE_EXECUTE_READWRITE, &old))
        throw std::runtime_error("VirtualAlloc commit");
    memcpy(p, b.data(), b.size());
    VirtualProtect(p, b.size(), PAGE_EXECUTE_READ, &old);
}

Mapped::Mapped(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    std::vector<uint8_t> file;
    fseek(f, 0, SEEK_END);
    file.resize(static_cast<size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    size_t got = fread(file.data(), 1, file.size(), f);
    fclose(f);
    if (got != file.size() || file.size() < 0x1000) return;

    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x800) return;
    auto *fnt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(file.data() + dos->e_lfanew);
    if (fnt->Signature != IMAGE_NT_SIGNATURE || fnt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return;

    size_t size = fnt->OptionalHeader.SizeOfImage;
    base = static_cast<uint8_t *>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) throw std::runtime_error("VirtualAlloc image");
    memcpy(base, file.data(), fnt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(fnt);
    for (unsigned i = 0; i < fnt->FileHeader.NumberOfSections; i++) {
        size_t n = sec[i].SizeOfRawData;
        if (sec[i].PointerToRawData + n > file.size() || sec[i].VirtualAddress + n > size)
            throw std::runtime_error("section outside the file or the image");
        memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, n);
    }
    DWORD old;
    VirtualProtect(base, size, PAGE_EXECUTE_READ, &old);
    nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
}

Mapped::~Mapped()
{
    if (base) VirtualFree(base, 0, MEM_RELEASE);
}

const char *game_exe(const char *game)
{
    if (strcmp(game, "FM5") == 0) return TEST_SOURCE_DIR "/game_binaries/FM5/forza_durango_release_final.exe";
    if (strcmp(game, "FM6") == 0) return TEST_SOURCE_DIR "/game_binaries/FM6/forza_durango_release_final.exe";
    if (strcmp(game, "AZIMUTH") == 0) return TEST_SOURCE_DIR "/game_binaries/AZIMUTH/azimuth_1.0.2.exe";
    if (strcmp(game, "AZIMUTH_104") == 0) return TEST_SOURCE_DIR "/game_binaries/AZIMUTH/azimuth_1.0.4.exe";
    return "";
}

} // namespace test_image
