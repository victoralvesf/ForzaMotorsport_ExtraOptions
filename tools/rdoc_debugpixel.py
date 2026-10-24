# rdoc_debugpixel.py - roda dentro do qrenderdoc: qrenderdoc.exe --python tools\rdoc_debugpixel.py
# RDOC_CAP (.rdc), RDOC_OUT (arquivo), RDOC_EID (draw), RDOC_PIXELS ("x,y;x,y"). Depura o pixel shader em cada pixel
# e escreve as entradas e, a cada passo que muda variaveis, os valores (instrucao + registradores alterados).
import os, traceback
import renderdoc as rd

out = open(os.environ['RDOC_OUT'], 'w', encoding='utf-8')
try:
    capf = rd.OpenCaptureFile()
    if capf.OpenFile(os.environ['RDOC_CAP'], '', None) != rd.ResultCode.Succeeded: raise RuntimeError('OpenFile')
    st, ctl = capf.OpenCapture(rd.ReplayOptions(), None)
    ctl.SetFrameEvent(int(os.environ['RDOC_EID']), True)
    pipe = ctl.GetPipelineState()
    refl = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
    for pxy in os.environ['RDOC_PIXELS'].split(';'):
        x, y = [int(v) for v in pxy.split(',')]
        inp = rd.DebugPixelInputs()
        trace = ctl.DebugPixel(x, y, inp)
        if trace is None or trace.debugger is None:
            out.write('pixel %d,%d: sem trace (%s)\n' % (x, y, trace and [a for a in dir(trace) if not a.startswith('_')]))
            for m in ctl.GetDebugMessages(): out.write('  msg %s\n' % m.description)
            continue
        out.write('=== pixel %d,%d\n' % (x, y))
        for v in trace.inputs:
            out.write('  in %s = %s\n' % (v.name, list(v.value.f32v[:v.columns])))
        while True:
            states = ctl.ContinueDebug(trace.debugger)
            if not states: break
            for s in states:
                for ch in s.changes:
                    a = ch.after
                    if a.name:
                        out.write('  step %d %s = f%s u%s\n' % (s.nextInstruction, a.name,
                                  ['%.5g' % f for f in a.value.f32v[:max(a.columns, 1)]],
                                  ['%x' % u for u in a.value.u32v[:max(a.columns, 1)]]))
        ctl.FreeTrace(trace)
    ctl.Shutdown()
    capf.Shutdown()
except Exception:
    out.write(traceback.format_exc())
out.close()
os._exit(0)
