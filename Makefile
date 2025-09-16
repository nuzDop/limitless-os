# Limitless OS - Root Makefile
# Master build orchestration

# Configuration
ARCH ?= x86_64
BUILD_TYPE ?= debug
JOBS ?= $(shell nproc)

# Directories
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
SYSROOT = $(BUILD_DIR)/sysroot

# Components
COMPONENTS = genesis continuum harmony prism nexus manifold infinity forge

# Toolchain
export CC = $(ARCH)-elf-gcc
export LD = $(ARCH)-elf-ld
export AS = $(ARCH)-elf-as
export AR = $(ARCH)-elf-ar
export OBJCOPY = $(ARCH)-elf-objcopy

# Flags
export CFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector
export LDFLAGS = -nostdlib

ifeq ($(BUILD_TYPE),debug)
	CFLAGS += -g -O0 -DDEBUG
else
	CFLAGS += -O2
endif

# Targets
.PHONY: all clean iso run test $(COMPONENTS)

all: $(COMPONENTS) apps

# Build components in dependency order
genesis:
	@echo "Building Genesis bootloader..."
	@$(MAKE) -C genesis

continuum: genesis
	@echo "Building Continuum kernel..."
	@$(MAKE) -C continuum

harmony: continuum
	@echo "Building Harmony networking..."
	@$(MAKE) -C harmony

manifold: continuum
	@echo "Building Manifold VFS..."
	@$(MAKE) -C manifold

nexus: continuum manifold
	@echo "Building Nexus service manager..."
	@$(MAKE) -C nexus

prism: continuum manifold
	@echo "Building Prism compositor..."
	@$(MAKE) -C prism

infinity: continuum manifold harmony
	@echo "Building Infinity package manager..."
	@$(MAKE) -C infinity

forge: continuum manifold
	@echo "Building Forge build system..."
	@$(MAKE) -C forge

apps: $(COMPONENTS)
	@echo "Building applications..."
	@$(MAKE) -C apps

# Create bootable ISO
iso: all
	@echo "Creating ISO image..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp genesis/output/genesis.bin $(ISO_DIR)/boot/
	@cp continuum/build/bin/continuum.elf $(ISO_DIR)/boot/
	@echo "menuentry 'Limitless OS' {" > $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    multiboot2 /boot/genesis.bin" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    module2 /boot/continuum.elf" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    boot" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "}" >> $(ISO_DIR)/boot/grub/grub.cfg
	@grub-mkrescue -o $(BUILD_DIR)/limitless.iso $(ISO_DIR)
	@echo "ISO created: $(BUILD_DIR)/limitless.iso"

# Run in QEMU
run: iso
	@qemu-system-x86_64 \
		-cdrom $(BUILD_DIR)/limitless.iso \
		-m 2G \
		-smp 4 \
		-enable-kvm \
		-vga std \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-serial stdio

# Run tests
test:
	@echo "Running tests..."
	@for comp in $(COMPONENTS); do \
		if [ -f $$comp/tests/Makefile ]; then \
			$(MAKE) -C $$comp/tests; \
		fi; \
	done

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@for comp in $(COMPONENTS); do \
		$(MAKE) -C $$comp clean; \
	done
	@$(MAKE) -C apps clean
	@rm -rf $(BUILD_DIR)

# Install to sysroot
install: all
	@echo "Installing to sysroot..."
	@mkdir -p $(SYSROOT)/{bin,lib,include,etc,usr,var}
	@for comp in $(COMPONENTS); do \
		if [ -f $$comp/Makefile ]; then \
			$(MAKE) -C $$comp install DESTDIR=$(SYSROOT); \
		fi; \
	done
	@$(MAKE) -C apps install DESTDIR=$(SYSROOT)

# Documentation
docs:
	@doxygen Doxyfile

# Help
help:
	@echo "Limitless OS Build System"
	@echo "========================="
	@echo "Targets:"
	@echo "  all      - Build all components"
	@echo "  iso      - Create bootable ISO"
	@echo "  run      - Run in QEMU"
	@echo "  test     - Run tests"
	@echo "  clean    - Clean build artifacts"
	@echo "  install  - Install to sysroot"
	@echo "  docs     - Generate documentation"
	@echo ""
	@echo "Variables:"
	@echo "  ARCH=$(ARCH)"
	@echo "  BUILD_TYPE=$(BUILD_TYPE)"
	@echo "  JOBS=$(JOBS)"
