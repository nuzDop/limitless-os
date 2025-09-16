# Applications Makefile
# Build user applications for Limitless OS

# Directories
BUILD_DIR = ../build/apps
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Applications
APPS = terminal filemanager editor shell

# Compiler flags
CFLAGS += -I../include -I..
LDFLAGS += -L../build/lib -lprism -lharmony -lmanifold

# Targets
.PHONY: all clean $(APPS)

all: directories $(APPS)

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

terminal:
	@echo "  BUILD   terminal"
	@$(CC) $(CFLAGS) terminal/terminal.c -o $(BIN_DIR)/terminal $(LDFLAGS)

filemanager:
	@echo "  BUILD   filemanager"
	@$(CC) $(CFLAGS) filemanager/filemanager.c -o $(BIN_DIR)/filemanager $(LDFLAGS)

editor:
	@echo "  BUILD   editor"
	@$(CC) $(CFLAGS) editor/editor.c -o $(BIN_DIR)/editor $(LDFLAGS)

shell:
	@echo "  BUILD   shell"
	@$(CC) $(CFLAGS) shell/shell.c -o $(BIN_DIR)/shell $(LDFLAGS)

install: all
	@echo "  INSTALL apps"
	@mkdir -p $(DESTDIR)/bin
	@cp $(BIN_DIR)/* $(DESTDIR)/bin/

clean:
	@echo "  CLEAN   apps"
	@rm -rf $(BUILD_DIR)
