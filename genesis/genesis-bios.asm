#!/bin/bash
# build-genesis-bios.sh

echo "Building Genesis BIOS Bootloader..."

# Assemble the bootloader
nasm -f bin genesis-bios.asm -o genesis-bios.bin

# Verify size constraints
BOOT_SIZE=$(stat -c%s genesis-bios.bin)
if [ $BOOT_SIZE -ne 32768 ]; then
    echo "ERROR: Bootloader must be exactly 32KB (is $BOOT_SIZE bytes)"
    exit 1
fi

# Extract first sector for validation
dd if=genesis-bios.bin of=boot-sector.bin bs=512 count=1 2>/dev/null

# Check boot signature
SIGNATURE=$(xxd -s 510 -l 2 -p boot-sector.bin)
if [ "$SIGNATURE" != "55aa" ]; then
    echo "ERROR: Invalid boot signature"
    exit 1
fi

echo "✓ Genesis BIOS bootloader built successfully"
echo "  Size: 32KB"
echo "  Boot signature: Valid"
echo "  Output: genesis-bios.bin"

# Optional: Create test disk image
if [ "$1" == "--test" ]; then
    echo "Creating test disk image..."
    
    # Create 100MB disk image
    dd if=/dev/zero of=test-disk.img bs=1M count=100 2>/dev/null
    
    # Write bootloader
    dd if=genesis-bios.bin of=test-disk.img conv=notrunc 2>/dev/null
    
    # Add placeholder kernel and initrd
    # (In real build, these would be actual compiled binaries)
    dd if=/dev/zero of=test-disk.img bs=512 seek=65 count=512 conv=notrunc 2>/dev/null
    dd if=/dev/zero of=test-disk.img bs=512 seek=578 count=8192 conv=notrunc 2>/dev/null
    
    echo "✓ Test disk image created: test-disk.img"
    echo "  Test with: qemu-system-x86_64 -hda test-disk.img"
fi
