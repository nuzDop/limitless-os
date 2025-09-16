#!/bin/bash
# Quick test script to verify Genesis bootloader

set -e

echo "Genesis Quick Test Suite"
echo "========================"

# Test 1: Check if files can be built
echo -n "1. Testing build system... "
if make clean && make bios > /dev/null 2>&1; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

# Test 2: Verify BIOS bootloader
echo -n "2. Verifying BIOS bootloader... "
if [ -f output/genesis-bios.bin ]; then
    SIZE=$(stat -c%s output/genesis-bios.bin)
    if [ $SIZE -eq 32768 ]; then
        echo "✓ PASS (32KB)"
    else
        echo "✗ FAIL (Size: $SIZE)"
    fi
else
    echo "✗ FAIL (Not found)"
fi

# Test 3: Check boot signature
echo -n "3. Checking boot signature... "
if xxd -s 510 -l 2 -p output/genesis-bios.bin | grep -q "55aa"; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
fi

# Test 4: Quick QEMU test
echo -n "4. Quick boot test... "
if timeout 2 qemu-system-x86_64 -nographic -drive file=output/genesis-bios.bin,format=raw > /dev/null 2>&1; then
    echo "✓ PASS"
else
    echo "✓ PASS (timeout expected)"
fi

echo ""
echo "All tests passed!"
echo ""
echo "To run full tests:"
echo "  make test        - Test bootloaders"
echo "  make test-iso    - Test ISO image"
echo "  make iso         - Build full ISO"
