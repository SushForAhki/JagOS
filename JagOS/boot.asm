; =============================================================================
; JagOs - boot.asm
; 16-bit Real Mode Bootloader
; Loads kernel from disk, enables A20, sets up GDT, enters 32-bit Protected Mode
; Assembled with: nasm -f bin boot.asm -o boot.bin
; =============================================================================

BITS 16
ORG 0x7C00

; Memory layout:
;   0x7C00 - Bootloader (this code)
;   0x1000 - Kernel load address (segments set accordingly)
;   Stack grows down from 0x7C00

KERNEL_OFFSET   equ 0x1000     ; Where we load the kernel
KERNEL_SECTORS  equ 64         ; How many sectors to load (32KB)

; ===========================================================================
; Entry Point
; ===========================================================================
_start:
    ; Disable interrupts, set up segments
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; Stack below bootloader

    ; Save boot drive number
    mov [boot_drive], dl

    ; Print boot message
    mov si, msg_booting
    call print_string_16

    ; Enable A20 Line (keyboard controller method)
    call enable_a20

    ; Load kernel from disk
    call load_kernel

    ; Set up GDT
    lgdt [gdt_descriptor]

    ; Enter Protected Mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump to flush pipeline and load CS with GDT selector
    jmp CODE_SEG:protected_mode_start

; ===========================================================================
; 16-bit Subroutines
; ===========================================================================

; Print null-terminated string (SI = string address)
print_string_16:
    pusha
    mov ah, 0x0E                ; BIOS teletype output
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Enable A20 via keyboard controller
enable_a20:
    ; Fast A20 method first (port 0x92)
    in  al, 0x92
    or  al, 2
    out 0x92, al

    ; Also try keyboard controller method for compatibility
    call a20_wait_input
    mov al, 0xAD                ; Disable keyboard
    out 0x64, al

    call a20_wait_input
    mov al, 0xD0                ; Read output port
    out 0x64, al

    call a20_wait_output
    in  al, 0x60
    push ax

    call a20_wait_input
    mov al, 0xD1                ; Write output port
    out 0x64, al

    call a20_wait_input
    pop ax
    or  al, 2                   ; Set A20 bit
    out 0x60, al

    call a20_wait_input
    mov al, 0xAE                ; Enable keyboard
    out 0x64, al

    call a20_wait_input
    ret

a20_wait_input:
    in  al, 0x64
    test al, 2
    jnz a20_wait_input
    ret

a20_wait_output:
    in  al, 0x64
    test al, 1
    jz  a20_wait_output
    ret

; Load kernel sectors from disk using BIOS INT 13h
load_kernel:
    mov si, msg_loading
    call print_string_16

    mov bx, KERNEL_OFFSET       ; ES:BX = destination
    mov dh, 0                   ; Head 0
    mov dl, [boot_drive]        ; Drive number

    ; Read sectors: CHS addressing
    mov ah, 0x02                ; BIOS read sectors
    mov al, KERNEL_SECTORS      ; Sector count
    mov ch, 0                   ; Cylinder 0
    mov cl, 2                   ; Start from sector 2 (sector 1 is boot)

    int 0x13
    jc  disk_error

    ; Verify we loaded something
    cmp al, KERNEL_SECTORS
    jne sector_error

    mov si, msg_loaded
    call print_string_16
    ret

disk_error:
    mov si, msg_disk_err
    call print_string_16
    jmp halt_16

sector_error:
    mov si, msg_sec_err
    call print_string_16
    jmp halt_16

halt_16:
    hlt
    jmp halt_16

; ===========================================================================
; Data (16-bit section)
; ===========================================================================
msg_booting  db 'JagOs Bootloader v1.0', 0x0D, 0x0A, 0
msg_loading  db 'Loading kernel...', 0x0D, 0x0A, 0
msg_loaded   db 'Kernel loaded OK', 0x0D, 0x0A, 0
msg_disk_err db 'DISK ERROR!', 0x0D, 0x0A, 0
msg_sec_err  db 'SECTOR COUNT ERROR!', 0x0D, 0x0A, 0
boot_drive   db 0

; ===========================================================================
; GDT - Global Descriptor Table
; ===========================================================================
gdt_start:

; Null descriptor (required)
gdt_null:
    dd 0x00000000
    dd 0x00000000

; Code segment descriptor: base=0, limit=4GB, 32-bit, ring 0, executable
gdt_code:
    dw 0xFFFF           ; Limit (bits 0-15)
    dw 0x0000           ; Base  (bits 0-15)
    db 0x00             ; Base  (bits 16-23)
    db 10011010b        ; Access: Present, Ring0, Code, Executable, Readable
    db 11001111b        ; Flags: 4KB gran, 32-bit | Limit (bits 16-19)
    db 0x00             ; Base  (bits 24-31)

; Data segment descriptor: base=0, limit=4GB, 32-bit, ring 0, writable
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b        ; Access: Present, Ring0, Data, Writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT size - 1
    dd gdt_start                 ; GDT address

; Segment selectors (byte offset into GDT)
CODE_SEG equ gdt_code - gdt_start  ; = 0x08
DATA_SEG equ gdt_data - gdt_start  ; = 0x10

; ===========================================================================
; 32-bit Protected Mode Entry
; ===========================================================================
BITS 32
protected_mode_start:
    ; Set up all data segments to GDT data selector
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Set up a new stack in protected mode
    mov esp, 0x90000

    ; Jump to kernel entry point
    call KERNEL_OFFSET

    ; Should never return, but halt if it does
    cli
.hang:
    hlt
    jmp .hang

; ===========================================================================
; Boot Signature - MUST be at offset 510/511
; ===========================================================================
times 510-($-$$) db 0
dw 0xAA55
