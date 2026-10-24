"""audio_dump.py [DIR] [--wav OUTDIR] [--ctx N] - analisa a gravacao do audio do FM6 (debug.ini AudioDump=N).

DIR (padrao: raiz do projeto) tem audio_fm6_shape.bin e audio_fm6_out.wav (winmm_proxy/audio_fm6.c):
  - shape.bin: por grafo SHAPE consumido as entradas, contextos SRC/XMA de cada SRC (antes/depois) e os 128 floats
    de cada mix buffer levado por DMA ao jogo; comandos de contexto XMA (ACP 5..8).
  - out.wav: saida do WASAPI do jogo.
Relata por contexto XMA cada trecho tocado (grafos seguidos com SRC): inicio/fim abrupto, blocos de silencio no
meio (falta de dados), saltos de amostra; e os cliques na saida final. --wav grava um WAV por contexto (linha do
tempo inteira, 48 kHz mono, zeros fora dos trechos). --ctx N mostra os contextos SRC/XMA crus de N.
--xma OUTDIR: pacotes XMA crus de cada voz nova -> OUTDIR/ctxN_seqM.xma, decodificados pelo FFmpeg do Azimuth
(winmm_proxy/build/xma_decode.exe, tools/xma_decode.c) e comparados com o que o Azimuth entregou ao jogo:
onde a 1a amostra entregue cai no PCM completo (= amostras descartadas no inicio).
"""
import subprocess
import os, struct, sys, wave
from collections import defaultdict

try:
    import numpy as np
except ImportError:
    np = None

BLOCK = 128
RATE = 48000


def read_shape(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'AZSH', 'cabecalho'
    o = 8
    graphs, srcs, mixes, cmds, inputs = [], defaultdict(list), defaultdict(dict), [], []
    while o + 8 <= len(d):
        t, n = struct.unpack_from('<II', d, o)
        p = d[o + 8:o + 8 + n]
        if len(p) < n:
            break
        o += 8 + n
        if t == 1:
            seq, gid, stamp, cnt, _ = struct.unpack_from('<IIQII', p)
            ents = struct.unpack_from('<%dQ' % cnt, p, 24)
            graphs.append((seq, gid, stamp, ents))
        elif t == 2:
            seq, ctx, reset = struct.unpack_from('<IIQ', p)
            srcs[seq].append((ctx, reset, p[16:48], p[48:112], p[112:144], p[144:208]))
        elif t == 3:
            seq, mix, dma, _ = struct.unpack_from('<IIII', p)
            mixes[seq][mix] = (dma, struct.unpack_from('<128f', p, 16))
        elif t == 4:
            seq, typ, stamp = struct.unpack_from('<IIQ', p)
            mask = p[16:80]
            ctxs = [i for i in range(512) if mask[i >> 3] >> (i & 7) & 1]
            cmds.append((seq, typ, stamp, ctxs))
        elif t == 5:
            seq, ctx = struct.unpack_from('<II', p)
            xma = p[8:72]
            s0, s1 = struct.unpack_from('<II', p, 72)
            inputs.append((seq, ctx, xma, p[80:80 + s0] + p[80 + s0:80 + s0 + s1]))
    return graphs, srcs, mixes, cmds, inputs


def xma_fields(xma):
    w = struct.unpack_from('<16I', xma)
    ch = (w[1] >> 29 & 1) + 1
    rate = (24000, 32000, 44100, 48000)[w[1] >> 27 & 3]
    skip_sub = w[1] >> 24 & 7
    bit = w[2] & 0x3fff
    packet = (w[2] & 0x3ffffff) >> 14
    return ch, rate, skip_sub, bit, packet


def frames_before(data, packet, bit):
    """quadros do pacote inicial antes do bit de inicio (como o Azimuth conta em 0x14018ed10)"""
    pk = data[packet * 0x800:(packet + 1) * 0x800]
    if len(pk) < 4:
        return 0
    pos = ((pk[0] << 16 | pk[1] << 8 | pk[2]) & 0x3fff8) >> 3
    pos += 0x20
    n = 0
    while pos < bit and n < pk[0] >> 2:
        ln = 0
        for k in range(15):
            ln = ln << 1 | (pk[(pos + k) >> 3] >> (7 - ((pos + k) & 7)) & 1)
        if ln < 15:
            break
        pos += ln
        n += 1
    return n


def check_xma(inputs, v, outdir):
    exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'winmm_proxy', 'build', 'xma_decode.exe')
    os.makedirs(outdir, exist_ok=True)
    seen = set()
    for seq, ctx, xma, data in inputs:
        ch, rate, skip_sub, bit, packet = xma_fields(xma)
        key = (len(data), data[:64])
        fb = frames_before(data, packet, bit)
        discard = (skip_sub + fb * 4) * 128
        print('voz nova ctx %d grafo %d: %d canais %d Hz, %d pacotes, inicio pacote %d bit %#x (%d quadros antes), '
              'subquadros a pular %d -> Azimuth descarta %d' % (ctx, seq, ch, rate, len(data) // 0x800, packet, bit,
                                                                fb, skip_sub, discard))
        if key in seen:
            continue
        seen.add(key)
        base = os.path.join(outdir, 'ctx%03d_seq%d' % (ctx, seq))
        open(base + '.xma', 'wb').write(struct.pack('<II', ch, rate) + data)
        if not os.path.exists(exe):
            continue
        r = subprocess.run([exe, base + '.xma', base + '.f32', os.path.join(os.path.dirname(exe), '..', '..', 'data', 'bin')],
                           capture_output=True, text=True)
        print('   ' + r.stdout.strip().replace('\n', '\n   '))
        raw = open(base + '.f32', 'rb').read()
        pcm = struct.unpack('<%df' % (len(raw) // 4), raw)[::ch]
        e = v.get(ctx, {}).get(seq)
        if not e or not e[1] or not pcm:
            continue
        got = e[1]
        best = None
        for off in range(0, min(len(pcm) - 16, 8192)):
            if all(abs(pcm[off + i] - got[i]) < 1e-4 for i in range(16)):
                best = off
                break
        print('   entregue ao jogo (1as amostras): %s' % ' '.join('%.4f' % x for x in got[:6]))
        print('   PCM do FFmpeg (1as amostras):    %s' % ' '.join('%.4f' % x for x in pcm[:6]))
        print('   1a amostra entregue = amostra %s da saida do FFmpeg (o Azimuth descartou %d)' % (best, discard))


def voices(graphs, srcs, mixes):
    """ctx -> {seq: (mix ids, samples do 1o mix)} a partir das entradas SRC (op 2) de cada grafo"""
    v = defaultdict(dict)
    for seq, gid, stamp, ents in graphs:
        k = 0
        for w in ents:
            if w & 0x1f != 2:
                continue
            ctx = (w >> 13) & 0xfff
            mix = (w >> 25) & 0x1fff
            mix2 = (w >> 38) & 0x1fff
            s = srcs[seq][k] if k < len(srcs[seq]) else None
            k += 1
            xma = s[5] if s else b'\0' * 64
            stereo = struct.unpack_from('<I', xma, 4)[0] & 0x20000000
            m = mixes[seq].get(mix)
            data = m[1] if m else None
            data2 = mixes[seq].get(mix2, (0, None))[1] if stereo else None
            skipped = bool(s and s[1] > stamp)
            v[ctx][seq] = (mix, data, data2, skipped, s)
    return v


def segments(seqs):
    out, start, prev = [], None, None
    for q in sorted(seqs):
        if start is None:
            start = q
        elif q != prev + 1:
            out.append((start, prev))
            start = q
        prev = q
    if start is not None:
        out.append((start, prev))
    return out


def analyze_segment(ctx, a, b, tl):
    samples = []
    zero_blocks = []
    missing = 0
    for q in range(a, b + 1):
        mix, data, data2, skipped, s = tl[q]
        if data is None:
            missing += 1
            data = (0.0,) * BLOCK
        if not any(data):
            zero_blocks.append(q)
        samples.extend(data)
    nz = [i for i, x in enumerate(samples) if x != 0.0]
    if not nz:
        return None
    first, last = nz[0], nz[-1]
    peak = max(abs(x) for x in samples)
    # blocos zerados entre o primeiro e o ultimo bloco com som = buraco (falta de dados do XMA)
    holes = [q for q in zero_blocks if a + first // BLOCK < q < a + last // BLOCK]
    # salto: |x[n] - x[n-1]| comparado ao tipico do trecho
    diffs = [abs(samples[i] - samples[i - 1]) for i in range(first + 1, last + 1)]
    med = sorted(diffs)[len(diffs) // 2] if diffs else 0
    jumps = [(i, d) for i, d in zip(range(first + 1, last + 1), diffs) if d > max(0.05 * peak, 20 * med)]
    return dict(first=first, last=last, peak=peak, onset=abs(samples[first]), tail=abs(samples[last]),
                holes=holes, jumps=jumps, missing=missing, n=len(samples))


def write_wav(path, samples):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b''.join(struct.pack('<h', max(-32767, min(32767, int(x * 32767)))) for x in samples))
    w.close()


def out_clicks(path):
    """cliques na saida final: saltos da 2a diferenca bem acima da vizinhanca"""
    if np is None:
        print('saida: numpy ausente, pulando a analise do out.wav')
        return
    raw = open(path, 'rb').read()
    if raw[:4] != b'RIFF':
        return
    fmt = struct.unpack_from('<HHIIHH', raw, 20)
    tag, ch, rate, _, block, bits = fmt
    data = raw[44:]
    if tag == 3 and bits == 32:
        x = np.frombuffer(data[:len(data) // block * block], dtype='<f4').reshape(-1, ch)
    else:
        x = np.frombuffer(data[:len(data) // block * block], dtype='<i2').reshape(-1, ch) / 32768.0
    print('saida: %d canais %d Hz, %.1f s, pico %.3f' % (ch, rate, len(x) / rate, float(np.abs(x).max())))
    m = x.mean(axis=1)
    d2 = np.abs(np.diff(m, 2))
    win = min(2048, len(d2))
    if win < 8:
        return
    k = np.ones(win) / win
    local = np.sqrt(np.convolve(d2 ** 2, k, mode='same')) + 1e-6
    cand = np.where((d2 > 10 * local) & (d2 > 0.02))[0]
    last = -10 ** 9
    shown = 0
    for i in cand:
        if i - last < rate // 50:
            continue
        last = i
        shown += 1
        if shown <= 40:
            print('  clique? t=%.4f s (amostra %d): d2 %.3f, vizinhanca %.4f, amostras %s' % (
                i / rate, i, d2[i], local[i], ' '.join('%.3f' % v for v in m[max(0, i - 3):i + 5])))
    print('  %d candidatos' % shown)


def main():
    args = sys.argv[1:]
    wavdir = None
    show_ctx = None
    if '--wav' in args:
        i = args.index('--wav'); wavdir = args[i + 1]; del args[i:i + 2]
    xmadir = None
    if '--xma' in args:
        i = args.index('--xma'); xmadir = args[i + 1]; del args[i:i + 2]
    if '--ctx' in args:
        i = args.index('--ctx'); show_ctx = int(args[i + 1], 0); del args[i:i + 2]
    d = args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    graphs, srcs, mixes, cmds, inputs = read_shape(os.path.join(d, 'audio_fm6_shape.bin'))
    print('%d grafos (%.1f s), %d comandos XMA' % (len(graphs), len(graphs) * BLOCK / RATE, len(cmds)))
    v = voices(graphs, srcs, mixes)
    if xmadir:
        check_xma(inputs, v, xmadir)
        return
    resets = defaultdict(list)
    for seq, typ, stamp, ctxs in cmds:
        for c in ctxs:
            resets[c].append((seq, typ))
    total_holes = total_jumps = 0
    for ctx in sorted(v):
        tl = v[ctx]
        segs = segments(tl)
        print('ctx %d: %d grafos, %d trechos, comandos %s' % (ctx, len(tl), len(segs),
              ' '.join('%d@%d' % (t, q) for q, t in resets[ctx][:12])))
        for a, b in segs:
            r = analyze_segment(ctx, a, b, tl)
            if not r:
                continue
            total_holes += len(r['holes']); total_jumps += len(r['jumps'])
            skipped = sum(1 for q in range(a, b + 1) if tl[q][3])
            print('  grafos %d..%d (%.3f s): som %d..%d pico %.3f inicio |%.4f| fim |%.4f| buracos %s saltos %s%s%s' % (
                a, b, a * BLOCK / RATE, r['first'], r['last'], r['peak'], r['onset'], r['tail'],
                r['holes'][:8], [(i, round(x, 3)) for i, x in r['jumps'][:6]],
                ' sem-mix %d' % r['missing'] if r['missing'] else '', ' pulados %d' % skipped if skipped else ''))
        if show_ctx == ctx:
            for q in sorted(tl)[:40]:
                s = tl[q][4]
                if s:
                    print('    %d reset %#x src %s -> %s' % (q, s[1], s[2].hex(), s[4].hex()))
                    print('         xma %s' % s[3].hex())
                    print('          -> %s' % s[5].hex())
        if wavdir:
            os.makedirs(wavdir, exist_ok=True)
            n = graphs[-1][0] + 1 if graphs else 0
            out = []
            for q in range(n):
                e = tl.get(q)
                out.extend(e[1] if e and e[1] else (0.0,) * BLOCK)
            write_wav(os.path.join(wavdir, 'ctx%03d.wav' % ctx), out)
    print('total: %d buracos, %d saltos' % (total_holes, total_jumps))
    p = os.path.join(d, 'audio_fm6_out.wav')
    if os.path.exists(p):
        out_clicks(p)


if __name__ == '__main__':
    main()
