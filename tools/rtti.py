import struct,sys,subprocess
d=open('azimuth.exe','rb').read()
IB=0x140000000
# section table
e=struct.unpack_from('<I',d,0x3c)[0]; opt=e+24; nsec=struct.unpack_from('<H',d,e+6)[0]; so=struct.unpack_from('<H',d,e+20)[0]
secs=[struct.unpack_from('<8sIIII',d,opt+so+i*40) for i in range(nsec)]
def o2v(o):
    for n,vs,va,rs,ro in secs:
        if ro<=o<ro+rs: return IB+va+o-ro
def v2o(v):
    r=v-IB
    for n,vs,va,rs,ro in secs:
        if va<=r<va+rs: return r-va+ro
tds=[int(x,16) for x in sys.argv[1:]]
for name_va in tds:
    td=name_va-0x10  # TypeDescriptor: vftable ptr, spare, name
    rva=td-IB
    cols=[]
    for i in range(0,len(d)-24,4):
        if struct.unpack_from('<I',d,i)[0]==1 and struct.unpack_from('<I',d,i+12)[0]==rva:
            cols.append(o2v(i))
    for c in cols:
        cq=struct.pack('<Q',c); i=d.find(cq)
        while i!=-1:
            vt=o2v(i)+8
            print(hex(name_va),'COL',hex(c),'vtable',hex(vt))
            i=d.find(cq,i+1)
