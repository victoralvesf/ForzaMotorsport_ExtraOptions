"""gen_winmm_stubs.py OUTDIR - gera winmm_exports.def + winmm_stubs.S (repasse de todos os
exports do winmm.dll do System32 via tabela de ponteiros preenchida em runtime)."""
import sys, os, subprocess, re
out = sys.argv[1]
txt = subprocess.run([r'C:\mingw64\bin\objdump.exe', '-p', r'C:\Windows\System32\winmm.dll'],
                     capture_output=True, text=True).stdout
sec = txt.split('[Ordinal/Name Pointer] Table', 1)[1]
names = []
for line in sec.splitlines()[2:]:
    m = re.match(r'\s*\[\s*\d+\]\s+\+base\[\s*\d+\]\s+[0-9a-f]+\s+(\S+)', line)
    if not m: break
    names.append(m.group(1))
with open(os.path.join(out, 'winmm_exports.def'), 'w') as f:
    f.write('LIBRARY winmm\nEXPORTS\n')
    for n in names: f.write(f'  {n}=fwd_{n}\n')
with open(os.path.join(out, 'winmm_stubs.S'), 'w') as f:
    f.write('/* gerado por tools/gen_winmm_stubs.py */\n\t.text\n')
    for i, n in enumerate(names):
        f.write(f'\t.globl fwd_{n}\nfwd_{n}:\n\tjmp *winmm_real+{i*8}(%rip)\n')
    f.write(f'\t.data\n\t.globl winmm_real\n\t.p2align 3\nwinmm_real:\n\t.fill {len(names)},8,0\n')
    f.write('\t.section .rdata,"dr"\n\t.globl winmm_names\nwinmm_names:\n')
    for n in names: f.write(f'\t.quad name_{n}\n')
    f.write('\t.quad 0\n')
    for n in names: f.write(f'name_{n}: .asciz "{n}"\n')
print(len(names), 'exports')
