import struct,hashlib,json
az=open('azimuth.exe','rb').read()
def o(va): return va-0x140001400
def q(va): return struct.unpack_from('<Q',az,o(va))[0]
def I(va): return struct.unpack_from('<I',az,o(va))[0]
def cs(va): s=o(va); return az[s:az.find(b'\0',s)].decode()
class PE:
    def __init__(s,p):
        d=s.d=open(p,'rb').read()
        e=struct.unpack_from('<I',d,0x3c)[0]; opt=e+24
        n=struct.unpack_from('<H',d,e+6)[0]; so=struct.unpack_from('<H',d,e+20)[0]
        s.secs=[struct.unpack_from('<8sIIII',d,opt+so+i*40) for i in range(n)]
    def r2o(s,rva):
        for nm,vs,va,rs,ro in s.secs:
            if va<=rva<va+rs: return rva-va+ro
    def read(s,rva,n): x=s.r2o(rva); return s.d[x:x+n]
L={0xa8:2,0xb8:8,0xd0:0x16,0xe0:0x124,0xf0:4,0x100:10,0x110:0x15,0x120:0x1b,0x130:0x3c,0x140:0x61,0x150:4,0x160:0x1d,0x170:0x29,0x180:0x1a,0x190:0x1f,0x1a8:0x22,0x1b8:0x15,0x1c8:4,0x1d8:0x13,0x1e8:6,0x1f8:6,0x208:6,0x218:0x10,0x228:0x10,0x240:0xe,0x2d8:10,0x2e8:0x10,0x2f8:5}
R1=0x1402a1eb0
fh=PE('game_binaries/FH2/forza_durango_release_final.exe')
out={}
for off,n in sorted(L.items()):
    rva=I(R1+off); h=cs(q(R1+off+8)); b=fh.read(rva,n)
    ok=hashlib.sha256(b).hexdigest()==h
    # try other lengths if mismatch
    alt=None
    if not ok:
        for m in range(1,0x200):
            if hashlib.sha256(fh.read(rva,m)).hexdigest()==h: alt=m;break
    print('+%#05x rva %#09x len %#05x %s%s'%(off,rva,n,'OK' if ok else 'MISMATCH',' (len real %#x)'%alt if alt else ''))
    out[off]=(rva,alt or n)
json.dump({hex(k):v for k,v in out.items()},open(r'%s/sites.json'%__import__('sys').argv[1],'w'))
