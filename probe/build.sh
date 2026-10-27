#!/bin/sh
# winmm.dll probe (MIGRATION.md phase 0) -> probe/build/winmm.dll; copy next to azimuth.exe
cd "$(dirname "$0")"
mkdir -p build
/c/mingw64/bin/g++ -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -shared -o build/winmm.dll probe.cpp \
  winmm_exports.def -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup || exit 1
/c/mingw64/bin/objdump -p build/winmm.dll | sed -n 's/^\s*DLL Name: //p'
