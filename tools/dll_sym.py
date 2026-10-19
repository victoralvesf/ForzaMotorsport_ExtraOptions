"""Nome exportado mais proximo de offsets (RVA) de uma DLL do sistema (para os "ntdll.dll+0x..." do StutterLog).

uso: python tools/dll_sym.py C:/Windows/System32/ntdll.dll 0x1623d0 0x161dd0 ...
Sem pefile: le o diretorio de exportacao direto do PE. Offsets dentro de uma funcao mostram nome+delta.
"""
import bisect
import struct
import sys


def exports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    ddir = opt + (112 if magic == 0x20b else 96)
    exp_rva = struct.unpack_from("<I", d, ddir)[0]
    secs = []
    so = opt + struct.unpack_from("<H", d, pe + 20)[0]
    for i in range(nsec):
        vsz, va, raw_sz, raw = struct.unpack_from("<IIII", d, so + i * 40 + 8)
        secs.append((va, max(vsz, raw_sz), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return rva - va + raw
        raise ValueError(hex(rva))

    e = off(exp_rva)
    nfun, nname, afun, aname, aord = struct.unpack_from("<IIIII", d, e + 20)
    funcs = [struct.unpack_from("<I", d, off(afun) + 4 * i)[0] for i in range(nfun)]
    out = {}
    for i in range(nname):
        nrva = struct.unpack_from("<I", d, off(aname) + 4 * i)[0]
        o = off(nrva)
        name = d[o:d.index(b"\0", o)].decode()
        ordi = struct.unpack_from("<H", d, off(aord) + 2 * i)[0]
        out.setdefault(funcs[ordi], name)
    return sorted(out.items())


def main():
    path, offs = sys.argv[1], [int(x, 16) for x in sys.argv[2:]]
    ex = exports(path)
    rvas = [r for r, _ in ex]
    for o in offs:
        k = bisect.bisect_right(rvas, o) - 1
        print(f"{o:#x}: " + (f"{ex[k][1]}+{o - ex[k][0]:#x}" if k >= 0 else "?"))


if __name__ == "__main__":
    main()
