#!/bin/bash

echo "Fixing Genesis UEFI build..."

# Install dependencies
sudo apt-get update
sudo apt-get install -y gnu-efi build-essential

# Find GNU-EFI files
echo "Locating GNU-EFI files..."
CRT0=$(find /usr -name "crt0-efi-x86_64.o" 2>/dev/null | head -1)
LDSCRIPT=$(find /usr -name "elf_x86_64_efi.lds" 2>/dev/null | head -1)

if [ -z "$CRT0" ] || [ -z "$LDSCRIPT" ]; then
    echo "GNU-EFI not found properly. Building from source..."
    
    # Build GNU-EFI from source
    cd /tmp
    rm -rf gnu-efi
    git clone https://git.code.sf.net/p/gnu-efi/code gnu-efi
    cd gnu-efi
    make
    
    # Use local build
    CRT0="/tmp/gnu-efi/x86_64/gnuefi/crt0-efi-x86_64.o"
    LDSCRIPT="/tmp/gnu-efi/gnuefi/elf_x86_64_efi.lds"
    
    # Copy to project
    mkdir -p ~/Limitless/genesis/efi
    cp -r /tmp/gnu-efi/inc ~/Limitless/genesis/efi/
    cp -r /tmp/gnu-efi/x86_64 ~/Limitless/genesis/efi/
    cp $CRT0 ~/Limitless/genesis/efi/
    cp $LDSCRIPT ~/Limitless/genesis/efi/
fi

echo "Found:"
echo "  CRT0: $CRT0"
echo "  LDSCRIPT: $LDSCRIPT"

# Create a simple UEFI app to test
cat > test-uefi.c << 'EOF'
#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Hello from UEFI!\n");
    return EFI_SUCCESS;
}
EOF

# Try to compile
echo "Testing UEFI compilation..."
gcc -c -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
    -I/usr/include/efi -I/usr/include/efi/x86_64 \
    -DEFI_FUNCTION_WRAPPER -o test-uefi.o test-uefi.c

ld -nostdlib -znocombreloc -T $LDSCRIPT -shared -Bsymbolic \
   -L/usr/lib -L/usr/lib64 $CRT0 test-uefi.o \
   -o test-uefi.so -lefi -lgnuefi

if [ $? -eq 0 ]; then
    echo "UEFI compilation successful!"
    objcopy -j .text -j .sdata -j .data -j .dynamic \
            -j .dynsym -j .rel -j .rela -j .reloc \
            --target=efi-app-x86_64 test-uefi.so test-uefi.efi
    echo "Created test-uefi.efi"
else
    echo "UEFI compilation failed. Trying alternative method..."
fi

# Clean up
rm -f test-uefi.*
