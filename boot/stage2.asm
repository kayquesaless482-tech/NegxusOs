; NegxusOS - Estagio 2
;  * habilita A20, le o mapa de memoria (E820)
;  * carrega kernel+initrd (PAYLOAD_*) para 1 MiB usando "unreal mode"
;  * escolhe e ativa um modo de video VESA 32 bpp com framebuffer linear
;  * entra em modo longo (64 bits) com 4 GiB mapeados por paginas de 2 MiB e salta ao kernel
; Informacoes para o kernel em BOOTINFO (0x600).
[bits 16]
[org 0x8000]

BOOTINFO    equ 0x600
READBUF_SEG equ 0x1000
KERNEL_BASE equ 0x100000
; PAYLOAD_LBA e PAYLOAD_SECTORS vem do -D do montador

s2_entry:
    mov [bootdrv], dl
    xor ax, ax
    mov ds, ax
    mov es, ax
    cli
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov di, BOOTINFO
    mov cx, 0x500/2
    xor ax, ax
    rep stosw
    mov dword [BOOTINFO], 0x4942584E          ; 'NXBI'
    mov al, [bootdrv]
    mov [BOOTINFO+0x14], al
    call enable_a20
    call get_e820
    call load_payload
    call set_vbe
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    lgdt [cs:gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; ---------------------------------------------------------------- A20
enable_a20:
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    test al, 2
    jnz .ok
    or al, 2
    and al, 0xFE
    out 0x92, al
.ok: ret

; ---------------------------------------------------------------- E820
get_e820:
    xor ebx, ebx
    mov di, BOOTINFO+0x18
    xor bp, bp
.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [di+20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    mov eax, [di+8]
    or eax, [di+12]
    jz .skip
    inc bp
    add di, 24
    cmp bp, 48
    jae .done
.skip:
    test ebx, ebx
    jnz .loop
.done:
    movzx eax, bp
    mov [BOOTINFO+0x10], eax
    ret

; ---------------------------------------------------------------- unreal mode
enter_unreal:                       ; mantem DS/ES com limite de 4 GiB (unreal mode)
    push eax
    cli
    lgdt [cs:gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .a
.a: mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp short .b
.b: sti
    pop eax
    ret

; ---------------------------------------------------------------- carga do kernel
; Le blocos de 64 setores para 0x10000 (BIOS INT 13h) e copia para KERNEL_BASE
; com INT 15h/AH=87h (mover memoria estendida), sem trocar de modo.
load_payload:
    mov dword [cur_lba], PAYLOAD_LBA
    mov dword [remaining], PAYLOAD_SECTORS
    mov dword [dest], KERNEL_BASE
.next:
    mov eax, [remaining]
    test eax, eax
    jz .done
    cmp eax, 64
    jbe .ok
    mov eax, 64
.ok:
    mov [chunk], eax
    mov [dap2_count], ax
    mov eax, [cur_lba]
    mov [dap2_lba], eax
    xor ax, ax
    mov ds, ax
    mov si, dap2
    mov ah, 0x42
    mov dl, [bootdrv]
    int 0x13
    jc disk_error
    ; descritor de destino = dest
    mov eax, [dest]
    mov [gdt_copy + 0x18 + 2], ax
    shr eax, 16
    mov [gdt_copy + 0x18 + 4], al
    mov [gdt_copy + 0x18 + 7], ah
    xor ax, ax
    mov es, ax
    mov si, gdt_copy
    mov cx, [chunk]
    shl cx, 8                       ; sectors * 256 palavras
    mov ah, 0x87
    int 0x15
    jc copy_error
    mov eax, [chunk]
    add [cur_lba], eax
    sub [remaining], eax
    shl eax, 9
    add [dest], eax
    mov ax, 0x0E2E                  ; '.' de progresso
    mov bx, 7
    int 0x10
    jmp .next
.done: ret

copy_error:
    mov si, msg_copy
    jmp die

disk_error:
    mov si, msg_disk
    jmp die
vbe_error:
    mov si, msg_vbe
die:
    call puts
.h: hlt
    jmp .h

puts:
    lodsb
    test al, al
    jz .d
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp puts
.d: ret

; ---------------------------------------------------------------- VBE
set_vbe:
    mov di, 0x5000
    mov dword [di], 0x32454256                ; 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne vbe_error
    mov si, [0x5000+14]
    mov ax, [0x5000+16]
    mov fs, ax
    mov word [best_rank], 0xFFFF
    mov word [best_mode], 0
.nm:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je .chosen
    push si
    mov ax, 0x4F01
    mov di, 0x5200
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .nm
    mov ax, [0x5200]
    and ax, 0x0091                            ; suportado + grafico + LFB
    cmp ax, 0x0091
    jne .nm
    cmp byte [0x5200+0x19], 32
    jne .nm
    cmp byte [0x5200+0x1B], 6                 ; direct color
    jne .nm
    mov ax, [0x5200+0x12]
    mov dx, [0x5200+0x14]
    call rank_mode
    cmp bx, [best_rank]
    jae .nm
    mov [best_rank], bx
    mov [best_mode], cx
    jmp .nm
.chosen:
    mov cx, [best_mode]
    test cx, cx
    jz vbe_error
    mov ax, 0x4F01
    mov di, 0x5200
    int 0x10
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne vbe_error
    mov eax, [0x5200+0x28]
    mov [BOOTINFO+0x04], eax
    mov ax, [0x5200+0x12]
    mov [BOOTINFO+0x08], ax
    mov ax, [0x5200+0x14]
    mov [BOOTINFO+0x0A], ax
    mov ax, [0x5200+0x10]
    mov [BOOTINFO+0x0C], ax
    mov word [BOOTINFO+0x0E], 32
    ret

rank_mode:                                    ; ax=largura dx=altura -> bx=posicao
    push si
    mov si, pref_modes
    xor bx, bx
.l: cmp word [si], 0
    je .none
    cmp ax, [si]
    jne .n
    cmp dx, [si+2]
    je .f
.n: add si, 4
    inc bx
    jmp .l
.none: mov bx, 99
.f: pop si
    ret

; ---------------------------------------------------------------- modo protegido -> longo
[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000
    mov edi, 0x70000
    mov ecx, 0x6000/4
    xor eax, eax
    rep stosd
    mov dword [0x70000], 0x71003
    mov dword [0x71000], 0x72003
    mov dword [0x71008], 0x73003
    mov dword [0x71010], 0x74003
    mov dword [0x71018], 0x75003
    mov edi, 0x72000
    mov eax, 0x83                             ; presente | escrita | 2MiB
    mov ecx, 2048
.fill:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    loop .fill
    mov eax, cr4
    or eax, 0x20                              ; PAE
    mov cr4, eax
    mov eax, 0x70000
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100                             ; LME
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp 0x18:lm_entry

[bits 64]
lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov edi, BOOTINFO
    mov rax, KERNEL_BASE
    jmp rax

[bits 16]
align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF                     ; 0x08 codigo 32
    dq 0x00CF92000000FFFF                     ; 0x10 dados 32 (4 GiB)
    dq 0x00AF9A000000FFFF                     ; 0x18 codigo 64
gdt_end:
gdt_desc: dw gdt_end - gdt - 1
          dd gdt

pref_modes: dw 1280,720, 1366,768, 1024,768, 1920,1080, 800,600, 0,0

gdt_copy:
    dq 0
    dq 0
    dw 0xFFFF, 0x0000
    db 0x01, 0x93, 0x00, 0x00          ; origem: 0x10000
    dw 0xFFFF, 0x0000
    db 0x00, 0x93, 0x00, 0x00          ; destino (preenchido em tempo de execucao)
    dq 0x00009B000000FFFF
    dq 0x000093000000FFFF
msg_copy:   db 13, 10, "Erro ao copiar o kernel.", 0

dap2:       db 0x10, 0
dap2_count: dw 0
            dw 0x0000, READBUF_SEG
dap2_lba:   dq 0

bootdrv:    db 0
cur_lba:    dd 0
remaining:  dd 0
dest:       dd 0
chunk:      dd 0
best_rank:  dw 0
best_mode:  dw 0
msg_disk:   db 13, 10, "Erro de leitura do disco.", 0
msg_vbe:    db 13, 10, "Erro: modo de video VESA 32 bpp indisponivel.", 0

times (16*512) - ($-$$) db 0                  ; trava em STAGE2_SECTORS setores (erro se maior)
