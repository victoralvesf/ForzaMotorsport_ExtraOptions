"""gen_d3d_thunks.py OUTDIR - gera d3d_thunks.S para o proxy winmm (contexto D3D11 no layout do FM5)

Proxy do contexto (ver PASSO 8 no CONTEXTO): [P] = ctx_proxy_vtbl, P+0x08..0x27 = copia do cabecalho do
contexto real, P+0x28 = tabela embutida (mesmos thunks da vtable, o FM5 chama [ctx+0x28+8*slot]),
P+CTX_REAL_OFF = contexto real do Azimuth.
Thunk i: rcx = real; se i tem wrapper em C -> jmp wrapper; senao jmp [[real] + 8*map(i)].
map (FM5 = ordem padrao do D3D11.1; Azimuth/FH2 = padrao + SetPrivateDataInterfaceGraphics no slot 7
     - IASetPrimitiveTopology (inline no XDK do FH2) + 6 metodos de tiles do Context2 antes da parte X):
  s < 7 -> s ; 7..0x17 -> s+1 ; 0x18 IASetPrimitiveTopology -> wrapper C ; 0x19..0x85 -> s ; >= 0x86 -> s+6
"""
import sys, os

SLOTS = 0x10e                  # tamanho das tabelas do Azimuth (0x870 bytes)
X_FIRST = 0x86                 # primeiro slot X no FM5
SHIFT = 6
REAL_OFF = 0x28 + SLOTS * 8    # 0x898
WRAPPED = {0: 'ctx_wrap_qi',    # slot do FM5 -> funcao C (recebe o contexto real em rcx)
           0x0c: 'ctx_DrawIndexed',             # Azimuth: argumentos empacotados em 64 bits
           0x14: 'ctx_DrawIndexedInstanced',    # idem
           0x15: 'ctx_DrawInstanced',           # idem
           0x13: 'ctx_IASetIndexBuffer',        # Azimuth: (HardwareIndexFormat, buf, off)
           0x18: 'ctx_IASetPrimitiveTopology',
           0x21: 'ctx_OMSetRenderTargets',
           0x39: 'ctx_ResolveSubresource',      # origem de 1 amostra -> CopySubresourceRegion
           0x3a: 'ctx_ExecuteCommandList',
           0x23: 'ctx_OMSetBlendState',         # debug (FM5_DEBUG_CLEAR=4)
           0x5f: 'ctx_RSGetViewports',          # diagnostico
           0x72: 'ctx_FinishCommandList',       # trace + zera os retangulos guardados
           0x2d: 'ctx_RSSetScissorRects',       # guarda os retangulos (o Azimuth nao tem o Get)
           0x60: 'ctx_RSGetScissorRects',       # stub fatal no Azimuth (crash ao pausar)
           0x6e: 'ctx_ClearState',              # zera os retangulos guardados (+ experimento de aliasing)
           0x29: 'ctx_Dispatch',                # experimento de aliasing (FM5_ALIAS_TEST)
           0x0a: 'ctx_PSSetSamplers',           # guarda os samplers do PS (o Azimuth nao tem o Get)
           0x4b: 'ctx_PSGetSamplers'}           # stub fatal no Azimuth (menu de pausa)
# X SetFast* (FH2 0xa0..0xb3 = FM5 0x9a..0xad): o Azimuth so tem stubs fatais -> wrappers sobre os Set* padrao
FAST = ['PSSetFastConstantBuffer', 'PSSetFastShaderResource', 'PSSetFastSampler',
        'VSSetFastConstantBuffer', 'VSSetFastShaderResource', 'VSSetFastSampler',
        'GSSetFastConstantBuffer', 'GSSetFastShaderResource', 'GSSetFastSampler',
        'CSSetFastConstantBuffer', 'CSSetFastShaderResource', 'CSSetFastSampler',
        'HSSetFastConstantBuffer', 'HSSetFastShaderResource', 'HSSetFastSampler',
        'DSSetFastConstantBuffer', 'DSSetFastShaderResource', 'DSSetFastSampler',
        'IASetFastVertexBuffer', 'IASetFastIndexBuffer']
for k, n in enumerate(FAST):
    WRAPPED[0x9a + k] = 'ctx_' + n

def fh2_slot(i):
    if i >= X_FIRST: return i + SHIFT
    if 7 <= i < 0x18: return i + 1
    return i

out = [f'/* gerado por tools/gen_d3d_thunks.py - nao editar */',
       f'    .set CTX_REAL_OFF, {REAL_OFF:#x}',
       '    .text', '    .p2align 4']
for i in range(SLOTS):
    out.append(f'ctx_thunk_{i:x}:')
    out.append(f'    lock incl ctx_counts+{i * 4:#x}(%rip)')   # contagem por slot (trace, d3d_fm5.c)
    out.append(f'    cmpl $0, ctx_trace_on(%rip)')             # trace detalhado: ctx_trace(slot, proxy, args)
    out.append(f'    je 1f')
    out.append(f'    movl ${i:#x}, %r10d')
    out.append(f'    leaq 1f(%rip), %r11')
    out.append(f'    jmp ctx_trace_common')
    out.append(f'1:')
    out.append(f'    movq CTX_REAL_OFF(%rcx), %rcx')
    if i in WRAPPED:
        out.append(f'    jmp {WRAPPED[i]}')
    else:
        j = fh2_slot(i)
        if j >= SLOTS:
            # sem equivalente no Azimuth: falha com o numero do slot
            out.append(f'    movl ${i:#x}, %edx')
            out.append(f'    jmp ctx_bad_slot')
            continue
        out.append(f'    movq (%rcx), %rax')
        out.append(f'    jmpq *{j * 8:#x}(%rax)')
# ctx_trace_common: r10d = slot, r11 = continuacao; preserva rcx/rdx/r8/r9/xmm0-3
out += ['ctx_trace_common:',
        '    pushq %r11',
        '    subq $0x90, %rsp',
        '    movq %rcx, 0x30(%rsp)', '    movq %rdx, 0x38(%rsp)', '    movq %r8, 0x40(%rsp)', '    movq %r9, 0x48(%rsp)',
        '    movdqu %xmm0, 0x50(%rsp)', '    movdqu %xmm1, 0x60(%rsp)', '    movdqu %xmm2, 0x70(%rsp)',
        '    movdqu %xmm3, 0x80(%rsp)',
        '    movq %r9, 0x20(%rsp)',
        '    leaq 0xc0(%rsp), %rax',            # args da pilha do chamador (5o em diante)
        '    movq %rax, 0x28(%rsp)',
        '    movq %r8, %r9', '    movq %rdx, %r8', '    movq %rcx, %rdx', '    movl %r10d, %ecx',
        '    call ctx_trace',
        '    movq 0x30(%rsp), %rcx', '    movq 0x38(%rsp), %rdx', '    movq 0x40(%rsp), %r8', '    movq 0x48(%rsp), %r9',
        '    movdqu 0x50(%rsp), %xmm0', '    movdqu 0x60(%rsp), %xmm1', '    movdqu 0x70(%rsp), %xmm2',
        '    movdqu 0x80(%rsp), %xmm3',
        '    addq $0x90, %rsp',
        '    popq %r11',
        '    jmp *%r11']
# device: thunks que contam e saltam para dev_impl[i] (tabela no layout do FM5 montada em d3d_fm5.c)
for i in range(SLOTS):
    out.append(f'dev_thunk_{i:x}:')
    out.append(f'    lock incl dev_counts+{i * 4:#x}(%rip)')
    out.append(f'    jmpq *dev_impl+{i * 8:#x}(%rip)')
# DMA (tipo 6): thunks que contam, chamam ctx_trace (slot | 0x1000) no trace detalhado e saltam para dma_orig[i]
for i in range(SLOTS):
    out.append(f'dma_thunk_{i:x}:')
    out.append(f'    lock incl dma_counts+{i * 4:#x}(%rip)')
    out.append(f'    cmpl $0, ctx_trace_on(%rip)')
    out.append(f'    je 1f')
    out.append(f'    movl ${i | 0x1000:#x}, %r10d')
    out.append(f'    leaq 1f(%rip), %r11')
    out.append(f'    jmp ctx_trace_common')
    out.append(f'1:')
    out.append(f'    jmpq *dma_orig+{i * 8:#x}(%rip)')
out += ['    .section .rdata,"dr"', '    .p2align 3', '    .globl ctx_proxy_vtbl', 'ctx_proxy_vtbl:']
out += [f'    .quad ctx_thunk_{i:x}' for i in range(SLOTS)]
# tipo de cada slot do contexto (so p/ os testes, winmm_proxy/tests): 0 = direto, 1 = wrapper C, 2 = sem equivalente
kinds = [1 if i in WRAPPED else 0 if fh2_slot(i) < SLOTS else 2 for i in range(SLOTS)]
out += ['    .globl ctx_thunk_kind', 'ctx_thunk_kind:']
out += ['    .byte ' + ', '.join(str(k) for k in kinds[i:i + 16]) for i in range(0, SLOTS, 16)]
out += ['    .p2align 3', '    .globl dev_thunk_vtbl', 'dev_thunk_vtbl:']
out += [f'    .quad dev_thunk_{i:x}' for i in range(SLOTS)]
out += ['    .globl dma_thunk_vtbl', 'dma_thunk_vtbl:']
out += [f'    .quad dma_thunk_{i:x}' for i in range(SLOTS)]
open(os.path.join(sys.argv[1], 'd3d_thunks.S'), 'w').write('\n'.join(out) + '\n')
