/* gerado por tools/gen_d3d_thunks.py - nao editar */
    .set CTX_REAL_OFF, 0x898
    .text
    .p2align 4
ctx_thunk_0:
    lock incl ctx_counts+0x0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_wrap_qi
ctx_thunk_1:
    lock incl ctx_counts+0x4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x8(%rax)
ctx_thunk_2:
    lock incl ctx_counts+0x8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x10(%rax)
ctx_thunk_3:
    lock incl ctx_counts+0xc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x18(%rax)
ctx_thunk_4:
    lock incl ctx_counts+0x10(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x20(%rax)
ctx_thunk_5:
    lock incl ctx_counts+0x14(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x28(%rax)
ctx_thunk_6:
    lock incl ctx_counts+0x18(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x30(%rax)
ctx_thunk_7:
    lock incl ctx_counts+0x1c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x40(%rax)
ctx_thunk_8:
    lock incl ctx_counts+0x20(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x48(%rax)
ctx_thunk_9:
    lock incl ctx_counts+0x24(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x50(%rax)
ctx_thunk_a:
    lock incl ctx_counts+0x28(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_PSSetSamplers
ctx_thunk_b:
    lock incl ctx_counts+0x2c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x60(%rax)
ctx_thunk_c:
    lock incl ctx_counts+0x30(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DrawIndexed
ctx_thunk_d:
    lock incl ctx_counts+0x34(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x70(%rax)
ctx_thunk_e:
    lock incl ctx_counts+0x38(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x78(%rax)
ctx_thunk_f:
    lock incl ctx_counts+0x3c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x80(%rax)
ctx_thunk_10:
    lock incl ctx_counts+0x40(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x88(%rax)
ctx_thunk_11:
    lock incl ctx_counts+0x44(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x11, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x90(%rax)
ctx_thunk_12:
    lock incl ctx_counts+0x48(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x12, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x98(%rax)
ctx_thunk_13:
    lock incl ctx_counts+0x4c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x13, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_IASetIndexBuffer
ctx_thunk_14:
    lock incl ctx_counts+0x50(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x14, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DrawIndexedInstanced
ctx_thunk_15:
    lock incl ctx_counts+0x54(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x15, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DrawInstanced
ctx_thunk_16:
    lock incl ctx_counts+0x58(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x16, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xb8(%rax)
ctx_thunk_17:
    lock incl ctx_counts+0x5c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x17, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xc0(%rax)
ctx_thunk_18:
    lock incl ctx_counts+0x60(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x18, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_IASetPrimitiveTopology
ctx_thunk_19:
    lock incl ctx_counts+0x64(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x19, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xc8(%rax)
ctx_thunk_1a:
    lock incl ctx_counts+0x68(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xd0(%rax)
ctx_thunk_1b:
    lock incl ctx_counts+0x6c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xd8(%rax)
ctx_thunk_1c:
    lock incl ctx_counts+0x70(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xe0(%rax)
ctx_thunk_1d:
    lock incl ctx_counts+0x74(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xe8(%rax)
ctx_thunk_1e:
    lock incl ctx_counts+0x78(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xf0(%rax)
ctx_thunk_1f:
    lock incl ctx_counts+0x7c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0xf8(%rax)
ctx_thunk_20:
    lock incl ctx_counts+0x80(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x20, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x100(%rax)
ctx_thunk_21:
    lock incl ctx_counts+0x84(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x21, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_OMSetRenderTargets
ctx_thunk_22:
    lock incl ctx_counts+0x88(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x22, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x110(%rax)
ctx_thunk_23:
    lock incl ctx_counts+0x8c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x23, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_OMSetBlendState
ctx_thunk_24:
    lock incl ctx_counts+0x90(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x24, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x120(%rax)
ctx_thunk_25:
    lock incl ctx_counts+0x94(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x25, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x128(%rax)
ctx_thunk_26:
    lock incl ctx_counts+0x98(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x26, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x130(%rax)
ctx_thunk_27:
    lock incl ctx_counts+0x9c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x27, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x138(%rax)
ctx_thunk_28:
    lock incl ctx_counts+0xa0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x28, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x140(%rax)
ctx_thunk_29:
    lock incl ctx_counts+0xa4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x29, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_Dispatch
ctx_thunk_2a:
    lock incl ctx_counts+0xa8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x150(%rax)
ctx_thunk_2b:
    lock incl ctx_counts+0xac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x158(%rax)
ctx_thunk_2c:
    lock incl ctx_counts+0xb0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x160(%rax)
ctx_thunk_2d:
    lock incl ctx_counts+0xb4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_RSSetScissorRects
ctx_thunk_2e:
    lock incl ctx_counts+0xb8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x170(%rax)
ctx_thunk_2f:
    lock incl ctx_counts+0xbc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x2f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x178(%rax)
ctx_thunk_30:
    lock incl ctx_counts+0xc0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x30, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x180(%rax)
ctx_thunk_31:
    lock incl ctx_counts+0xc4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x31, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x188(%rax)
ctx_thunk_32:
    lock incl ctx_counts+0xc8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x32, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x190(%rax)
ctx_thunk_33:
    lock incl ctx_counts+0xcc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x33, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x198(%rax)
ctx_thunk_34:
    lock incl ctx_counts+0xd0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x34, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1a0(%rax)
ctx_thunk_35:
    lock incl ctx_counts+0xd4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x35, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1a8(%rax)
ctx_thunk_36:
    lock incl ctx_counts+0xd8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x36, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1b0(%rax)
ctx_thunk_37:
    lock incl ctx_counts+0xdc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x37, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1b8(%rax)
ctx_thunk_38:
    lock incl ctx_counts+0xe0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x38, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1c0(%rax)
ctx_thunk_39:
    lock incl ctx_counts+0xe4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x39, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_ResolveSubresource
ctx_thunk_3a:
    lock incl ctx_counts+0xe8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_ExecuteCommandList
ctx_thunk_3b:
    lock incl ctx_counts+0xec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1d8(%rax)
ctx_thunk_3c:
    lock incl ctx_counts+0xf0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1e0(%rax)
ctx_thunk_3d:
    lock incl ctx_counts+0xf4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1e8(%rax)
ctx_thunk_3e:
    lock incl ctx_counts+0xf8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1f0(%rax)
ctx_thunk_3f:
    lock incl ctx_counts+0xfc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x3f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x1f8(%rax)
ctx_thunk_40:
    lock incl ctx_counts+0x100(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x40, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x200(%rax)
ctx_thunk_41:
    lock incl ctx_counts+0x104(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x41, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x208(%rax)
ctx_thunk_42:
    lock incl ctx_counts+0x108(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x42, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x210(%rax)
ctx_thunk_43:
    lock incl ctx_counts+0x10c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x43, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x218(%rax)
ctx_thunk_44:
    lock incl ctx_counts+0x110(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x44, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x220(%rax)
ctx_thunk_45:
    lock incl ctx_counts+0x114(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x45, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x228(%rax)
ctx_thunk_46:
    lock incl ctx_counts+0x118(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x46, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x230(%rax)
ctx_thunk_47:
    lock incl ctx_counts+0x11c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x47, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x238(%rax)
ctx_thunk_48:
    lock incl ctx_counts+0x120(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x48, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x240(%rax)
ctx_thunk_49:
    lock incl ctx_counts+0x124(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x49, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x248(%rax)
ctx_thunk_4a:
    lock incl ctx_counts+0x128(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x250(%rax)
ctx_thunk_4b:
    lock incl ctx_counts+0x12c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_PSGetSamplers
ctx_thunk_4c:
    lock incl ctx_counts+0x130(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x260(%rax)
ctx_thunk_4d:
    lock incl ctx_counts+0x134(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x268(%rax)
ctx_thunk_4e:
    lock incl ctx_counts+0x138(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x270(%rax)
ctx_thunk_4f:
    lock incl ctx_counts+0x13c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x4f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x278(%rax)
ctx_thunk_50:
    lock incl ctx_counts+0x140(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x50, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x280(%rax)
ctx_thunk_51:
    lock incl ctx_counts+0x144(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x51, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x288(%rax)
ctx_thunk_52:
    lock incl ctx_counts+0x148(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x52, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x290(%rax)
ctx_thunk_53:
    lock incl ctx_counts+0x14c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x53, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x298(%rax)
ctx_thunk_54:
    lock incl ctx_counts+0x150(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x54, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2a0(%rax)
ctx_thunk_55:
    lock incl ctx_counts+0x154(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x55, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2a8(%rax)
ctx_thunk_56:
    lock incl ctx_counts+0x158(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x56, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2b0(%rax)
ctx_thunk_57:
    lock incl ctx_counts+0x15c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x57, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2b8(%rax)
ctx_thunk_58:
    lock incl ctx_counts+0x160(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x58, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2c0(%rax)
ctx_thunk_59:
    lock incl ctx_counts+0x164(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x59, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2c8(%rax)
ctx_thunk_5a:
    lock incl ctx_counts+0x168(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2d0(%rax)
ctx_thunk_5b:
    lock incl ctx_counts+0x16c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2d8(%rax)
ctx_thunk_5c:
    lock incl ctx_counts+0x170(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2e0(%rax)
ctx_thunk_5d:
    lock incl ctx_counts+0x174(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2e8(%rax)
ctx_thunk_5e:
    lock incl ctx_counts+0x178(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x2f0(%rax)
ctx_thunk_5f:
    lock incl ctx_counts+0x17c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x5f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_RSGetViewports
ctx_thunk_60:
    lock incl ctx_counts+0x180(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x60, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_RSGetScissorRects
ctx_thunk_61:
    lock incl ctx_counts+0x184(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x61, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x308(%rax)
ctx_thunk_62:
    lock incl ctx_counts+0x188(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x62, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x310(%rax)
ctx_thunk_63:
    lock incl ctx_counts+0x18c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x63, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x318(%rax)
ctx_thunk_64:
    lock incl ctx_counts+0x190(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x64, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x320(%rax)
ctx_thunk_65:
    lock incl ctx_counts+0x194(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x65, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x328(%rax)
ctx_thunk_66:
    lock incl ctx_counts+0x198(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x66, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x330(%rax)
ctx_thunk_67:
    lock incl ctx_counts+0x19c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x67, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x338(%rax)
ctx_thunk_68:
    lock incl ctx_counts+0x1a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x68, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x340(%rax)
ctx_thunk_69:
    lock incl ctx_counts+0x1a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x69, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x348(%rax)
ctx_thunk_6a:
    lock incl ctx_counts+0x1a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x350(%rax)
ctx_thunk_6b:
    lock incl ctx_counts+0x1ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x358(%rax)
ctx_thunk_6c:
    lock incl ctx_counts+0x1b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x360(%rax)
ctx_thunk_6d:
    lock incl ctx_counts+0x1b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x368(%rax)
ctx_thunk_6e:
    lock incl ctx_counts+0x1b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_ClearState
ctx_thunk_6f:
    lock incl ctx_counts+0x1bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x6f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x378(%rax)
ctx_thunk_70:
    lock incl ctx_counts+0x1c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x70, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x380(%rax)
ctx_thunk_71:
    lock incl ctx_counts+0x1c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x71, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x388(%rax)
ctx_thunk_72:
    lock incl ctx_counts+0x1c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x72, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_FinishCommandList
ctx_thunk_73:
    lock incl ctx_counts+0x1cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x73, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x398(%rax)
ctx_thunk_74:
    lock incl ctx_counts+0x1d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x74, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3a0(%rax)
ctx_thunk_75:
    lock incl ctx_counts+0x1d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x75, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3a8(%rax)
ctx_thunk_76:
    lock incl ctx_counts+0x1d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x76, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3b0(%rax)
ctx_thunk_77:
    lock incl ctx_counts+0x1dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x77, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3b8(%rax)
ctx_thunk_78:
    lock incl ctx_counts+0x1e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x78, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3c0(%rax)
ctx_thunk_79:
    lock incl ctx_counts+0x1e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x79, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3c8(%rax)
ctx_thunk_7a:
    lock incl ctx_counts+0x1e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3d0(%rax)
ctx_thunk_7b:
    lock incl ctx_counts+0x1ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3d8(%rax)
ctx_thunk_7c:
    lock incl ctx_counts+0x1f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3e0(%rax)
ctx_thunk_7d:
    lock incl ctx_counts+0x1f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3e8(%rax)
ctx_thunk_7e:
    lock incl ctx_counts+0x1f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3f0(%rax)
ctx_thunk_7f:
    lock incl ctx_counts+0x1fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x7f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x3f8(%rax)
ctx_thunk_80:
    lock incl ctx_counts+0x200(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x80, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x400(%rax)
ctx_thunk_81:
    lock incl ctx_counts+0x204(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x81, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x408(%rax)
ctx_thunk_82:
    lock incl ctx_counts+0x208(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x82, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x410(%rax)
ctx_thunk_83:
    lock incl ctx_counts+0x20c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x83, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x418(%rax)
ctx_thunk_84:
    lock incl ctx_counts+0x210(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x84, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x420(%rax)
ctx_thunk_85:
    lock incl ctx_counts+0x214(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x85, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x428(%rax)
ctx_thunk_86:
    lock incl ctx_counts+0x218(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x86, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x460(%rax)
ctx_thunk_87:
    lock incl ctx_counts+0x21c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x87, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x468(%rax)
ctx_thunk_88:
    lock incl ctx_counts+0x220(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x88, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x470(%rax)
ctx_thunk_89:
    lock incl ctx_counts+0x224(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x89, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x478(%rax)
ctx_thunk_8a:
    lock incl ctx_counts+0x228(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x480(%rax)
ctx_thunk_8b:
    lock incl ctx_counts+0x22c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x488(%rax)
ctx_thunk_8c:
    lock incl ctx_counts+0x230(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x490(%rax)
ctx_thunk_8d:
    lock incl ctx_counts+0x234(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x498(%rax)
ctx_thunk_8e:
    lock incl ctx_counts+0x238(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4a0(%rax)
ctx_thunk_8f:
    lock incl ctx_counts+0x23c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x8f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4a8(%rax)
ctx_thunk_90:
    lock incl ctx_counts+0x240(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x90, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4b0(%rax)
ctx_thunk_91:
    lock incl ctx_counts+0x244(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x91, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4b8(%rax)
ctx_thunk_92:
    lock incl ctx_counts+0x248(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x92, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4c0(%rax)
ctx_thunk_93:
    lock incl ctx_counts+0x24c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x93, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4c8(%rax)
ctx_thunk_94:
    lock incl ctx_counts+0x250(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x94, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4d0(%rax)
ctx_thunk_95:
    lock incl ctx_counts+0x254(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x95, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4d8(%rax)
ctx_thunk_96:
    lock incl ctx_counts+0x258(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x96, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4e0(%rax)
ctx_thunk_97:
    lock incl ctx_counts+0x25c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x97, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4e8(%rax)
ctx_thunk_98:
    lock incl ctx_counts+0x260(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x98, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4f0(%rax)
ctx_thunk_99:
    lock incl ctx_counts+0x264(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x99, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x4f8(%rax)
ctx_thunk_9a:
    lock incl ctx_counts+0x268(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_PSSetFastConstantBuffer
ctx_thunk_9b:
    lock incl ctx_counts+0x26c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_PSSetFastShaderResource
ctx_thunk_9c:
    lock incl ctx_counts+0x270(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_PSSetFastSampler
ctx_thunk_9d:
    lock incl ctx_counts+0x274(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_VSSetFastConstantBuffer
ctx_thunk_9e:
    lock incl ctx_counts+0x278(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_VSSetFastShaderResource
ctx_thunk_9f:
    lock incl ctx_counts+0x27c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x9f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_VSSetFastSampler
ctx_thunk_a0:
    lock incl ctx_counts+0x280(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_GSSetFastConstantBuffer
ctx_thunk_a1:
    lock incl ctx_counts+0x284(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_GSSetFastShaderResource
ctx_thunk_a2:
    lock incl ctx_counts+0x288(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_GSSetFastSampler
ctx_thunk_a3:
    lock incl ctx_counts+0x28c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_CSSetFastConstantBuffer
ctx_thunk_a4:
    lock incl ctx_counts+0x290(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_CSSetFastShaderResource
ctx_thunk_a5:
    lock incl ctx_counts+0x294(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_CSSetFastSampler
ctx_thunk_a6:
    lock incl ctx_counts+0x298(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_HSSetFastConstantBuffer
ctx_thunk_a7:
    lock incl ctx_counts+0x29c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_HSSetFastShaderResource
ctx_thunk_a8:
    lock incl ctx_counts+0x2a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_HSSetFastSampler
ctx_thunk_a9:
    lock incl ctx_counts+0x2a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xa9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DSSetFastConstantBuffer
ctx_thunk_aa:
    lock incl ctx_counts+0x2a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xaa, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DSSetFastShaderResource
ctx_thunk_ab:
    lock incl ctx_counts+0x2ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xab, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_DSSetFastSampler
ctx_thunk_ac:
    lock incl ctx_counts+0x2b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xac, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_IASetFastVertexBuffer
ctx_thunk_ad:
    lock incl ctx_counts+0x2b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xad, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    jmp ctx_IASetFastIndexBuffer
ctx_thunk_ae:
    lock incl ctx_counts+0x2b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xae, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5a0(%rax)
ctx_thunk_af:
    lock incl ctx_counts+0x2bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xaf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5a8(%rax)
ctx_thunk_b0:
    lock incl ctx_counts+0x2c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5b0(%rax)
ctx_thunk_b1:
    lock incl ctx_counts+0x2c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5b8(%rax)
ctx_thunk_b2:
    lock incl ctx_counts+0x2c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5c0(%rax)
ctx_thunk_b3:
    lock incl ctx_counts+0x2cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5c8(%rax)
ctx_thunk_b4:
    lock incl ctx_counts+0x2d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5d0(%rax)
ctx_thunk_b5:
    lock incl ctx_counts+0x2d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5d8(%rax)
ctx_thunk_b6:
    lock incl ctx_counts+0x2d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5e0(%rax)
ctx_thunk_b7:
    lock incl ctx_counts+0x2dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5e8(%rax)
ctx_thunk_b8:
    lock incl ctx_counts+0x2e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5f0(%rax)
ctx_thunk_b9:
    lock incl ctx_counts+0x2e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xb9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x5f8(%rax)
ctx_thunk_ba:
    lock incl ctx_counts+0x2e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xba, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x600(%rax)
ctx_thunk_bb:
    lock incl ctx_counts+0x2ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xbb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x608(%rax)
ctx_thunk_bc:
    lock incl ctx_counts+0x2f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xbc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x610(%rax)
ctx_thunk_bd:
    lock incl ctx_counts+0x2f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xbd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x618(%rax)
ctx_thunk_be:
    lock incl ctx_counts+0x2f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xbe, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x620(%rax)
ctx_thunk_bf:
    lock incl ctx_counts+0x2fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xbf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x628(%rax)
ctx_thunk_c0:
    lock incl ctx_counts+0x300(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x630(%rax)
ctx_thunk_c1:
    lock incl ctx_counts+0x304(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x638(%rax)
ctx_thunk_c2:
    lock incl ctx_counts+0x308(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x640(%rax)
ctx_thunk_c3:
    lock incl ctx_counts+0x30c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x648(%rax)
ctx_thunk_c4:
    lock incl ctx_counts+0x310(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x650(%rax)
ctx_thunk_c5:
    lock incl ctx_counts+0x314(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x658(%rax)
ctx_thunk_c6:
    lock incl ctx_counts+0x318(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x660(%rax)
ctx_thunk_c7:
    lock incl ctx_counts+0x31c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x668(%rax)
ctx_thunk_c8:
    lock incl ctx_counts+0x320(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x670(%rax)
ctx_thunk_c9:
    lock incl ctx_counts+0x324(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xc9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x678(%rax)
ctx_thunk_ca:
    lock incl ctx_counts+0x328(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xca, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x680(%rax)
ctx_thunk_cb:
    lock incl ctx_counts+0x32c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xcb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x688(%rax)
ctx_thunk_cc:
    lock incl ctx_counts+0x330(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xcc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x690(%rax)
ctx_thunk_cd:
    lock incl ctx_counts+0x334(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xcd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x698(%rax)
ctx_thunk_ce:
    lock incl ctx_counts+0x338(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xce, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6a0(%rax)
ctx_thunk_cf:
    lock incl ctx_counts+0x33c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xcf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6a8(%rax)
ctx_thunk_d0:
    lock incl ctx_counts+0x340(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6b0(%rax)
ctx_thunk_d1:
    lock incl ctx_counts+0x344(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6b8(%rax)
ctx_thunk_d2:
    lock incl ctx_counts+0x348(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6c0(%rax)
ctx_thunk_d3:
    lock incl ctx_counts+0x34c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6c8(%rax)
ctx_thunk_d4:
    lock incl ctx_counts+0x350(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6d0(%rax)
ctx_thunk_d5:
    lock incl ctx_counts+0x354(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6d8(%rax)
ctx_thunk_d6:
    lock incl ctx_counts+0x358(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6e0(%rax)
ctx_thunk_d7:
    lock incl ctx_counts+0x35c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6e8(%rax)
ctx_thunk_d8:
    lock incl ctx_counts+0x360(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6f0(%rax)
ctx_thunk_d9:
    lock incl ctx_counts+0x364(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xd9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x6f8(%rax)
ctx_thunk_da:
    lock incl ctx_counts+0x368(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xda, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x700(%rax)
ctx_thunk_db:
    lock incl ctx_counts+0x36c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xdb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x708(%rax)
ctx_thunk_dc:
    lock incl ctx_counts+0x370(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xdc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x710(%rax)
ctx_thunk_dd:
    lock incl ctx_counts+0x374(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xdd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x718(%rax)
ctx_thunk_de:
    lock incl ctx_counts+0x378(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xde, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x720(%rax)
ctx_thunk_df:
    lock incl ctx_counts+0x37c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xdf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x728(%rax)
ctx_thunk_e0:
    lock incl ctx_counts+0x380(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x730(%rax)
ctx_thunk_e1:
    lock incl ctx_counts+0x384(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x738(%rax)
ctx_thunk_e2:
    lock incl ctx_counts+0x388(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x740(%rax)
ctx_thunk_e3:
    lock incl ctx_counts+0x38c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x748(%rax)
ctx_thunk_e4:
    lock incl ctx_counts+0x390(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x750(%rax)
ctx_thunk_e5:
    lock incl ctx_counts+0x394(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x758(%rax)
ctx_thunk_e6:
    lock incl ctx_counts+0x398(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x760(%rax)
ctx_thunk_e7:
    lock incl ctx_counts+0x39c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x768(%rax)
ctx_thunk_e8:
    lock incl ctx_counts+0x3a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x770(%rax)
ctx_thunk_e9:
    lock incl ctx_counts+0x3a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xe9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x778(%rax)
ctx_thunk_ea:
    lock incl ctx_counts+0x3a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xea, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x780(%rax)
ctx_thunk_eb:
    lock incl ctx_counts+0x3ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xeb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x788(%rax)
ctx_thunk_ec:
    lock incl ctx_counts+0x3b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xec, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x790(%rax)
ctx_thunk_ed:
    lock incl ctx_counts+0x3b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xed, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x798(%rax)
ctx_thunk_ee:
    lock incl ctx_counts+0x3b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xee, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7a0(%rax)
ctx_thunk_ef:
    lock incl ctx_counts+0x3bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xef, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7a8(%rax)
ctx_thunk_f0:
    lock incl ctx_counts+0x3c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7b0(%rax)
ctx_thunk_f1:
    lock incl ctx_counts+0x3c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7b8(%rax)
ctx_thunk_f2:
    lock incl ctx_counts+0x3c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7c0(%rax)
ctx_thunk_f3:
    lock incl ctx_counts+0x3cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7c8(%rax)
ctx_thunk_f4:
    lock incl ctx_counts+0x3d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7d0(%rax)
ctx_thunk_f5:
    lock incl ctx_counts+0x3d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7d8(%rax)
ctx_thunk_f6:
    lock incl ctx_counts+0x3d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7e0(%rax)
ctx_thunk_f7:
    lock incl ctx_counts+0x3dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7e8(%rax)
ctx_thunk_f8:
    lock incl ctx_counts+0x3e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7f0(%rax)
ctx_thunk_f9:
    lock incl ctx_counts+0x3e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xf9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x7f8(%rax)
ctx_thunk_fa:
    lock incl ctx_counts+0x3e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xfa, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x800(%rax)
ctx_thunk_fb:
    lock incl ctx_counts+0x3ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xfb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x808(%rax)
ctx_thunk_fc:
    lock incl ctx_counts+0x3f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xfc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x810(%rax)
ctx_thunk_fd:
    lock incl ctx_counts+0x3f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xfd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x818(%rax)
ctx_thunk_fe:
    lock incl ctx_counts+0x3f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xfe, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x820(%rax)
ctx_thunk_ff:
    lock incl ctx_counts+0x3fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0xff, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x828(%rax)
ctx_thunk_100:
    lock incl ctx_counts+0x400(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x830(%rax)
ctx_thunk_101:
    lock incl ctx_counts+0x404(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x838(%rax)
ctx_thunk_102:
    lock incl ctx_counts+0x408(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x840(%rax)
ctx_thunk_103:
    lock incl ctx_counts+0x40c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x848(%rax)
ctx_thunk_104:
    lock incl ctx_counts+0x410(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x850(%rax)
ctx_thunk_105:
    lock incl ctx_counts+0x414(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x858(%rax)
ctx_thunk_106:
    lock incl ctx_counts+0x418(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x860(%rax)
ctx_thunk_107:
    lock incl ctx_counts+0x41c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movq (%rcx), %rax
    jmpq *0x868(%rax)
ctx_thunk_108:
    lock incl ctx_counts+0x420(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x108, %edx
    jmp ctx_bad_slot
ctx_thunk_109:
    lock incl ctx_counts+0x424(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x109, %edx
    jmp ctx_bad_slot
ctx_thunk_10a:
    lock incl ctx_counts+0x428(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x10a, %edx
    jmp ctx_bad_slot
ctx_thunk_10b:
    lock incl ctx_counts+0x42c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x10b, %edx
    jmp ctx_bad_slot
ctx_thunk_10c:
    lock incl ctx_counts+0x430(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x10c, %edx
    jmp ctx_bad_slot
ctx_thunk_10d:
    lock incl ctx_counts+0x434(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    movq CTX_REAL_OFF(%rcx), %rcx
    movl $0x10d, %edx
    jmp ctx_bad_slot
ctx_trace_common:
    pushq %r11
    subq $0x90, %rsp
    movq %rcx, 0x30(%rsp)
    movq %rdx, 0x38(%rsp)
    movq %r8, 0x40(%rsp)
    movq %r9, 0x48(%rsp)
    movdqu %xmm0, 0x50(%rsp)
    movdqu %xmm1, 0x60(%rsp)
    movdqu %xmm2, 0x70(%rsp)
    movdqu %xmm3, 0x80(%rsp)
    movq %r9, 0x20(%rsp)
    leaq 0xc0(%rsp), %rax
    movq %rax, 0x28(%rsp)
    movq %r8, %r9
    movq %rdx, %r8
    movq %rcx, %rdx
    movl %r10d, %ecx
    call ctx_trace
    movq 0x30(%rsp), %rcx
    movq 0x38(%rsp), %rdx
    movq 0x40(%rsp), %r8
    movq 0x48(%rsp), %r9
    movdqu 0x50(%rsp), %xmm0
    movdqu 0x60(%rsp), %xmm1
    movdqu 0x70(%rsp), %xmm2
    movdqu 0x80(%rsp), %xmm3
    addq $0x90, %rsp
    popq %r11
    jmp *%r11
dev_thunk_0:
    lock incl dev_counts+0x0(%rip)
    jmpq *dev_impl+0x0(%rip)
dev_thunk_1:
    lock incl dev_counts+0x4(%rip)
    jmpq *dev_impl+0x8(%rip)
dev_thunk_2:
    lock incl dev_counts+0x8(%rip)
    jmpq *dev_impl+0x10(%rip)
dev_thunk_3:
    lock incl dev_counts+0xc(%rip)
    jmpq *dev_impl+0x18(%rip)
dev_thunk_4:
    lock incl dev_counts+0x10(%rip)
    jmpq *dev_impl+0x20(%rip)
dev_thunk_5:
    lock incl dev_counts+0x14(%rip)
    jmpq *dev_impl+0x28(%rip)
dev_thunk_6:
    lock incl dev_counts+0x18(%rip)
    jmpq *dev_impl+0x30(%rip)
dev_thunk_7:
    lock incl dev_counts+0x1c(%rip)
    jmpq *dev_impl+0x38(%rip)
dev_thunk_8:
    lock incl dev_counts+0x20(%rip)
    jmpq *dev_impl+0x40(%rip)
dev_thunk_9:
    lock incl dev_counts+0x24(%rip)
    jmpq *dev_impl+0x48(%rip)
dev_thunk_a:
    lock incl dev_counts+0x28(%rip)
    jmpq *dev_impl+0x50(%rip)
dev_thunk_b:
    lock incl dev_counts+0x2c(%rip)
    jmpq *dev_impl+0x58(%rip)
dev_thunk_c:
    lock incl dev_counts+0x30(%rip)
    jmpq *dev_impl+0x60(%rip)
dev_thunk_d:
    lock incl dev_counts+0x34(%rip)
    jmpq *dev_impl+0x68(%rip)
dev_thunk_e:
    lock incl dev_counts+0x38(%rip)
    jmpq *dev_impl+0x70(%rip)
dev_thunk_f:
    lock incl dev_counts+0x3c(%rip)
    jmpq *dev_impl+0x78(%rip)
dev_thunk_10:
    lock incl dev_counts+0x40(%rip)
    jmpq *dev_impl+0x80(%rip)
dev_thunk_11:
    lock incl dev_counts+0x44(%rip)
    jmpq *dev_impl+0x88(%rip)
dev_thunk_12:
    lock incl dev_counts+0x48(%rip)
    jmpq *dev_impl+0x90(%rip)
dev_thunk_13:
    lock incl dev_counts+0x4c(%rip)
    jmpq *dev_impl+0x98(%rip)
dev_thunk_14:
    lock incl dev_counts+0x50(%rip)
    jmpq *dev_impl+0xa0(%rip)
dev_thunk_15:
    lock incl dev_counts+0x54(%rip)
    jmpq *dev_impl+0xa8(%rip)
dev_thunk_16:
    lock incl dev_counts+0x58(%rip)
    jmpq *dev_impl+0xb0(%rip)
dev_thunk_17:
    lock incl dev_counts+0x5c(%rip)
    jmpq *dev_impl+0xb8(%rip)
dev_thunk_18:
    lock incl dev_counts+0x60(%rip)
    jmpq *dev_impl+0xc0(%rip)
dev_thunk_19:
    lock incl dev_counts+0x64(%rip)
    jmpq *dev_impl+0xc8(%rip)
dev_thunk_1a:
    lock incl dev_counts+0x68(%rip)
    jmpq *dev_impl+0xd0(%rip)
dev_thunk_1b:
    lock incl dev_counts+0x6c(%rip)
    jmpq *dev_impl+0xd8(%rip)
dev_thunk_1c:
    lock incl dev_counts+0x70(%rip)
    jmpq *dev_impl+0xe0(%rip)
dev_thunk_1d:
    lock incl dev_counts+0x74(%rip)
    jmpq *dev_impl+0xe8(%rip)
dev_thunk_1e:
    lock incl dev_counts+0x78(%rip)
    jmpq *dev_impl+0xf0(%rip)
dev_thunk_1f:
    lock incl dev_counts+0x7c(%rip)
    jmpq *dev_impl+0xf8(%rip)
dev_thunk_20:
    lock incl dev_counts+0x80(%rip)
    jmpq *dev_impl+0x100(%rip)
dev_thunk_21:
    lock incl dev_counts+0x84(%rip)
    jmpq *dev_impl+0x108(%rip)
dev_thunk_22:
    lock incl dev_counts+0x88(%rip)
    jmpq *dev_impl+0x110(%rip)
dev_thunk_23:
    lock incl dev_counts+0x8c(%rip)
    jmpq *dev_impl+0x118(%rip)
dev_thunk_24:
    lock incl dev_counts+0x90(%rip)
    jmpq *dev_impl+0x120(%rip)
dev_thunk_25:
    lock incl dev_counts+0x94(%rip)
    jmpq *dev_impl+0x128(%rip)
dev_thunk_26:
    lock incl dev_counts+0x98(%rip)
    jmpq *dev_impl+0x130(%rip)
dev_thunk_27:
    lock incl dev_counts+0x9c(%rip)
    jmpq *dev_impl+0x138(%rip)
dev_thunk_28:
    lock incl dev_counts+0xa0(%rip)
    jmpq *dev_impl+0x140(%rip)
dev_thunk_29:
    lock incl dev_counts+0xa4(%rip)
    jmpq *dev_impl+0x148(%rip)
dev_thunk_2a:
    lock incl dev_counts+0xa8(%rip)
    jmpq *dev_impl+0x150(%rip)
dev_thunk_2b:
    lock incl dev_counts+0xac(%rip)
    jmpq *dev_impl+0x158(%rip)
dev_thunk_2c:
    lock incl dev_counts+0xb0(%rip)
    jmpq *dev_impl+0x160(%rip)
dev_thunk_2d:
    lock incl dev_counts+0xb4(%rip)
    jmpq *dev_impl+0x168(%rip)
dev_thunk_2e:
    lock incl dev_counts+0xb8(%rip)
    jmpq *dev_impl+0x170(%rip)
dev_thunk_2f:
    lock incl dev_counts+0xbc(%rip)
    jmpq *dev_impl+0x178(%rip)
dev_thunk_30:
    lock incl dev_counts+0xc0(%rip)
    jmpq *dev_impl+0x180(%rip)
dev_thunk_31:
    lock incl dev_counts+0xc4(%rip)
    jmpq *dev_impl+0x188(%rip)
dev_thunk_32:
    lock incl dev_counts+0xc8(%rip)
    jmpq *dev_impl+0x190(%rip)
dev_thunk_33:
    lock incl dev_counts+0xcc(%rip)
    jmpq *dev_impl+0x198(%rip)
dev_thunk_34:
    lock incl dev_counts+0xd0(%rip)
    jmpq *dev_impl+0x1a0(%rip)
dev_thunk_35:
    lock incl dev_counts+0xd4(%rip)
    jmpq *dev_impl+0x1a8(%rip)
dev_thunk_36:
    lock incl dev_counts+0xd8(%rip)
    jmpq *dev_impl+0x1b0(%rip)
dev_thunk_37:
    lock incl dev_counts+0xdc(%rip)
    jmpq *dev_impl+0x1b8(%rip)
dev_thunk_38:
    lock incl dev_counts+0xe0(%rip)
    jmpq *dev_impl+0x1c0(%rip)
dev_thunk_39:
    lock incl dev_counts+0xe4(%rip)
    jmpq *dev_impl+0x1c8(%rip)
dev_thunk_3a:
    lock incl dev_counts+0xe8(%rip)
    jmpq *dev_impl+0x1d0(%rip)
dev_thunk_3b:
    lock incl dev_counts+0xec(%rip)
    jmpq *dev_impl+0x1d8(%rip)
dev_thunk_3c:
    lock incl dev_counts+0xf0(%rip)
    jmpq *dev_impl+0x1e0(%rip)
dev_thunk_3d:
    lock incl dev_counts+0xf4(%rip)
    jmpq *dev_impl+0x1e8(%rip)
dev_thunk_3e:
    lock incl dev_counts+0xf8(%rip)
    jmpq *dev_impl+0x1f0(%rip)
dev_thunk_3f:
    lock incl dev_counts+0xfc(%rip)
    jmpq *dev_impl+0x1f8(%rip)
dev_thunk_40:
    lock incl dev_counts+0x100(%rip)
    jmpq *dev_impl+0x200(%rip)
dev_thunk_41:
    lock incl dev_counts+0x104(%rip)
    jmpq *dev_impl+0x208(%rip)
dev_thunk_42:
    lock incl dev_counts+0x108(%rip)
    jmpq *dev_impl+0x210(%rip)
dev_thunk_43:
    lock incl dev_counts+0x10c(%rip)
    jmpq *dev_impl+0x218(%rip)
dev_thunk_44:
    lock incl dev_counts+0x110(%rip)
    jmpq *dev_impl+0x220(%rip)
dev_thunk_45:
    lock incl dev_counts+0x114(%rip)
    jmpq *dev_impl+0x228(%rip)
dev_thunk_46:
    lock incl dev_counts+0x118(%rip)
    jmpq *dev_impl+0x230(%rip)
dev_thunk_47:
    lock incl dev_counts+0x11c(%rip)
    jmpq *dev_impl+0x238(%rip)
dev_thunk_48:
    lock incl dev_counts+0x120(%rip)
    jmpq *dev_impl+0x240(%rip)
dev_thunk_49:
    lock incl dev_counts+0x124(%rip)
    jmpq *dev_impl+0x248(%rip)
dev_thunk_4a:
    lock incl dev_counts+0x128(%rip)
    jmpq *dev_impl+0x250(%rip)
dev_thunk_4b:
    lock incl dev_counts+0x12c(%rip)
    jmpq *dev_impl+0x258(%rip)
dev_thunk_4c:
    lock incl dev_counts+0x130(%rip)
    jmpq *dev_impl+0x260(%rip)
dev_thunk_4d:
    lock incl dev_counts+0x134(%rip)
    jmpq *dev_impl+0x268(%rip)
dev_thunk_4e:
    lock incl dev_counts+0x138(%rip)
    jmpq *dev_impl+0x270(%rip)
dev_thunk_4f:
    lock incl dev_counts+0x13c(%rip)
    jmpq *dev_impl+0x278(%rip)
dev_thunk_50:
    lock incl dev_counts+0x140(%rip)
    jmpq *dev_impl+0x280(%rip)
dev_thunk_51:
    lock incl dev_counts+0x144(%rip)
    jmpq *dev_impl+0x288(%rip)
dev_thunk_52:
    lock incl dev_counts+0x148(%rip)
    jmpq *dev_impl+0x290(%rip)
dev_thunk_53:
    lock incl dev_counts+0x14c(%rip)
    jmpq *dev_impl+0x298(%rip)
dev_thunk_54:
    lock incl dev_counts+0x150(%rip)
    jmpq *dev_impl+0x2a0(%rip)
dev_thunk_55:
    lock incl dev_counts+0x154(%rip)
    jmpq *dev_impl+0x2a8(%rip)
dev_thunk_56:
    lock incl dev_counts+0x158(%rip)
    jmpq *dev_impl+0x2b0(%rip)
dev_thunk_57:
    lock incl dev_counts+0x15c(%rip)
    jmpq *dev_impl+0x2b8(%rip)
dev_thunk_58:
    lock incl dev_counts+0x160(%rip)
    jmpq *dev_impl+0x2c0(%rip)
dev_thunk_59:
    lock incl dev_counts+0x164(%rip)
    jmpq *dev_impl+0x2c8(%rip)
dev_thunk_5a:
    lock incl dev_counts+0x168(%rip)
    jmpq *dev_impl+0x2d0(%rip)
dev_thunk_5b:
    lock incl dev_counts+0x16c(%rip)
    jmpq *dev_impl+0x2d8(%rip)
dev_thunk_5c:
    lock incl dev_counts+0x170(%rip)
    jmpq *dev_impl+0x2e0(%rip)
dev_thunk_5d:
    lock incl dev_counts+0x174(%rip)
    jmpq *dev_impl+0x2e8(%rip)
dev_thunk_5e:
    lock incl dev_counts+0x178(%rip)
    jmpq *dev_impl+0x2f0(%rip)
dev_thunk_5f:
    lock incl dev_counts+0x17c(%rip)
    jmpq *dev_impl+0x2f8(%rip)
dev_thunk_60:
    lock incl dev_counts+0x180(%rip)
    jmpq *dev_impl+0x300(%rip)
dev_thunk_61:
    lock incl dev_counts+0x184(%rip)
    jmpq *dev_impl+0x308(%rip)
dev_thunk_62:
    lock incl dev_counts+0x188(%rip)
    jmpq *dev_impl+0x310(%rip)
dev_thunk_63:
    lock incl dev_counts+0x18c(%rip)
    jmpq *dev_impl+0x318(%rip)
dev_thunk_64:
    lock incl dev_counts+0x190(%rip)
    jmpq *dev_impl+0x320(%rip)
dev_thunk_65:
    lock incl dev_counts+0x194(%rip)
    jmpq *dev_impl+0x328(%rip)
dev_thunk_66:
    lock incl dev_counts+0x198(%rip)
    jmpq *dev_impl+0x330(%rip)
dev_thunk_67:
    lock incl dev_counts+0x19c(%rip)
    jmpq *dev_impl+0x338(%rip)
dev_thunk_68:
    lock incl dev_counts+0x1a0(%rip)
    jmpq *dev_impl+0x340(%rip)
dev_thunk_69:
    lock incl dev_counts+0x1a4(%rip)
    jmpq *dev_impl+0x348(%rip)
dev_thunk_6a:
    lock incl dev_counts+0x1a8(%rip)
    jmpq *dev_impl+0x350(%rip)
dev_thunk_6b:
    lock incl dev_counts+0x1ac(%rip)
    jmpq *dev_impl+0x358(%rip)
dev_thunk_6c:
    lock incl dev_counts+0x1b0(%rip)
    jmpq *dev_impl+0x360(%rip)
dev_thunk_6d:
    lock incl dev_counts+0x1b4(%rip)
    jmpq *dev_impl+0x368(%rip)
dev_thunk_6e:
    lock incl dev_counts+0x1b8(%rip)
    jmpq *dev_impl+0x370(%rip)
dev_thunk_6f:
    lock incl dev_counts+0x1bc(%rip)
    jmpq *dev_impl+0x378(%rip)
dev_thunk_70:
    lock incl dev_counts+0x1c0(%rip)
    jmpq *dev_impl+0x380(%rip)
dev_thunk_71:
    lock incl dev_counts+0x1c4(%rip)
    jmpq *dev_impl+0x388(%rip)
dev_thunk_72:
    lock incl dev_counts+0x1c8(%rip)
    jmpq *dev_impl+0x390(%rip)
dev_thunk_73:
    lock incl dev_counts+0x1cc(%rip)
    jmpq *dev_impl+0x398(%rip)
dev_thunk_74:
    lock incl dev_counts+0x1d0(%rip)
    jmpq *dev_impl+0x3a0(%rip)
dev_thunk_75:
    lock incl dev_counts+0x1d4(%rip)
    jmpq *dev_impl+0x3a8(%rip)
dev_thunk_76:
    lock incl dev_counts+0x1d8(%rip)
    jmpq *dev_impl+0x3b0(%rip)
dev_thunk_77:
    lock incl dev_counts+0x1dc(%rip)
    jmpq *dev_impl+0x3b8(%rip)
dev_thunk_78:
    lock incl dev_counts+0x1e0(%rip)
    jmpq *dev_impl+0x3c0(%rip)
dev_thunk_79:
    lock incl dev_counts+0x1e4(%rip)
    jmpq *dev_impl+0x3c8(%rip)
dev_thunk_7a:
    lock incl dev_counts+0x1e8(%rip)
    jmpq *dev_impl+0x3d0(%rip)
dev_thunk_7b:
    lock incl dev_counts+0x1ec(%rip)
    jmpq *dev_impl+0x3d8(%rip)
dev_thunk_7c:
    lock incl dev_counts+0x1f0(%rip)
    jmpq *dev_impl+0x3e0(%rip)
dev_thunk_7d:
    lock incl dev_counts+0x1f4(%rip)
    jmpq *dev_impl+0x3e8(%rip)
dev_thunk_7e:
    lock incl dev_counts+0x1f8(%rip)
    jmpq *dev_impl+0x3f0(%rip)
dev_thunk_7f:
    lock incl dev_counts+0x1fc(%rip)
    jmpq *dev_impl+0x3f8(%rip)
dev_thunk_80:
    lock incl dev_counts+0x200(%rip)
    jmpq *dev_impl+0x400(%rip)
dev_thunk_81:
    lock incl dev_counts+0x204(%rip)
    jmpq *dev_impl+0x408(%rip)
dev_thunk_82:
    lock incl dev_counts+0x208(%rip)
    jmpq *dev_impl+0x410(%rip)
dev_thunk_83:
    lock incl dev_counts+0x20c(%rip)
    jmpq *dev_impl+0x418(%rip)
dev_thunk_84:
    lock incl dev_counts+0x210(%rip)
    jmpq *dev_impl+0x420(%rip)
dev_thunk_85:
    lock incl dev_counts+0x214(%rip)
    jmpq *dev_impl+0x428(%rip)
dev_thunk_86:
    lock incl dev_counts+0x218(%rip)
    jmpq *dev_impl+0x430(%rip)
dev_thunk_87:
    lock incl dev_counts+0x21c(%rip)
    jmpq *dev_impl+0x438(%rip)
dev_thunk_88:
    lock incl dev_counts+0x220(%rip)
    jmpq *dev_impl+0x440(%rip)
dev_thunk_89:
    lock incl dev_counts+0x224(%rip)
    jmpq *dev_impl+0x448(%rip)
dev_thunk_8a:
    lock incl dev_counts+0x228(%rip)
    jmpq *dev_impl+0x450(%rip)
dev_thunk_8b:
    lock incl dev_counts+0x22c(%rip)
    jmpq *dev_impl+0x458(%rip)
dev_thunk_8c:
    lock incl dev_counts+0x230(%rip)
    jmpq *dev_impl+0x460(%rip)
dev_thunk_8d:
    lock incl dev_counts+0x234(%rip)
    jmpq *dev_impl+0x468(%rip)
dev_thunk_8e:
    lock incl dev_counts+0x238(%rip)
    jmpq *dev_impl+0x470(%rip)
dev_thunk_8f:
    lock incl dev_counts+0x23c(%rip)
    jmpq *dev_impl+0x478(%rip)
dev_thunk_90:
    lock incl dev_counts+0x240(%rip)
    jmpq *dev_impl+0x480(%rip)
dev_thunk_91:
    lock incl dev_counts+0x244(%rip)
    jmpq *dev_impl+0x488(%rip)
dev_thunk_92:
    lock incl dev_counts+0x248(%rip)
    jmpq *dev_impl+0x490(%rip)
dev_thunk_93:
    lock incl dev_counts+0x24c(%rip)
    jmpq *dev_impl+0x498(%rip)
dev_thunk_94:
    lock incl dev_counts+0x250(%rip)
    jmpq *dev_impl+0x4a0(%rip)
dev_thunk_95:
    lock incl dev_counts+0x254(%rip)
    jmpq *dev_impl+0x4a8(%rip)
dev_thunk_96:
    lock incl dev_counts+0x258(%rip)
    jmpq *dev_impl+0x4b0(%rip)
dev_thunk_97:
    lock incl dev_counts+0x25c(%rip)
    jmpq *dev_impl+0x4b8(%rip)
dev_thunk_98:
    lock incl dev_counts+0x260(%rip)
    jmpq *dev_impl+0x4c0(%rip)
dev_thunk_99:
    lock incl dev_counts+0x264(%rip)
    jmpq *dev_impl+0x4c8(%rip)
dev_thunk_9a:
    lock incl dev_counts+0x268(%rip)
    jmpq *dev_impl+0x4d0(%rip)
dev_thunk_9b:
    lock incl dev_counts+0x26c(%rip)
    jmpq *dev_impl+0x4d8(%rip)
dev_thunk_9c:
    lock incl dev_counts+0x270(%rip)
    jmpq *dev_impl+0x4e0(%rip)
dev_thunk_9d:
    lock incl dev_counts+0x274(%rip)
    jmpq *dev_impl+0x4e8(%rip)
dev_thunk_9e:
    lock incl dev_counts+0x278(%rip)
    jmpq *dev_impl+0x4f0(%rip)
dev_thunk_9f:
    lock incl dev_counts+0x27c(%rip)
    jmpq *dev_impl+0x4f8(%rip)
dev_thunk_a0:
    lock incl dev_counts+0x280(%rip)
    jmpq *dev_impl+0x500(%rip)
dev_thunk_a1:
    lock incl dev_counts+0x284(%rip)
    jmpq *dev_impl+0x508(%rip)
dev_thunk_a2:
    lock incl dev_counts+0x288(%rip)
    jmpq *dev_impl+0x510(%rip)
dev_thunk_a3:
    lock incl dev_counts+0x28c(%rip)
    jmpq *dev_impl+0x518(%rip)
dev_thunk_a4:
    lock incl dev_counts+0x290(%rip)
    jmpq *dev_impl+0x520(%rip)
dev_thunk_a5:
    lock incl dev_counts+0x294(%rip)
    jmpq *dev_impl+0x528(%rip)
dev_thunk_a6:
    lock incl dev_counts+0x298(%rip)
    jmpq *dev_impl+0x530(%rip)
dev_thunk_a7:
    lock incl dev_counts+0x29c(%rip)
    jmpq *dev_impl+0x538(%rip)
dev_thunk_a8:
    lock incl dev_counts+0x2a0(%rip)
    jmpq *dev_impl+0x540(%rip)
dev_thunk_a9:
    lock incl dev_counts+0x2a4(%rip)
    jmpq *dev_impl+0x548(%rip)
dev_thunk_aa:
    lock incl dev_counts+0x2a8(%rip)
    jmpq *dev_impl+0x550(%rip)
dev_thunk_ab:
    lock incl dev_counts+0x2ac(%rip)
    jmpq *dev_impl+0x558(%rip)
dev_thunk_ac:
    lock incl dev_counts+0x2b0(%rip)
    jmpq *dev_impl+0x560(%rip)
dev_thunk_ad:
    lock incl dev_counts+0x2b4(%rip)
    jmpq *dev_impl+0x568(%rip)
dev_thunk_ae:
    lock incl dev_counts+0x2b8(%rip)
    jmpq *dev_impl+0x570(%rip)
dev_thunk_af:
    lock incl dev_counts+0x2bc(%rip)
    jmpq *dev_impl+0x578(%rip)
dev_thunk_b0:
    lock incl dev_counts+0x2c0(%rip)
    jmpq *dev_impl+0x580(%rip)
dev_thunk_b1:
    lock incl dev_counts+0x2c4(%rip)
    jmpq *dev_impl+0x588(%rip)
dev_thunk_b2:
    lock incl dev_counts+0x2c8(%rip)
    jmpq *dev_impl+0x590(%rip)
dev_thunk_b3:
    lock incl dev_counts+0x2cc(%rip)
    jmpq *dev_impl+0x598(%rip)
dev_thunk_b4:
    lock incl dev_counts+0x2d0(%rip)
    jmpq *dev_impl+0x5a0(%rip)
dev_thunk_b5:
    lock incl dev_counts+0x2d4(%rip)
    jmpq *dev_impl+0x5a8(%rip)
dev_thunk_b6:
    lock incl dev_counts+0x2d8(%rip)
    jmpq *dev_impl+0x5b0(%rip)
dev_thunk_b7:
    lock incl dev_counts+0x2dc(%rip)
    jmpq *dev_impl+0x5b8(%rip)
dev_thunk_b8:
    lock incl dev_counts+0x2e0(%rip)
    jmpq *dev_impl+0x5c0(%rip)
dev_thunk_b9:
    lock incl dev_counts+0x2e4(%rip)
    jmpq *dev_impl+0x5c8(%rip)
dev_thunk_ba:
    lock incl dev_counts+0x2e8(%rip)
    jmpq *dev_impl+0x5d0(%rip)
dev_thunk_bb:
    lock incl dev_counts+0x2ec(%rip)
    jmpq *dev_impl+0x5d8(%rip)
dev_thunk_bc:
    lock incl dev_counts+0x2f0(%rip)
    jmpq *dev_impl+0x5e0(%rip)
dev_thunk_bd:
    lock incl dev_counts+0x2f4(%rip)
    jmpq *dev_impl+0x5e8(%rip)
dev_thunk_be:
    lock incl dev_counts+0x2f8(%rip)
    jmpq *dev_impl+0x5f0(%rip)
dev_thunk_bf:
    lock incl dev_counts+0x2fc(%rip)
    jmpq *dev_impl+0x5f8(%rip)
dev_thunk_c0:
    lock incl dev_counts+0x300(%rip)
    jmpq *dev_impl+0x600(%rip)
dev_thunk_c1:
    lock incl dev_counts+0x304(%rip)
    jmpq *dev_impl+0x608(%rip)
dev_thunk_c2:
    lock incl dev_counts+0x308(%rip)
    jmpq *dev_impl+0x610(%rip)
dev_thunk_c3:
    lock incl dev_counts+0x30c(%rip)
    jmpq *dev_impl+0x618(%rip)
dev_thunk_c4:
    lock incl dev_counts+0x310(%rip)
    jmpq *dev_impl+0x620(%rip)
dev_thunk_c5:
    lock incl dev_counts+0x314(%rip)
    jmpq *dev_impl+0x628(%rip)
dev_thunk_c6:
    lock incl dev_counts+0x318(%rip)
    jmpq *dev_impl+0x630(%rip)
dev_thunk_c7:
    lock incl dev_counts+0x31c(%rip)
    jmpq *dev_impl+0x638(%rip)
dev_thunk_c8:
    lock incl dev_counts+0x320(%rip)
    jmpq *dev_impl+0x640(%rip)
dev_thunk_c9:
    lock incl dev_counts+0x324(%rip)
    jmpq *dev_impl+0x648(%rip)
dev_thunk_ca:
    lock incl dev_counts+0x328(%rip)
    jmpq *dev_impl+0x650(%rip)
dev_thunk_cb:
    lock incl dev_counts+0x32c(%rip)
    jmpq *dev_impl+0x658(%rip)
dev_thunk_cc:
    lock incl dev_counts+0x330(%rip)
    jmpq *dev_impl+0x660(%rip)
dev_thunk_cd:
    lock incl dev_counts+0x334(%rip)
    jmpq *dev_impl+0x668(%rip)
dev_thunk_ce:
    lock incl dev_counts+0x338(%rip)
    jmpq *dev_impl+0x670(%rip)
dev_thunk_cf:
    lock incl dev_counts+0x33c(%rip)
    jmpq *dev_impl+0x678(%rip)
dev_thunk_d0:
    lock incl dev_counts+0x340(%rip)
    jmpq *dev_impl+0x680(%rip)
dev_thunk_d1:
    lock incl dev_counts+0x344(%rip)
    jmpq *dev_impl+0x688(%rip)
dev_thunk_d2:
    lock incl dev_counts+0x348(%rip)
    jmpq *dev_impl+0x690(%rip)
dev_thunk_d3:
    lock incl dev_counts+0x34c(%rip)
    jmpq *dev_impl+0x698(%rip)
dev_thunk_d4:
    lock incl dev_counts+0x350(%rip)
    jmpq *dev_impl+0x6a0(%rip)
dev_thunk_d5:
    lock incl dev_counts+0x354(%rip)
    jmpq *dev_impl+0x6a8(%rip)
dev_thunk_d6:
    lock incl dev_counts+0x358(%rip)
    jmpq *dev_impl+0x6b0(%rip)
dev_thunk_d7:
    lock incl dev_counts+0x35c(%rip)
    jmpq *dev_impl+0x6b8(%rip)
dev_thunk_d8:
    lock incl dev_counts+0x360(%rip)
    jmpq *dev_impl+0x6c0(%rip)
dev_thunk_d9:
    lock incl dev_counts+0x364(%rip)
    jmpq *dev_impl+0x6c8(%rip)
dev_thunk_da:
    lock incl dev_counts+0x368(%rip)
    jmpq *dev_impl+0x6d0(%rip)
dev_thunk_db:
    lock incl dev_counts+0x36c(%rip)
    jmpq *dev_impl+0x6d8(%rip)
dev_thunk_dc:
    lock incl dev_counts+0x370(%rip)
    jmpq *dev_impl+0x6e0(%rip)
dev_thunk_dd:
    lock incl dev_counts+0x374(%rip)
    jmpq *dev_impl+0x6e8(%rip)
dev_thunk_de:
    lock incl dev_counts+0x378(%rip)
    jmpq *dev_impl+0x6f0(%rip)
dev_thunk_df:
    lock incl dev_counts+0x37c(%rip)
    jmpq *dev_impl+0x6f8(%rip)
dev_thunk_e0:
    lock incl dev_counts+0x380(%rip)
    jmpq *dev_impl+0x700(%rip)
dev_thunk_e1:
    lock incl dev_counts+0x384(%rip)
    jmpq *dev_impl+0x708(%rip)
dev_thunk_e2:
    lock incl dev_counts+0x388(%rip)
    jmpq *dev_impl+0x710(%rip)
dev_thunk_e3:
    lock incl dev_counts+0x38c(%rip)
    jmpq *dev_impl+0x718(%rip)
dev_thunk_e4:
    lock incl dev_counts+0x390(%rip)
    jmpq *dev_impl+0x720(%rip)
dev_thunk_e5:
    lock incl dev_counts+0x394(%rip)
    jmpq *dev_impl+0x728(%rip)
dev_thunk_e6:
    lock incl dev_counts+0x398(%rip)
    jmpq *dev_impl+0x730(%rip)
dev_thunk_e7:
    lock incl dev_counts+0x39c(%rip)
    jmpq *dev_impl+0x738(%rip)
dev_thunk_e8:
    lock incl dev_counts+0x3a0(%rip)
    jmpq *dev_impl+0x740(%rip)
dev_thunk_e9:
    lock incl dev_counts+0x3a4(%rip)
    jmpq *dev_impl+0x748(%rip)
dev_thunk_ea:
    lock incl dev_counts+0x3a8(%rip)
    jmpq *dev_impl+0x750(%rip)
dev_thunk_eb:
    lock incl dev_counts+0x3ac(%rip)
    jmpq *dev_impl+0x758(%rip)
dev_thunk_ec:
    lock incl dev_counts+0x3b0(%rip)
    jmpq *dev_impl+0x760(%rip)
dev_thunk_ed:
    lock incl dev_counts+0x3b4(%rip)
    jmpq *dev_impl+0x768(%rip)
dev_thunk_ee:
    lock incl dev_counts+0x3b8(%rip)
    jmpq *dev_impl+0x770(%rip)
dev_thunk_ef:
    lock incl dev_counts+0x3bc(%rip)
    jmpq *dev_impl+0x778(%rip)
dev_thunk_f0:
    lock incl dev_counts+0x3c0(%rip)
    jmpq *dev_impl+0x780(%rip)
dev_thunk_f1:
    lock incl dev_counts+0x3c4(%rip)
    jmpq *dev_impl+0x788(%rip)
dev_thunk_f2:
    lock incl dev_counts+0x3c8(%rip)
    jmpq *dev_impl+0x790(%rip)
dev_thunk_f3:
    lock incl dev_counts+0x3cc(%rip)
    jmpq *dev_impl+0x798(%rip)
dev_thunk_f4:
    lock incl dev_counts+0x3d0(%rip)
    jmpq *dev_impl+0x7a0(%rip)
dev_thunk_f5:
    lock incl dev_counts+0x3d4(%rip)
    jmpq *dev_impl+0x7a8(%rip)
dev_thunk_f6:
    lock incl dev_counts+0x3d8(%rip)
    jmpq *dev_impl+0x7b0(%rip)
dev_thunk_f7:
    lock incl dev_counts+0x3dc(%rip)
    jmpq *dev_impl+0x7b8(%rip)
dev_thunk_f8:
    lock incl dev_counts+0x3e0(%rip)
    jmpq *dev_impl+0x7c0(%rip)
dev_thunk_f9:
    lock incl dev_counts+0x3e4(%rip)
    jmpq *dev_impl+0x7c8(%rip)
dev_thunk_fa:
    lock incl dev_counts+0x3e8(%rip)
    jmpq *dev_impl+0x7d0(%rip)
dev_thunk_fb:
    lock incl dev_counts+0x3ec(%rip)
    jmpq *dev_impl+0x7d8(%rip)
dev_thunk_fc:
    lock incl dev_counts+0x3f0(%rip)
    jmpq *dev_impl+0x7e0(%rip)
dev_thunk_fd:
    lock incl dev_counts+0x3f4(%rip)
    jmpq *dev_impl+0x7e8(%rip)
dev_thunk_fe:
    lock incl dev_counts+0x3f8(%rip)
    jmpq *dev_impl+0x7f0(%rip)
dev_thunk_ff:
    lock incl dev_counts+0x3fc(%rip)
    jmpq *dev_impl+0x7f8(%rip)
dev_thunk_100:
    lock incl dev_counts+0x400(%rip)
    jmpq *dev_impl+0x800(%rip)
dev_thunk_101:
    lock incl dev_counts+0x404(%rip)
    jmpq *dev_impl+0x808(%rip)
dev_thunk_102:
    lock incl dev_counts+0x408(%rip)
    jmpq *dev_impl+0x810(%rip)
dev_thunk_103:
    lock incl dev_counts+0x40c(%rip)
    jmpq *dev_impl+0x818(%rip)
dev_thunk_104:
    lock incl dev_counts+0x410(%rip)
    jmpq *dev_impl+0x820(%rip)
dev_thunk_105:
    lock incl dev_counts+0x414(%rip)
    jmpq *dev_impl+0x828(%rip)
dev_thunk_106:
    lock incl dev_counts+0x418(%rip)
    jmpq *dev_impl+0x830(%rip)
dev_thunk_107:
    lock incl dev_counts+0x41c(%rip)
    jmpq *dev_impl+0x838(%rip)
dev_thunk_108:
    lock incl dev_counts+0x420(%rip)
    jmpq *dev_impl+0x840(%rip)
dev_thunk_109:
    lock incl dev_counts+0x424(%rip)
    jmpq *dev_impl+0x848(%rip)
dev_thunk_10a:
    lock incl dev_counts+0x428(%rip)
    jmpq *dev_impl+0x850(%rip)
dev_thunk_10b:
    lock incl dev_counts+0x42c(%rip)
    jmpq *dev_impl+0x858(%rip)
dev_thunk_10c:
    lock incl dev_counts+0x430(%rip)
    jmpq *dev_impl+0x860(%rip)
dev_thunk_10d:
    lock incl dev_counts+0x434(%rip)
    jmpq *dev_impl+0x868(%rip)
dma_thunk_0:
    lock incl dma_counts+0x0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1000, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x0(%rip)
dma_thunk_1:
    lock incl dma_counts+0x4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1001, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x8(%rip)
dma_thunk_2:
    lock incl dma_counts+0x8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1002, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x10(%rip)
dma_thunk_3:
    lock incl dma_counts+0xc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1003, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x18(%rip)
dma_thunk_4:
    lock incl dma_counts+0x10(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1004, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x20(%rip)
dma_thunk_5:
    lock incl dma_counts+0x14(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1005, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x28(%rip)
dma_thunk_6:
    lock incl dma_counts+0x18(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1006, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x30(%rip)
dma_thunk_7:
    lock incl dma_counts+0x1c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1007, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x38(%rip)
dma_thunk_8:
    lock incl dma_counts+0x20(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1008, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x40(%rip)
dma_thunk_9:
    lock incl dma_counts+0x24(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1009, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x48(%rip)
dma_thunk_a:
    lock incl dma_counts+0x28(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x50(%rip)
dma_thunk_b:
    lock incl dma_counts+0x2c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x58(%rip)
dma_thunk_c:
    lock incl dma_counts+0x30(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x60(%rip)
dma_thunk_d:
    lock incl dma_counts+0x34(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x68(%rip)
dma_thunk_e:
    lock incl dma_counts+0x38(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x70(%rip)
dma_thunk_f:
    lock incl dma_counts+0x3c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x100f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x78(%rip)
dma_thunk_10:
    lock incl dma_counts+0x40(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1010, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x80(%rip)
dma_thunk_11:
    lock incl dma_counts+0x44(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1011, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x88(%rip)
dma_thunk_12:
    lock incl dma_counts+0x48(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1012, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x90(%rip)
dma_thunk_13:
    lock incl dma_counts+0x4c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1013, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x98(%rip)
dma_thunk_14:
    lock incl dma_counts+0x50(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1014, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xa0(%rip)
dma_thunk_15:
    lock incl dma_counts+0x54(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1015, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xa8(%rip)
dma_thunk_16:
    lock incl dma_counts+0x58(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1016, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xb0(%rip)
dma_thunk_17:
    lock incl dma_counts+0x5c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1017, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xb8(%rip)
dma_thunk_18:
    lock incl dma_counts+0x60(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1018, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xc0(%rip)
dma_thunk_19:
    lock incl dma_counts+0x64(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1019, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xc8(%rip)
dma_thunk_1a:
    lock incl dma_counts+0x68(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xd0(%rip)
dma_thunk_1b:
    lock incl dma_counts+0x6c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xd8(%rip)
dma_thunk_1c:
    lock incl dma_counts+0x70(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xe0(%rip)
dma_thunk_1d:
    lock incl dma_counts+0x74(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xe8(%rip)
dma_thunk_1e:
    lock incl dma_counts+0x78(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xf0(%rip)
dma_thunk_1f:
    lock incl dma_counts+0x7c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x101f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0xf8(%rip)
dma_thunk_20:
    lock incl dma_counts+0x80(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1020, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x100(%rip)
dma_thunk_21:
    lock incl dma_counts+0x84(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1021, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x108(%rip)
dma_thunk_22:
    lock incl dma_counts+0x88(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1022, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x110(%rip)
dma_thunk_23:
    lock incl dma_counts+0x8c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1023, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x118(%rip)
dma_thunk_24:
    lock incl dma_counts+0x90(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1024, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x120(%rip)
dma_thunk_25:
    lock incl dma_counts+0x94(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1025, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x128(%rip)
dma_thunk_26:
    lock incl dma_counts+0x98(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1026, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x130(%rip)
dma_thunk_27:
    lock incl dma_counts+0x9c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1027, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x138(%rip)
dma_thunk_28:
    lock incl dma_counts+0xa0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1028, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x140(%rip)
dma_thunk_29:
    lock incl dma_counts+0xa4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1029, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x148(%rip)
dma_thunk_2a:
    lock incl dma_counts+0xa8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x150(%rip)
dma_thunk_2b:
    lock incl dma_counts+0xac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x158(%rip)
dma_thunk_2c:
    lock incl dma_counts+0xb0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x160(%rip)
dma_thunk_2d:
    lock incl dma_counts+0xb4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x168(%rip)
dma_thunk_2e:
    lock incl dma_counts+0xb8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x170(%rip)
dma_thunk_2f:
    lock incl dma_counts+0xbc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x102f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x178(%rip)
dma_thunk_30:
    lock incl dma_counts+0xc0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1030, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x180(%rip)
dma_thunk_31:
    lock incl dma_counts+0xc4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1031, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x188(%rip)
dma_thunk_32:
    lock incl dma_counts+0xc8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1032, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x190(%rip)
dma_thunk_33:
    lock incl dma_counts+0xcc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1033, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x198(%rip)
dma_thunk_34:
    lock incl dma_counts+0xd0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1034, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1a0(%rip)
dma_thunk_35:
    lock incl dma_counts+0xd4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1035, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1a8(%rip)
dma_thunk_36:
    lock incl dma_counts+0xd8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1036, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1b0(%rip)
dma_thunk_37:
    lock incl dma_counts+0xdc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1037, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1b8(%rip)
dma_thunk_38:
    lock incl dma_counts+0xe0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1038, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1c0(%rip)
dma_thunk_39:
    lock incl dma_counts+0xe4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1039, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1c8(%rip)
dma_thunk_3a:
    lock incl dma_counts+0xe8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1d0(%rip)
dma_thunk_3b:
    lock incl dma_counts+0xec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1d8(%rip)
dma_thunk_3c:
    lock incl dma_counts+0xf0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1e0(%rip)
dma_thunk_3d:
    lock incl dma_counts+0xf4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1e8(%rip)
dma_thunk_3e:
    lock incl dma_counts+0xf8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1f0(%rip)
dma_thunk_3f:
    lock incl dma_counts+0xfc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x103f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x1f8(%rip)
dma_thunk_40:
    lock incl dma_counts+0x100(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1040, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x200(%rip)
dma_thunk_41:
    lock incl dma_counts+0x104(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1041, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x208(%rip)
dma_thunk_42:
    lock incl dma_counts+0x108(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1042, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x210(%rip)
dma_thunk_43:
    lock incl dma_counts+0x10c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1043, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x218(%rip)
dma_thunk_44:
    lock incl dma_counts+0x110(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1044, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x220(%rip)
dma_thunk_45:
    lock incl dma_counts+0x114(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1045, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x228(%rip)
dma_thunk_46:
    lock incl dma_counts+0x118(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1046, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x230(%rip)
dma_thunk_47:
    lock incl dma_counts+0x11c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1047, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x238(%rip)
dma_thunk_48:
    lock incl dma_counts+0x120(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1048, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x240(%rip)
dma_thunk_49:
    lock incl dma_counts+0x124(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1049, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x248(%rip)
dma_thunk_4a:
    lock incl dma_counts+0x128(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x250(%rip)
dma_thunk_4b:
    lock incl dma_counts+0x12c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x258(%rip)
dma_thunk_4c:
    lock incl dma_counts+0x130(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x260(%rip)
dma_thunk_4d:
    lock incl dma_counts+0x134(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x268(%rip)
dma_thunk_4e:
    lock incl dma_counts+0x138(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x270(%rip)
dma_thunk_4f:
    lock incl dma_counts+0x13c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x104f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x278(%rip)
dma_thunk_50:
    lock incl dma_counts+0x140(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1050, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x280(%rip)
dma_thunk_51:
    lock incl dma_counts+0x144(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1051, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x288(%rip)
dma_thunk_52:
    lock incl dma_counts+0x148(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1052, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x290(%rip)
dma_thunk_53:
    lock incl dma_counts+0x14c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1053, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x298(%rip)
dma_thunk_54:
    lock incl dma_counts+0x150(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1054, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2a0(%rip)
dma_thunk_55:
    lock incl dma_counts+0x154(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1055, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2a8(%rip)
dma_thunk_56:
    lock incl dma_counts+0x158(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1056, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2b0(%rip)
dma_thunk_57:
    lock incl dma_counts+0x15c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1057, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2b8(%rip)
dma_thunk_58:
    lock incl dma_counts+0x160(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1058, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2c0(%rip)
dma_thunk_59:
    lock incl dma_counts+0x164(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1059, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2c8(%rip)
dma_thunk_5a:
    lock incl dma_counts+0x168(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2d0(%rip)
dma_thunk_5b:
    lock incl dma_counts+0x16c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2d8(%rip)
dma_thunk_5c:
    lock incl dma_counts+0x170(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2e0(%rip)
dma_thunk_5d:
    lock incl dma_counts+0x174(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2e8(%rip)
dma_thunk_5e:
    lock incl dma_counts+0x178(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2f0(%rip)
dma_thunk_5f:
    lock incl dma_counts+0x17c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x105f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x2f8(%rip)
dma_thunk_60:
    lock incl dma_counts+0x180(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1060, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x300(%rip)
dma_thunk_61:
    lock incl dma_counts+0x184(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1061, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x308(%rip)
dma_thunk_62:
    lock incl dma_counts+0x188(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1062, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x310(%rip)
dma_thunk_63:
    lock incl dma_counts+0x18c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1063, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x318(%rip)
dma_thunk_64:
    lock incl dma_counts+0x190(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1064, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x320(%rip)
dma_thunk_65:
    lock incl dma_counts+0x194(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1065, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x328(%rip)
dma_thunk_66:
    lock incl dma_counts+0x198(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1066, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x330(%rip)
dma_thunk_67:
    lock incl dma_counts+0x19c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1067, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x338(%rip)
dma_thunk_68:
    lock incl dma_counts+0x1a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1068, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x340(%rip)
dma_thunk_69:
    lock incl dma_counts+0x1a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1069, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x348(%rip)
dma_thunk_6a:
    lock incl dma_counts+0x1a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x350(%rip)
dma_thunk_6b:
    lock incl dma_counts+0x1ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x358(%rip)
dma_thunk_6c:
    lock incl dma_counts+0x1b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x360(%rip)
dma_thunk_6d:
    lock incl dma_counts+0x1b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x368(%rip)
dma_thunk_6e:
    lock incl dma_counts+0x1b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x370(%rip)
dma_thunk_6f:
    lock incl dma_counts+0x1bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x106f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x378(%rip)
dma_thunk_70:
    lock incl dma_counts+0x1c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1070, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x380(%rip)
dma_thunk_71:
    lock incl dma_counts+0x1c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1071, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x388(%rip)
dma_thunk_72:
    lock incl dma_counts+0x1c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1072, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x390(%rip)
dma_thunk_73:
    lock incl dma_counts+0x1cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1073, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x398(%rip)
dma_thunk_74:
    lock incl dma_counts+0x1d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1074, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3a0(%rip)
dma_thunk_75:
    lock incl dma_counts+0x1d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1075, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3a8(%rip)
dma_thunk_76:
    lock incl dma_counts+0x1d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1076, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3b0(%rip)
dma_thunk_77:
    lock incl dma_counts+0x1dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1077, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3b8(%rip)
dma_thunk_78:
    lock incl dma_counts+0x1e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1078, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3c0(%rip)
dma_thunk_79:
    lock incl dma_counts+0x1e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1079, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3c8(%rip)
dma_thunk_7a:
    lock incl dma_counts+0x1e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3d0(%rip)
dma_thunk_7b:
    lock incl dma_counts+0x1ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3d8(%rip)
dma_thunk_7c:
    lock incl dma_counts+0x1f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3e0(%rip)
dma_thunk_7d:
    lock incl dma_counts+0x1f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3e8(%rip)
dma_thunk_7e:
    lock incl dma_counts+0x1f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3f0(%rip)
dma_thunk_7f:
    lock incl dma_counts+0x1fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x107f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x3f8(%rip)
dma_thunk_80:
    lock incl dma_counts+0x200(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1080, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x400(%rip)
dma_thunk_81:
    lock incl dma_counts+0x204(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1081, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x408(%rip)
dma_thunk_82:
    lock incl dma_counts+0x208(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1082, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x410(%rip)
dma_thunk_83:
    lock incl dma_counts+0x20c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1083, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x418(%rip)
dma_thunk_84:
    lock incl dma_counts+0x210(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1084, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x420(%rip)
dma_thunk_85:
    lock incl dma_counts+0x214(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1085, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x428(%rip)
dma_thunk_86:
    lock incl dma_counts+0x218(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1086, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x430(%rip)
dma_thunk_87:
    lock incl dma_counts+0x21c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1087, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x438(%rip)
dma_thunk_88:
    lock incl dma_counts+0x220(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1088, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x440(%rip)
dma_thunk_89:
    lock incl dma_counts+0x224(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1089, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x448(%rip)
dma_thunk_8a:
    lock incl dma_counts+0x228(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x450(%rip)
dma_thunk_8b:
    lock incl dma_counts+0x22c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x458(%rip)
dma_thunk_8c:
    lock incl dma_counts+0x230(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x460(%rip)
dma_thunk_8d:
    lock incl dma_counts+0x234(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x468(%rip)
dma_thunk_8e:
    lock incl dma_counts+0x238(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x470(%rip)
dma_thunk_8f:
    lock incl dma_counts+0x23c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x108f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x478(%rip)
dma_thunk_90:
    lock incl dma_counts+0x240(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1090, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x480(%rip)
dma_thunk_91:
    lock incl dma_counts+0x244(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1091, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x488(%rip)
dma_thunk_92:
    lock incl dma_counts+0x248(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1092, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x490(%rip)
dma_thunk_93:
    lock incl dma_counts+0x24c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1093, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x498(%rip)
dma_thunk_94:
    lock incl dma_counts+0x250(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1094, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4a0(%rip)
dma_thunk_95:
    lock incl dma_counts+0x254(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1095, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4a8(%rip)
dma_thunk_96:
    lock incl dma_counts+0x258(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1096, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4b0(%rip)
dma_thunk_97:
    lock incl dma_counts+0x25c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1097, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4b8(%rip)
dma_thunk_98:
    lock incl dma_counts+0x260(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1098, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4c0(%rip)
dma_thunk_99:
    lock incl dma_counts+0x264(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1099, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4c8(%rip)
dma_thunk_9a:
    lock incl dma_counts+0x268(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4d0(%rip)
dma_thunk_9b:
    lock incl dma_counts+0x26c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4d8(%rip)
dma_thunk_9c:
    lock incl dma_counts+0x270(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4e0(%rip)
dma_thunk_9d:
    lock incl dma_counts+0x274(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4e8(%rip)
dma_thunk_9e:
    lock incl dma_counts+0x278(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109e, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4f0(%rip)
dma_thunk_9f:
    lock incl dma_counts+0x27c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x109f, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x4f8(%rip)
dma_thunk_a0:
    lock incl dma_counts+0x280(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x500(%rip)
dma_thunk_a1:
    lock incl dma_counts+0x284(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x508(%rip)
dma_thunk_a2:
    lock incl dma_counts+0x288(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x510(%rip)
dma_thunk_a3:
    lock incl dma_counts+0x28c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x518(%rip)
dma_thunk_a4:
    lock incl dma_counts+0x290(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x520(%rip)
dma_thunk_a5:
    lock incl dma_counts+0x294(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x528(%rip)
dma_thunk_a6:
    lock incl dma_counts+0x298(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x530(%rip)
dma_thunk_a7:
    lock incl dma_counts+0x29c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x538(%rip)
dma_thunk_a8:
    lock incl dma_counts+0x2a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x540(%rip)
dma_thunk_a9:
    lock incl dma_counts+0x2a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10a9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x548(%rip)
dma_thunk_aa:
    lock incl dma_counts+0x2a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10aa, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x550(%rip)
dma_thunk_ab:
    lock incl dma_counts+0x2ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ab, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x558(%rip)
dma_thunk_ac:
    lock incl dma_counts+0x2b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ac, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x560(%rip)
dma_thunk_ad:
    lock incl dma_counts+0x2b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ad, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x568(%rip)
dma_thunk_ae:
    lock incl dma_counts+0x2b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ae, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x570(%rip)
dma_thunk_af:
    lock incl dma_counts+0x2bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10af, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x578(%rip)
dma_thunk_b0:
    lock incl dma_counts+0x2c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x580(%rip)
dma_thunk_b1:
    lock incl dma_counts+0x2c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x588(%rip)
dma_thunk_b2:
    lock incl dma_counts+0x2c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x590(%rip)
dma_thunk_b3:
    lock incl dma_counts+0x2cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x598(%rip)
dma_thunk_b4:
    lock incl dma_counts+0x2d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5a0(%rip)
dma_thunk_b5:
    lock incl dma_counts+0x2d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5a8(%rip)
dma_thunk_b6:
    lock incl dma_counts+0x2d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5b0(%rip)
dma_thunk_b7:
    lock incl dma_counts+0x2dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5b8(%rip)
dma_thunk_b8:
    lock incl dma_counts+0x2e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5c0(%rip)
dma_thunk_b9:
    lock incl dma_counts+0x2e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10b9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5c8(%rip)
dma_thunk_ba:
    lock incl dma_counts+0x2e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ba, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5d0(%rip)
dma_thunk_bb:
    lock incl dma_counts+0x2ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10bb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5d8(%rip)
dma_thunk_bc:
    lock incl dma_counts+0x2f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10bc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5e0(%rip)
dma_thunk_bd:
    lock incl dma_counts+0x2f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10bd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5e8(%rip)
dma_thunk_be:
    lock incl dma_counts+0x2f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10be, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5f0(%rip)
dma_thunk_bf:
    lock incl dma_counts+0x2fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10bf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x5f8(%rip)
dma_thunk_c0:
    lock incl dma_counts+0x300(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x600(%rip)
dma_thunk_c1:
    lock incl dma_counts+0x304(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x608(%rip)
dma_thunk_c2:
    lock incl dma_counts+0x308(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x610(%rip)
dma_thunk_c3:
    lock incl dma_counts+0x30c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x618(%rip)
dma_thunk_c4:
    lock incl dma_counts+0x310(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x620(%rip)
dma_thunk_c5:
    lock incl dma_counts+0x314(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x628(%rip)
dma_thunk_c6:
    lock incl dma_counts+0x318(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x630(%rip)
dma_thunk_c7:
    lock incl dma_counts+0x31c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x638(%rip)
dma_thunk_c8:
    lock incl dma_counts+0x320(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x640(%rip)
dma_thunk_c9:
    lock incl dma_counts+0x324(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10c9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x648(%rip)
dma_thunk_ca:
    lock incl dma_counts+0x328(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ca, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x650(%rip)
dma_thunk_cb:
    lock incl dma_counts+0x32c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10cb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x658(%rip)
dma_thunk_cc:
    lock incl dma_counts+0x330(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10cc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x660(%rip)
dma_thunk_cd:
    lock incl dma_counts+0x334(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10cd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x668(%rip)
dma_thunk_ce:
    lock incl dma_counts+0x338(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ce, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x670(%rip)
dma_thunk_cf:
    lock incl dma_counts+0x33c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10cf, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x678(%rip)
dma_thunk_d0:
    lock incl dma_counts+0x340(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x680(%rip)
dma_thunk_d1:
    lock incl dma_counts+0x344(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x688(%rip)
dma_thunk_d2:
    lock incl dma_counts+0x348(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x690(%rip)
dma_thunk_d3:
    lock incl dma_counts+0x34c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x698(%rip)
dma_thunk_d4:
    lock incl dma_counts+0x350(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6a0(%rip)
dma_thunk_d5:
    lock incl dma_counts+0x354(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6a8(%rip)
dma_thunk_d6:
    lock incl dma_counts+0x358(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6b0(%rip)
dma_thunk_d7:
    lock incl dma_counts+0x35c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6b8(%rip)
dma_thunk_d8:
    lock incl dma_counts+0x360(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6c0(%rip)
dma_thunk_d9:
    lock incl dma_counts+0x364(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10d9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6c8(%rip)
dma_thunk_da:
    lock incl dma_counts+0x368(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10da, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6d0(%rip)
dma_thunk_db:
    lock incl dma_counts+0x36c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10db, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6d8(%rip)
dma_thunk_dc:
    lock incl dma_counts+0x370(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10dc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6e0(%rip)
dma_thunk_dd:
    lock incl dma_counts+0x374(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10dd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6e8(%rip)
dma_thunk_de:
    lock incl dma_counts+0x378(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10de, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6f0(%rip)
dma_thunk_df:
    lock incl dma_counts+0x37c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10df, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x6f8(%rip)
dma_thunk_e0:
    lock incl dma_counts+0x380(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x700(%rip)
dma_thunk_e1:
    lock incl dma_counts+0x384(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x708(%rip)
dma_thunk_e2:
    lock incl dma_counts+0x388(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x710(%rip)
dma_thunk_e3:
    lock incl dma_counts+0x38c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x718(%rip)
dma_thunk_e4:
    lock incl dma_counts+0x390(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x720(%rip)
dma_thunk_e5:
    lock incl dma_counts+0x394(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x728(%rip)
dma_thunk_e6:
    lock incl dma_counts+0x398(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x730(%rip)
dma_thunk_e7:
    lock incl dma_counts+0x39c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x738(%rip)
dma_thunk_e8:
    lock incl dma_counts+0x3a0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x740(%rip)
dma_thunk_e9:
    lock incl dma_counts+0x3a4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10e9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x748(%rip)
dma_thunk_ea:
    lock incl dma_counts+0x3a8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ea, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x750(%rip)
dma_thunk_eb:
    lock incl dma_counts+0x3ac(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10eb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x758(%rip)
dma_thunk_ec:
    lock incl dma_counts+0x3b0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ec, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x760(%rip)
dma_thunk_ed:
    lock incl dma_counts+0x3b4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ed, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x768(%rip)
dma_thunk_ee:
    lock incl dma_counts+0x3b8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ee, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x770(%rip)
dma_thunk_ef:
    lock incl dma_counts+0x3bc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ef, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x778(%rip)
dma_thunk_f0:
    lock incl dma_counts+0x3c0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f0, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x780(%rip)
dma_thunk_f1:
    lock incl dma_counts+0x3c4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f1, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x788(%rip)
dma_thunk_f2:
    lock incl dma_counts+0x3c8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f2, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x790(%rip)
dma_thunk_f3:
    lock incl dma_counts+0x3cc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f3, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x798(%rip)
dma_thunk_f4:
    lock incl dma_counts+0x3d0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f4, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7a0(%rip)
dma_thunk_f5:
    lock incl dma_counts+0x3d4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f5, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7a8(%rip)
dma_thunk_f6:
    lock incl dma_counts+0x3d8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f6, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7b0(%rip)
dma_thunk_f7:
    lock incl dma_counts+0x3dc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f7, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7b8(%rip)
dma_thunk_f8:
    lock incl dma_counts+0x3e0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f8, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7c0(%rip)
dma_thunk_f9:
    lock incl dma_counts+0x3e4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10f9, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7c8(%rip)
dma_thunk_fa:
    lock incl dma_counts+0x3e8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10fa, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7d0(%rip)
dma_thunk_fb:
    lock incl dma_counts+0x3ec(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10fb, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7d8(%rip)
dma_thunk_fc:
    lock incl dma_counts+0x3f0(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10fc, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7e0(%rip)
dma_thunk_fd:
    lock incl dma_counts+0x3f4(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10fd, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7e8(%rip)
dma_thunk_fe:
    lock incl dma_counts+0x3f8(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10fe, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7f0(%rip)
dma_thunk_ff:
    lock incl dma_counts+0x3fc(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x10ff, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x7f8(%rip)
dma_thunk_100:
    lock incl dma_counts+0x400(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1100, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x800(%rip)
dma_thunk_101:
    lock incl dma_counts+0x404(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1101, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x808(%rip)
dma_thunk_102:
    lock incl dma_counts+0x408(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1102, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x810(%rip)
dma_thunk_103:
    lock incl dma_counts+0x40c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1103, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x818(%rip)
dma_thunk_104:
    lock incl dma_counts+0x410(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1104, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x820(%rip)
dma_thunk_105:
    lock incl dma_counts+0x414(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1105, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x828(%rip)
dma_thunk_106:
    lock incl dma_counts+0x418(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1106, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x830(%rip)
dma_thunk_107:
    lock incl dma_counts+0x41c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1107, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x838(%rip)
dma_thunk_108:
    lock incl dma_counts+0x420(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1108, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x840(%rip)
dma_thunk_109:
    lock incl dma_counts+0x424(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x1109, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x848(%rip)
dma_thunk_10a:
    lock incl dma_counts+0x428(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x110a, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x850(%rip)
dma_thunk_10b:
    lock incl dma_counts+0x42c(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x110b, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x858(%rip)
dma_thunk_10c:
    lock incl dma_counts+0x430(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x110c, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x860(%rip)
dma_thunk_10d:
    lock incl dma_counts+0x434(%rip)
    cmpl $0, ctx_trace_on(%rip)
    je 1f
    movl $0x110d, %r10d
    leaq 1f(%rip), %r11
    jmp ctx_trace_common
1:
    jmpq *dma_orig+0x868(%rip)
    .section .rdata,"dr"
    .p2align 3
    .globl ctx_proxy_vtbl
ctx_proxy_vtbl:
    .quad ctx_thunk_0
    .quad ctx_thunk_1
    .quad ctx_thunk_2
    .quad ctx_thunk_3
    .quad ctx_thunk_4
    .quad ctx_thunk_5
    .quad ctx_thunk_6
    .quad ctx_thunk_7
    .quad ctx_thunk_8
    .quad ctx_thunk_9
    .quad ctx_thunk_a
    .quad ctx_thunk_b
    .quad ctx_thunk_c
    .quad ctx_thunk_d
    .quad ctx_thunk_e
    .quad ctx_thunk_f
    .quad ctx_thunk_10
    .quad ctx_thunk_11
    .quad ctx_thunk_12
    .quad ctx_thunk_13
    .quad ctx_thunk_14
    .quad ctx_thunk_15
    .quad ctx_thunk_16
    .quad ctx_thunk_17
    .quad ctx_thunk_18
    .quad ctx_thunk_19
    .quad ctx_thunk_1a
    .quad ctx_thunk_1b
    .quad ctx_thunk_1c
    .quad ctx_thunk_1d
    .quad ctx_thunk_1e
    .quad ctx_thunk_1f
    .quad ctx_thunk_20
    .quad ctx_thunk_21
    .quad ctx_thunk_22
    .quad ctx_thunk_23
    .quad ctx_thunk_24
    .quad ctx_thunk_25
    .quad ctx_thunk_26
    .quad ctx_thunk_27
    .quad ctx_thunk_28
    .quad ctx_thunk_29
    .quad ctx_thunk_2a
    .quad ctx_thunk_2b
    .quad ctx_thunk_2c
    .quad ctx_thunk_2d
    .quad ctx_thunk_2e
    .quad ctx_thunk_2f
    .quad ctx_thunk_30
    .quad ctx_thunk_31
    .quad ctx_thunk_32
    .quad ctx_thunk_33
    .quad ctx_thunk_34
    .quad ctx_thunk_35
    .quad ctx_thunk_36
    .quad ctx_thunk_37
    .quad ctx_thunk_38
    .quad ctx_thunk_39
    .quad ctx_thunk_3a
    .quad ctx_thunk_3b
    .quad ctx_thunk_3c
    .quad ctx_thunk_3d
    .quad ctx_thunk_3e
    .quad ctx_thunk_3f
    .quad ctx_thunk_40
    .quad ctx_thunk_41
    .quad ctx_thunk_42
    .quad ctx_thunk_43
    .quad ctx_thunk_44
    .quad ctx_thunk_45
    .quad ctx_thunk_46
    .quad ctx_thunk_47
    .quad ctx_thunk_48
    .quad ctx_thunk_49
    .quad ctx_thunk_4a
    .quad ctx_thunk_4b
    .quad ctx_thunk_4c
    .quad ctx_thunk_4d
    .quad ctx_thunk_4e
    .quad ctx_thunk_4f
    .quad ctx_thunk_50
    .quad ctx_thunk_51
    .quad ctx_thunk_52
    .quad ctx_thunk_53
    .quad ctx_thunk_54
    .quad ctx_thunk_55
    .quad ctx_thunk_56
    .quad ctx_thunk_57
    .quad ctx_thunk_58
    .quad ctx_thunk_59
    .quad ctx_thunk_5a
    .quad ctx_thunk_5b
    .quad ctx_thunk_5c
    .quad ctx_thunk_5d
    .quad ctx_thunk_5e
    .quad ctx_thunk_5f
    .quad ctx_thunk_60
    .quad ctx_thunk_61
    .quad ctx_thunk_62
    .quad ctx_thunk_63
    .quad ctx_thunk_64
    .quad ctx_thunk_65
    .quad ctx_thunk_66
    .quad ctx_thunk_67
    .quad ctx_thunk_68
    .quad ctx_thunk_69
    .quad ctx_thunk_6a
    .quad ctx_thunk_6b
    .quad ctx_thunk_6c
    .quad ctx_thunk_6d
    .quad ctx_thunk_6e
    .quad ctx_thunk_6f
    .quad ctx_thunk_70
    .quad ctx_thunk_71
    .quad ctx_thunk_72
    .quad ctx_thunk_73
    .quad ctx_thunk_74
    .quad ctx_thunk_75
    .quad ctx_thunk_76
    .quad ctx_thunk_77
    .quad ctx_thunk_78
    .quad ctx_thunk_79
    .quad ctx_thunk_7a
    .quad ctx_thunk_7b
    .quad ctx_thunk_7c
    .quad ctx_thunk_7d
    .quad ctx_thunk_7e
    .quad ctx_thunk_7f
    .quad ctx_thunk_80
    .quad ctx_thunk_81
    .quad ctx_thunk_82
    .quad ctx_thunk_83
    .quad ctx_thunk_84
    .quad ctx_thunk_85
    .quad ctx_thunk_86
    .quad ctx_thunk_87
    .quad ctx_thunk_88
    .quad ctx_thunk_89
    .quad ctx_thunk_8a
    .quad ctx_thunk_8b
    .quad ctx_thunk_8c
    .quad ctx_thunk_8d
    .quad ctx_thunk_8e
    .quad ctx_thunk_8f
    .quad ctx_thunk_90
    .quad ctx_thunk_91
    .quad ctx_thunk_92
    .quad ctx_thunk_93
    .quad ctx_thunk_94
    .quad ctx_thunk_95
    .quad ctx_thunk_96
    .quad ctx_thunk_97
    .quad ctx_thunk_98
    .quad ctx_thunk_99
    .quad ctx_thunk_9a
    .quad ctx_thunk_9b
    .quad ctx_thunk_9c
    .quad ctx_thunk_9d
    .quad ctx_thunk_9e
    .quad ctx_thunk_9f
    .quad ctx_thunk_a0
    .quad ctx_thunk_a1
    .quad ctx_thunk_a2
    .quad ctx_thunk_a3
    .quad ctx_thunk_a4
    .quad ctx_thunk_a5
    .quad ctx_thunk_a6
    .quad ctx_thunk_a7
    .quad ctx_thunk_a8
    .quad ctx_thunk_a9
    .quad ctx_thunk_aa
    .quad ctx_thunk_ab
    .quad ctx_thunk_ac
    .quad ctx_thunk_ad
    .quad ctx_thunk_ae
    .quad ctx_thunk_af
    .quad ctx_thunk_b0
    .quad ctx_thunk_b1
    .quad ctx_thunk_b2
    .quad ctx_thunk_b3
    .quad ctx_thunk_b4
    .quad ctx_thunk_b5
    .quad ctx_thunk_b6
    .quad ctx_thunk_b7
    .quad ctx_thunk_b8
    .quad ctx_thunk_b9
    .quad ctx_thunk_ba
    .quad ctx_thunk_bb
    .quad ctx_thunk_bc
    .quad ctx_thunk_bd
    .quad ctx_thunk_be
    .quad ctx_thunk_bf
    .quad ctx_thunk_c0
    .quad ctx_thunk_c1
    .quad ctx_thunk_c2
    .quad ctx_thunk_c3
    .quad ctx_thunk_c4
    .quad ctx_thunk_c5
    .quad ctx_thunk_c6
    .quad ctx_thunk_c7
    .quad ctx_thunk_c8
    .quad ctx_thunk_c9
    .quad ctx_thunk_ca
    .quad ctx_thunk_cb
    .quad ctx_thunk_cc
    .quad ctx_thunk_cd
    .quad ctx_thunk_ce
    .quad ctx_thunk_cf
    .quad ctx_thunk_d0
    .quad ctx_thunk_d1
    .quad ctx_thunk_d2
    .quad ctx_thunk_d3
    .quad ctx_thunk_d4
    .quad ctx_thunk_d5
    .quad ctx_thunk_d6
    .quad ctx_thunk_d7
    .quad ctx_thunk_d8
    .quad ctx_thunk_d9
    .quad ctx_thunk_da
    .quad ctx_thunk_db
    .quad ctx_thunk_dc
    .quad ctx_thunk_dd
    .quad ctx_thunk_de
    .quad ctx_thunk_df
    .quad ctx_thunk_e0
    .quad ctx_thunk_e1
    .quad ctx_thunk_e2
    .quad ctx_thunk_e3
    .quad ctx_thunk_e4
    .quad ctx_thunk_e5
    .quad ctx_thunk_e6
    .quad ctx_thunk_e7
    .quad ctx_thunk_e8
    .quad ctx_thunk_e9
    .quad ctx_thunk_ea
    .quad ctx_thunk_eb
    .quad ctx_thunk_ec
    .quad ctx_thunk_ed
    .quad ctx_thunk_ee
    .quad ctx_thunk_ef
    .quad ctx_thunk_f0
    .quad ctx_thunk_f1
    .quad ctx_thunk_f2
    .quad ctx_thunk_f3
    .quad ctx_thunk_f4
    .quad ctx_thunk_f5
    .quad ctx_thunk_f6
    .quad ctx_thunk_f7
    .quad ctx_thunk_f8
    .quad ctx_thunk_f9
    .quad ctx_thunk_fa
    .quad ctx_thunk_fb
    .quad ctx_thunk_fc
    .quad ctx_thunk_fd
    .quad ctx_thunk_fe
    .quad ctx_thunk_ff
    .quad ctx_thunk_100
    .quad ctx_thunk_101
    .quad ctx_thunk_102
    .quad ctx_thunk_103
    .quad ctx_thunk_104
    .quad ctx_thunk_105
    .quad ctx_thunk_106
    .quad ctx_thunk_107
    .quad ctx_thunk_108
    .quad ctx_thunk_109
    .quad ctx_thunk_10a
    .quad ctx_thunk_10b
    .quad ctx_thunk_10c
    .quad ctx_thunk_10d
    .globl ctx_thunk_kind
ctx_thunk_kind:
    .byte 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0
    .byte 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    .byte 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0
    .byte 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1
    .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2
    .p2align 3
    .globl dev_thunk_vtbl
dev_thunk_vtbl:
    .quad dev_thunk_0
    .quad dev_thunk_1
    .quad dev_thunk_2
    .quad dev_thunk_3
    .quad dev_thunk_4
    .quad dev_thunk_5
    .quad dev_thunk_6
    .quad dev_thunk_7
    .quad dev_thunk_8
    .quad dev_thunk_9
    .quad dev_thunk_a
    .quad dev_thunk_b
    .quad dev_thunk_c
    .quad dev_thunk_d
    .quad dev_thunk_e
    .quad dev_thunk_f
    .quad dev_thunk_10
    .quad dev_thunk_11
    .quad dev_thunk_12
    .quad dev_thunk_13
    .quad dev_thunk_14
    .quad dev_thunk_15
    .quad dev_thunk_16
    .quad dev_thunk_17
    .quad dev_thunk_18
    .quad dev_thunk_19
    .quad dev_thunk_1a
    .quad dev_thunk_1b
    .quad dev_thunk_1c
    .quad dev_thunk_1d
    .quad dev_thunk_1e
    .quad dev_thunk_1f
    .quad dev_thunk_20
    .quad dev_thunk_21
    .quad dev_thunk_22
    .quad dev_thunk_23
    .quad dev_thunk_24
    .quad dev_thunk_25
    .quad dev_thunk_26
    .quad dev_thunk_27
    .quad dev_thunk_28
    .quad dev_thunk_29
    .quad dev_thunk_2a
    .quad dev_thunk_2b
    .quad dev_thunk_2c
    .quad dev_thunk_2d
    .quad dev_thunk_2e
    .quad dev_thunk_2f
    .quad dev_thunk_30
    .quad dev_thunk_31
    .quad dev_thunk_32
    .quad dev_thunk_33
    .quad dev_thunk_34
    .quad dev_thunk_35
    .quad dev_thunk_36
    .quad dev_thunk_37
    .quad dev_thunk_38
    .quad dev_thunk_39
    .quad dev_thunk_3a
    .quad dev_thunk_3b
    .quad dev_thunk_3c
    .quad dev_thunk_3d
    .quad dev_thunk_3e
    .quad dev_thunk_3f
    .quad dev_thunk_40
    .quad dev_thunk_41
    .quad dev_thunk_42
    .quad dev_thunk_43
    .quad dev_thunk_44
    .quad dev_thunk_45
    .quad dev_thunk_46
    .quad dev_thunk_47
    .quad dev_thunk_48
    .quad dev_thunk_49
    .quad dev_thunk_4a
    .quad dev_thunk_4b
    .quad dev_thunk_4c
    .quad dev_thunk_4d
    .quad dev_thunk_4e
    .quad dev_thunk_4f
    .quad dev_thunk_50
    .quad dev_thunk_51
    .quad dev_thunk_52
    .quad dev_thunk_53
    .quad dev_thunk_54
    .quad dev_thunk_55
    .quad dev_thunk_56
    .quad dev_thunk_57
    .quad dev_thunk_58
    .quad dev_thunk_59
    .quad dev_thunk_5a
    .quad dev_thunk_5b
    .quad dev_thunk_5c
    .quad dev_thunk_5d
    .quad dev_thunk_5e
    .quad dev_thunk_5f
    .quad dev_thunk_60
    .quad dev_thunk_61
    .quad dev_thunk_62
    .quad dev_thunk_63
    .quad dev_thunk_64
    .quad dev_thunk_65
    .quad dev_thunk_66
    .quad dev_thunk_67
    .quad dev_thunk_68
    .quad dev_thunk_69
    .quad dev_thunk_6a
    .quad dev_thunk_6b
    .quad dev_thunk_6c
    .quad dev_thunk_6d
    .quad dev_thunk_6e
    .quad dev_thunk_6f
    .quad dev_thunk_70
    .quad dev_thunk_71
    .quad dev_thunk_72
    .quad dev_thunk_73
    .quad dev_thunk_74
    .quad dev_thunk_75
    .quad dev_thunk_76
    .quad dev_thunk_77
    .quad dev_thunk_78
    .quad dev_thunk_79
    .quad dev_thunk_7a
    .quad dev_thunk_7b
    .quad dev_thunk_7c
    .quad dev_thunk_7d
    .quad dev_thunk_7e
    .quad dev_thunk_7f
    .quad dev_thunk_80
    .quad dev_thunk_81
    .quad dev_thunk_82
    .quad dev_thunk_83
    .quad dev_thunk_84
    .quad dev_thunk_85
    .quad dev_thunk_86
    .quad dev_thunk_87
    .quad dev_thunk_88
    .quad dev_thunk_89
    .quad dev_thunk_8a
    .quad dev_thunk_8b
    .quad dev_thunk_8c
    .quad dev_thunk_8d
    .quad dev_thunk_8e
    .quad dev_thunk_8f
    .quad dev_thunk_90
    .quad dev_thunk_91
    .quad dev_thunk_92
    .quad dev_thunk_93
    .quad dev_thunk_94
    .quad dev_thunk_95
    .quad dev_thunk_96
    .quad dev_thunk_97
    .quad dev_thunk_98
    .quad dev_thunk_99
    .quad dev_thunk_9a
    .quad dev_thunk_9b
    .quad dev_thunk_9c
    .quad dev_thunk_9d
    .quad dev_thunk_9e
    .quad dev_thunk_9f
    .quad dev_thunk_a0
    .quad dev_thunk_a1
    .quad dev_thunk_a2
    .quad dev_thunk_a3
    .quad dev_thunk_a4
    .quad dev_thunk_a5
    .quad dev_thunk_a6
    .quad dev_thunk_a7
    .quad dev_thunk_a8
    .quad dev_thunk_a9
    .quad dev_thunk_aa
    .quad dev_thunk_ab
    .quad dev_thunk_ac
    .quad dev_thunk_ad
    .quad dev_thunk_ae
    .quad dev_thunk_af
    .quad dev_thunk_b0
    .quad dev_thunk_b1
    .quad dev_thunk_b2
    .quad dev_thunk_b3
    .quad dev_thunk_b4
    .quad dev_thunk_b5
    .quad dev_thunk_b6
    .quad dev_thunk_b7
    .quad dev_thunk_b8
    .quad dev_thunk_b9
    .quad dev_thunk_ba
    .quad dev_thunk_bb
    .quad dev_thunk_bc
    .quad dev_thunk_bd
    .quad dev_thunk_be
    .quad dev_thunk_bf
    .quad dev_thunk_c0
    .quad dev_thunk_c1
    .quad dev_thunk_c2
    .quad dev_thunk_c3
    .quad dev_thunk_c4
    .quad dev_thunk_c5
    .quad dev_thunk_c6
    .quad dev_thunk_c7
    .quad dev_thunk_c8
    .quad dev_thunk_c9
    .quad dev_thunk_ca
    .quad dev_thunk_cb
    .quad dev_thunk_cc
    .quad dev_thunk_cd
    .quad dev_thunk_ce
    .quad dev_thunk_cf
    .quad dev_thunk_d0
    .quad dev_thunk_d1
    .quad dev_thunk_d2
    .quad dev_thunk_d3
    .quad dev_thunk_d4
    .quad dev_thunk_d5
    .quad dev_thunk_d6
    .quad dev_thunk_d7
    .quad dev_thunk_d8
    .quad dev_thunk_d9
    .quad dev_thunk_da
    .quad dev_thunk_db
    .quad dev_thunk_dc
    .quad dev_thunk_dd
    .quad dev_thunk_de
    .quad dev_thunk_df
    .quad dev_thunk_e0
    .quad dev_thunk_e1
    .quad dev_thunk_e2
    .quad dev_thunk_e3
    .quad dev_thunk_e4
    .quad dev_thunk_e5
    .quad dev_thunk_e6
    .quad dev_thunk_e7
    .quad dev_thunk_e8
    .quad dev_thunk_e9
    .quad dev_thunk_ea
    .quad dev_thunk_eb
    .quad dev_thunk_ec
    .quad dev_thunk_ed
    .quad dev_thunk_ee
    .quad dev_thunk_ef
    .quad dev_thunk_f0
    .quad dev_thunk_f1
    .quad dev_thunk_f2
    .quad dev_thunk_f3
    .quad dev_thunk_f4
    .quad dev_thunk_f5
    .quad dev_thunk_f6
    .quad dev_thunk_f7
    .quad dev_thunk_f8
    .quad dev_thunk_f9
    .quad dev_thunk_fa
    .quad dev_thunk_fb
    .quad dev_thunk_fc
    .quad dev_thunk_fd
    .quad dev_thunk_fe
    .quad dev_thunk_ff
    .quad dev_thunk_100
    .quad dev_thunk_101
    .quad dev_thunk_102
    .quad dev_thunk_103
    .quad dev_thunk_104
    .quad dev_thunk_105
    .quad dev_thunk_106
    .quad dev_thunk_107
    .quad dev_thunk_108
    .quad dev_thunk_109
    .quad dev_thunk_10a
    .quad dev_thunk_10b
    .quad dev_thunk_10c
    .quad dev_thunk_10d
    .globl dma_thunk_vtbl
dma_thunk_vtbl:
    .quad dma_thunk_0
    .quad dma_thunk_1
    .quad dma_thunk_2
    .quad dma_thunk_3
    .quad dma_thunk_4
    .quad dma_thunk_5
    .quad dma_thunk_6
    .quad dma_thunk_7
    .quad dma_thunk_8
    .quad dma_thunk_9
    .quad dma_thunk_a
    .quad dma_thunk_b
    .quad dma_thunk_c
    .quad dma_thunk_d
    .quad dma_thunk_e
    .quad dma_thunk_f
    .quad dma_thunk_10
    .quad dma_thunk_11
    .quad dma_thunk_12
    .quad dma_thunk_13
    .quad dma_thunk_14
    .quad dma_thunk_15
    .quad dma_thunk_16
    .quad dma_thunk_17
    .quad dma_thunk_18
    .quad dma_thunk_19
    .quad dma_thunk_1a
    .quad dma_thunk_1b
    .quad dma_thunk_1c
    .quad dma_thunk_1d
    .quad dma_thunk_1e
    .quad dma_thunk_1f
    .quad dma_thunk_20
    .quad dma_thunk_21
    .quad dma_thunk_22
    .quad dma_thunk_23
    .quad dma_thunk_24
    .quad dma_thunk_25
    .quad dma_thunk_26
    .quad dma_thunk_27
    .quad dma_thunk_28
    .quad dma_thunk_29
    .quad dma_thunk_2a
    .quad dma_thunk_2b
    .quad dma_thunk_2c
    .quad dma_thunk_2d
    .quad dma_thunk_2e
    .quad dma_thunk_2f
    .quad dma_thunk_30
    .quad dma_thunk_31
    .quad dma_thunk_32
    .quad dma_thunk_33
    .quad dma_thunk_34
    .quad dma_thunk_35
    .quad dma_thunk_36
    .quad dma_thunk_37
    .quad dma_thunk_38
    .quad dma_thunk_39
    .quad dma_thunk_3a
    .quad dma_thunk_3b
    .quad dma_thunk_3c
    .quad dma_thunk_3d
    .quad dma_thunk_3e
    .quad dma_thunk_3f
    .quad dma_thunk_40
    .quad dma_thunk_41
    .quad dma_thunk_42
    .quad dma_thunk_43
    .quad dma_thunk_44
    .quad dma_thunk_45
    .quad dma_thunk_46
    .quad dma_thunk_47
    .quad dma_thunk_48
    .quad dma_thunk_49
    .quad dma_thunk_4a
    .quad dma_thunk_4b
    .quad dma_thunk_4c
    .quad dma_thunk_4d
    .quad dma_thunk_4e
    .quad dma_thunk_4f
    .quad dma_thunk_50
    .quad dma_thunk_51
    .quad dma_thunk_52
    .quad dma_thunk_53
    .quad dma_thunk_54
    .quad dma_thunk_55
    .quad dma_thunk_56
    .quad dma_thunk_57
    .quad dma_thunk_58
    .quad dma_thunk_59
    .quad dma_thunk_5a
    .quad dma_thunk_5b
    .quad dma_thunk_5c
    .quad dma_thunk_5d
    .quad dma_thunk_5e
    .quad dma_thunk_5f
    .quad dma_thunk_60
    .quad dma_thunk_61
    .quad dma_thunk_62
    .quad dma_thunk_63
    .quad dma_thunk_64
    .quad dma_thunk_65
    .quad dma_thunk_66
    .quad dma_thunk_67
    .quad dma_thunk_68
    .quad dma_thunk_69
    .quad dma_thunk_6a
    .quad dma_thunk_6b
    .quad dma_thunk_6c
    .quad dma_thunk_6d
    .quad dma_thunk_6e
    .quad dma_thunk_6f
    .quad dma_thunk_70
    .quad dma_thunk_71
    .quad dma_thunk_72
    .quad dma_thunk_73
    .quad dma_thunk_74
    .quad dma_thunk_75
    .quad dma_thunk_76
    .quad dma_thunk_77
    .quad dma_thunk_78
    .quad dma_thunk_79
    .quad dma_thunk_7a
    .quad dma_thunk_7b
    .quad dma_thunk_7c
    .quad dma_thunk_7d
    .quad dma_thunk_7e
    .quad dma_thunk_7f
    .quad dma_thunk_80
    .quad dma_thunk_81
    .quad dma_thunk_82
    .quad dma_thunk_83
    .quad dma_thunk_84
    .quad dma_thunk_85
    .quad dma_thunk_86
    .quad dma_thunk_87
    .quad dma_thunk_88
    .quad dma_thunk_89
    .quad dma_thunk_8a
    .quad dma_thunk_8b
    .quad dma_thunk_8c
    .quad dma_thunk_8d
    .quad dma_thunk_8e
    .quad dma_thunk_8f
    .quad dma_thunk_90
    .quad dma_thunk_91
    .quad dma_thunk_92
    .quad dma_thunk_93
    .quad dma_thunk_94
    .quad dma_thunk_95
    .quad dma_thunk_96
    .quad dma_thunk_97
    .quad dma_thunk_98
    .quad dma_thunk_99
    .quad dma_thunk_9a
    .quad dma_thunk_9b
    .quad dma_thunk_9c
    .quad dma_thunk_9d
    .quad dma_thunk_9e
    .quad dma_thunk_9f
    .quad dma_thunk_a0
    .quad dma_thunk_a1
    .quad dma_thunk_a2
    .quad dma_thunk_a3
    .quad dma_thunk_a4
    .quad dma_thunk_a5
    .quad dma_thunk_a6
    .quad dma_thunk_a7
    .quad dma_thunk_a8
    .quad dma_thunk_a9
    .quad dma_thunk_aa
    .quad dma_thunk_ab
    .quad dma_thunk_ac
    .quad dma_thunk_ad
    .quad dma_thunk_ae
    .quad dma_thunk_af
    .quad dma_thunk_b0
    .quad dma_thunk_b1
    .quad dma_thunk_b2
    .quad dma_thunk_b3
    .quad dma_thunk_b4
    .quad dma_thunk_b5
    .quad dma_thunk_b6
    .quad dma_thunk_b7
    .quad dma_thunk_b8
    .quad dma_thunk_b9
    .quad dma_thunk_ba
    .quad dma_thunk_bb
    .quad dma_thunk_bc
    .quad dma_thunk_bd
    .quad dma_thunk_be
    .quad dma_thunk_bf
    .quad dma_thunk_c0
    .quad dma_thunk_c1
    .quad dma_thunk_c2
    .quad dma_thunk_c3
    .quad dma_thunk_c4
    .quad dma_thunk_c5
    .quad dma_thunk_c6
    .quad dma_thunk_c7
    .quad dma_thunk_c8
    .quad dma_thunk_c9
    .quad dma_thunk_ca
    .quad dma_thunk_cb
    .quad dma_thunk_cc
    .quad dma_thunk_cd
    .quad dma_thunk_ce
    .quad dma_thunk_cf
    .quad dma_thunk_d0
    .quad dma_thunk_d1
    .quad dma_thunk_d2
    .quad dma_thunk_d3
    .quad dma_thunk_d4
    .quad dma_thunk_d5
    .quad dma_thunk_d6
    .quad dma_thunk_d7
    .quad dma_thunk_d8
    .quad dma_thunk_d9
    .quad dma_thunk_da
    .quad dma_thunk_db
    .quad dma_thunk_dc
    .quad dma_thunk_dd
    .quad dma_thunk_de
    .quad dma_thunk_df
    .quad dma_thunk_e0
    .quad dma_thunk_e1
    .quad dma_thunk_e2
    .quad dma_thunk_e3
    .quad dma_thunk_e4
    .quad dma_thunk_e5
    .quad dma_thunk_e6
    .quad dma_thunk_e7
    .quad dma_thunk_e8
    .quad dma_thunk_e9
    .quad dma_thunk_ea
    .quad dma_thunk_eb
    .quad dma_thunk_ec
    .quad dma_thunk_ed
    .quad dma_thunk_ee
    .quad dma_thunk_ef
    .quad dma_thunk_f0
    .quad dma_thunk_f1
    .quad dma_thunk_f2
    .quad dma_thunk_f3
    .quad dma_thunk_f4
    .quad dma_thunk_f5
    .quad dma_thunk_f6
    .quad dma_thunk_f7
    .quad dma_thunk_f8
    .quad dma_thunk_f9
    .quad dma_thunk_fa
    .quad dma_thunk_fb
    .quad dma_thunk_fc
    .quad dma_thunk_fd
    .quad dma_thunk_fe
    .quad dma_thunk_ff
    .quad dma_thunk_100
    .quad dma_thunk_101
    .quad dma_thunk_102
    .quad dma_thunk_103
    .quad dma_thunk_104
    .quad dma_thunk_105
    .quad dma_thunk_106
    .quad dma_thunk_107
    .quad dma_thunk_108
    .quad dma_thunk_109
    .quad dma_thunk_10a
    .quad dma_thunk_10b
    .quad dma_thunk_10c
    .quad dma_thunk_10d
