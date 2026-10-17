import sys,os,pickle
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
import fmatch
fh,fm=PE(sys.argv[1]),PE(sys.argv[2])
F,inv=pickle.load(open(os.path.join(os.path.dirname(__file__),'fm5_index.pkl'),'rb'))
for a in sys.argv[3:]:
    f=fh.func(int(a,16))
    ins,top=fmatch.best_matches(fh,f[0],f[1],F,inv,k=4)
    print(hex(f[0]),'(%d instr)'%len(ins),' '.join('%s:%.2f'%(hex(c),s) for s,c,b in top))
