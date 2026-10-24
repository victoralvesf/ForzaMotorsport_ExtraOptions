"""check_isolation.py OBJDIR - confere a separacao por jogo do proxy winmm (winmm_proxy/build.sh).

Os objetos em OBJDIR seguem as pastas dos fontes: raiz (main.o, azimuth.o, winmm_stubs.o), common/, fm5/, fm6/.
Regras (simbolos globais, via nm):
  - fm5/ nunca usa um simbolo definido em fm6/, e vice-versa;
  - a raiz e common/ so usam de fm5/ e fm6/ os descritores (fm5_game, fm6_game).
Assim uma mudanca feita para um jogo nao chega ao outro sem passar pelo descritor dele (ou por common/, que vale
para os dois). Sai com 1 e lista as referencias proibidas.
"""
import os, subprocess, sys

NM = r'C:\mingw64\bin\nm.exe' if os.path.exists(r'C:\mingw64\bin\nm.exe') else 'nm'
ALLOWED = {'fm5_game', 'fm6_game'}


def group(rel):
    top = rel.replace('\\', '/').split('/')[0]
    return top if top in ('common', 'fm5', 'fm6') else 'root'


def symbols(path):
    out = subprocess.run([NM, path], capture_output=True, text=True, check=True).stdout
    defined, undefined = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == 'U':
            undefined.add(parts[1])
        elif len(parts) == 3 and parts[1] in 'TDBRCVW' and parts[1].isupper():
            defined.add(parts[2])
    return defined, undefined


def main():
    root = sys.argv[1]
    objs = []
    for d, _, files in os.walk(root):
        for f in files:
            if f.endswith('.o'):
                p = os.path.join(d, f)
                objs.append((os.path.relpath(p, root), p))
    owner, uses = {}, []
    for rel, p in objs:
        defined, undefined = symbols(p)
        for s in defined:
            owner[s] = group(rel)
        uses.append((rel, group(rel), undefined))
    bad = []
    for rel, g, undefined in uses:
        for s in sorted(undefined):
            o = owner.get(s)
            if o in (None, g, 'root', 'common'):
                continue
            if g in ('fm5', 'fm6') or s not in ALLOWED:
                bad.append('%s (%s) uses %s from %s/' % (rel.replace('\\', '/'), g, s, o))
    if bad:
        print('check_isolation: forbidden references between games (see proxy.h):')
        for b in bad:
            print('  ' + b)
        return 1
    print('check_isolation: ok (%d objects)' % len(objs))
    return 0


if __name__ == '__main__':
    sys.exit(main())
