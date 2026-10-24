# rdoc_dump.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_dump.py
# Le as variaveis de ambiente RDOC_CAP (arquivo .rdc), RDOC_OUT (pasta de saida) e RDOC_EIDS (lista "eid,eid,..."
# opcional: exporta os render targets ligados nesses eventos). Gera actions.txt (arvore de acoes com RTs) e PNGs.
import os, sys, traceback
import renderdoc as rd

cap_path = os.environ['RDOC_CAP']
out = os.environ['RDOC_OUT']
eids = [int(x) if x.strip() != 'last' else -1 for x in os.environ.get('RDOC_EIDS', '').split(',') if x.strip()]   # last = ultimo draw
os.makedirs(out, exist_ok=True)
log = open(os.path.join(out, 'actions.txt'), 'w', encoding='utf-8')

def save_tex(ctl, rid, name, sub=None):
    s = rd.TextureSave()
    s.resourceId = rid
    s.destType = rd.FileType.PNG
    s.alpha = rd.AlphaMapping.Discard
    s.comp.blackPoint = 0.0
    s.comp.whitePoint = 1.0
    if sub is not None: s.mip = sub
    ctl.SaveTexture(s, os.path.join(out, name))

try:
    capf = rd.OpenCaptureFile()
    st = capf.OpenFile(cap_path, '', None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile %s' % st)
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    texs = {t.resourceId: t for t in ctl.GetTextures()}
    names = {r.resourceId: r.name for r in ctl.GetResources()}

    def tdesc(rid):
        t = texs.get(rid)
        return '%s %dx%d %s ms%d' % (names.get(rid, rid), t.width, t.height, t.format.Name(), t.msSamp) if t else str(rid)

    n = [0]
    def walk(acts, depth):
        for a in acts:
            n[0] += 1
            if a.numIndices and a.outputs[0] != rd.ResourceId.Null(): last[0] = a.eventId
            outs = [o for o in a.outputs if o != rd.ResourceId.Null()]
            log.write('%s%d %s idx=%d inst=%d outs=[%s] depth=%s\n' % ('  ' * depth, a.eventId, a.GetName(ctl.GetStructuredFile()),
                      a.numIndices, a.numInstances, '; '.join(tdesc(o) for o in outs),
                      tdesc(a.depthOut) if a.depthOut != rd.ResourceId.Null() else '-'))
            walk(a.children, depth + 1)
    last = [0]
    walk(ctl.GetRootActions(), 0)
    eids = [last[0] if e < 0 else e for e in eids]
    log.write('total %d acoes\n' % n[0])
    for r in [x for x in os.environ.get('RDOC_USAGE', '').split(',') if x.strip()]:
        rid = [k for k in list(texs) + [b.resourceId for b in ctl.GetBuffers()]
               if str(k).endswith('::' + r.strip())]
        for k in rid:
            us = ctl.GetUsage(k)
            log.write('usage %s: %s\n' % (tdesc(k), ' '.join('%d:%s' % (u.eventId, str(u.usage).split('.')[-1]) for u in us)))
    # RDOC_API="a-b": todas as chamadas da API (nao so acoes) entre os eventos a e b, com os argumentos
    api = os.environ.get('RDOC_API', '')
    if api:
        lo, hi = [int(x) for x in api.split('-')]
        sf = ctl.GetStructuredFile()
        def sdump(o, d=0):
            if o.NumChildren() == 0:
                bt = o.type.basetype
                v = o.AsString() if bt in (rd.SDBasic.String, rd.SDBasic.Enum) else (o.AsFloat() if bt == rd.SDBasic.Float
                    else (o.AsResourceId() if bt == rd.SDBasic.Resource else o.AsInt()))
                return '%s=%s' % (o.name, v)
            if d > 2: return o.name + '{..}'
            return '%s{%s}' % (o.name, ', '.join(sdump(o.GetChild(i), d + 1) for i in range(min(o.NumChildren(), 12))))
        def wapi(acts):
            for a in acts:
                for ev in a.events:
                    if lo <= ev.eventId <= hi:
                        c = sf.chunks[ev.chunkIndex]
                        log.write('api %d %s %s\n' % (ev.eventId, c.name,
                                  ', '.join(sdump(c.GetChild(i)) for i in range(c.NumChildren()))[:600]))
                wapi(a.children)
        wapi(ctl.GetRootActions())
    for e in eids:
        ctl.SetFrameEvent(e, True)
        pipe = ctl.GetPipelineState()
        for i, o in enumerate(pipe.GetOutputTargets()):
            if o.resource != rd.ResourceId.Null():
                save_tex(ctl, o.resource, 'eid%d_rt%d.png' % (e, i))
                t = texs.get(o.resource)
                px = []
                if t:
                    for fx, fy in ((0.5, 0.5), (0.25, 0.75), (0.75, 0.25)):
                        v = ctl.PickPixel(o.resource, int(t.width * fx), int(t.height * fy), rd.Subresource(0, 0, 0),
                                          rd.CompType.Typeless)
                        px.append('(%.4g %.4g %.4g %.4g)' % tuple(v.floatValue[:4]))
                log.write('salvo eid %d rt%d %s px %s\n' % (e, i, tdesc(o.resource), ' '.join(px)))
        # entradas do pixel shader: SRVs (salvas em PNG), samplers, constant buffers (primeiros floats)
        try:
            vp = pipe.GetViewport(0)
            log.write('  viewport %g,%g %gx%g\n' % (vp.x, vp.y, vp.width, vp.height))
            for k, u in enumerate(pipe.GetReadOnlyResources(rd.ShaderStage.Pixel)):
                d = u.descriptor
                log.write('  ps srv[%s] %s fmt %s mip %d+%d\n' % (u.access.index, tdesc(d.resource), d.format.Name(),
                          d.firstMip, d.numMips))
                if d.resource in texs:
                    save_tex(ctl, d.resource, 'eid%d_srv%s.png' % (e, u.access.index))
            for u in pipe.GetSamplers(rd.ShaderStage.Pixel):
                s = u.sampler
                log.write('  ps sampler[%s] filter %s addr %s/%s\n' % (u.access.index, s.filter.minify, s.addressU, s.addressV))
            for u in pipe.GetConstantBlocks(rd.ShaderStage.Pixel):
                d = u.descriptor
                if d.resource == rd.ResourceId.Null(): continue
                data = ctl.GetBufferData(d.resource, d.byteOffset, min(d.byteSize, 64) if d.byteSize else 64)
                import struct
                fl = struct.unpack('<%df' % (len(data) // 4), data[:len(data) // 4 * 4])
                log.write('  ps cb[%s] %s +%#x: %s\n' % (u.access.index, names.get(d.resource, d.resource), d.byteOffset,
                          ' '.join('%.4g' % f for f in fl)))
            for u in pipe.GetReadOnlyResources(rd.ShaderStage.Compute):
                d = u.descriptor
                log.write('  cs srv[%s] %s fmt %s\n' % (u.access.index, tdesc(d.resource), d.format.Name()))
            for u in pipe.GetReadWriteResources(rd.ShaderStage.Compute):
                d = u.descriptor
                log.write('  cs uav[%s] %s fmt %s\n' % (u.access.index, tdesc(d.resource), d.format.Name()))
                if d.resource in texs:
                    save_tex(ctl, d.resource, 'eid%d_uav%s.png' % (e, u.access.index))
            for u in pipe.GetConstantBlocks(rd.ShaderStage.Compute):
                d = u.descriptor
                if d.resource == rd.ResourceId.Null(): continue
                data = ctl.GetBufferData(d.resource, d.byteOffset, 64)
                import struct
                fl = struct.unpack('<16f', data[:64])
                ui = struct.unpack('<16I', data[:64])
                log.write('  cs cb[%s] +%#x: %s | %s\n' % (u.access.index, d.byteOffset, ' '.join('%.4g' % f for f in fl),
                          ' '.join('%#x' % x for x in ui)))
            crefl = pipe.GetShaderReflection(rd.ShaderStage.Compute)
            if crefl:
                dis = ctl.DisassembleShader(pipe.GetComputePipelineObject(), crefl, ctl.GetDisassemblyTargets(True)[0])
                open(os.path.join(out, 'eid%d_cs.txt' % e), 'w', encoding='utf-8').write(dis)
            refl = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
            if refl:
                log.write('  ps shader %s\n' % refl.resourceId)
                dis = ctl.DisassembleShader(pipe.GetGraphicsPipelineObject(), refl, ctl.GetDisassemblyTargets(True)[0])
                open(os.path.join(out, 'eid%d_ps.txt' % e), 'w', encoding='utf-8').write(dis)
            # vertex shader (RDOC_VS=1): disassembly + SRVs/CBs do VS
            if os.environ.get('RDOC_VS'):
                vrefl = pipe.GetShaderReflection(rd.ShaderStage.Vertex)
                if vrefl:
                    log.write('  vs shader %s\n' % vrefl.resourceId)
                    dis = ctl.DisassembleShader(pipe.GetGraphicsPipelineObject(), vrefl, ctl.GetDisassemblyTargets(True)[0])
                    open(os.path.join(out, 'eid%d_vs.txt' % e), 'w', encoding='utf-8').write(dis)
                for u in pipe.GetReadOnlyResources(rd.ShaderStage.Vertex):
                    d = u.descriptor
                    log.write('  vs srv[%s] %s fmt %s\n' % (u.access.index, tdesc(d.resource), d.format.Name()))
                for u in pipe.GetConstantBlocks(rd.ShaderStage.Vertex):
                    d = u.descriptor
                    if d.resource == rd.ResourceId.Null(): continue
                    data = ctl.GetBufferData(d.resource, d.byteOffset, 512)
                    import struct
                    fl = struct.unpack('<%df' % (len(data) // 4), data[:len(data) // 4 * 4])
                    log.write('  vs cb[%s] %s +%#x: %s\n' % (u.access.index, names.get(d.resource, d.resource), d.byteOffset,
                              ' '.join('%.4g' % f for f in fl)))
        except Exception:
            log.write(traceback.format_exc())
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
