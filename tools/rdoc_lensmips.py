# rdoc_lensmips.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_lensmips.py
# RDOC_CAP (.rdc), RDOC_OUT (pasta). Acha as texturas BC 960x540 com 10 mips (txLens do flare do FM5), le os blocos
# crus de cada mip no ultimo evento e escreve em lensmips.txt, por mip, se a ultima linha de blocos e igual a
# penultima (com a borda repetida pelo proxy: igual) e quantos bytes nao nulos ela tem.
import os, traceback
import renderdoc as rd

out = os.environ['RDOC_OUT']
os.makedirs(out, exist_ok=True)
log = open(os.path.join(out, 'lensmips.txt'), 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    if st != rd.ResultCode.Succeeded: raise RuntimeError('OpenCapture %s' % st)
    last = ctl.GetRootActions()[-1]
    while len(last.children): last = last.children[-1]
    ctl.SetFrameEvent(last.eventId, True)
    for tex in ctl.GetTextures():
        if tex.width != 960 or tex.height != 540 or tex.mips != 10: continue
        name = tex.format.Name()
        log.write('%s %dx%d mips %d fmt %s\n' % (tex.resourceId, tex.width, tex.height, tex.mips, name))
        if 'BC' not in name: continue
        for m in range(1, 5):
            w, h = max(1, 960 >> m), max(1, 540 >> m)
            bw, bh = (w + 3) // 4, (h + 3) // 4
            data = bytes(ctl.GetTextureData(tex.resourceId, rd.Subresource(m, 0, 0)))
            row = bw * 16
            if len(data) < row * bh:
                log.write('  mip %d: %d bytes, esperado %d\n' % (m, len(data), row * bh)); continue
            lastrow, prev = data[row * (bh - 1):row * bh], data[row * (bh - 2):row * (bh - 1)]
            log.write('  mip %d %dx%d (%dx%d blocos): ultima linha == penultima: %s; nao nulos ultima %d penultima %d\n'
                      % (m, w, h, bw, bh, lastrow == prev, sum(1 for b in lastrow if b), sum(1 for b in prev if b)))
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    log.write(traceback.format_exc())
log.close()
os._exit(0)
