import sys, struct
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
exc = mf.exception.exception_records[0]
tid = exc.ThreadId
print('exc', exc.ExceptionRecord.ExceptionCode, hex(exc.ExceptionRecord.ExceptionAddress), 'tid', tid)
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    return None
# context of thread
ctx = None
for t in mf.threads.threads:
    if t.ThreadId == tid:
        ctx = t.ContextObject
print('rip', hex(ctx.Rip), 'rsp', hex(ctx.Rsp))
for reg in ['Rax','Rbx','Rcx','Rdx','Rsi','Rdi','R8','R9','R10','R11','R12','R13','R14','R15','Rbp']:
    v = getattr(ctx, reg); print(reg, hex(v), sym(v) or '')
data = r.read(ctx.Rsp, 0x400)
for i in range(0, len(data), 8):
    v = struct.unpack_from('<Q', data, i)[0]
    s = sym(v)
    if s: print(f'rsp+{i:#x}: {v:#x} {s}')
