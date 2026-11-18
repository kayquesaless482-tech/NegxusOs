; NegxusOS - ponto de entrada do kernel (modo longo, paginacao com identidade 0-4GiB)
[bits 64]
default rel
extern kmain
extern __bss_start
extern __bss_end
global _start
global kstack_top

section .text.entry
_start:
    cli
    mov dx, 0x3F8
    mov al, 'S'
    out dx, al
    mov rbx, rdi                    ; bootinfo
    lea rsp, [kstack_top]
    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb
    mov dx, 0x3F8
    mov al, 'B'
    out dx, al
    mov rdi, rbx
    xor ebp, ebp
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
kstack_bottom: resb 524288
kstack_top:
