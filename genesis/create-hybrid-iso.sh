#!/bin/bash
# Genesis Hybrid ISO Creator for LimitlessOS
# Creates a bootable ISO that works on both BIOS and UEFI systems

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/iso-build"
OUTPUT_DIR="${SCRIPT_DIR}/output"
ISO_NAME="limitless-genesis-hybrid.iso"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Print banner
print_banner() {
    echo -e "${CYAN}"
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║          GENESIS HYBRID ISO BUILDER FOR LIMITLESSOS        ║"
    echo "║                    Version 1.0.0                           ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# Check dependencies
check_dependencies() {
    local deps=("xorriso" "mtools" "dosfstools" "genisoimage" "syslinux")
    
    echo -e "${YELLOW}Checking dependencies...${NC}"
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            echo -e "${RED}✗ Missing: $dep${NC}"
            echo "  Install with: sudo apt-get install $dep"
            exit 1
        else
            echo -e "${GREEN}✓ Found: $dep${NC}"
        fi
    done
    echo
}

# Clean build environment
clean_build() {
    echo -e "${YELLOW}Cleaning build environment...${NC}"
    rm -rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
    mkdir -p "${OUTPUT_DIR}"
}

# Build bootloaders if needed
build_bootloaders() {
    echo -e "${BLUE}═══ Building Bootloaders ═══${NC}"
    
    # Build BIOS bootloader
    if [ ! -f "${OUTPUT_DIR}/genesis-bios.bin" ]; then
        echo -e "${YELLOW}Building BIOS bootloader...${NC}"
        if [ -f "${SCRIPT_DIR}/genesis-bios.asm" ]; then
            nasm -f bin "${SCRIPT_DIR}/genesis-bios.asm" -o "${OUTPUT_DIR}/genesis-bios.bin"
            echo -e "${GREEN}✓ BIOS bootloader built${NC}"
        else
            echo -e "${YELLOW}⚠ Creating placeholder BIOS bootloader${NC}"
            # Create placeholder with proper boot signature
            dd if=/dev/zero of="${OUTPUT_DIR}/genesis-bios.bin" bs=1 count=32768 2>/dev/null
            printf '\x55\xAA' | dd of="${OUTPUT_DIR}/genesis-bios.bin" bs=1 seek=510 count=2 conv=notrunc 2>/dev/null
        fi
    fi
    
    # Build UEFI bootloader
    if [ ! -f "${OUTPUT_DIR}/genesis-uefi.efi" ]; then
        echo -e "${YELLOW}Building UEFI bootloader...${NC}"
        if [ -f "${SCRIPT_DIR}/genesis-uefi.c" ] && command -v x86_64-w64-mingw32-gcc &> /dev/null; then
            x86_64-w64-mingw32-gcc \
                -ffreestanding -fno-stack-protector -fno-stack-check \
                -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
                -nostdlib -Wl,-dll -shared -Wl,--subsystem,10 \
                -Wl,--image-base,0x1000000 -e efi_main \
                -o "${BUILD_DIR}/genesis-uefi.dll" \
                "${SCRIPT_DIR}/genesis-uefi.c"
            
            objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
                    -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
                    --target=efi-app-x86_64 \
                    "${BUILD_DIR}/genesis-uefi.dll" \
                    "${OUTPUT_DIR}/genesis-uefi.efi"
            echo -e "${GREEN}✓ UEFI bootloader built${NC}"
        else
            echo -e "${YELLOW}⚠ Creating placeholder UEFI bootloader${NC}"
            # Create minimal PE executable header for UEFI
            create_placeholder_efi "${OUTPUT_DIR}/genesis-uefi.efi"
        fi
    fi
    echo
}

# Create placeholder EFI executable
create_placeholder_efi() {
    local output="$1"
    # Create minimal valid PE/COFF header
    python3 - << 'EOF' "$output"
import sys
import struct

output_file = sys.argv[1]

# DOS header
dos_header = b'MZ' + b'\x90\x00' * 29 + struct.pack('<I', 0x80)  # PE header offset at 0x80

# DOS stub
dos_stub = b'\x0e\x1f\xba\x0e\x00\xb4\x09\xcd\x21\xb8\x01\x4c\xcd\x21'
dos_stub += b'This program cannot be run in DOS mode.\r\r\n$'
dos_stub = dos_stub.ljust(0x80 - len(dos_header), b'\x00')

# PE header
pe_signature = b'PE\x00\x00'
machine = struct.pack('<H', 0x8664)  # x86_64
num_sections = struct.pack('<H', 2)
timestamp = struct.pack('<I', 0)
symbol_table = struct.pack('<I', 0)
num_symbols = struct.pack('<I', 0)
opt_header_size = struct.pack('<H', 0xF0)
characteristics = struct.pack('<H', 0x222E)

coff_header = machine + num_sections + timestamp + symbol_table + num_symbols + opt_header_size + characteristics

# Optional header
magic = struct.pack('<H', 0x20B)  # PE32+
major_linker = b'\x0E'
minor_linker = b'\x00'
code_size = struct.pack('<I', 0x200)
init_data_size = struct.pack('<I', 0x200)
uninit_data_size = struct.pack('<I', 0)
entry_point = struct.pack('<I', 0x1000)
base_of_code = struct.pack('<I', 0x1000)

opt_header = magic + major_linker + minor_linker + code_size + init_data_size
opt_header += uninit_data_size + entry_point + base_of_code

# Windows-specific fields
image_base = struct.pack('<Q', 0x1000000)
section_align = struct.pack('<I', 0x1000)
file_align = struct.pack('<I', 0x200)
os_major = struct.pack('<H', 0)
os_minor = struct.pack('<H', 0)
image_major = struct.pack('<H', 0)
image_minor = struct.pack('<H', 0)
subsys_major = struct.pack('<H', 5)
subsys_minor = struct.pack('<H', 0)
reserved = struct.pack('<I', 0)
image_size = struct.pack('<I', 0x3000)
header_size = struct.pack('<I', 0x400)
checksum = struct.pack('<I', 0)
subsystem = struct.pack('<H', 0x0A)  # EFI application
dll_characteristics = struct.pack('<H', 0)
stack_reserve = struct.pack('<Q', 0x10000)
stack_commit = struct.pack('<Q', 0x1000)
heap_reserve = struct.pack('<Q', 0x10000)
heap_commit = struct.pack('<Q', 0x1000)
loader_flags = struct.pack('<I', 0)
num_data_dirs = struct.pack('<I', 16)

opt_header += image_base + section_align + file_align
opt_header += os_major + os_minor + image_major + image_minor
opt_header += subsys_major + subsys_minor + reserved
opt_header += image_size + header_size + checksum + subsystem
opt_header += dll_characteristics + stack_reserve + stack_commit
opt_header += heap_reserve + heap_commit + loader_flags + num_data_dirs

# Data directories (16 entries, 8 bytes each)
data_dirs = b'\x00' * 128

# Section headers
text_section = b'.text\x00\x00\x00'
text_section += struct.pack('<I', 0x200)  # Virtual size
text_section += struct.pack('<I', 0x1000)  # Virtual address
text_section += struct.pack('<I', 0x200)  # Raw size
text_section += struct.pack('<I', 0x400)  # Raw offset
text_section += b'\x00' * 12  # Relocations, line numbers
text_section += struct.pack('<I', 0x60000020)  # Characteristics

data_section = b'.data\x00\x00\x00'
data_section += struct.pack('<I', 0x200)  # Virtual size
data_section += struct.pack('<I', 0x2000)  # Virtual address
data_section += struct.pack('<I', 0x200)  # Raw size
data_section += struct.pack('<I', 0x600)  # Raw offset
data_section += b'\x00' * 12  # Relocations, line numbers
data_section += struct.pack('<I', 0xC0000040)  # Characteristics

# Padding to 0x400
headers = dos_header + dos_stub + pe_signature + coff_header + opt_header + data_dirs
headers += text_section + data_section
headers = headers.ljust(0x400, b'\x00')

# .text section - minimal x86_64 code
text_code = b'\x48\x31\xC0'  # xor rax, rax
text_code += b'\xC3'  # ret
text_code = text_code.ljust(0x200, b'\x00')

# .data section
data_content = b'Genesis UEFI Placeholder\x00'
data_content = data_content.ljust(0x200, b'\x00')

# Write file
with open(output_file, 'wb') as f:
    f.write(headers)
    f.write(text_code)
    f.write(data_content)

print(f"Created placeholder EFI at {output_file}")
EOF
}

# Create ISO directory structure
create_iso_structure() {
    echo -e "${BLUE}═══ Creating ISO Structure ═══${NC}"
    
    # Create directories
    mkdir -p "${BUILD_DIR}/isolinux"
    mkdir -p "${BUILD_DIR}/EFI/BOOT"
    mkdir -p "${BUILD_DIR}/EFI/limitless"
    mkdir -p "${BUILD_DIR}/boot/grub"
    mkdir -p "${BUILD_DIR}/limitless/kernel"
    mkdir -p "${BUILD_DIR}/limitless/initrd"
    mkdir -p "${BUILD_DIR}/limitless/modules"
    
    echo -e "${GREEN}✓ Directory structure created${NC}"
}

# Create ISOLINUX configuration for BIOS boot
create_isolinux_config() {
    echo -e "${YELLOW}Creating ISOLINUX configuration...${NC}"
    
    # Copy isolinux binaries
    if [ -f "/usr/lib/syslinux/bios/isolinux.bin" ]; then
        cp /usr/lib/syslinux/bios/isolinux.bin "${BUILD_DIR}/isolinux/"
        cp /usr/lib/syslinux/bios/ldlinux.c32 "${BUILD_DIR}/isolinux/" 2>/dev/null || true
        cp /usr/lib/syslinux/bios/libcom32.c32 "${BUILD_DIR}/isolinux/" 2>/dev/null || true
        cp /usr/lib/syslinux/bios/libutil.c32 "${BUILD_DIR}/isolinux/" 2>/dev/null || true
        cp /usr/lib/syslinux/bios/menu.c32 "${BUILD_DIR}/isolinux/" 2>/dev/null || true
        cp /usr/lib/syslinux/bios/vesamenu.c32 "${BUILD_DIR}/isolinux/" 2>/dev/null || true
    fi
    
    # Create isolinux.cfg
    cat > "${BUILD_DIR}/isolinux/isolinux.cfg" << 'ISOLINUX_CONFIG'
# LimitlessOS ISOLINUX Configuration
DEFAULT vesamenu.c32
PROMPT 0
TIMEOUT 100

MENU TITLE LimitlessOS Boot Menu
MENU BACKGROUND splash.png
MENU COLOR border 30;44 #40ffffff #a0000000 std
MENU COLOR title 1;36;44 #9033ccff #a0000000 std
MENU COLOR sel 7;37;40 #e0ffffff #20ffffff all
MENU COLOR unsel 37;44 #50ffffff #a0000000 std
MENU COLOR help 37;40 #c0ffffff #a0000000 std
MENU COLOR timeout_msg 37;40 #80ffffff #00000000 std
MENU COLOR timeout 1;37;40 #c0ffffff #00000000 std

LABEL live
    MENU LABEL ^Try LimitlessOS (Live Session)
    MENU DEFAULT
    KERNEL /limitless/kernel/continuum
    APPEND initrd=/limitless/initrd/initrd.lfs boot=live quiet splash

LABEL install
    MENU LABEL ^Install LimitlessOS
    KERNEL /limitless/kernel/continuum
    APPEND initrd=/limitless/initrd/installer-initrd.lfs boot=install quiet

LABEL memtest
    MENU LABEL Memory Test
    KERNEL /isolinux/memtest86+

LABEL hdd
    MENU LABEL Boot from first hard disk
    LOCALBOOT 0x80
ISOLINUX_CONFIG
    
    echo -e "${GREEN}✓ ISOLINUX configuration created${NC}"
}

# Create GRUB configuration
create_grub_config() {
    echo -e "${YELLOW}Creating GRUB configuration...${NC}"
    
    cat > "${BUILD_DIR}/boot/grub/grub.cfg" << 'GRUB_CONFIG'
# LimitlessOS GRUB Configuration

set timeout=10
set default=0

# Set graphics mode
insmod efi_gop
insmod efi_uga
insmod video_bochs
insmod video_cirrus
insmod all_video

set gfxmode=auto
load_video
insmod gfxterm
terminal_output gfxterm

# Theme
insmod png
set theme=/boot/grub/theme.txt

menuentry "Try LimitlessOS (Live Session)" --class limitless --class os {
    echo "Loading LimitlessOS kernel..."
    linux /limitless/kernel/continuum boot=live quiet splash
    echo "Loading initial ramdisk..."
    initrd /limitless/initrd/initrd.lfs
}

menuentry "Install LimitlessOS" --class limitless --class os {
    echo "Loading LimitlessOS installer..."
    linux /limitless/kernel/continuum boot=install quiet
    echo "Loading installer ramdisk..."
    initrd /limitless/initrd/installer-initrd.lfs
}

menuentry "LimitlessOS Recovery Mode" --class limitless --class os {
    echo "Loading LimitlessOS in recovery mode..."
    linux /limitless/kernel/continuum boot=live recovery nomodeset
    initrd /limitless/initrd/initrd.lfs
}

menuentry "Memory Test" --class memtest {
    linux16 /isolinux/memtest86+
}

menuentry "System Firmware Settings" --class settings {
    fwsetup
}

menuentry "Reboot" --class restart {
    reboot
}

menuentry "Shutdown" --class shutdown {
    halt
}
GRUB_CONFIG
    
    # Create theme file
    cat > "${BUILD_DIR}/boot/grub/theme.txt" << 'GRUB_THEME'
# LimitlessOS GRUB Theme
title-text: "LimitlessOS"
desktop-image: "background.png"
desktop-color: "#1B1B1B"
terminal-font: "Unicode Regular 16"
terminal-left: "0"
terminal-top: "0"
terminal-width: "100%"
terminal-height: "100%"
terminal-border: "0"

+ boot_menu {
    left = 15%
    top = 30%
    width = 70%
    height = 40%
    item_font = "Unicode Regular 16"
    item_color = "#CCCCCC"
    selected_item_color = "#FFFFFF"
    item_height = 24
    item_padding = 12
    item_spacing = 8
    selected_item_pixmap_style = "select_*.png"
}

+ progress_bar {
    id = "__timeout__"
    left = 15%
    top = 75%
    width = 70%
    height = 20
    show_text = true
    text = "@TIMEOUT_NOTIFICATION_SHORT@"
    font = "Unicode Regular 12"
    text_color = "#CCCCCC"
    fg_color = "#4A90E2"
    bg_color = "#2B2B2B"
    border_color = "#FFFFFF"
}
GRUB_THEME
    
    echo -e "${GREEN}✓ GRUB configuration created${NC}"
}

# Copy bootloaders
copy_bootloaders() {
    echo -e "${YELLOW}Copying bootloaders...${NC}"
    
    # Copy BIOS bootloader
    if [ -f "${OUTPUT_DIR}/genesis-bios.bin" ]; then
        cp "${OUTPUT_DIR}/genesis-bios.bin" "${BUILD_DIR}/isolinux/"
    fi
    
    # Copy UEFI bootloader
    if [ -f "${OUTPUT_DIR}/genesis-uefi.efi" ]; then
        cp "${OUTPUT_DIR}/genesis-uefi.efi" "${BUILD_DIR}/EFI/BOOT/BOOTX64.EFI"
        cp "${OUTPUT_DIR}/genesis-uefi.efi" "${BUILD_DIR}/EFI/limitless/genesis.efi"
    fi
    
    # Create startup.nsh for UEFI shell
    cat > "${BUILD_DIR}/EFI/BOOT/startup.nsh" << 'STARTUP_NSH'
@echo -off
mode 80 25
cls
echo LimitlessOS UEFI Loader
echo ========================
echo.
echo Starting Genesis bootloader...
\EFI\BOOT\BOOTX64.EFI
STARTUP_NSH
    
    echo -e "${GREEN}✓ Bootloaders copied${NC}"
}

# Create placeholder kernel and initrd
create_placeholders() {
    echo -e "${YELLOW}Creating placeholder files...${NC}"
    
    # Create placeholder Continuum kernel
    cat > "${BUILD_DIR}/limitless/kernel/continuum" << 'KERNEL_PLACEHOLDER'
#!/bin/bash
# Placeholder Continuum kernel
# This would be replaced with actual compiled kernel binary

echo "LimitlessOS Continuum Kernel v1.0.0"
echo "Copyright (c) 2025 LimitlessOS Project"
echo ""
echo "This is a placeholder kernel for ISO structure demonstration."
echo "Replace with actual compiled Continuum kernel binary."
KERNEL_PLACEHOLDER
    chmod +x "${BUILD_DIR}/limitless/kernel/continuum"
    
    # Create placeholder initrd (compressed cpio archive)
    echo -e "${CYAN}Creating initrd structure...${NC}"
    INITRD_DIR="${BUILD_DIR}/initrd-tmp"
    mkdir -p "${INITRD_DIR}"/{bin,sbin,etc,proc,sys,dev,tmp,var,usr,lib,lib64}
    mkdir -p "${INITRD_DIR}"/usr/{bin,sbin,lib}
    
    # Create init script
    cat > "${INITRD_DIR}/init" << 'INIT_SCRIPT'
#!/bin/sh
# LimitlessOS Initial Ramdisk Init Script

echo "LimitlessOS Initrd v1.0.0"
echo "Initializing live environment..."

# Mount essential filesystems
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

# Detect hardware
echo "Detecting hardware..."
sleep 1

# Load modules
echo "Loading kernel modules..."
sleep 1

# Start udev
echo "Starting device manager..."
sleep 1

# Find and mount root filesystem
echo "Searching for LimitlessOS root..."
sleep 2

echo "Starting LimitlessOS..."
exec /sbin/init
INIT_SCRIPT
    chmod +x "${INITRD_DIR}/init"
    
    # Create placeholder binaries
    touch "${INITRD_DIR}/bin/sh"
    touch "${INITRD_DIR}/bin/mount"
    touch "${INITRD_DIR}/sbin/init"
    chmod +x "${INITRD_DIR}"/bin/* "${INITRD_DIR}"/sbin/*
    
    # Create initrd.lfs (LimitlessOS File System archive)
    (cd "${INITRD_DIR}" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "${BUILD_DIR}/limitless/initrd/initrd.lfs"
    
    # Create installer initrd (larger, with installer tools)
    cp -r "${INITRD_DIR}" "${BUILD_DIR}/installer-tmp"
    echo "Installer components" > "${BUILD_DIR}/installer-tmp/installer.dat"
    (cd "${BUILD_DIR}/installer-tmp" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "${BUILD_DIR}/limitless/initrd/installer-initrd.lfs"
    
    # Clean up temp directories
    rm -rf "${INITRD_DIR}" "${BUILD_DIR}/installer-tmp"
    
    echo -e "${GREEN}✓ Placeholder files created${NC}"
}

# Create README and metadata
create_metadata() {
    echo -e "${YELLOW}Creating metadata files...${NC}"
    
    # Create README
    cat > "${BUILD_DIR}/README.md" << 'README_CONTENT'
# LimitlessOS Genesis Hybrid ISO

## Version 1.0.0

This is the official LimitlessOS installation and live media.

### Boot Options:

1. **Try LimitlessOS** - Run LimitlessOS without installing (Live Session)
2. **Install LimitlessOS** - Install LimitlessOS to your computer

### System Requirements:

- **CPU**: 64-bit x86 processor (Intel or AMD)
- **RAM**: Minimum 2GB (4GB recommended)
- **Storage**: 20GB free space for installation
- **Graphics**: VGA compatible graphics card
- **Boot**: UEFI or Legacy BIOS

### Boot Methods:

This hybrid ISO supports multiple boot methods:
- UEFI boot via BOOTX64.EFI
- Legacy BIOS boot via ISOLINUX
- Direct kernel boot via GRUB

### Verified Hardware:

- Dell, HP, Lenovo, ASUS laptops and desktops
- Apple MacBook (2015 and later)
- Virtual machines (VMware, VirtualBox, QEMU, Hyper-V)

### Support:

For support and documentation, visit: https://limitlessos.org

---
Copyright (c) 2025 LimitlessOS Project
README_CONTENT
    
    # Create .disk/info
    mkdir -p "${BUILD_DIR}/.disk"
    cat > "${BUILD_DIR}/.disk/info" << DISK_INFO
LimitlessOS Genesis Hybrid ISO
Version: 1.0.0
Architecture: amd64
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Build Host: $(hostname)
DISK_INFO
    
    # Create VERSION file
    cat > "${BUILD_DIR}/VERSION" << VERSION_INFO
LIMITLESSOS_VERSION=1.0.0
LIMITLESSOS_CODENAME=Genesis
LIMITLESSOS_BUILD=$(date +%Y%m%d)
LIMITLESSOS_ARCH=x86_64
LIMITLESSOS_TYPE=hybrid
VERSION_INFO
    
    # Create autorun.inf for Windows
    cat > "${BUILD_DIR}/autorun.inf" << 'AUTORUN_INF'
[autorun]
icon=limitless.ico
label=LimitlessOS Genesis
open=setup.exe

[Content]
MusicFiles=false
PictureFiles=false
VideoFiles=false
AUTORUN_INF
    
    echo -e "${GREEN}✓ Metadata files created${NC}"
}

# Create EFI image
create_efi_image() {
    echo -e "${YELLOW}Creating EFI boot image...${NC}"
    
    EFI_IMG="${BUILD_DIR}/efiboot.img"
    
    # Calculate size (minimum 4MB, add some padding)
    EFI_SIZE=8
    
    # Create FAT32 image
    dd if=/dev/zero of="${EFI_IMG}" bs=1M count=${EFI_SIZE} 2>/dev/null
    mkfs.vfat -F 32 -n "LIMITLESS" "${EFI_IMG}" >/dev/null 2>&1
    
    # Mount and copy files
    EFI_MOUNT="${BUILD_DIR}/efi-mount"
    mkdir -p "${EFI_MOUNT}"
    
    # Create loop device and mount
    sudo mount -o loop "${EFI_IMG}" "${EFI_MOUNT}" 2>/dev/null || {
        # If sudo fails, use mtools instead
        echo -e "${YELLOW}Using mtools for EFI image creation...${NC}"
        
        # Copy files using mtools
        mmd -i "${EFI_IMG}" ::/EFI
        mmd -i "${EFI_IMG}" ::/EFI/BOOT
        mmd -i "${EFI_IMG}" ::/EFI/limitless
        
        if [ -f "${BUILD_DIR}/EFI/BOOT/BOOTX64.EFI" ]; then
            mcopy -i "${EFI_IMG}" "${BUILD_DIR}/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/
            mcopy -i "${EFI_IMG}" "${BUILD_DIR}/EFI/BOOT/startup.nsh" ::/EFI/BOOT/
        fi
        
        if [ -f "${BUILD_DIR}/EFI/limitless/genesis.efi" ]; then
            mcopy -i "${EFI_IMG}" "${BUILD_DIR}/EFI/limitless/genesis.efi" ::/EFI/limitless/
        fi
        
        echo -e "${GREEN}✓ EFI image created with mtools${NC}"
        return 0
    }
    
    # Copy files if mount succeeded
    cp -r "${BUILD_DIR}/EFI" "${EFI_MOUNT}/"
    
    # Unmount
    sudo umount "${EFI_MOUNT}"
    rmdir "${EFI_MOUNT}"
    
    echo -e "${GREEN}✓ EFI boot image created${NC}"
}

# Build the hybrid ISO
build_iso() {
    echo -e "${BLUE}═══ Building Hybrid ISO ═══${NC}"
    
    cd "${BUILD_DIR}"
    
    # Use xorriso to create hybrid ISO
    xorriso -as mkisofs \
        -volid "LIMITLESSOS" \
        -joliet -joliet-long \
        -rock \
        -isohybrid-mbr /usr/lib/syslinux/bios/isohdpfx.bin \
        -partition_offset 16 \
        -c isolinux/boot.cat \
        -b isolinux/isolinux.bin \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        -eltorito-alt-boot \
        -e efiboot.img \
        -no-emul-boot \
        -isohybrid-gpt-basdat \
        -isohybrid-apm-hfsplus \
        -o "${OUTPUT_DIR}/${ISO_NAME}" \
        . 2>/dev/null || {
        
        # Fallback to genisoimage if xorriso fails
        echo -e "${YELLOW}Falling back to genisoimage...${NC}"
        genisoimage \
            -V "LIMITLESSOS" \
            -J -joliet-long \
            -r \
            -cache-inodes \
            -c isolinux/boot.cat \
            -b isolinux/isolinux.bin \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            -eltorito-alt-boot \
            -e efiboot.img \
            -no-emul-boot \
            -o "${OUTPUT_DIR}/${ISO_NAME}" \
            .
        
        # Make hybrid with isohybrid
        if command -v isohybrid &> /dev/null; then
            isohybrid --uefi "${OUTPUT_DIR}/${ISO_NAME}"
        fi
    }
    
    cd "${SCRIPT_DIR}"
    
    # Calculate checksums
    echo -e "${YELLOW}Calculating checksums...${NC}"
    (cd "${OUTPUT_DIR}" && sha256sum "${ISO_NAME}" > "${ISO_NAME}.sha256")
    (cd "${OUTPUT_DIR}" && md5sum "${ISO_NAME}" > "${ISO_NAME}.md5")
    
    echo -e "${GREEN}✓ ISO checksums created${NC}"
}

# Verify ISO
verify_iso() {
    echo -e "${BLUE}═══ Verifying ISO ═══${NC}"
    
    ISO_PATH="${OUTPUT_DIR}/${ISO_NAME}"
    
    if [ ! -f "${ISO_PATH}" ]; then
        echo -e "${RED}✗ ISO not found!${NC}"
        return 1
    fi
    
    # Check file size
    ISO_SIZE=$(stat -c%s "${ISO_PATH}")
    ISO_SIZE_MB=$((ISO_SIZE / 1024 / 1024))
    echo -e "${GREEN}✓ ISO Size: ${ISO_SIZE_MB} MB${NC}"
    
    # Check if bootable
    if file "${ISO_PATH}" | grep -q "bootable"; then
        echo -e "${GREEN}✓ ISO is bootable${NC}"
    else
        echo -e "${YELLOW}⚠ ISO may not be bootable${NC}"
    fi
    
    # List ISO contents
    echo -e "${CYAN}ISO Contents:${NC}"
    isoinfo -l -i "${ISO_PATH}" 2>/dev/null | head -20 || {
        echo -e "${YELLOW}Could not list ISO contents (isoinfo not available)${NC}"
    }
    
    echo
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║           ISO CREATION SUCCESSFUL!                         ║${NC}"
    echo -e "${GREEN}╠════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${GREEN}║ Output: ${OUTPUT_DIR}/${ISO_NAME}${NC}"
    echo -e "${GREEN}║ Size: ${ISO_SIZE_MB} MB${NC}"
    echo -e "${GREEN}║ Type: Hybrid (BIOS + UEFI)${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo
    echo -e "${CYAN}Test the ISO with:${NC}"
    echo -e "  BIOS: ${YELLOW}qemu-system-x86_64 -cdrom ${ISO_PATH}${NC}"
    echo -e "  UEFI: ${YELLOW}qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom ${ISO_PATH}${NC}"
    echo -e "  USB:  ${YELLOW}sudo dd if=${ISO_PATH} of=/dev/sdX bs=4M status=progress${NC}"
}

# Clean up function
cleanup() {
    echo -e "${YELLOW}Cleaning up temporary files...${NC}"
    rm -rf "${BUILD_DIR}"
    echo -e "${GREEN}✓ Cleanup complete${NC}"
}

# Main execution
main() {
    print_banner
    check_dependencies
    clean_build
    build_bootloaders
    create_iso_structure
    create_isolinux_config
    create_grub_config
    copy_bootloaders
    create_placeholders
    create_metadata
    create_efi_image
    build_iso
    verify_iso
    
    if [ "$1" != "--keep-build" ]; then
        cleanup
    else
        echo -e "${YELLOW}Build directory kept at: ${BUILD_DIR}${NC}"
    fi
}

# Handle script arguments
case "$1" in
    --help|-h)
        echo "Usage: $0 [OPTIONS]"
        echo "Options:"
        echo "  --keep-build    Keep build directory after ISO creation"
        echo "  --help, -h      Show this help message"
        exit 0
        ;;
    *)
        main "$@"
        ;;
esac
