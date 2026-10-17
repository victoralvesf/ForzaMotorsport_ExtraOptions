import struct,sys
d=open(sys.argv[1],'rb').read()
B=0x140000000
def off(va):
    # .rdata VA = file_offset + 0x140001400 ; .text off = rva-0xc00
    return va-0x140001400
def cstr(va):
    o=off(va); e=d.index(b'\0',o); return d[o:e].decode('latin1')
tab=int(sys.argv[2],16); n=int(sys.argv[3],16)
for i in range(n):
    p=struct.unpack_from('<Q',d,off(tab)+8*i)[0]
    print(hex(i), hex(i*8), cstr(p) if p else '-')
