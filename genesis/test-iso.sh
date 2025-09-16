#!/bin/bash
# Test script for Genesis Hybrid ISO

ISO_PATH="output/limitless-genesis-hybrid.iso"

if [ ! -f "$ISO_PATH" ]; then
    echo "Error: ISO not found at $ISO_PATH"
    echo "Run 'make iso' first"
    exit 1
fi

echo "Testing LimitlessOS Genesis Hybrid ISO"
echo "======================================"

# Test 1: BIOS Boot
echo "1. Testing BIOS boot..."
timeout 30 qemu-system-x86_64 \
    -m 2048 \
    -cdrom "$ISO_PATH" \
    -display none \
    -serial stdio \
    -boot d &
BIOS_PID=$!

sleep 5
if ps -p $BIOS_PID > /dev/null; then
    echo "✓ BIOS boot appears successful"
    kill $BIOS_PID 2>/dev/null
else
    echo "✗ BIOS boot may have failed"
fi

# Test 2: UEFI Boot
echo "2. Testing UEFI boot..."
if [ -f "/usr/share/ovmf/OVMF.fd" ]; then
    timeout 30 qemu-system-x86_64 \
        -m 2048 \
        -bios /usr/share/ovmf/OVMF.fd \
        -cdrom "$ISO_PATH" \
        -display none \
        -serial stdio \
        -boot d &
    UEFI_PID=$!
    
    sleep 5
    if ps -p $UEFI_PID > /dev/null; then
        echo "✓ UEFI boot appears successful"
        kill $UEFI_PID 2>/dev/null
    else
        echo "✗ UEFI boot may have failed"
    fi
else
    echo "⚠ OVMF not found, skipping UEFI test"
fi

# Test 3: ISO Structure
echo "3. Checking ISO structure..."
EXPECTED_FILES=(
    "/ISOLINUX"
    "/EFI/BOOT/BOOTX64.EFI"
    "/LIMITLESS/KERNEL"
    "/LIMITLESS/INITRD"
)

for file in "${EXPECTED_FILES[@]}"; do
    if isoinfo -R -i "$ISO_PATH" -f 2>/dev/null | grep -qi "$file"; then
        echo "✓ Found: $file"
    else
        echo "✗ Missing: $file"
    fi
done

# Test 4: Bootability
echo "4. Checking bootability..."
if file "$ISO_PATH" | grep -q "bootable"; then
    echo "✓ ISO is marked as bootable"
else
    echo "✗ ISO is not marked as bootable"
fi

# Test 5: Size check
ISO_SIZE=$(stat -c%s "$ISO_PATH")
ISO_SIZE_MB=$((ISO_SIZE / 1024 / 1024))
echo "5. ISO size: ${ISO_SIZE_MB} MB"

if [ $ISO_SIZE_MB -lt 10 ]; then
    echo "⚠ ISO seems too small, may be missing components"
elif [ $ISO_SIZE_MB -gt 1000 ]; then
    echo "⚠ ISO seems very large"
else
    echo "✓ ISO size appears reasonable"
fi

echo ""
echo "Test Summary"
echo "============"
echo "ISO Path: $ISO_PATH"
echo "Size: ${ISO_SIZE_MB} MB"
echo "Type: Hybrid (BIOS + UEFI)"
echo ""
echo "To perform manual testing:"
echo "  BIOS: qemu-system-x86_64 -m 2048 -cdrom $ISO_PATH"
echo "  UEFI: qemu-system-x86_64 -m 2048 -bios /usr/share/ovmf/OVMF.fd -cdrom $ISO_PATH"
