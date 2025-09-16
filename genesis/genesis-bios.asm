; =============================================================================
; Genesis BIOS Bootloader for LimitlessOS
; =============================================================================

BITS 16         ; We start in 16-bit real mode
ORG 0x7C00      ; BIOS loads us at this address

; Entry point
start:
    ; Initialize segments
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    
    ; Save boot drive
    mov [boot_drive], dl
    
    ; Print boot message
    mov si, boot_msg
    call print_string
    
    ; TODO: Load second stage
    mov si, load_msg
    call print_string
    
    ; Halt
    cli
.halt:
    hlt
    jmp .halt

; Print null-terminated string
; Input: SI = string pointer
print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Data section
boot_msg:   db 'Genesis BIOS Bootloader v1.0', 13, 10, 0
load_msg:   db 'Loading next stage...', 13, 10, 0
boot_drive: db 0

; Boot sector padding and signature
times 510 - ($ - $$) db 0
dw 0xAA55
