# rdoc_root.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_root.py
# Root signature ligada em alguns eventos (D3D12): parametros (tipo, visibilidade, registradores) e o que esta ligado
# em cada um (buffer+offset dos root CBVs com os primeiros floats). Variaveis: RDOC_CAP (.rdc), RDOC_OUT (arquivo),
# RDOC_EIDS ("eid,eid"); RDOC_INTRO=1 lista os atributos dos objetos da API (para versoes novas do RenderDoc).
import os, struct, traceback
import renderdoc as rd

log = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')
eids = [int(x) for x in os.environ['RDOC_EIDS'].split(',') if x.strip()]
SKIP = ('acquire', 'append', 'disown', 'next', 'own', 'this', 'thisown')

def intro(o, name, d=0):
    log.write('%s%s: %s\n' % ('  ' * d, name, [a for a in dir(o) if not a.startswith('_') and a not in SKIP]))
    if d > 2: return
    for a in dir(o):
        if a.startswith('_') or a in SKIP: continue
        try: v = getattr(o, a)
        except Exception: continue
        if callable(v) or isinstance(v, (int, float, str, bool)): continue
        if hasattr(v, '__len__'):
            if len(v): intro(v[0], '%s[0] (%d)' % (a, len(v)), d + 1)
        elif type(v).__module__ == 'renderdoc' and not isinstance(v, rd.ResourceId):
            intro(v, a, d + 1)

def short(v):
    return str(v).split('.')[-1]

try:
    capf = rd.OpenCaptureFile()
    st = capf.OpenFile(os.environ['RDOC_CAP'], '', None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile %s' % st)
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    names = {r.resourceId: r.name for r in ctl.GetResources()}
    for e in eids:
        ctl.SetFrameEvent(e, True)
        ps = ctl.GetD3D12PipelineState()
        rs = ps.rootSignature
        log.write('=== eid %d rs %s\n' % (e, getattr(rs, 'resourceId', '?')))
        if os.environ.get('RDOC_INTRO'):
            intro(rs, 'rootSignature')
            continue
        for i, p in enumerate(rs.parameters):
            line = 'p%d vis %s' % (i, short(p.visibility))
            if len(p.tableRanges):
                line += ' tabela[%s]' % ' '.join('%s %d %s%d sp%d' % (short(r.category), r.count, '', r.baseRegister, r.space)
                                                 for r in p.tableRanges)
                line += ' heap +%#x' % p.heapByteOffset
            elif len(p.constants):
                line += ' %d constantes reg b%d sp%d: %s' % (len(p.constants) // 4, p.reg, p.space, bytes(p.constants[:32]).hex())
            else:
                d = p.descriptor
                line += ' %s reg %d sp%d -> %s +%#x' % (short(d.type), p.reg, p.space, names.get(d.resource, d.resource),
                                                       d.byteOffset)
                if d.resource != rd.ResourceId.Null() and 'Buffer' in short(d.type) or short(d.type) == 'ConstantBuffer':
                    try:
                        data = ctl.GetBufferData(d.resource, d.byteOffset, 64)
                        fl = struct.unpack('<%df' % (len(data) // 4), data[:len(data) // 4 * 4])
                        line += ' | ' + ' '.join('%.4g' % f for f in fl)
                    except Exception:
                        pass
            log.write(line + '\n')
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
