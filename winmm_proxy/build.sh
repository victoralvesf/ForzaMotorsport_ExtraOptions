#!/bin/sh
# gera winmm.dll (proxy) -> copiar para a pasta do azimuth.exe
# fontes: raiz (DllMain, patches do azimuth.exe e despacho por jogo), common\ (generico), fm5\ e fm6\ (um jogo so);
# tools/check_isolation.py confere que fm5\ e fm6\ nao se usam e que a raiz/common so usam os descritores
cd "$(dirname "$0")"
python ../tools/gen_winmm_stubs.py . || exit 1
python ../tools/gen_d3d_thunks.py fm5 || exit 1
mkdir -p build/obj/common build/obj/fm5 build/obj/fm6
SRCS="main.c azimuth.c $(ls common/*.c fm5/*.c fm6/*.c | tr '\n' ' ') fm5/d3d_thunks.S winmm_stubs.S"
OBJS=""
for f in $SRCS; do
  o=build/obj/$(echo "$f" | sed 's/\.[cS]$/.o/')
  /c/mingw64/bin/gcc -O2 -Wall -c -o "$o" "$f" || exit 1
  OBJS="$OBJS $o"
done
python ../tools/check_isolation.py build/obj || exit 1
/c/mingw64/bin/gcc -O2 -Wall -shared -o build/winmm.dll $OBJS \
  winmm_exports.def -static-libgcc -static -Wl,--enable-stdcall-fixup -luser32 -lkernel32 || exit 1
# testes (tests/run_tests.sh, ~10 s): SKIP_TESTS=1 pula
[ -n "$SKIP_TESTS" ] || sh tests/run_tests.sh > build/tests.txt 2>&1 || { tail -40 build/tests.txt; echo "TESTES FALHARAM (build/tests.txt)"; exit 1; }
[ -n "$SKIP_TESTS" ] || tail -1 build/tests.txt
