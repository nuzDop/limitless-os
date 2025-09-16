#!/bin/bash
set -e

echo "LimitlessOS Quick Build Script"
echo "=============================="

# Clean previous build
echo "Cleaning..."
make clean 2>/dev/null || true

# Build Genesis
echo "Building Genesis bootloader..."
cd genesis
make
cd ..

# Skip other components for now to test Genesis
echo "Genesis built successfully!"

# Check output
echo ""
echo "Checking output files:"
ls -la genesis/output/

# Optional: Create a test disk image
if command -v qemu-img &> /dev/null; then
    echo ""
    echo "Creating test disk image..."
    qemu-img create -f raw test.img 64M
    dd if=genesis/output/genesis-bios.bin of=test.img bs=512 count=1 conv=notrunc
    echo "Test image created: test.img"
    
    echo ""
    echo "You can test with: qemu-system-x86_64 -drive file=test.img,format=raw"
fi
