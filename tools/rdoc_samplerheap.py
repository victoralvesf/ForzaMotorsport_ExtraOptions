# rdoc_samplerheap.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_samplerheap.py
# RDOC_CAP (.rdc), RDOC_OUT (arquivo), RDOC_HEAP (id do heap), RDOC_IDX (indice), RDOC_SPAN (+-indices, default 4):
# lista os chunks (inclusive do device, fora das acoes) que citam o heap, e os que escrevem perto do indice.
import os, traceback
import renderdoc as rd

out = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    sf = capf.GetStructuredData()
    heap = os.environ['RDOC_HEAP']
    idx = int(os.environ['RDOC_IDX'])
    span = int(os.environ.get('RDOC_SPAN', '4'))
    names = {}
    def flat(o, pre=''):
        if o.NumChildren() == 0:
            bt = o.type.basetype
            try:
                v = o.AsResourceId() if bt == rd.SDBasic.Resource else (o.AsString() if bt in (rd.SDBasic.String, rd.SDBasic.Enum) else (o.AsFloat() if bt == rd.SDBasic.Float else o.AsInt()))
            except Exception:
                v = '?'
            return [(pre + o.name, v)]
        r = []
        for i in range(o.NumChildren()):
            r += flat(o.GetChild(i), pre + o.name + '.')
        return r
    counts = {}
    for ci, c in enumerate(sf.chunks):
        f = flat(c)
        hit = [kv for kv in f if str(kv[1]).endswith('::' + heap)]
        if not hit: continue
        counts[c.name] = counts.get(c.name, 0) + 1
        near = [kv for kv in f if kv[0].endswith('index') and isinstance(kv[1], int) and abs(kv[1] - idx) <= span]
        if near or 'Sampler' in c.name:
            out.write('chunk %d %s: %s\n' % (ci, c.name, ', '.join('%s=%s' % kv for kv in f)[:700]))
    out.write('chunks com o heap: %s\n' % counts)
    capf.Shutdown()
except Exception:
    out.write(traceback.format_exc())
out.close()
os._exit(0)
