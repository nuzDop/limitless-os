# Genesis Boot Core Makefile

CC = x86_64-elf-gcc
AS = x86_64-elf-as
LD = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

CFLAGS = -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -fno-pie \
         -fno-pic \
         -m64 \
         -mno-red-zone \
         -mno-mmx \
         -mno-sse \
         -mno-sse2 \
         -mcmodel=kernel \
         -march=x86-64 \
         -O2 \
         -Wall \
         -Wextra \
         -Werror

LDFLAGS = -nostdlib \
          -no-pie \
          -z max-page-size=0x1000 \
          -T genesis_boot.ld

SOURCES = genesis_boot.c
OBJECTS = $(SOURCES:.c=.o)
OUTPUT = genesis_boot.bin

all: $(OUTPUT)

$(OUTPUT): $(OBJECTS)
	$(LD) $(LDFLAGS) -o genesis_boot.elf $(OBJECTS)
	$(OBJCOPY) -O binary genesis_boot.elf $(OUTPUT)
	@echo "Genesis Boot Core built successfully"
	@echo "Size: $$(stat -c%s $(OUTPUT)) bytes"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) genesis_boot.elf $(OUTPUT)

.PHONY: all clean
