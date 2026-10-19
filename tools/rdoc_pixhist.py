# rdoc_pixhist.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_pixhist.py
# Historico de pixel (PixelHistory) de uma textura em alguns pontos, com o estado de cada draw que tocou o pixel.
# Variaveis: RDOC_CAP (.rdc), RDOC_OUT (pasta), RDOC_TEX (id numerico da textura, ex. 141573),
# RDOC_PIXELS ("x,y;x,y;..."), RDOC_SAMPLE (amostra MSAA, padrao 0), RDOC_EVENT (evento final, padrao o ultimo).
# Gera pixhist.txt: por pixel, cada modificacao (eid, flags de descarte/teste, cor antes/saida/depois, prof) e o
# estado do draw (VS/PS, topologia, cull, depth func/write, write mask do RT0).
import os, traceback
import renderdoc as rd

cap_path = os.environ['RDOC_CAP']
out = os.environ['RDOC_OUT']
tex_id = os.environ['RDOC_TEX'].strip()
pixels = [tuple(int(v) for v in p.split(',')) for p in os.environ['RDOC_PIXELS'].split(';') if p.strip()]
sample = int(os.environ.get('RDOC_SAMPLE', '0'))
os.makedirs(out, exist_ok=True)
log = open(os.path.join(out, 'pixhist.txt'), 'w', encoding='utf-8')

def col(v):
    return '(%.4g %.4g %.4g %.4g)' % tuple(v.floatValue[:4])

try:
    capf = rd.OpenCaptureFile()
    st = capf.OpenFile(cap_path, '', None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile %s' % st)
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    texs = {t.resourceId: t for t in ctl.GetTextures()}
    tex = [k for k in texs if str(int(k)) == tex_id or str(k).endswith('::' + tex_id)][0]
    last = [0]
    def walk(acts):
        for a in acts:
            last[0] = max(last[0], a.eventId)
            walk(a.children)
    walk(ctl.GetRootActions())
    end = int(os.environ.get('RDOC_EVENT', '0')) or last[0]
    ctl.SetFrameEvent(end, True)
    states = {}
    def state(eid):
        if eid in states: return states[eid]
        ctl.SetFrameEvent(eid, True)
        p = ctl.GetPipelineState()
        s = []
        try:
            s.append('vs %s ps %s' % (p.GetShader(rd.ShaderStage.Vertex), p.GetShader(rd.ShaderStage.Pixel)))
            d = ctl.GetD3D12PipelineState()
            r = d.rasterizer.state
            s.append('cull %s fill %s' % (str(r.cullMode).split('.')[-1], str(r.fillMode).split('.')[-1]))
            ds = d.outputMerger.depthStencilState
            s.append('depth test %d write %d func %s stencil %d' % (ds.depthEnable, ds.depthWrites,
                     str(ds.depthFunction).split('.')[-1], ds.stencilEnable))
            b = d.outputMerger.blendState.blends[0]
            s.append('blend %d mask %#x' % (b.enabled, b.writeMask))
        except Exception as e:
            s.append('estado: %s' % e)
        states[eid] = ' | '.join(s)
        return states[eid]
    for (x, y) in pixels:
        ctl.SetFrameEvent(end, True)
        hist = ctl.PixelHistory(tex, x, y, rd.Subresource(0, 0, sample), rd.CompType.Typeless)
        log.write('=== pixel %d,%d amostra %d: %d modificacoes\n' % (x, y, sample, len(hist)))
        for m in hist:
            fl = [n for n in ('sampleMasked', 'backfaceCulled', 'depthClipped', 'depthBoundsFailed', 'viewClipped',
                              'scissorClipped', 'shaderDiscarded', 'depthTestFailed', 'stencilTestFailed')
                  if getattr(m, n, False)]
            log.write('%d prim %d %s antes %s saida %s depois %s prof %.6g->%.6g\n' % (
                m.eventId, m.primitiveID, ','.join(fl) or 'passou', col(m.preMod.col), col(m.shaderOut.col),
                col(m.postMod.col), m.preMod.depth, m.postMod.depth))
        log.flush()
    seen = sorted({m.eventId for (x, y) in pixels for m in ctl.PixelHistory(tex, x, y, rd.Subresource(0, 0, sample),
                                                                             rd.CompType.Typeless)})
    log.write('=== estado dos draws\n')
    for e in seen:
        log.write('%d %s\n' % (e, state(e)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
