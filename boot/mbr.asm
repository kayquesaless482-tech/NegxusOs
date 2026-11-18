; NegxusOS - Estagio 1 (setor de boot, 512 bytes)
; Carrega o estagio 2 (setores 1..STAGE2_SECTORS) em 0x8000 via BIOS INT 13h estendido.
[bits 16]
[org 0x7C00]
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [drive], dl
    mov si, msg_boot
    call puts
    mov ah, 0x41                ; extensoes LBA disponiveis?
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc .nolba
    cmp bx, 0xAA55
    jne .nolba
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc .diskerr
    mov dl, [drive]
    jmp 0x0000:0x8000
.nolba:
    mov si, msg_nolba
    jmp .die
.diskerr:
    mov si, msg_disk
.die:
    call puts
.hang:
    hlt
    jmp .hang

puts:
    lodsb
    test al, al
    jz .d
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp puts
.d: ret

dap:    db 0x10, 0
        dw STAGE2_SECTORS
        dw 0x8000, 0x0000
        dq 1
drive:  db 0
msg_boot:  db "NegxusOS...", 0
msg_nolba: db 13, 10, "Erro: BIOS sem suporte a LBA.", 0
msg_disk:  db 13, 10, "Erro de leitura do disco.", 0

times 446-($-$$) db 0
times 64 db 0                    ; tabela de particoes (preenchida pelo mkimage)
dw 0xAA55
