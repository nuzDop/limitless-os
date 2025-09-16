# Genesis Bootloader Makefile
# Builds both BIOS and UEFI bootloaders

# --- Tools ---
# Use the native GCC for compiling UEFI code with gnu-efi
CC_UEFI = gcc
LD_UEFI = ld
AS_BIOS = nasm
OBJCOPY = objcopy

# --- Flags ---
# UEFI CFLAGS: Note the addition of -fpic, required for shared objects
UEFI_CFLAGS = -ffreestanding -fno-stack-protector -fno-stack-check \
              -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
              -I/usr/include/efi -I/usr/include/efi/x86_64 -fpic

# UEFI LDFLAGS: Linker flags to create a shared object file
UEFI_LDFLAGS = -nostdlib -shared -Bsymbolic -L/usr/lib \
               /usr/lib/crt0-efi-x86_64.o -lgnuefi -lefi

# BIOS Assembler Flags
BIOS_FLAGS = -f bin

# --- Directories ---
BUILD_DIR = build
OUTPUT_DIR = output

# --- Targets ---
.PHONY: all bios uefi clean

# Default target
all: bios uefi

# Build BIOS bootloader
bios: $(OUTPUT_DIR)/genesis-bios.bin

# Build UEFI bootloader
uefi: $(OUTPUT_DIR)/genesis-uefi.efi

# --- Build Rules ---

# Rule to build the BIOS bootloader
$(OUTPUT_DIR)/genesis-bios.bin: genesis-bios.asm
	@mkdir -p $(OUTPUT_DIR)
	@echo "Building BIOS bootloader..."
	$(AS_BIOS) $(BIOS_FLAGS) $< -o $@
	@echo "✓ BIOS bootloader built: $@"

# Step 1: Compile UEFI C source to a relocatable object file (.o)
$(BUILD_DIR)/genesis-uefi.o: genesis-uefi.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling UEFI source..."
	$(CC_UEFI) $(UEFI_CFLAGS) -c $< -o $@

# Step 2: Link the object file into a shared object (.so)
$(BUILD_DIR)/genesis-uefi.so: $(BUILD_DIR)/genesis-uefi.o
	@echo "Linking UEFI shared object..."
	$(LD_UEFI) $(UEFI_LDFLAGS) -o $@ $<

# Step 3: Convert the shared object into the final EFI executable
$(OUTPUT_DIR)/genesis-uefi.efi: $(BUILD_DIR)/genesis-uefi.so
	@mkdir -p $(OUTPUT_DIR)
	@echo "Creating EFI executable..."
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	           -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
	           --target=efi-app-x86_64 $< $@
	@echo "✓ UEFI bootloader built: $@"

# Clean build artifacts
clean:
	@echo "Cleaning Genesis build files..."
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
