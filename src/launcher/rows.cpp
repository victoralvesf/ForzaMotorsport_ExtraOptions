#include "rows.hpp"

#include "ini.hpp"
#include "log.hpp"

#include <windows.h>
#include <cstring>
#include <cwchar>

namespace launcher {

namespace {

wchar_t g_ini[260];

// values of up to 4 games x 16 options, read once
struct Values {
    const game::Module *game;
    int value[16];
};
Values g_values[4];

Values *values_of(const game::Module &g)
{
    for (Values &v : g_values) {
        if (v.game == &g) return &v;
        if (v.game) continue;
        v.game = &g;
        for (size_t i = 0; i < g.options.size() && i < 16; i++)
            v.value[i] = core::ini::get_int(g_ini, g.tag, g.options[i].key, 0) != 0;
        return &v;
    }
    return nullptr;
}

// The callable of a row's on_change, kept in the std::function's inline storage. Called by the launcher with the
// MSVC x64 convention (this in rcx), which is mingw's too.
struct Toggle {
    const void *const *vft;
    const game::Module *game;
    size_t option;
};
static_assert(sizeof(Toggle) <= sizeof(msvc::Function::storage));

void *toggle_copy(const Toggle *self, void *where)
{
    memcpy(where, self, sizeof *self);
    return where;
}

int32_t toggle_call(const Toggle *self, void * /*settings*/, const size_t *choice)
{
    const game::Module &g = *self->game;
    const char *key = g.options[self->option].key;
    int v = *choice != 0;
    if (Values *vals = values_of(g)) vals->value[self->option] = v;
    if (core::ini::set_int(g_ini, g.tag, key, v)) core::log::write("launcher: [%s] %s=%d saved", g.tag, key, v);
    else core::log::write("launcher: [%s] %s=%d NOT saved (error %lu)", g.tag, key, v, GetLastError());
    return k_no_setting;
}

const void *toggle_target_type(const Toggle *) { return nullptr; } // only std::function::target_type() asks

void toggle_delete_this(Toggle *, bool) {} // always inline: nothing to free

const void *toggle_get(const Toggle *self) { return &self->game; }

const void *const k_toggle_vft[] = {
    reinterpret_cast<const void *>(toggle_copy),        // _Copy
    reinterpret_cast<const void *>(toggle_copy),        // _Move
    reinterpret_cast<const void *>(toggle_call),        // _Do_call
    reinterpret_cast<const void *>(toggle_target_type), // _Target_type
    reinterpret_cast<const void *>(toggle_delete_this), // _Delete_this
    reinterpret_cast<const void *>(toggle_get),         // _Get
};

void destroy_row(Row &r)
{
    msvc::destroy(r.label);
    for (msvc::WString *c = r.choices.first; c != r.choices.last; c++) msvc::destroy(*c);
    msvc::free(r.choices.first);
    r.choices = {};
}

bool make_row(Row &r, const game::Module &g, size_t i)
{
    memset(&r, 0, sizeof r);
    r.key = g.options[i].key;
    r.control = k_control_toggle;
    r.selected = static_cast<size_t>(value(g, i));
    r.label.cap = 7;
    bool ok = msvc::assign(r.label, g.options[i].label) && msvc::reserve(r.choices, 2);
    for (const char *c : { "Off", "On" }) {
        if (!ok) break;
        ok = msvc::assign(*r.choices.last, c);
        r.choices.last++;
    }
    if (!ok) {
        destroy_row(r);
        return false;
    }
    auto *t = reinterpret_cast<Toggle *>(r.on_change.storage);
    *t = Toggle{ k_toggle_vft, &g, i };
    r.on_change.impl = t;
    return true;
}

} // namespace

void set_ini(const wchar_t *path)
{
    wcsncpy(g_ini, path, 259);
    g_ini[259] = 0;
}

int value(const game::Module &g, size_t i)
{
    Values *v = values_of(g);
    return v && i < 16 ? v->value[i] : 0;
}

bool fill_rows(msvc::Vector<Row> &rows, const game::Module &g)
{
    size_t n = 0;
    for (const core::patch::Option &o : g.options)
        if (o.label) n++;
    if (!msvc::reserve(rows, n)) return false;
    for (size_t i = 0; i < g.options.size() && i < 16; i++) {
        if (!g.options[i].label) continue;
        if (!make_row(*rows.last, g, i)) {
            destroy_rows(rows);
            return false;
        }
        rows.last++;
    }
    return true;
}

void destroy_rows(msvc::Vector<Row> &rows)
{
    for (Row *r = rows.first; r != rows.last; r++) destroy_row(*r);
    msvc::free(rows.first);
    rows = {};
}

} // namespace launcher
