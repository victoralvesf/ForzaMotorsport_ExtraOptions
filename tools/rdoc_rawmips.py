# rdoc_rawmips.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_rawmips.py
# RDOC_CAP (.rdc), RDOC_OUT (pasta), RDOC_TEX (id). Salva os bytes crus de cada mip (GetTextureData) em mip<N>.bin,
# lidos no ultimo evento da captura.
import os, traceback
import renderdoc as rd

out = os.environ['RDOC_OUT']
os.makedirs(out, exist_ok=True)
log = open(os.path.join(out, 'rawmips.txt'), 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    last = ctl.GetRootActions()[-1]
    while len(last.children): last = last.children[-1]
    ctl.SetFrameEvent(last.eventId, True)
    tex = [t for t in ctl.GetTextures() if str(t.resourceId).endswith('::' + os.environ['RDOC_TEX'])][0]
    for m in range(tex.mips):
        data = bytes(ctl.GetTextureData(tex.resourceId, rd.Subresource(m, 0, 0)))
        open(os.path.join(out, 'mip%d.bin' % m), 'wb').write(data)
        log.write('mip %d %d bytes\n' % (m, len(data)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
