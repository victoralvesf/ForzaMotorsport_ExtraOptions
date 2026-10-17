"""car_lods.py - lista os LODs dos modelos de um carro do FM5/FH2 (media\\cars\\<carro>.zip).
Uso: python tools\\car_lods.py <carro.zip> [filtro]
Mostra, por .modelbin, quantos meshes ha de cada LOD (nome do mesh termina em _LODS0, _LOD1.._LOD5, _LODS,
_Shadow) e o resumo de vertices por LOD do SizeReport.xml (LOD -1 = "Lod S", o super-LOD do Forza Vista/foto)."""
import collections, re, struct, sys, zipfile

def meshes(d):
    if d[:4] != b'burG': return None
    c = collections.Counter()
    for i in range(struct.unpack_from('<H', d, 6)[0]):
        # entrada: tag, versao, off dos metadados, off dos dados, tam, tam
        tag, ver, off, sz, a, b = struct.unpack_from('<4sIIIII', d, 0x10 + i * 24)
        if tag != b'hseM' or d[off:off + 4] != b'emaN': continue
        # metadado 'Name': u16 (len << 4), u16 offset do texto relativo a propria tag
        ln, rel = struct.unpack_from('<HH', d, off + 4)
        name = d[off + rel:off + rel + (ln >> 4)].decode('latin1')
        m = re.search(r'_(LODS?\d*|Shadow)$', name)
        c[m.group(1) if m else '?'] += 1
    return c

z = zipfile.ZipFile(sys.argv[1])
flt = sys.argv[2].lower() if len(sys.argv) > 2 else ''
for i in z.infolist():
    if i.filename.lower().endswith('.modelbin') and flt in i.filename.lower():
        c = meshes(z.read(i))
        if c: print('%-70s %s' % (i.filename, ' '.join('%s:%d' % kv for kv in sorted(c.items()))))
for i in z.infolist():
    if i.filename.lower().endswith('sizereport.xml'):
        x = z.read(i).decode('utf-8-sig', 'replace')
        print('\nSizeReport:', ', '.join('LOD %s = %s verts' % m for m in re.findall(r'<LOD Index="(-?\d+)" MaxVertCount="(\d+)"', x)))
