"""dpaths.py FILE... - lists d:\\ and D:/ path literals (UTF-16 and ASCII) in binaries."""
import sys
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    for pat in ['d:\\', 'D:\\', 'd:/', 'D:/']:
        for enc in ('utf-16le', 'ascii'):
            b = pat.encode(enc); i = d.find(b)
            while i >= 0:
                e = d.find(b'\0\0' if enc != 'ascii' else b'\0', i)
                print(p[-12:], enc, hex(i), d[i:e + 1][:120].decode(enc, 'replace')); i = d.find(b, i + 1)
