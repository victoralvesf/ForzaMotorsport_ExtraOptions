"""Aggregate the continuous profiler lines of azimuth_motorsport.log (StutterLog=1, PASSO 72/117).

Lines: "stutter: profile '<label>' 10 s: N samples ...; modules: m P% ...",
       "stutter: profile '<label>' RIPs: loc P%; ..." and "... 1st return into game/azimuth/proxy: loc P%; ...".
The label's " tid N" is dropped so every thread with the same name/start address adds up. Percentages are turned
back into sample counts with the window's N.

usage: python tools/profile_agg.py [log] [--from LINE] [--to LINE] [--top 25] [--thread SUBSTR]
"""
import re
import sys
from collections import defaultdict

args = sys.argv[1:]
log = "azimuth_motorsport.log"
lo, hi, top, only = 0, 1 << 60, 25, None
i = 0
while i < len(args):
    a = args[i]
    if a == "--from": lo = int(args[i + 1]); i += 2
    elif a == "--to": hi = int(args[i + 1]); i += 2
    elif a == "--top": top = int(args[i + 1]); i += 2
    elif a == "--thread": only = args[i + 1]; i += 2
    else: log = a; i += 1

head = re.compile(r"stutter: profile '(.+?)' 10 s: (\d+) samples.*?modules:(.*)")
body = re.compile(r"stutter: profile '(.+?)' (RIPs|1st return into game/azimuth/proxy):(.*)")
item = re.compile(r"\s*(\S+) ([\d.]+)%;?")
samples = defaultdict(float)
mods = defaultdict(lambda: defaultdict(float))
rips = defaultdict(lambda: defaultdict(float))
rets = defaultdict(lambda: defaultdict(float))
last_n = {}

def key(label):
    return re.sub(r" tid \d+", "", label)

with open(log, encoding="latin-1") as f:
    for no, line in enumerate(f, 1):
        if no < lo or no > hi or "stutter: profile '" not in line:
            continue
        m = head.search(line)
        if m:
            k, n = key(m.group(1)), int(m.group(2))
            if only and only not in k: continue
            last_n[k] = n
            samples[k] += n
            for name, p in re.findall(r" (\S+) (\d+)%", m.group(3)):
                mods[k][name] += n * float(p) / 100
            continue
        m = body.search(line)
        if m:
            k = key(m.group(1))
            if k not in last_n: continue
            dst = rips if m.group(2) == "RIPs" else rets
            for loc, p in re.findall(r" (\S+) ([\d.]+)%;", m.group(3)):
                dst[k][loc] += last_n[k] * float(p) / 100

for k in sorted(samples, key=lambda k: -samples[k]):
    n = samples[k]
    print(f"== {k}: {n:.0f} samples")
    print("   modules: " + ", ".join(f"{m} {v * 100 / n:.0f}%" for m, v in sorted(mods[k].items(), key=lambda x: -x[1])[:8]))
    print("   RIPs:")
    for loc, v in sorted(rips[k].items(), key=lambda x: -x[1])[:top]:
        print(f"     {v * 100 / n:5.1f}%  {loc}")
    print("   1st return:")
    for loc, v in sorted(rets[k].items(), key=lambda x: -x[1])[:top]:
        print(f"     {v * 100 / n:5.1f}%  {loc}")
