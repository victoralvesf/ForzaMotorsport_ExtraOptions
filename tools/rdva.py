import struct,sys
def load(f):
    d=open(f,'rb').read()
    pe=struct.unpack_from('<I',d,0x3c)[0]; ns=struct.unpack_from('<H',d,pe+6)[0]; so=struct.unpack_from('<H',d,pe+20)[0]
    ib=struct.unpack_from('<Q',d,pe+24+24)[0]; sh=pe+24+so
    secs=[struct.unpack_from('<IIII',d,sh+40*i+8) for i in range(ns)]
    return d,ib,secs
def rd(f,va,n,_c={}):
    if f not in _c: _c[f]=load(f)
    d,ib,secs=_c[f]; rva=va-ib
    for vs,v,rs,ro in secs:
        if v<=rva<v+max(vs,rs): return d[ro+rva-v:ro+rva-v+n]
if __name__=='__main__':
    import uuid
    f=sys.argv[1]
    for a in sys.argv[2:]:
        if a.startswith('g:'): va=int(a[2:],16); print(hex(va),uuid.UUID(bytes_le=rd(f,va,16)))
        else: va=int(a,16); print(hex(va),rd(f,va,64).hex())
