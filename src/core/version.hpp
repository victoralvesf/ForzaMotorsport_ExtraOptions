// File version of a module (VS_FIXEDFILEINFO of its VERSIONINFO resource, without version.dll).
#pragma once

#include <windows.h>

namespace core::version {

struct Version { unsigned major, minor, build, revision; };

// false when the module has no VERSIONINFO.
bool of_module(HMODULE mod, Version &out);

// v >= major.minor.build (revision ignored).
bool at_least(const Version &v, unsigned major, unsigned minor, unsigned build);

} // namespace core::version
