# Resumo de um azimuth_motorsport.log (StutterLog=1) por janela de 10 s, para comparar jogos (FH2 x FM6):
# quadros, ms/quadro, ECL/quadro, listas/quadro, abertas/quadro, draws/quadro, CreateCommandList/quadro, memoria
# privada + (linha "memoria 10 s") allocators vivos, committed DEFAULT, WC, RW com guarda.
# Uso: python tools/cmp_vazao.py <log> [<log2> ...]
import re, sys

PAT = {
    'quadros': r'vazao 10 s: (\d+) quadros',
    'ms': r'quadros, ([\d.]+) ms/quadro',
    'ECL': r'ECL ([\d.]+)/quadro',
    'listas': r'listas (\d+)/quadro',
    'max': r'listas \d+/quadro \(max (\d+)\)',
    'abertas': r'abertas (\d+)/quadro',
    'draws': r'draws (\d+)/quadro',
    'CCL': r'CreateCommandList ([\d.]+)/quadro',
    'priv': r'memoria privada (\d+) MiB',
}
MEM = {
    'alloc': r'\[allocators (\d+),',
    'def': r'\[committed DEFAULT (\d+),',
    'WC': r' WC (\d+) MiB',
    'guarda': r'RW com guarda (\d+) MiB',
}

def rows(path):
    out, mem = [], {}
    for line in open(path, encoding='latin-1'):
        if line.startswith('stutter: memoria'):
            mem = {k: (re.search(p, line).group(1) if re.search(p, line) else '-') for k, p in MEM.items()}
        elif line.startswith('stutter: vazao 10 s:'):
            r = {k: (re.search(p, line).group(1) if re.search(p, line) else '-') for k, p in PAT.items()}
            r.update(mem or {k: '-' for k in MEM})
            out.append(r)
    return out

cols = list(PAT) + list(MEM)
for path in sys.argv[1:]:
    rs = rows(path)
    print(f'== {path}: {len(rs)} janelas de 10 s')
    print(' '.join(f'{c:>8}' for c in cols))
    for r in rs: print(' '.join(f'{r[c]:>8}' for c in cols))
    def num(c):
        v = [float(r[c]) for r in rs if r[c] not in ('-', '')]
        return v
    for c in ('listas', 'abertas', 'draws', 'ECL', 'priv', 'alloc', 'WC', 'guarda'):
        v = num(c)
        if v: print(f'   {c:>8}: media {sum(v)/len(v):9.1f}  max {max(v):9.1f}')
