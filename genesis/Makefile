# Genesis Bootloader Makefile
# Unified BIOS/UEFI bootloader

# Architecture
ARCH = x86_64

# Directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
OUTPUT_DIR = output

# Tools
AS = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy

# UEFI paths
EFI_CRT0 = /usr/lib/crt0-efi-$(ARCH).o
EFI_LDSCRIPT = /usr/lib/elf_$(ARCH)_efi.lds
EFI_INCLUDE = /usr/include/efi
EFI_INCLUDES = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(ARCH) -I$(EFI_INCLUDE)/protocol
EFI_LIB = /usr/lib

# Flags
CFLAGS = -ffreestanding -fno-stack-protector -fpic -fshort-wchar \
         -mno-red-zone -Wall -Wextra -MMD -MP \
         $(EFI_INCLUDES) -DEFI_FUNCTION_WRAPPER

# UEFI-specific flags
EFIFLAGS = -nostdlib -znocombreloc -T $(EFI_LDSCRIPT) -shared -Bsymbolic \
           -L $(EFI_LIB) -L /usr/lib64

LDFLAGS = -nostdlib -n -T genesis_boot.ld

# Source files
BIOS_ASM = genesis-bios.asm
UEFI_C = genesis-uefi.c
BOOT_C = genesis_boot.c

# Output files
BIOS_BIN = $(OUTPUT_DIR)/genesis-bios.bin
UEFI_EFI = $(OUTPUT_DIR)/genesis-uefi.efi
HYBRID_BIN = $(OUTPUT_DIR)/genesis.bin

# Object files
UEFI_OBJ = $(OBJ_DIR)/genesis-uefi.o
BOOT_OBJ = $(OBJ_DIR)/genesis_boot.o

# Targets
.PHONY: all clean directories uefi bios hybrid

all: directories $(BIOS_BIN) $(UEFI_EFI)

directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OUTPUT_DIR)

# BIOS bootloader - Direct binary generation (no ELF intermediate)
$(BIOS_BIN): $(BIOS_ASM)
	@echo "  AS      $< (flat binary)"
	@$(AS) -f bin -o $@ $

# UEFI bootloader
$(UEFI_OBJ): $(UEFI_C)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/genesis-uefi.so: $(UEFI_OBJ)
	@echo "  LD      $@"
	@$(LD) $(EFIFLAGS) $(EFI_CRT0) $(UEFI_OBJ) -o $@ -lgnuefi -lefi

$(UEFI_EFI): $(BUILD_DIR)/genesis-uefi.so
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic \
	            -j .dynsym -j .rel -j .rela -j .reloc \
	            --target=efi-app-$(ARCH) $< $@

# Boot core (if you have one)
$(BOOT_OBJ): $(BOOT_C) genesis_boot.h
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Hybrid bootloader (if needed)
$(HYBRID_BIN): $(BIOS_BIN) $(BOOT_OBJ)
	@echo "  BUILD   $@"
	@cp $(BIOS_BIN) $@
	@# Pad to 1MB and append boot core
	@dd if=/dev/zero bs=1024 count=1024 >> $@ 2>/dev/null
	@cat $(BOOT_OBJ) >> $@

# Clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)

# Debug info
debug:
	@echo "EFI_CRT0: $(EFI_CRT0)"
	@echo "EFI_LDSCRIPT: $(EFI_LDSCRIPT)"
	@ls -la $(EFI_CRT0) || echo "CRT0 not found"
	@ls -la $(EFI_LDSCRIPT) || echo "LD script not found"
