"""reach.py EXE FROM_RVA TO_RVA [depth]: busca em largura por chamadas diretas (E8) de FROM ate TO
(limites de funcao pela .pdata); imprime o caminho achado."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); src = int(sys.argv[2], 16); dst = int(sys.argv[3], 16)
depth = int(sys.argv[4]) if len(sys.argv) > 4 else 6
t = pe.text; base = pe.text_rva

def callees(f):
    r = pe.func(f)
    if not r: return []
    a, b = r; out = []
    for i in range(a - base, min(b - base, len(t) - 5)):
        if t[i] == 0xe8:
            out.append(base + i + 5 + struct.unpack_from('<i', t, i + 1)[0])
    return out

prev = {src: None}; front = [src]
for _ in range(depth):
    nxt = []
    for f in front:
        for c in callees(f):
            fc = pe.func(c); c = fc[0] if fc else c
            if c in prev: continue
            prev[c] = f; nxt.append(c)
            if c == dst or (pe.func(dst) and c == pe.func(dst)[0]):
                path = [c]
                while prev[path[-1]] is not None: path.append(prev[path[-1]])
                print(' -> '.join(hex(x) for x in reversed(path))); sys.exit(0)
    front = nxt
print('nao alcancado em', depth, 'niveis;', len(prev), 'funcoes visitadas')
