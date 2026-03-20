; Minimal 16-bit BIOS boot sector.
; Prints a debug character to COM1 and on-screen, then halts.
; Keeps the file small and valid so the build pipeline succeeds.

org 0x7c00
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; set text mode 80x25
    mov ah, 0x00
    mov al, 0x03
    int 0x10
    ; Write visible error message directly to VGA text buffer at 0xB8000
    ; ES:DI -> VGA memory segment 0xB800
    mov ax, 0xB800
    mov es, ax
    xor di, di

    ; SI points to message in our data below
    mov si, boot_msg
.write_loop:
    lodsb
    cmp al, 0
    je .done
    mov ah, 0x07        ; attribute: light grey on black
    stosw               ; write AX (char + attribute) to [es:di]
    jmp .write_loop
.done:

    ; Also echo a visible char in top-left cell to be extra obvious
    mov ax, 0xB800
    mov es, ax
    mov di, 0
    mov al, 'E'
    mov ah, 0x0C        ; red on black
    stosw

halt_loop:
    cli
    hlt
    jmp halt_loop

boot_msg: db "LEGACY BOOT ERROR: see screen",0

times 510-($-$$) db 0
dw 0xAA55

