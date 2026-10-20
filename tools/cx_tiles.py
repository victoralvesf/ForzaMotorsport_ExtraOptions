"""cx_tiles.py LOG - caixa-preta do proxy (linhas "stutter: cx", PASSO 76): heaps usados nos UpdateTileMappings,
   tiles de heap mapeados mais de uma vez (e se o recurso anterior ja tinha sido solto/destruido antes do remapeamento)
   e resumo por tipo de evento."""
import sys, re, collections
ev = []
pat = re.compile(r'stutter: cx ([\d.]+) ms tid (\d+) (.+?) (0x)?([0-9A-Fa-f]{16}) (0x)?([0-9A-Fa-f]{16}) \[([\d ]+)\]')
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = pat.search(line)
    if m: ev.append((float(m[1]), int(m[2]), m[3], m[5], m[7], [int(v) for v in m[8].split()]))
print(len(ev), 'eventos; de', ev[0][0] if ev else 0, 'ms a', ev[-1][0] if ev else 0, 'ms antes da queda')
print('por tipo:', dict(collections.Counter(e[2] for e in ev)))
heaps = collections.Counter(e[4] for e in ev if e[2] == 'UpdateTileMappings')
print('heaps dos mapeamentos:', dict(heaps))
gone = {}                                         # recurso -> ms em que o reaper soltou / foi destruido
for t, tid, k, a, b, x in ev:
    if k.startswith('reaper') or k == 'destruido': gone.setdefault(a, (t, k))
owner = {}
for t, tid, k, a, b, x in ev:
    if k != 'UpdateTileMappings' or x[1] == 0: continue
    start, cnt = x[6], max(x[7], 1)
    for tile in range(start, start + cnt):
        key = (b, tile)
        if key in owner and owner[key][1] != a:
            pt, pa = owner[key]
            g = gone.get(pa)
            print(f'tile {tile} do heap {b}: {pa} em {pt} ms -> {a} em {t} ms; anterior solto: {g}')
        owner[key] = (t, a)
