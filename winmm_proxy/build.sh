#!/bin/sh
# gera winmm.dll (proxy) -> copiar para a pasta do azimuth.exe
cd "$(dirname "$0")"
python ../tools/gen_winmm_stubs.py . || exit 1
python ../tools/gen_d3d_thunks.py . || exit 1
mkdir -p build
/c/mingw64/bin/gcc -O2 -Wall -shared -o build/winmm.dll fm5_patch.c d3d_fm5.c winrt_fm5.c shape_fm5.c debug12_fm5.c gfx_fm5.c shader_fm6.c d3d_thunks.S winmm_stubs.S \
  winmm_exports.def -static-libgcc -static -Wl,--enable-stdcall-fixup -luser32 -lkernel32 || exit 1
# testes (tests/run_tests.sh, ~10 s): SKIP_TESTS=1 pula
[ -n "$SKIP_TESTS" ] || sh tests/run_tests.sh > build/tests.txt 2>&1 || { tail -40 build/tests.txt; echo "TESTES FALHARAM (build/tests.txt)"; exit 1; }
[ -n "$SKIP_TESTS" ] || tail -1 build/tests.txt
