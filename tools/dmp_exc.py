import sys, struct
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
e = mf.exception.exception_records[0]
loc = e.ThreadContext
raw = open(sys.argv[1],'rb').read()
c = raw[loc.Rva:loc.Rva+loc.DataSize]
names = ['Rax','Rcx','Rdx','Rbx','Rsp','Rbp','Rsi','Rdi','R8','R9','R10','R11','R12','R13','R14','R15','Rip']
regs = dict(zip(names, struct.unpack_from('<17Q', c, 0x78)))
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    return ''
for k,v in regs.items(): print(k, hex(v), sym(v))
print('game base?', [ (hex(lo),n) for lo,hi,n in mods][:5])
try:
    data = r.read(regs['Rsp'], 0x600)
except Exception as ex:
    print('stack read fail', ex); sys.exit()
for i in range(0, len(data), 8):
    v = struct.unpack_from('<Q', data, i)[0]
    s = sym(v)
    if s or 0x140000000 <= v < 0x143000000: print(f'rsp+{i:#x}: {v:#x} {s}')
