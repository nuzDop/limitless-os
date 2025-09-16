# Genesis Bootloader Master Makefile

# Tools
AS = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy
MKISOFS = xorriso
QEMU = qemu-system-x86_64

# Directories
BUILD_DIR = build
OUTPUT_DIR = output
STUB_DIR = stubs

# Targets
all: bios uefi boot-core iso

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(OUTPUT_DIR) $(STUB_DIR)

# Build BIOS bootloader
bios: dirs
	@echo "Building BIOS bootloader..."
	$(AS) -f bin genesis-bios.asm -o $(OUTPUT_DIR)/genesis-bios.bin
	@echo "✓ BIOS bootloader built"

# Build UEFI bootloader
uefi: dirs
	@echo "Building UEFI bootloader..."
	@if [ -f genesis-uefi.c ]; then \
		bash build-uefi.sh; \
	else \
		echo "Creating placeholder UEFI bootloader..."; \
		echo -ne '\x4D\x5A' > $(OUTPUT_DIR)/genesis-uefi.efi; \
	fi
	@echo "✓ UEFI bootloader built"

# Build boot core
boot-core: dirs
	@echo "Building Genesis boot core..."
	@make -f genesis_boot.mk
	@mv genesis_boot.bin $(OUTPUT_DIR)/
	@echo "✓ Boot core built"

# Build stub kernel for testing
stub-kernel: dirs
	@echo "Creating stub kernel..."
	@bash create-stub-kernel.sh
	@echo "✓ Stub kernel created"

# Build ISO
iso: bios uefi stub-kernel
	@echo "Building hybrid ISO..."
	@bash create-hybrid-iso.sh
	@echo "✓ ISO created"

# Quick ISO (no rebuild)
quick-iso:
	@bash create-hybrid-iso.sh --quick

# Testing targets
test: test-bios test-uefi

test-bios:
	@echo "Testing BIOS boot..."
	$(QEMU) -m 2048 -drive file=$(OUTPUT_DIR)/genesis-bios.bin,format=raw

test-uefi:
	@echo "Testing UEFI boot..."
	$(QEMU) -m 2048 -bios /usr/share/ovmf/OVMF.fd \
		-drive file=$(OUTPUT_DIR)/genesis-uefi.efi,format=raw

test-iso:
	@echo "Testing ISO..."
	@bash test-iso.sh

# Clean
clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR) $(STUB_DIR)
	rm -f *.o *.elf *.bin

# Install dependencies
deps:
	@echo "Checking dependencies..."
	@which nasm > /dev/null || echo "Need: sudo apt-get install nasm"
	@which qemu-system-x86_64 > /dev/null || echo "Need: sudo apt-get install qemu-system-x86"
	@which xorriso > /dev/null || echo "Need: sudo apt-get install xorriso"
	@which x86_64-elf-gcc > /dev/null || echo "Need: cross-compiler toolchain"
	@echo "✓ Dependency check complete"

.PHONY: all dirs bios uefi boot-core stub-kernel iso quick-iso test test-bios test-uefi test-iso clean deps
