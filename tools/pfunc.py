import sys,struct
d=open(sys.argv[1],'rb').read()
p=0x327000; ents=[]
while True:
    b,e,u=struct.unpack_from('<III',d,p); p+=12
    if b==0: break
    ents.append((b,e,u))
for a in sys.argv[2:]:
    r=int(a,16)-0x140000000
    for b,e,u in ents:
        if b<=r<e: print(a,'in',hex(b+0x140000000),'-',hex(e+0x140000000),'unwind',hex(u)); break
