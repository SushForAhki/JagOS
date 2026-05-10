BITS 16
ORG 0x7C00

KERNEL_LOAD_SEG equ 0x0100
KERNEL_SECTORS  equ 48

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov si, msg_load
    call print16

    ; Reset disk system first
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13

    ; Load kernel to 0x1000 (seg=0x100, off=0x0000)
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0           ; Cylinder 0
    mov cl, 2           ; Start from sector 2 (after boot sector)
    mov dh, 0           ; Head 0
    mov dl, [boot_drive]
    int 0x13
    jc  disk_err

    mov si, msg_ok
    call print16

    ; A20 fast gate
    in  al, 0x92
    or  al, 2
    out 0x92, al

    lgdt [gdt_desc]

    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:pm_entry

print16:
    pusha
    mov ah, 0x0E
.lp:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .lp
.done:
    popa
    ret

disk_err:
    mov si, msg_err
    call print16
.hang:
    hlt
    jmp .hang

msg_load    db 'JagOs Loading...', 13, 10, 0
msg_ok      db 'Kernel loaded! Entering PM...', 13, 10, 0
msg_err     db 'DISK ERROR!', 0
boot_drive  db 0

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000
    jmp 0x08:0x1000

times 510-($-$$) db 0
dw 0xAA55
