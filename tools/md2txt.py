"""md2txt.py ENTRADA.md SAIDA.txt - converte o README.md em texto puro para o release
(sem **, `crase` e cercas ```; titulos sublinhados; CRLF para abrir em qualquer editor)."""
import sys, re

out = []
code = False
for line in open(sys.argv[1], encoding='utf-8').read().splitlines():
    if line.lstrip().startswith('```'):
        code = not code
        continue
    if not code:
        m = re.match(r'(#+)\s+(.*)', line)
        if m:
            line = re.sub(r'[*`]', '', m.group(2))
            out += [line, ('=' if len(m.group(1)) == 1 else '-') * len(line)]
            continue
        line = line.replace('**', '')
        line = re.sub(r'`([^`]*)`', r'\1', line)
    out.append(line)
open(sys.argv[2], 'w', encoding='utf-8', newline='\r\n').write('\n'.join(out) + '\n')
