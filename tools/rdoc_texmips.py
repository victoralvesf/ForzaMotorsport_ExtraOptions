# rdoc_texmips.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_texmips.py
# RDOC_CAP (.rdc), RDOC_OUT (pasta), RDOC_TEX (id do recurso), RDOC_EID (evento onde ler). Salva cada mip em PNG
# (texmip<N>.png, sem normalizar) e escreve em mips.txt o tamanho e as linhas mais claras de cada mip.
import os, traceback
import renderdoc as rd

out = os.environ['RDOC_OUT']
os.makedirs(out, exist_ok=True)
log = open(os.path.join(out, 'mips.txt'), 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    ctl.SetFrameEvent(int(os.environ['RDOC_EID']), True)
    tex = [t for t in ctl.GetTextures() if str(t.resourceId).endswith('::' + os.environ['RDOC_TEX'])][0]
    log.write('%s %dx%d mips %d fmt %s\n' % (tex.resourceId, tex.width, tex.height, tex.mips, tex.format.Name()))
    for m in range(tex.mips):
        s = rd.TextureSave()
        s.resourceId = tex.resourceId
        s.destType = rd.FileType.PNG
        s.alpha = rd.AlphaMapping.Discard
        s.mip = m
        s.comp.blackPoint = 0.0
        s.comp.whitePoint = 1.0
        ctl.SaveTexture(s, os.path.join(out, 'texmip%d.png' % m))
        log.write('mip %d %dx%d\n' % (m, max(1, tex.width >> m), max(1, tex.height >> m)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
