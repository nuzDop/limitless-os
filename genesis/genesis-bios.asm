; =============================================================================
; Genesis BIOS Bootloader for LimitlessOS
;
; FILENAME:      genesis-bios.asm
; ARCHITECTURE:  x86 (16-bit Real Mode)
; PURPOSE:       Stage 1 bootloader for legacy BIOS systems.
; AUTHOR:        nuzDop
; VERSION:       1.1.0
; LAST MODIFIED: 2025-09-15
;
; DESCRIPTION:
; This bootloader is loaded by the BIOS at address 0x7C00. Its primary
; responsibilities are:
;   1. Set up a basic execution environment (stack, segment registers).
;   2. Print a boot confirmation message to the screen.
;   3. Load the next stage of the boot process (the kernel loader).
;   4. Halt the CPU safely if the next stage is not found or fails.
;
; This code adheres to robust practices, including clear labeling,
; explicit segment setup, and a defined data section.
; =============================================================================

[ORG 0x7C00]    ; The BIOS loads the boot sector at this memory address.
[BITS 16]       ; We start in 16-bit real mode.

SECTION .text
    ; --- Entry Point ---
    jmp short start

    ; --- BIOS Parameter Block (optional, but good practice) ---
    times 3 - ($ - $$) db 0   ; Padding to offset 3
    db "GENESIS "              ; OEM Identifier
    times 50 db 0              ; Reserve space for a BPB if needed later

start:
    ; --- Stage 1: Initialize Execution Environment ---
    ; Set up segment registers to a known state. We use a flat memory model
    ; where all segments point to the same base address (0x0000).
    cli                     ; Disable interrupts during setup
    xor ax, ax              ; AX = 0
    mov ds, ax              ; Data Segment
    mov es, ax              ; Extra Segment
    mov ss, ax              ; Stack Segment

    ; Set up the stack. We place it right below our own code at 0x7C00.
    mov sp, 0x7C00          ; Stack pointer grows downwards from here.
    sti                     ; Re-enable interrupts

    ; --- Stage 2: Display Boot Message ---
    ; Use BIOS interrupt 0x10 to print a string to the screen.
    mov si, boot_msg        ; SI points to the message string
    call print_string

    ; --- Stage 3: Load Next Stage (Placeholder) ---
    ; In a real OS, this section would load the kernel loader from the disk.
    ; For now, it's a placeholder.
    ; TODO: Implement disk read and jump to kernel loader.
    mov si, load_msg
    call print_string

    ; --- Stage 4: Halt ---
    ; If the next stage is not found or fails to execute, we enter an
    ; infinite loop to halt the system safely.
    cli                     ; Disable interrupts
.halt:
    hlt                     ; Halt the CPU to save power
    jmp .halt               ; Loop indefinitely

; --- Subroutine: print_string ---
; Prints a null-terminated string to the screen using BIOS teletype output.
; Input: SI = Address of the string
print_string:
    mov ah, 0x0E            ; BIOS teletype function
.loop:
    lodsb                   ; Load character from SI into AL, advance SI
    test al, al             ; Check if the character is null
    jz .done                ; If so, we are done
    int 0x10                ; Otherwise, call BIOS video interrupt
    jmp .loop
.done:
    ret

SECTION .data
    boot_msg db 'Genesis BIOS Bootloader Initialized.', 0x0D, 0x0A, 0
    load_msg db 'Next stage not found. System halted.', 0x0D, 0x0A, 0

SECTION .bootsector
    ; --- Boot Sector Signature ---
    ; The BIOS requires the last two bytes of the 512-byte boot sector
    ; to be 0x55AA to consider it bootable.
    times 510 - ($ - $$) db 0 ; Pad the rest of the sector with zeros
    dw 0xAA55               ; Boot signature
