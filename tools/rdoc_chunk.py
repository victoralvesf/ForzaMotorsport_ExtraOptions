# rdoc_chunk.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_chunk.py
# RDOC_CAP (.rdc), RDOC_OUT (arquivo de saida), RDOC_IDS ("1609,1582": ResourceIds) -> grava os chunks do arquivo
# estruturado (inclusive os de criacao, antes do frame) que citam esses ids, com os argumentos em ate 4 niveis.
import os, traceback
import renderdoc as rd

ids = [x.strip() for x in os.environ.get('RDOC_IDS', '').split(',') if x.strip()]
log = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')

def sval(o):
    bt = o.type.basetype
    if bt in (rd.SDBasic.String, rd.SDBasic.Enum): return o.AsString()
    if bt == rd.SDBasic.Float: return o.AsFloat()
    if bt == rd.SDBasic.Resource: return o.AsResourceId()
    return o.AsInt()

def sdump(o, d=0):
    if o.NumChildren() == 0: return '%s=%s' % (o.name, sval(o))
    if d > 4: return o.name + '{..}'
    return '%s{%s}' % (o.name, ', '.join(sdump(o.GetChild(i), d + 1) for i in range(min(o.NumChildren(), 64))))

try:
    capf = rd.OpenCaptureFile()
    st = capf.OpenFile(os.environ['RDOC_CAP'], '', None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile %s' % st)
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    sf = ctl.GetStructuredFile()
    for i, c in enumerate(sf.chunks):
        text = ', '.join(sdump(c.GetChild(k)) for k in range(c.NumChildren()))
        if any(('ResourceId::%s,' % r) in text + ',' or ('ResourceId::%s}' % r) in text for r in ids):
            log.write('chunk %d %s %s\n' % (i, c.name, text[:12000]))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
