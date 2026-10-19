"""Agrega as linhas "stutter:   tid" (amostrador do StutterLog) de varios picos: RIP + 1o retorno por thread.

uso: python tools/stutter_agg.py [azimuth_motorsport.log] [--from QUADRO] [--to QUADRO] [--min-stall MS]
Saida: por thread (tid, inicio), total de amostras e os pares (RIP de 16 bytes, retornos) mais frequentes; com
--blocked, as linhas "bloqueada" (onde as threads do jogo esperam durante a parada).
"""
import re
import sys
from collections import Counter, defaultdict

path, lo, hi, min_stall, blocked = "azimuth_motorsport.log", 0, 1 << 30, 0.0, False
a = sys.argv[1:]
while a:
    x = a.pop(0)
    if x == "--from": lo = int(a.pop(0))
    elif x == "--to": hi = int(a.pop(0))
    elif x == "--min-stall": min_stall = float(a.pop(0))
    elif x == "--blocked": blocked = True
    else: path = x

head = re.compile(r"stutter: amostras do quadro #(\d+): ([\d.]+) ms parado \(apos (\d+) ms\)")
start = re.compile(r"\[tid (\d+)(?: '([^']*)')? [\d.]+ ms inicio ([^\]]+)\]")
row = re.compile(r"stutter:   tid (\d+) \(\d+ amostras\):(.*)")
rowb = re.compile(r"stutter:   bloqueada tid (\d+) inicio (\S+) \((\d+) de \d+\):(.*)")
item = re.compile(r"\s*(\S+) x(\d+)(?: < ([^>]*) >)?;")
per = defaultdict(Counter)
tot = Counter()
names = {}
spikes = 0
take = False
for line in open(path, encoding="utf-8", errors="replace"):
    m = head.match(line)
    if m:
        f, st = int(m[1]), float(m[2]) + int(m[3])
        take = lo <= f <= hi and st >= min_stall
        spikes += take
        for t in start.findall(line):
            names.setdefault(t[0], (t[1] + " " if t[1] else "") + t[2].strip())
        continue
    m = rowb.match(line) if blocked else row.match(line)
    if not m or not take:
        continue
    if blocked:
        names.setdefault(m[1], m[2])
    for rip, n, rets in item.findall(m[4] if blocked else m[2]):
        key = rip + "  < " + " ".join((rets or "").split()[:4 if blocked else 2])
        per[m[1]][key] += int(n)
        tot[m[1]] += int(n)
print(f"{spikes} picos")
for tid, n in tot.most_common(12):
    print(f"\ntid {tid} ({names.get(tid, '?')}): {n} amostras")
    for k, c in per[tid].most_common(12):
        print(f"   {c:5d}  {k}")
