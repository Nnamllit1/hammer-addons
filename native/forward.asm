; Preserve the Windows x64 argument registers, resolve the target, then tail-jump.
; Restoring RSP leaves stack arguments and the original return address untouched.
; Each procedure has unwind metadata for the resolver call.
EXTERN HAProxyResolve:PROC
.code
FORWARD MACRO symbol, index
symbol PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, index
    call HAProxyResolve
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    ; Indirect memory jump is a recognized Windows x64 unwind epilog.
    jmp qword ptr [rax]
symbol ENDP
ENDM
FORWARD HAForward0, 0
FORWARD HAForward2, 2
FORWARD HAForward3, 3
FORWARD HAForward4, 4
FORWARD HAForward5, 5
END
