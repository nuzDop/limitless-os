# Harmony Networking Stack Makefile
# Build configuration for the network subsystem

# Compiler and tools
CC = x86_64-elf-gcc
AR = x86_64-elf-ar
LD = x86_64-elf-ld

# Directories
BUILD_DIR = ../build/harmony
OBJ_DIR = $(BUILD_DIR)/obj
LIB_DIR = $(BUILD_DIR)/lib
INC_DIR = ../include

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -g \
         -ffreestanding -fno-stack-protector \
         -fno-pic -mno-red-zone \
         -I. -I$(INC_DIR) -I../continuum

# Source files
SRCS = harmony_net.c \
       ethernet.c \
       arp.c \
       ip.c \
       icmp.c \
       tcp.c \
       udp.c \
       socket.c \
       dhcp.c \
       dns.c \
       http.c \
       netutils.c

# Object files
OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o)

# Output library
LIBRARY = $(LIB_DIR)/libharmony.a

# Targets
.PHONY: all clean directories

all: directories $(LIBRARY)

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(LIB_DIR)

# Compile source files
$(OBJ_DIR)/%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Create static library
$(LIBRARY): $(OBJS)
	@echo "  AR      $(LIBRARY)"
	@$(AR) rcs $(LIBRARY) $(OBJS)

# Clean build artifacts
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR)

# Generate dependencies
-include $(OBJS:.o=.d)

$(OBJ_DIR)/%.d: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT $(OBJ_DIR)/$*.o $< > $@

# Debug build
debug: CFLAGS += -DDEBUG -g3
debug: all

# Test programs
test: all
	@$(MAKE) -C tests

# Documentation
docs:
	@doxygen Doxyfile
