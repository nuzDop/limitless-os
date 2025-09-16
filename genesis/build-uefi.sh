#!/bin/bash
# Build script for Genesis UEFI bootloader

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Building Genesis UEFI Bootloader${NC}"

# Create build directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

# Check for required tools
check_tool() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}Error: $1 is not installed${NC}"
        exit 1
    fi
}

check_tool x86_64-w64-mingw32-gcc
check_tool objcopy

# Compile UEFI application
echo -e "${YELLOW}Compiling genesis-uefi.c...${NC}"
x86_64-w64-mingw32-gcc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fshort-wchar \
    -mno-red-zone \
    -maccumulate-outgoing-args \
    -nostdlib \
    -Wl,-dll \
    -shared \
    -Wl,--subsystem,10 \
    -Wl,--image-base,0x1000000 \
    -e efi_main \
    -o "${BUILD_DIR}/genesis-uefi.dll" \
    "${SCRIPT_DIR}/genesis-uefi.c"

# Convert to EFI binary
echo -e "${YELLOW}Converting to EFI format...${NC}"
objcopy \
    -j .text \
    -j .sdata \
    -j .data \
    -j .dynamic \
    -j .dynsym \
    -j .rel \
    -j .rela \
    -j .rel.* \
    -j .rela.* \
    -j .reloc \
    --target=efi-app-x86_64 \
    "${BUILD_DIR}/genesis-uefi.dll" \
    "${OUTPUT_DIR}/genesis-uefi.efi"

# Get file size
SIZE=$(stat -c%s "${OUTPUT_DIR}/genesis-uefi.efi")
SIZE_KB=$((SIZE / 1024))

echo -e "${GREEN}✓ Genesis UEFI bootloader built successfully${NC}"
echo -e "  Output: ${OUTPUT_DIR}/genesis-uefi.efi"
echo -e "  Size: ${SIZE_KB} KB"

# Create ESP directory structure for testing
if [ "$1" == "--create-esp" ]; then
    echo -e "${YELLOW}Creating ESP directory structure...${NC}"
    
    ESP_DIR="${OUTPUT_DIR}/esp"
    mkdir -p "${ESP_DIR}/EFI/BOOT"
    mkdir -p "${ESP_DIR}/EFI/limitless"
    
    # Copy bootloader to standard location
    cp "${OUTPUT_DIR}/genesis-uefi.efi" "${ESP_DIR}/EFI/BOOT/BOOTX64.EFI"
    cp "${OUTPUT_DIR}/genesis-uefi.efi" "${ESP_DIR}/EFI/limitless/genesis.efi"
    
    # Create placeholder kernel and initrd files
    echo "Placeholder Continuum kernel" > "${ESP_DIR}/EFI/limitless/continuum.efi"
    echo "Placeholder initrd" > "${ESP_DIR}/EFI/limitless/initrd.lfs"
    echo "Placeholder installer" > "${ESP_DIR}/EFI/limitless/installer.efi"
    echo "Placeholder installer initrd" > "${ESP_DIR}/EFI/limitless/installer-initrd.lfs"
    
    echo -e "${GREEN}✓ ESP structure created at: ${ESP_DIR}${NC}"
    echo -e "  Test with: qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -hda fat:rw:${ESP_DIR}"
fi
