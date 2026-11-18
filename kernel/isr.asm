; NegxusOS - stubs de interrupcao (excecoes 0-31, IRQs 32-47)
[bits 64]
default rel
extern isr_dispatch
global isr_stub_table
global gdt_flush

%macro ISR_NOERR 1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro
%macro ISR_ERR 1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

section .text
%assign i 0
%rep 48
  %if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30
    ISR_ERR i
  %else
    ISR_NOERR i
  %endif
  %assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    cld
    call isr_dispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

; void gdt_flush(void *gdtr)
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    pop rax                         ; endereco de retorno
    push qword 0x08
    push rax
    retfq

global isr128
isr128:
    push qword 0
    push qword 128
    jmp isr_common

section .data
isr_stub_table:
%assign i 0
%rep 48
    dq isr %+ i
  %assign i i+1
%endrep
