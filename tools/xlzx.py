"""xlzx.py - extrai entradas de zips do Forza com metodo 21 (LZX do XMemCompress: blocos com tamanho BE16,
prefixo 0xFF = tamanho descomprimido BE16 + comprimido BE16; senao comprimido BE16 e 0x8000 descomprimidos;
fluxo LZX classico, janela 128 KiB, estado continuo entre os blocos).
Uso: python tools\\xlzx.py <zip> [lista|nome...] [-o pasta]
  sem nomes: lista as entradas; com nomes (ou '*'): extrai para a pasta (padrao: atual)."""
import os, struct, sys, zipfile

EXTRA = []
j = 0
for i in range(0, 52, 2):
    EXTRA += [j, j]
    if i and j < 17: j += 1
BASE = []
j = 0
for e in EXTRA:
    BASE.append(j); j += 1 << e


class Bits:
    def __init__(s, d):
        s.d, s.p, s.buf, s.n = d, 0, 0, 0

    def need(s, k):
        while s.n < k:
            w = s.d[s.p] | (s.d[s.p + 1] << 8) if s.p + 1 < len(s.d) else 0
            s.p += 2
            s.buf = (s.buf << 16) | w; s.n += 16

    def peek(s, k):
        s.need(k)
        return (s.buf >> (s.n - k)) & ((1 << k) - 1)

    def get(s, k):
        if not k: return 0
        v = s.peek(k); s.n -= k; s.buf &= (1 << s.n) - 1
        return v

    def align_raw(s):
        """bloco sem compressao: descarta ate 16 bits e passa a ler bytes"""
        if s.n == 0: s.need(16)
        s.n -= s.n & 15
        # bits que sobraram no buffer ja sao bytes do fluxo: devolve-os
        back = s.n // 8
        s.p -= back; s.n = 0; s.buf = 0


class Huff:
    def __init__(s, lens):
        s.t = {}
        s.maxl = max(lens) if lens and max(lens) else 0
        code = 0
        for l in range(1, s.maxl + 1):
            for sym, sl in enumerate(lens):
                if sl == l:
                    s.t[(l, code)] = sym; code += 1
            code <<= 1

    def sym(s, b):
        if not s.maxl: raise ValueError('arvore vazia')
        for l in range(1, s.maxl + 1):
            v = s.t.get((l, b.peek(l)))
            if v is not None:
                b.get(l); return v
        raise ValueError('codigo invalido')


def read_lengths(b, lens, a, z):
    pre = Huff([b.get(4) for _ in range(20)])
    x = a
    while x < z:
        c = pre.sym(b)
        if c == 17:
            for _ in range(b.get(4) + 4):
                lens[x] = 0; x += 1
        elif c == 18:
            for _ in range(b.get(5) + 20):
                lens[x] = 0; x += 1
        elif c == 19:
            n = b.get(1) + 4
            c = pre.sym(b)
            v = (lens[x] - c) % 17
            for _ in range(n):
                lens[x] = v; x += 1
        else:
            lens[x] = (lens[x] - c) % 17; x += 1


def lzx_decode(chunks, window_bits=17):
    slots = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}[window_bits]
    out = bytearray()
    main_lens = [0] * (256 + slots * 8)
    len_lens = [0] * 249
    R = [1, 1, 1]
    btype = brem = 0
    main = lent = alg = None
    header = True
    target = 0
    for comp, usize in chunks:
        b = Bits(comp)
        target += usize
        if header:
            if b.get(1): b.get(32)          # traducao e8 (nao usada nesses arquivos)
            header = False
        while len(out) < target:
            if brem == 0:
                btype = b.get(3)
                brem = (b.get(16) << 8) | b.get(8)
                if btype == 2:
                    alg = Huff([b.get(3) for _ in range(8)])
                if btype in (1, 2):
                    read_lengths(b, main_lens, 0, 256)
                    read_lengths(b, main_lens, 256, len(main_lens))
                    main = Huff(main_lens)
                    read_lengths(b, len_lens, 0, 249)
                    lent = Huff(len_lens)
                elif btype == 3:
                    b.align_raw()
                    R = list(struct.unpack_from('<3I', b.d, b.p)); b.p += 12
                else:
                    raise ValueError('bloco tipo %d' % btype)
            run = min(brem, target - len(out))
            if btype == 3:
                out += b.d[b.p:b.p + run]; b.p += run; brem -= run
                if brem == 0 and run & 1: b.p += 1
                continue
            end = len(out) + run
            while len(out) < end:
                m = main.sym(b)
                if m < 256:
                    out.append(m); continue
                m -= 256
                ml = m & 7
                if ml == 7: ml += lent.sym(b)
                ml += 2
                slot = m >> 3
                if slot > 2:
                    e = EXTRA[slot]
                    if btype == 2:
                        off = BASE[slot] - 2
                        if e > 3:
                            off += b.get(e - 3) << 3; off += alg.sym(b)
                        elif e == 3:
                            off += alg.sym(b)
                        elif e > 0:
                            off += b.get(e)
                        else:
                            off = 1
                    else:
                        off = BASE[slot] - 2 + b.get(e)
                    R = [off, R[0], R[1]]
                elif slot == 0:
                    off = R[0]
                elif slot == 1:
                    off = R[1]; R = [R[1], R[0], R[2]]
                else:
                    off = R[2]; R = [R[2], R[1], R[0]]
                p = len(out) - off
                for k in range(ml):
                    out.append(out[p + k])
            brem -= run if len(out) <= end else run
            if len(out) > end:   # match passou do quadro: desconta do bloco
                brem -= len(out) - end
    return bytes(out)


def xmem_chunks(data):
    p = 0
    while p < len(data):
        if data[p] == 0xff:
            usize, csize = struct.unpack_from('>HH', data, p + 1); p += 5
        else:
            csize, = struct.unpack_from('>H', data, p); usize = 0x8000; p += 2
        if csize == 0: break
        yield data[p:p + csize], usize
        p += csize


def entry_raw(path, info):
    with open(path, 'rb') as f:
        f.seek(info.header_offset)
        h = f.read(30)
        n, x = struct.unpack_from('<HH', h, 26)
        f.seek(info.header_offset + 30 + n + x)
        return f.read(info.compress_size)


def extract(path, info):
    if info.compress_type != 21:
        return zipfile.ZipFile(path).read(info.filename)
    d = lzx_decode(xmem_chunks(entry_raw(path, info)))
    return d[:info.file_size]


if __name__ == '__main__':
    args = sys.argv[1:]
    outdir = '.'
    if '-o' in args:
        i = args.index('-o'); outdir = args[i + 1]; del args[i:i + 2]
    z = zipfile.ZipFile(args[0])
    if len(args) == 1:
        for i in z.infolist(): print(i.filename, i.file_size, i.compress_type)
        sys.exit(0)
    for i in z.infolist():
        if '*' in args[1:] or i.filename in args[1:]:
            d = extract(args[0], i)
            dst = os.path.join(outdir, i.filename)
            os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
            open(dst, 'wb').write(d)
            print('%s %d bytes%s' % (i.filename, len(d), '' if len(d) == i.file_size else ' (esperado %d)' % i.file_size))
