r"""fm5_record.py [GAME_DIR] [--exe EXE] [--manifest XML] [--json OUT]
Collects the values the Azimuth game record (0x350 bytes, see docs/contexto/06_registro_do_jogo.txt,
PASSO 4) needs for a new title. GAME_DIR is the game folder holding Mount\ and EmbeddedXvd\.
Without GAME_DIR only the exe/manifest fields are filled (defaults: game_binaries\)."""
import sys, os, struct, hashlib, json, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# locale -> (FM5 language index, has speech); see main() and CONTEXTO PASSO 5
FM5_LANG = {
    'en': (0x0, 1), 'cs': (0x0, 0), 'de': (0x4, 1), 'en-GB': (0x2, 0), 'en-AU': (0x0, 0),
    'en-CA': (0x0, 0), 'en-IE': (0x1, 0), 'en-NZ': (0x1, 0), 'es': (0x7, 1), 'es-MX': (0x6, 1),
    'fr': (0x5, 1), 'fr-CA': (0x5, 0), 'hu': (0x0, 0), 'it': (0x8, 1), 'ja': (0x3, 1),
    'ko': (0x9, 0), 'nl': (0x10, 1), 'pl': (0x0, 0), 'pt': (0xb, 1), 'ru': (0xf, 1),
    'zh-Hant': (0xa, 0),
}

def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def find(base, *parts):
    """case-insensitive path join (Xbox dumps vary in casing)"""
    cur = base
    for part in parts:
        if not os.path.isdir(cur): return None
        m = [x for x in os.listdir(cur) if x.lower() == part.lower()]
        if not m: return None
        cur = os.path.join(cur, m[0])
    return cur

def pe_info(p):
    d = open(p, 'rb').read(0x1000)
    e = struct.unpack_from('<I', d, 0x3c)[0]; o = e + 24
    nsec = struct.unpack_from('<H', d, e + 6)[0]
    entry = struct.unpack_from('<I', d, o + 16)[0]; simg = struct.unpack_from('<I', d, o + 56)[0]
    return nsec, entry, simg

def iat_slots(p):
    sys.path.insert(0, HERE)
    from sigfind import PE
    pe = PE(p); d = pe.d; e = struct.unpack_from('<I', d, 0x3c)[0]; o = e + 24
    ir = struct.unpack_from('<I', d, o + 112 + 8)[0]; n = i = 0
    while True:
        oft, ts, fc, name, ft = struct.unpack('<IIIII', pe.read(ir + i * 20, 20))
        if not ft: return n
        j = 0
        while struct.unpack('<Q', pe.read(ft + j * 8, 8))[0]: j += 1
        n += j; i += 1

def main():
    a = sys.argv[1:]; opt = {}
    for k in ('--exe', '--manifest', '--json'):
        if k in a: i = a.index(k); opt[k] = a[i + 1]; del a[i:i + 2]
    game = a[0] if a else None
    mount = find(game, 'Mount') if game else None
    exe = opt.get('--exe') or (mount and find(mount, 'forza_durango_release_final.exe')) \
        or os.path.join(ROOT, 'game_binaries', 'forza_durango_release_final.exe')
    man = opt.get('--manifest') or (mount and find(mount, 'AppxManifest.xml')) \
        or os.path.join(ROOT, 'game_binaries', 'AppxManifest.xml')
    xml = open(man, 'rb').read().decode('utf-8', 'replace')
    tid = re.search(r'TitleId="([0-9A-Fa-f]{8})"', xml).group(1).lower()
    ver = re.search(r'<Identity[^>]*Version="([^"]+)"', xml).group(1)
    nsec, entry, simg = pe_info(exe)
    r = {}; todo = []
    r['+0x000 title id (UTF-16)'] = tid
    r['+0x008 package version'] = ver
    r['+0x010 sha256 Mount\\AppxManifest.xml'] = sha(man)
    r['+0x018 sha256 exe'] = sha(exe)
    r['+0x020 exe size'] = hex(os.path.getsize(exe))
    xsd = None
    if mount:
        hm = find(mount, 'hash.manifest')
        if hm:
            sz = os.path.getsize(hm)
            r['+0x028 sha256 Mount\\hash.manifest'] = sha(hm)
            r['+0x030 hash.manifest entries (size/12)'] = hex(sz // 12) + ('' if sz % 12 == 0 else f'  !! size {sz:#x} not /12')
        else: todo.append('Mount\\hash.manifest missing')
        xsd = find(game, 'EmbeddedXvd', 'Windows', 'System32')
        xg = xsd and find(xsd, 'xg_x.dll')
        if xg:
            r['+0x038 sha256 EmbeddedXvd xg_x.dll'] = sha(xg); r['+0x040 xg_x.dll size'] = hex(os.path.getsize(xg))
        else: todo.append('EmbeddedXvd\\Windows\\System32\\xg_x.dll missing (folder check requires it)')
        xbs = find(mount, 'Microsoft.Xbox.Services.dll')
        if xbs:
            r['+0x048 sha256 Mount\\Microsoft.Xbox.Services.dll'] = sha(xbs); r['+0x050 its size'] = hex(os.path.getsize(xbs))
        else: todo.append('Mount\\Microsoft.Xbox.Services.dll missing')
        for off, rel in ((0x58, 'config\\SOFTWARE'), (0x68, 'gameosproxy.dll'), (0x78, 'WinTypes.dll'), (0x88, 'Windows.Web.dll')):
            p = xsd and find(xsd, *rel.split('\\'))
            key = f'+{off:#05x} L"System32\\{rel}" / +{off + 8:#05x} sha256'
            if p: r[key] = sha(p)
            else: todo.append(f'EmbeddedXvd\\Windows\\System32\\{rel} missing')
    else:
        todo.append('no GAME_DIR: +0x028..+0x090 (hash.manifest, xg_x.dll, Xbox.Services.dll, EmbeddedXvd files) not computed')
    r['+0x098 SizeOfImage'] = hex(simg); r['+0x09c AddressOfEntryPoint'] = hex(entry)
    if nsec != 14: todo.append(f'exe has {nsec} sections; 0x1400bdf90 requires exactly 14')
    r['+0x0a0 IAT slot count'] = hex(iat_slots(exe))
    r['+0x1e8 window idle call RVA'] = '0x5d6ca7'
    r['+0x208 cinematic render idle call RVA'] = '0x5a8efc'
    r['+0x1f8 cinematic movie idle call RVA'] = '0 (no equivalent)'
    r['+0x2d8/+0x2e8 offers sites'] = '0 / 0 (skips content offers)'
    r['+0x320/+0x328 content packages'] = 'NULL / 0'
    r['+0x341 EXPERIMENTAL label'] = '1'
    langs = re.findall(r'<Resource Language="([^"]+)"', xml)
    r['manifest languages'] = ' '.join(langs)
    # +0x330 entries {wchar* locale, u32 game language, u8 speech}. The locale must equal a manifest
    # Resource Language (0x14000af50, CompareStringOrdinal). Index = what FM5 0x1407ca990 derives
    # from GetUserDefaultLocaleName -> LocaleNameToLCID; speech = language with VO that FM5 0x623cb7
    # keeps ({0,3..8,0xb,0xf,0x10}), one entry per voice.
    tab = []
    for loc in langs:
        if loc not in FM5_LANG: todo.append(f'language {loc}: no FM5 index known'); continue
        idx, sp = FM5_LANG[loc]; tab.append(f'{loc}={idx:#x}{"*" if sp else ""}')
    r['+0x330/+0x338 languages (*=speech)'] = f'{len(tab)}: ' + ' '.join(tab)
    w = max(len(k) for k in r)
    for k, v in r.items(): print(f'{k:<{w}}  {v}')
    if todo: print('\nTODO:'); [print('  -', t) for t in todo]
    if '--json' in opt: json.dump({'fields': r, 'todo': todo}, open(opt['--json'], 'w'), indent=1)

if __name__ == '__main__':
    main()
