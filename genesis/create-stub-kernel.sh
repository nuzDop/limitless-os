#!/bin/bash
# Creates a minimal stub kernel for testing

set -e

OUTPUT_DIR="${OUTPUT_DIR:-output}"
STUB_DIR="${STUB_DIR:-stubs}"

mkdir -p "$STUB_DIR" "$OUTPUT_DIR"

echo "Creating stub Continuum kernel..."

# Create minimal kernel assembly
cat > "$STUB_DIR/stub_kernel.asm" << 'EOF'
[BITS 64]
[ORG 0x100000]

; Continuum kernel header
kernel_header:
    dd 0x434F4E54          ; Magic: "CONT"
    dd 0x01000000          ; Version 1.0.0.0
    dq kernel_entry        ; Entry point
    dq 0x100000           ; Load address
    dq kernel_end         ; Load end address
    dq kernel_end         ; BSS end address
    dd 0x00000000         ; Flags
    dd 0x00000000         ; Checksum

kernel_entry:
    ; Set up stack
    mov rsp, 0x200000
    
    ; Save boot context
    mov [boot_context], rdi
    
    ; Clear screen
    mov rdi, 0xB8000
    mov rcx, 2000
    mov ax, 0x0F20
    rep stosw
    
    ; Print message
    mov rsi, kernel_msg
    mov rdi, 0xB8000
    call print_string
    
    ; Halt
    cli
.halt:
    hlt
    jmp .halt

print_string:
    push rax
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0F
    stosw
    jmp .loop
.done:
    pop rax
    ret

kernel_msg: db "Continuum Kernel Stub v1.0 - Boot Successful!", 0
boot_context: dq 0

align 4096
kernel_end:
EOF

# Assemble kernel
nasm -f bin "$STUB_DIR/stub_kernel.asm" -o "$OUTPUT_DIR/continuum.kernel"

# Create stub initrd
echo "Creating stub initrd..."
INITRD_DIR="$STUB_DIR/initrd"
mkdir -p "$INITRD_DIR"/{bin,dev,etc,proc,sys,tmp}

# Create init script
cat > "$INITRD_DIR/init" << 'EOF'
#!/bin/sh
echo "LimitlessOS InitRD Stub"
exec /bin/sh
EOF
chmod +x "$INITRD_DIR/init"

# Create minimal busybox placeholder
touch "$INITRD_DIR/bin/sh"
chmod +x "$INITRD_DIR/bin/sh"

# Pack initrd
(cd "$INITRD_DIR" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$OUTPUT_DIR/initrd.lfs"

echo "✓ Stub kernel and initrd created"
