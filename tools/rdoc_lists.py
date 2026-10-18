"""rdoc_lists.py actions.txt - resumo por command list (entre Reset e Close) das linhas "api" do rdoc_dump.py (RDOC_API):
eventos, draws, draws sem RT/DS ligado, OMSetRenderTargets (n, recurso do RT 0, DS) e a ordem de envio (ExecuteCommandLists)."""
import re, sys
from collections import OrderedDict

lists = OrderedDict()   # (cl, n-esimo Reset) -> info
cur = {}                # cl -> info aberto
submits = []
for line in open(sys.argv[1], encoding='utf-8'):
    if not line.startswith('api '):
        continue
    m = re.match(r'api (\d+) (\S+) (.*)', line)
    eid, name, args = int(m.group(1)), m.group(2), m.group(3)
    if name.endswith('::Reset'):   # o argumento da lista vem truncado no Reset: a lista abre na 1a chamada seguinte
        continue
    if name.endswith('::Close'):
        c = re.search(r'(?<!Baked)CommandList=ResourceId::(\d+)', args)
        if c: cur.pop(c.group(1), None)
        continue
    cl = re.search(r'pCommandList=ResourceId::(\d+)', args)
    cl = cl and cl.group(1)
    if cl and cl not in cur:
        info = {'cl': cl, 'first': eid, 'last': eid, 'draws': 0, 'nort': 0, 'om': [], 'clears': 0, 'rt': None}
        lists[(cl, eid)] = info
        cur[cl] = info
    if name.endswith('ExecuteCommandLists'):
        submits.append((eid, re.findall(r'ResourceId::(\d+)', args.split('ppCommandLists', 1)[-1])))
        continue
    info = cur.get(cl)
    if not info:
        continue
    info['last'] = eid
    if name.endswith('OMSetRenderTargets'):
        n = int(re.search(r'NumRenderTargetDescriptors=(\d+)', args).group(1))
        r = re.search(r'Resource=ResourceId::(\d+)', args)
        ds = 'pDepthStencilDescriptor=0' not in args
        info['rt'] = (n, r.group(1) if r else '-', ds)
        info['om'].append('%d:%d/%s%s' % (eid, n, r.group(1) if r else '-', '+ds' if ds else ''))
    elif 'Draw' in name or name.endswith('ExecuteIndirect'):
        info['draws'] += 1
        if not info['rt'] or (info['rt'][0] == 0 and not info['rt'][2]):
            info['nort'] += 1
    elif 'Clear' in name:
        info['clears'] += 1

tot = sum(i['nort'] for i in lists.values())
print('listas %d, draws sem RT/DS %d' % (len(lists), tot))
for (cl, first), i in lists.items():
    if i['draws']:
        print('cl %s eid %d-%d draws %d sem_rt %d clears %d om %s' % (cl, first, i['last'], i['draws'], i['nort'],
              i['clears'], ' '.join(i['om'][:6]) + (' ...' if len(i['om']) > 6 else '')))
for eid, cls in submits:
    print('submit %d: %s' % (eid, ' '.join(cls)))
