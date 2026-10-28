#include "options.hpp"

#include "ini.hpp"
#include "log.hpp"

namespace core::options {

int apply(const game::Module &g, uint8_t *base, const wchar_t *ini_path)
{
    log::write("%s at %p: options from %ls", g.tag, static_cast<void *>(base), ini_path);
    int applied = 0;
    for (const patch::Option &o : g.options) {
        int value = ini::get_int(ini_path, g.tag, o.key, 0);
        if (value == 0) {
            log::write("%s: %s=0: off", g.tag, o.key);
            continue;
        }
        if (patch::apply(base, g.size_of_image, o, g.tag) == patch::Result::applied) applied++;
    }
    if (g.apply_fixes) g.apply_fixes(base);
    log::write("%s: %d of %d options applied", g.tag, applied, static_cast<int>(g.options.size()));
    return applied;
}

bool log_disabled(const wchar_t *ini_path)
{
    return ini::get_int(ini_path, "GLOBAL", "DisableLog", 0) != 0;
}

} // namespace core::options
