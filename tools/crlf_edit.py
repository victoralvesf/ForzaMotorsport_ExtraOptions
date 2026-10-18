# usage: crlf_edit.py file edits.py  -- edits.py defines EDITS = [(old, new, count), ...] with \n; applied as \r\n
import sys
p = sys.argv[1]
ns = {}
exec(open(sys.argv[2], encoding='utf-8').read(), ns)
s = open(p, encoding='utf-8', newline='').read()
for a, b, n in ns['EDITS']:
    a2, b2 = a.replace('\n', '\r\n'), b.replace('\n', '\r\n')
    c = s.count(a2)
    if c != n:
        sys.exit('mismatch %d != %d: %r' % (c, n, a[:80]))
    s = s.replace(a2, b2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok', len(ns['EDITS']))
