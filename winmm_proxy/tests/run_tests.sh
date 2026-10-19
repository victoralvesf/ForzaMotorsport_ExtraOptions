#!/bin/sh
# compila e roda os testes do proxy: sh winmm_proxy/tests/run_tests.sh [filtro]
# (os geradores de d3d_thunks.S/winmm_stubs.S rodam no build.sh; aqui so se faltarem)
cd "$(dirname "$0")/.." || exit 1
[ -f d3d_thunks.S ] || python ../tools/gen_d3d_thunks.py . || exit 1
[ -f winmm_stubs.S ] || python ../tools/gen_winmm_stubs.py . || exit 1
mkdir -p build
/c/mingw64/bin/gcc -O1 -g -Wall -Wno-unused-function -o build/tests.exe \
  tests/main.c tests/t_patch.c tests/t_d3d.c tests/t_gfx.c tests/t_shape.c tests/t_audio.c tests/t_winrt.c tests/t_shader.c tests/t_gpuguard.c debug12_fm5.c \
  d3d_thunks.S winmm_stubs.S -static-libgcc -lbcrypt -luser32 -lkernel32 || exit 1
./build/tests.exe "$@"
