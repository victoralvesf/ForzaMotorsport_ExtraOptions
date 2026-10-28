// The Extra Options section of a game's settings page in Azimuth's launcher: one Off/On row per option, built as
// Azimuth builds its own V-sync row. Changing a row writes [game] key=0/1 to motorsport_extraoptions.ini at once.
#pragma once

#include "game.hpp"
#include "msvc.hpp"

#include <cstddef>
#include <cstdint>

namespace launcher {

// azimuth::launcher::SettingsPage::Row (Azimuth 1.0.2)
struct Row {
    const char *key;                     // the page finds the row of the element the player acts on by this key
    msvc::WString label;
    int32_t control;                     // azimuth::launcher::Control
    msvc::Vector<msvc::WString> choices;
    size_t selected;
    bool disabled;
    msvc::Function on_change;            // azimuth::app::Setting (azimuth::app::Settings &, size_t choice)
};
static_assert(sizeof(Row) == 0x98);
static_assert(offsetof(Row, label) == 0x08 && offsetof(Row, control) == 0x28 && offsetof(Row, choices) == 0x30);
static_assert(offsetof(Row, selected) == 0x48 && offsetof(Row, disabled) == 0x50 && offsetof(Row, on_change) == 0x58);

constexpr int32_t k_control_toggle = 0; // Off/On, choices { "Off", "On" }

// What on_change returns: above every azimuth::app::Setting (the last is 0x14 in 1.0.2), so Azimuth's settings
// update changes and saves nothing; the mod saves its own ini.
constexpr int32_t k_no_setting = 0x7fffffff;

// The options ini the rows read and write.
void set_ini(const wchar_t *path);

// Current value of option i of g (0/1): read from the ini the first time, then kept as the rows change it.
int value(const game::Module &g, size_t i);

// Fills rows (empty on entry) with a row per option of g that has a label; false (rows left empty) when the memory
// is not available.
bool fill_rows(msvc::Vector<Row> &rows, const game::Module &g);

// Frees what fill_rows built (the launcher does it with its own code; used on failure and by the tests).
void destroy_rows(msvc::Vector<Row> &rows);

} // namespace launcher
