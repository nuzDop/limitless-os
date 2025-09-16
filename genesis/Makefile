# Genesis Bootloader Makefile
# Unified BIOS/UEFI bootloader

# --- Tools ---
CC_UEFI = gcc
AS_BIOS = nasm
OBJCOPY = objcopy
LD = ld

# --- Architecture & Paths ---
ARCH = x86_64
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
OUTPUT_DIR = output

# GNU-EFI paths
EFI_CRT0 = /usr/lib/crt0-efi-$(ARCH).o
EFI_LDSCRIPT = /usr/lib/elf_$(ARCH)_efi.lds
EFI_INCLUDE = /usr/include/efi
EFI_LIB = /usr/lib

# --- Flags ---
UEFI_CFLAGS = -ffreestanding -fno-stack-protector -fPIC -fshort-wchar \
              -mno-red-zone -Wall -Wextra \
              -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(ARCH)

# BIOS Assembler Flags
BIOS_FLAGS = -f bin

# --- Files ---
BIOS_ASM = genesis-bios.asm
UEFI_C = genesis-uefi.c
UEFI_OBJ = $(OBJ_DIR)/genesis-uefi.o
BIOS_BIN = $(OUTPUT_DIR)/genesis-bios.bin
UEFI_EFI = $(OUTPUT_DIR)/genesis-uefi.efi

# --- Targets ---
.PHONY: all clean directories uefi bios

all: directories bios uefi

directories:
	@mkdir -p $(BUILD_DIR) $(OBJ_DIR) $(OUTPUT_DIR)

# --- Build Rules ---

# BIOS Bootloader
$(BIOS_BIN): $(BIOS_ASM)
	@echo "  AS      $<"
	@$(AS) $(BIOS_FLAGS) $< -o $@

# UEFI Bootloader (Multi-step process)

# Step 1: Compile C source to an object file
$(UEFI_OBJ): $(UEFI_C)
	@echo "  CC      $<"
	$(CC_UEFI) $(UEFI_CFLAGS) -c $< -o $@

# Step 2: Link the object into a correctly-formatted ELF using GCC as the driver
$(BUILD_DIR)/genesis-uefi.elf: $(UEFI_OBJ)
	@echo "  LD      $@"
	$(CC_UEFI) -nostdlib -Wl,-T,$(EFI_LDSCRIPT) -Wl,-Bsymbolic -o $@ $(EFI_CRT0) $< -L$(EFI_LIB) -lgnuefi -lefi

# Step 3: Convert the ELF file to the final PE/COFF EFI format
$(UEFI_EFI): $(BUILD_DIR)/genesis-uefi.elf
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) --target=efi-app-$(ARCH) $< $@

# --- Maintenance Targets ---
clean:
	@echo "  CLEAN   Genesis"
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
