# Continuum Kernel Makefile
# Build configuration for the microkernel

# Compiler and tools
CC = x86_64-elf-gcc
AS = x86_64-elf-as
LD = x86_64-elf-ld
AR = x86_64-elf-ar
OBJCOPY = x86_64-elf-objcopy

# Directories
BUILD_DIR = ../build/continuum
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
DRIVER_DIR = drivers

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -g \
         -ffreestanding -fno-stack-protector \
         -fno-pic -mno-red-zone \
         -mcmodel=kernel \
         -mno-mmx -mno-sse -mno-sse2 \
         -I. -I../include

ASFLAGS = -g
LDFLAGS = -nostdlib -z max-page-size=0x1000

# Source files
CORE_SRCS = continuum_core.c \
            flux_memory.c \
            temporal_scheduler.c \
            conduit_ipc.c

DRIVER_SRCS = $(DRIVER_DIR)/resonance.c \
              $(DRIVER_DIR)/storage/nvme.c \
              $(DRIVER_DIR)/storage/ahci.c \
              $(DRIVER_DIR)/storage/virtio_block.c \
              $(DRIVER_DIR)/storage/usb_mass.c \
              $(DRIVER_DIR)/network/intel.c \
              $(DRIVER_DIR)/network/realtek.c \
              $(DRIVER_DIR)/network/broadcom.c \
              $(DRIVER_DIR)/network/virtio_net.c \
              $(DRIVER_DIR)/usb/xhci.c \
              $(DRIVER_DIR)/filesystem/ext4.c \
              $(DRIVER_DIR)/filesystem/fat32.c \
              $(DRIVER_DIR)/input/ps2_keyboard.c \
              $(DRIVER_DIR)/input/ps2_mouse.c \
              $(DRIVER_DIR)/input/usb_hid.c \
              $(DRIVER_DIR)/graphics/vesa.c \
              $(DRIVER_DIR)/audio/ac97.c

# Object files
CORE_OBJS = $(CORE_SRCS:%.c=$(OBJ_DIR)/%.o)
DRIVER_OBJS = $(DRIVER_SRCS:%.c=$(OBJ_DIR)/%.o)
ALL_OBJS = $(CORE_OBJS) $(DRIVER_OBJS)

# Output files
KERNEL = $(BIN_DIR)/continuum.elf
KERNEL_BIN = $(BIN_DIR)/continuum.bin

# Targets
.PHONY: all clean directories

all: directories $(KERNEL) $(KERNEL_BIN)

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/storage
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/network
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/usb
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/filesystem
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/input
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/graphics
	@mkdir -p $(OBJ_DIR)/$(DRIVER_DIR)/audio
	@mkdir -p $(BIN_DIR)

# Compile source files
$(OBJ_DIR)/%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL): $(ALL_OBJS)
	@echo "  LD      $(KERNEL)"
	@$(LD) $(LDFLAGS) -T continuum.ld -o $(KERNEL) $(ALL_OBJS)

# Create binary
$(KERNEL_BIN): $(KERNEL)
	@echo "  OBJCOPY $(KERNEL_BIN)"
	@$(OBJCOPY) -O binary $(KERNEL) $(KERNEL_BIN)

# Clean build artifacts
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR)

# Generate dependencies
-include $(ALL_OBJS:.o=.d)

$(OBJ_DIR)/%.d: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT $(OBJ_DIR)/$*.o $< > $@

# Debug targets
debug: CFLAGS += -DDEBUG -g3
debug: all

# Size information
size: $(KERNEL)
	@echo "Kernel size information:"
	@size $(KERNEL)

# Disassembly
disasm: $(KERNEL)
	@objdump -d $(KERNEL) > $(BUILD_DIR)/continuum.asm
	@echo "Disassembly saved to $(BUILD_DIR)/continuum.asm"
