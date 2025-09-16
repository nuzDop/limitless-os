# Prism Display Compositor Makefile
# Build configuration for the compositor

# Compiler and tools
CC = x86_64-elf-gcc
AR = x86_64-elf-ar
LD = x86_64-elf-ld

# Directories
BUILD_DIR = ../build/prism
OBJ_DIR = $(BUILD_DIR)/obj
LIB_DIR = $(BUILD_DIR)/lib
INC_DIR = ../include

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -g \
         -ffreestanding -fno-stack-protector \
         -fno-pic -mno-red-zone \
         -I. -I$(INC_DIR) -I../continuum

# Math library flags
MATHFLAGS = -lm

# Source files
SRCS = prism.c \
       renderer.c \
       wayland_protocol.c \
       window_manager.c \
       animation.c \
       effects.c \
       input_handler.c \
       output_manager.c \
       client_handler.c

# Object files
OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o)

# Output library
LIBRARY = $(LIB_DIR)/libprism.a

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

# Test compositor
test: all
	@$(MAKE) -C tests
