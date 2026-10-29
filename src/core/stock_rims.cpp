#include "stock_rims.hpp"

#include "hook.hpp"
#include "log.hpp"
#include "patch.hpp"
#include "pe.hpp"

#include <windows.h>
#include <cstring>

namespace core::stock_rims {

namespace {

// ---- SQL ----------------------------------------------------------------------------------------------------
// The rim screens are SQL over GameDB (SQLite 3.6): the categories come from WheelCategories, the rims of a
// category from WheelAnnotations joined with the car's parts view, where a rim is listed only when it has a price
// and a part maker (the stock rims of the cars have neither). The mod's fragments add one category after the
// game's (id and order = max + 1), annotate in it the stock rim of every car whose rim has the car's media name
// (the rim's model is inside that car's package), ordered by make and car, and name each one by its car.

constexpr const char *k_categories =
    "(SELECT * FROM WheelCategories UNION ALL SELECT MAX(Id) + 1, 0, MAX(DisplayOrder) + 1, "
    "'Stock Rims (All Cars)', 'The stock rims of every car in the game, by make.', "
    "(SELECT IconPath FROM WheelCategories WHERE IsStock = 1), "
    "(SELECT ImagePath FROM WheelCategories WHERE IsStock = 1) FROM WheelCategories) AS WheelCategories";

constexpr const char *k_annotations =
    "(SELECT WheelID, CategoryID, DisplayOrder FROM WheelAnnotations UNION ALL "
    "SELECT Data_Car.StockWheelID, (SELECT MAX(Id) + 1 FROM WheelCategories), "
    "lower(List_CarMake.IconPathBase) || '|' || lower(Data_Car.MediaName) FROM Data_Car "
    "JOIN List_CarMake ON List_CarMake.ID = Data_Car.MakeID "
    "JOIN List_Wheels AS CarRim ON CarRim.ID = Data_Car.StockWheelID AND CarRim.MediaName = Data_Car.MediaName) "
    "AS WheelAnnotations";

// the car (and its make) whose stock rim a listed rim is
constexpr const char *k_stock_car_joins =
    "LEFT JOIN Data_Car AS StockCar ON List_Wheels.IsStock = 1 AND StockCar.StockWheelID = List_Wheels.ID "
    "LEFT JOIN List_CarMake AS StockMake ON StockMake.ID = StockCar.MakeID ";

struct Edit {
    const char *find;   // must occur once
    const char *before; // inserted before it (null = nothing)
    const char *with;   // replaces it (null = it stays)
    const char *after;  // inserted after it (null = nothing)
};

// out = game with each edit applied in turn; then append (if any).
bool rewrite(const char *game, const Edit *edits, size_t count, const char *append, char *out, size_t cap)
{
    size_t n = strnlen(game, cap);
    if (n == cap) return false;
    memcpy(out, game, n + 1);
    for (size_t i = 0; i < count; i++) {
        const Edit &e = edits[i];
        char *at = strstr(out, e.find);
        if (!at || strstr(at + 1, e.find)) return false;
        const char *w = e.with ? e.with : e.find;
        size_t flen = strlen(e.find), blen = e.before ? strlen(e.before) : 0, wlen = strlen(w),
               alen = e.after ? strlen(e.after) : 0;
        size_t grown = n - flen + blen + wlen + alen;
        if (grown >= cap) return false;
        char keep[2048]; // the token (w may be e.find itself, which the move below can overwrite)
        if (wlen >= sizeof keep) return false;
        memcpy(keep, w, wlen);
        memmove(at + blen + wlen + alen, at + flen, n - (at - out) - flen + 1);
        if (blen) memcpy(at, e.before, blen);
        memcpy(at + blen, keep, wlen);
        if (alen) memcpy(at + blen + wlen, e.after, alen);
        n = grown;
    }
    if (append) {
        size_t alen = strlen(append);
        if (n + alen >= cap) return false;
        memcpy(out + n, append, alen + 1);
    }
    return true;
}

// out = the concatenation of parts (null-terminated list)
bool concat(char *out, size_t cap, const char *const *parts)
{
    size_t n = 0;
    for (; *parts; parts++) {
        size_t l = strlen(*parts);
        if (n + l >= cap) return false;
        memcpy(out + n, *parts, l);
        n += l;
    }
    out[n] = 0;
    return true;
}

// ---- rim loader ---------------------------------------------------------------------------------------------
// The loader builds <rims folder><name>\<name>_wheelLF.modelbin (and RF, AO of each) where the rims folder is
// GAME:\Media\Cars\_library\scene\wheels\ (each rim's zip, mounted as a folder). A car's stock rim is inside the
// car's zip (name = the car's media name): <cars folder><name><folder><name><file> for one of the game's folders
// and files (FM6: \scene\_library\Scene\Wheels\ and _wheelLF.modelbin; FM5 packages vary), and its AO
// <cars folder><name>\Textures\AO\<name>_wheelLF_AO.swatchbin. The loader's leas of the rims folder, of the "\"
// separator and of the model file become calls to stubs that give the cars folder, the folder and the file found
// to the thread that loads a stock rim (the hook sets the redirect slot around the original), the game's strings
// otherwise.

constexpr const char k_rims_tail[] = "_library\\scene\\wheels\\";
constexpr const char k_model_lf[] = "_wheelLF.modelbin";
constexpr size_t k_part = 64; // room for a folder or a file name

// MSVC std::string as the game passes it
struct GameString {
    union {
        char buf[16];
        const char *ptr;
    };
    uint64_t size;
    uint64_t cap;
    const char *data() const { return cap >= 16 ? ptr : buf; }
};

using ExistsFn = bool (*)(void *fs, const GameString *path);
using LoadFn = bool (*)(void *self, const GameString *name);

struct State {
    const Game *game;
    void **fs;                       // the game's file system object pointer
    const char *rims_folder;         // the game's rims folder string
    char cars_folder[64];            // the same without "_library\scene\wheels\"
    LoadFn load;                     // trampoline of the loader
    volatile uint64_t *redirect_tid; // read by the stubs (gs:[0x48] = current thread id)
    char *folder;                    // the stubs' redirected separator and model file (set under lock)
    char *file;
    CRITICAL_SECTION lock;
};
State g;

bool exists(const char *path)
{
    void *fs = g.fs ? *g.fs : nullptr;
    if (!fs) return false;
    GameString s;
    s.ptr = path;
    s.size = strlen(path);
    s.cap = s.size < 16 ? 16 : s.size; // >= 16: the data is at ptr
    auto fn = reinterpret_cast<ExistsFn>((*static_cast<void ***>(fs))[g.game->exists_slot / 8]);
    return fn(fs, &s);
}

// a media name as the game's rims and cars have them (letters, digits, '_', '-')
bool plain_name(const char *s, size_t n)
{
    if (n == 0 || n > 64) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool join(char *out, size_t cap, const char *a, const char *name, const char *b, const char *name2, const char *c)
{
    const char *const parts[] = { a, name, b, name2, c, nullptr };
    return concat(out, cap, parts);
}

} // namespace

} // namespace core::stock_rims

extern "C" bool core_stock_rims_load(void *self, const core::stock_rims::GameString *name)
{
    using namespace core::stock_rims;
    char n[72], path[320];
    size_t len = name ? name->size : 0;
    if (!plain_name(name ? name->data() : nullptr, len)) return g.load(self, name);
    memcpy(n, name->data(), len);
    n[len] = 0;

    // an aftermarket rim (its own zip) loads as the game does
    if (!join(path, sizeof path, g.rims_folder, n, "\\", n, k_model_lf) || exists(path)) return g.load(self, name);
    const char *folder = nullptr, *file = nullptr;
    for (const char *d : g.game->folders)
        for (const char *f : g.game->files)
            if (!folder && join(path, sizeof path, g.cars_folder, n, d, n, f) && exists(path)) folder = d, file = f;
    if (!folder) {
        static volatile LONG missing;
        if (InterlockedIncrement(&missing) <= 8) core::log::write("%s: StockWheels: rim %s not found", g.game->tag, n);
        return g.load(self, name);
    }

    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 16)
        core::log::write("%s: StockWheels: rim %s from its car's package", g.game->tag, n);
    EnterCriticalSection(&g.lock);
    strcpy(g.folder, folder);
    strcpy(g.file, file);
    uint64_t previous = *g.redirect_tid;
    *g.redirect_tid = GetCurrentThreadId();
    bool ok = g.load(self, name);
    *g.redirect_tid = previous;
    LeaveCriticalSection(&g.lock);
    return ok;
}

namespace core::stock_rims {

bool view_sql(const char *game, char *out, size_t cap)
{
    static const Edit check[] = { { "FROM List_Wheels WHERE", nullptr, nullptr, nullptr } };
    return rewrite(game, check, 1,
                   " OR List_Wheels.ID IN (SELECT StockWheelID FROM Data_Car WHERE Data_Car.MediaName = "
                   "List_Wheels.MediaName)",
                   out, cap);
}

bool categories_sql(const char *game, char *out, size_t cap)
{
    const Edit e[] = { { "FROM WheelCategories ", "FROM ", k_categories, " " } };
    return rewrite(game, e, 1, nullptr, out, cap);
}

bool names_sql(const char *game, char *out, size_t cap)
{
    static const Edit e[] = {
        { "ELSE List_PartManufacturer.PartManufacturer", "WHEN StockMake.ID IS NOT NULL THEN StockMake.DisplayName ",
          nullptr, nullptr },
        { "ELSE List_Wheels.DisplayName", "WHEN StockCar.ID IS NOT NULL THEN StockCar.DisplayName ", nullptr,
          nullptr },
        { "WHEN List_PartManufacturer.LogoPath", "WHEN StockMake.ID IS NOT NULL THEN StockMake.IconPath ", nullptr,
          nullptr },
        { "CASE WHEN PartTable.IsStock = 1 THEN List_CarMake.IconPath", nullptr, nullptr,
          " WHEN StockMake.ID IS NOT NULL THEN StockMake.IconPath" },
    };
    return rewrite(game, e, 4, nullptr, out, cap);
}

bool joins_sql(const char *game, char *out, size_t cap)
{
    static char ann[2048];
    const char *const parts[] = { k_stock_car_joins, "JOIN ", k_annotations, " ON", nullptr };
    if (!concat(ann, sizeof ann, parts)) return false;
    const Edit e[] = {
        { "JOIN WheelAnnotations ON", nullptr, ann, nullptr },
        { "JOIN WheelCategories ON", "JOIN ", k_categories, " ON" },
    };
    return rewrite(game, e, 2, nullptr, out, cap);
}

namespace {

// target of the rel32 that ends the instruction of size len at insn
uint8_t *rip_target(uint8_t *insn, size_t len)
{
    int32_t d;
    memcpy(&d, insn + len - 4, 4);
    return insn + len + d;
}

// the game's NUL-terminated string at p, inside [base, base + image), at most max bytes
const char *game_cstr(uint8_t *base, size_t image, const uint8_t *p, size_t max)
{
    if (p < base || p >= base + image) return nullptr;
    size_t avail = core::pe::readable_size(p, max);
    if (!avail || !memchr(p, 0, avail)) return nullptr;
    return reinterpret_cast<const char *>(p);
}

// stub for "lea reg, [rip+x]" (reg = rdx or r8): reg = (current thread == *tid) ? alt : orig; ret. alt_len >= 0
// (rdx only): the alt path also sets r8d = alt_len (the length of a counted assign).
size_t make_stub(uint8_t *s, bool r8, const volatile uint64_t *tid, const void *orig, const void *alt,
                 int32_t alt_len = -1)
{
    size_t i = 0;
    // mov reg, gs:[0x48]
    const uint8_t mov_gs_rdx[] = { 0x65, 0x48, 0x8b, 0x14, 0x25, 0x48, 0, 0, 0 };
    const uint8_t mov_gs_r8[] = { 0x65, 0x4c, 0x8b, 0x04, 0x25, 0x48, 0, 0, 0 };
    memcpy(s, r8 ? mov_gs_r8 : mov_gs_rdx, 9);
    i = 9;
    // cmp reg, [rip+tid]
    s[i++] = r8 ? 0x4c : 0x48;
    s[i++] = 0x3b;
    s[i++] = r8 ? 0x05 : 0x15;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<const volatile uint8_t *>(tid) - (s + i + 4));
    memcpy(s + i, &rel, 4);
    i += 4;
    s[i++] = 0x74; // je alt
    s[i++] = 11;
    // mov reg, imm64 orig ; ret
    s[i++] = r8 ? 0x49 : 0x48;
    s[i++] = r8 ? 0xb8 : 0xba;
    memcpy(s + i, &orig, 8);
    i += 8;
    s[i++] = 0xc3;
    // alt: mov reg, imm64 alt ; [mov r8d, imm32 alt_len ;] ret
    s[i++] = r8 ? 0x49 : 0x48;
    s[i++] = r8 ? 0xb8 : 0xba;
    memcpy(s + i, &alt, 8);
    i += 8;
    if (alt_len >= 0) {
        s[i++] = 0x41;
        s[i++] = 0xb8;
        memcpy(s + i, &alt_len, 4);
        i += 4;
    }
    s[i++] = 0xc3;
    return i;
}

bool point_lea(uint8_t *insn, const void *to)
{
    int32_t d = static_cast<int32_t>(static_cast<const uint8_t *>(to) - (insn + 7));
    return core::patch::write_code(insn + 3, &d, 4);
}

bool lea_to_call(uint8_t *insn, const void *stub)
{
    uint8_t c[7] = { 0xe8, 0, 0, 0, 0, 0x66, 0x90 };
    int32_t d = static_cast<int32_t>(static_cast<const uint8_t *>(stub) - (insn + 5));
    memcpy(c + 1, &d, 4);
    return core::patch::write_code(insn, c, 7);
}

bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && _stricmp(s + n - t, tail) == 0;
}

// the leas of the loader point to one string each (all the same for a group)
const char *lea_group(uint8_t *base, size_t image, std::span<const uint32_t> leas, size_t max)
{
    if (leas.empty()) return nullptr;
    const char *s = game_cstr(base, image, rip_target(base + leas[0], 7), max);
    for (uint32_t r : leas) s = s && rip_target(base + r, 7) == reinterpret_cast<const uint8_t *>(s) ? s : nullptr;
    return s;
}

bool parts_fit(std::span<const char *const> parts)
{
    for (const char *p : parts)
        if (strlen(p) >= k_part) return false;
    return !parts.empty();
}

} // namespace

bool install(const Game &game, uint8_t *base, size_t image)
{
    const char *tag = game.tag;

    // the game's loader strings, from the checked leas
    const char *rims = lea_group(base, image, game.prefix_leas, 128);
    const char *sep = lea_group(base, image, game.sep_leas, 8);
    const char *file = game.file_leas.empty() ? k_model_lf : lea_group(base, image, game.file_leas, 32);
    bool same = rims && sep && file && strcmp(sep, "\\") == 0 && strcmp(file, k_model_lf) == 0 &&
                ends_with(rims, k_rims_tail) && strlen(rims) - strlen(k_rims_tail) < sizeof g.cars_folder &&
                parts_fit(game.folders) && parts_fit(game.files) &&
                (!game.file_leas.empty() || (game.files.size() == 1 && strcmp(game.files[0], k_model_lf) == 0));
    if (same && game.prefix_len_lea) {
        // lea r8d, [r15 + len] (r15 = 0) right before the first lea of the rims folder
        const uint8_t *p = base + game.prefix_len_lea;
        same = game.prefix_len_lea + 4 == game.prefix_leas[0] && p[0] == 0x45 && p[1] == 0x8d && p[2] == 0x47 &&
               p[3] == strlen(rims);
    }
    if (!same) {
        core::log::write("%s: StockWheels: rim loader paths are not as expected", tag);
        return false;
    }

    // one block within rel32 of every site: SQL, data and stubs
    uint32_t lo = game.load, hi = game.load;
    auto span_of = [&](uint32_t r) { lo = r < lo ? r : lo, hi = r > hi ? r : hi; };
    for (uint32_t r : { game.view_lea, game.cats_lea, game.names_lea, game.joins_lea }) span_of(r);
    for (std::span<const uint32_t> leas : { game.prefix_leas, game.sep_leas, game.file_leas })
        for (uint32_t r : leas) span_of(r);
    const size_t k_block = 0x10000;
    auto *block = static_cast<uint8_t *>(
        core::hook::alloc_reaching(base + lo, base + hi + 0x1000, k_block, PAGE_EXECUTE_READWRITE));
    if (!block) {
        core::log::write("%s: StockWheels: no memory within reach of the game", tag);
        return false;
    }

    const uint32_t sql_leas[] = { game.view_lea, game.cats_lea, game.names_lea, game.joins_lea };
    bool (*const build[])(const char *, char *, size_t) = { view_sql, categories_sql, names_sql, joins_sql };
    constexpr size_t k_sql_cap = 4096;
    size_t used = 0;
    uint8_t *sql[4];
    for (size_t i = 0; i < 4; i++) {
        const char *orig = game_cstr(base, image, rip_target(base + sql_leas[i], 7), k_sql_cap);
        sql[i] = block + used;
        if (!orig || !build[i](orig, reinterpret_cast<char *>(sql[i]), k_sql_cap)) {
            core::log::write("%s: StockWheels: SQL fragment at %#x is not as expected", tag, sql_leas[i]);
            VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }
        used += (strlen(reinterpret_cast<char *>(sql[i])) + 16) & ~size_t(15);
    }

    g.game = &game;
    g.fs = reinterpret_cast<void **>(rip_target(base + game.fs_read, 7));
    g.rims_folder = rims;
    size_t cars_len = strlen(rims) - strlen(k_rims_tail);
    memcpy(g.cars_folder, rims, cars_len);
    g.cars_folder[cars_len] = 0;
    static bool lock_ready;
    if (!lock_ready) InitializeCriticalSection(&g.lock);
    lock_ready = true;

    // data the stubs read, then the stubs
    used = (used + 15) & ~size_t(15);
    g.redirect_tid = reinterpret_cast<volatile uint64_t *>(block + used);
    *g.redirect_tid = 0;
    used += 16;
    char *cars = reinterpret_cast<char *>(block + used);
    memcpy(cars, g.cars_folder, cars_len + 1);
    used += (cars_len + 16) & ~size_t(15);
    g.folder = reinterpret_cast<char *>(block + used);
    strcpy(g.folder, game.folders[0]);
    used += k_part;
    g.file = reinterpret_cast<char *>(block + used);
    strcpy(g.file, game.files[0]);
    used += k_part;

    auto add_stub = [&](uint32_t rva, bool r8, const void *orig, const void *alt, int32_t alt_len = -1) {
        uint8_t *s = block + used;
        used += (make_stub(s, r8, g.redirect_tid, orig, alt, alt_len) + 15) & ~size_t(15);
        return lea_to_call(base + rva, s);
    };
    void *tramp = nullptr;
    if (!core::hook::install(base + game.load, game.load_entry, reinterpret_cast<void *>(core_stock_rims_load),
                             &tramp)) {
        core::log::write("%s: StockWheels: rim loader %#x could not be hooked", tag, game.load);
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    g.load = reinterpret_cast<LoadFn>(tramp);

    bool ok = true;
    for (size_t i = 0; i < 4; i++) ok = point_lea(base + sql_leas[i], sql[i]) && ok;
    for (uint32_t r : game.prefix_leas)
        ok = add_stub(r, false, rims, cars,
                      game.prefix_len_lea && r == game.prefix_leas[0] ? static_cast<int32_t>(cars_len) : -1) &&
             ok;
    for (uint32_t r : game.sep_leas) ok = add_stub(r, true, sep, g.folder) && ok;
    for (uint32_t r : game.file_leas) ok = add_stub(r, true, file, g.file) && ok;
    FlushInstructionCache(GetCurrentProcess(), block, used);
    if (!ok) core::log::write("%s: StockWheels: a site could not be written (error %lu)", tag, GetLastError());
    core::log::write("%s: StockWheels: rim category \"Stock Rims (All Cars)\" added, stock rims load from %s<car>",
                     tag, g.cars_folder);
    return ok;
}

} // namespace core::stock_rims
