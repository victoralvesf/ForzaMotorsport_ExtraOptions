# rdoc_bufdump.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_bufdump.py
# Variaveis: RDOC_CAP (.rdc), RDOC_OUT (arquivo de texto), RDOC_EID (evento), RDOC_STAGE (cs|vs|ps, default cs),
# RDOC_N (elementos por buffer, default 64). Lista os SRVs/UAVs de buffer do estagio nesse evento (offset, tamanho,
# stride) e despeja os primeiros N elementos de cada um como floats e uints.
import os, struct, traceback
import renderdoc as rd

out = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    eid = int(os.environ['RDOC_EID'])
    stage = {'cs': rd.ShaderStage.Compute, 'vs': rd.ShaderStage.Vertex, 'ps': rd.ShaderStage.Pixel}[
        os.environ.get('RDOC_STAGE', 'cs')]
    n = int(os.environ.get('RDOC_N', '64'))
    ctl.SetFrameEvent(eid, True)
    pipe = ctl.GetPipelineState()
    for kind, lst in (('srv', pipe.GetReadOnlyResources(stage)), ('uav', pipe.GetReadWriteResources(stage))):
        for u in lst:
            d = u.descriptor
            if d.resource == rd.ResourceId.Null(): continue
            stride = d.elementByteSize or 16
            out.write('%s[%s] %s off %#x size %#x stride %d fmt %s\n' % (kind, u.access.index, d.resource, d.byteOffset,
                      d.byteSize, stride, d.format.Name()))
            data = ctl.GetBufferData(d.resource, d.byteOffset, min(d.byteSize or stride * n, stride * n))
            for i in range(len(data) // stride):
                e = data[i * stride:(i + 1) * stride]
                k = len(e) // 4
                fl = struct.unpack('<%df' % k, e[:k * 4])
                ui = struct.unpack('<%dI' % k, e[:k * 4])
                out.write('  [%3d] %s | %s\n' % (i, ' '.join('%.4g' % f for f in fl), ' '.join('%x' % x for x in ui)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    out.write(traceback.format_exc())
out.close()
os._exit(0)
