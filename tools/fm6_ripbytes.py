"""fm6_ripbytes.py EXE OFF [OFF...] : acessos de byte [rip+X] (cmp/movzx/mov/test) e bases B com B+OFF lidos (objeto
estatico de opcoes da linha de comando). Imprime as bases que cobrem mais offsets e os sites."""
import sys,os,re,struct,collections
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1]); d=pe.d
offs=[int(x,16) for x in sys.argv[2:]]
t0=pe.r2o(pe.text_rva); t=d[t0:t0+pe.text_len]
acc=collections.defaultdict(list)
pats=[(rb'\x80\x3d',2,7),(rb'\x0f\xb6[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',3,7),(rb'\x44\x0f\xb6[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',4,8),
      (rb'[\x8a\x38\x3a][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',2,6),(rb'\x44[\x8a\x38\x3a][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',3,7),
      (rb'\xf6\x05',2,7)]
for p,dispo,ln in pats:
    for m in re.finditer(p,t):
        j=m.start(); r=pe.text_rva+j
        x=r+ln+struct.unpack_from('<i',t,j+dispo)[0]
        acc[x].append(r)
score=collections.Counter()
for x in acc:
    for o in offs: score[x-o]+=1
for b,n in score.most_common(5):
    print('base',hex(b),n,{hex(o):[hex(s) for s in acc.get(b+o,[])][:6] for o in offs})
