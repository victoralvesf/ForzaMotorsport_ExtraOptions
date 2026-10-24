# rdoc_sampler.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_sampler.py
# RDOC_CAP (.rdc), RDOC_OUT (arquivo de texto), RDOC_EIDS (eventos). Escreve todos os campos dos samplers do
# pixel shader em cada evento.
import os, traceback
import renderdoc as rd

out = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    for e in [int(x) for x in os.environ['RDOC_EIDS'].split(',')]:
        ctl.SetFrameEvent(e, True)
        for u in ctl.GetPipelineState().GetSamplers(rd.ShaderStage.Pixel):
            s = u.sampler
            f = s.filter
            out.write('eid %d s%s: min %s mag %s mip %s func %s aniso %s addr %s/%s/%s bias %g lod %g..%g cmp %s border %s\n' % (
                e, u.access.index, f.minify, f.magnify, f.mip, f.filter, s.maxAnisotropy, s.addressU, s.addressV,
                s.addressW, s.mipBias, s.minLOD, s.maxLOD, s.compareFunction, list(s.borderColorValue.floatValue)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    out.write(traceback.format_exc())
out.close()
os._exit(0)
