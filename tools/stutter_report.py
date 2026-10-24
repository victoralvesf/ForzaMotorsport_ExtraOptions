"""Resumo das linhas "stutter:" do azimuth_motorsport.log (common/stutter.c, debug.ini StutterLog=1).

uso: python tools/stutter_report.py [azimuth_motorsport.log] [--min MS]
Por pico: quadro, ms, media, tempo no Present, PSO/recursos/ECL e a espera da thread do Present (P);
as linhas do amostrador (CPU por thread, RIPs) logo abaixo; no fim: picos agrupados pela pilha da espera mais longa fora das threads ociosas.
"""
import re
import sys
from collections import defaultdict

path = "azimuth_motorsport.log"
min_ms = 0.0
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == "--min":
        min_ms = float(args.pop(0))
    else:
        path = a

head = re.compile(r"stutter: quadro #(\d+) \(pico (\d+)\) ([\d.]+) ms \(media ([\d.]+); ([\d.]+) no Present\) \| "
                  r"PSO (\d+) ([\d.]+) ms \| recursos (\d+) ([\d.]+) ms (\d+) MB \| ECL (\d+) \((\d+) listas\) ([\d.]+) ms")
wait = re.compile(r"\[(\w+) ([\d.]+) ms tid (\d+)( P)?((?: (?:game|azimuth)\+0x[0-9a-f]+)*)\]")

by_stack = defaultdict(list)
rows = []
with open(path, encoding="utf-8", errors="replace") as f:
    for line in f:
        if line.startswith(("stutter: amostras", "stutter:   tid", "stutter:   bloqueada")):
            rows.append(("amostra", line.strip()[9:]))
            continue
        if line.startswith("stutter: 10 s"):
            rows.append(("resumo", line.strip()[9:]))
            continue
        m = head.match(line)
        if not m:
            continue
        frame, _, ms, avg, inp, pso, pso_ms, res, res_ms, mb, ecl, lists, ecl_ms = m.groups()
        if float(ms) < min_ms:
            continue
        ws = wait.findall(line)
        p = [w for w in ws if w[3]]
        pw = "P %s %s ms%s" % (p[0][0], p[0][1], p[0][4]) if p else "P sem espera longa"
        rows.append(("pico", "#%s %6s ms (media %5s, Present %5s) PSO %s/%sms rec %s/%sms/%sMB ECL %s/%s | %s"
                     % (frame, ms, avg, inp, pso, pso_ms, res, res_ms, mb, ecl, lists, pw)))
        if p:
            by_stack[p[0][0] + p[0][4]].append(float(ms))

for kind, text in rows:
    print({"pico": "  ", "amostra": "      "}.get(kind, "-- ") + text)
print()
print("espera da thread do Present nos picos (pilha -> picos, soma ms):")
for k, v in sorted(by_stack.items(), key=lambda kv: -sum(kv[1])):
    print("  %3d  %8.1f  %s" % (len(v), sum(v), k))
