CC := gcc

ifeq ($(OS),Windows_NT)
MSYS2_PREFIX ?= C:/msys64/ucrt64
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -D_CRT_SECURE_NO_WARNINGS -Isrc -Ideps/usr/include -I$(MSYS2_PREFIX)/include
LDFLAGS ?= -L$(MSYS2_PREFIX)/lib -Ldeps
LIBS = -lraylib -ltree-sitter -lopengl32 -lgdi32 -lwinmm -luser32 -lshell32 -lkernel32
else
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -D_GNU_SOURCE -I./src -I./deps/usr/include -I/usr/local/include -I/usr/include
LDFLAGS ?= -L/usr/local/lib -L/usr/lib
LIBS = -lraylib -ltree-sitter -lm -lpthread -ldl
TEST_TREE_LIBS = -ltree-sitter -ldl
endif

ifeq ($(OS),Windows_NT)
TEST_TREE_LIBS = -ltree-sitter
endif

BUILD_DIR = build
BIN = $(BUILD_DIR)/thiruthi

SRCS = src/main.c \
       src/common/memory.c \
       src/services/config.c \
       src/services/file.c \
       src/services/editor.c \
       src/services/parser.c \
       src/services/lsp.c \
       src/services/formatter.c \
       src/services/linter.c \
       src/services/completion.c \
       src/services/renderer.c \
       src/ui/layout.c

TREE_SITTER_C_SRC = deps/usr/src/tree-sitter/c/0.24.1/parser/src/parser.c
TREE_SITTER_C_OBJ = $(BUILD_DIR)/tree-sitter-c.o

OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

# Test sources
TEST_EDITOR_BIN = $(BUILD_DIR)/test_editor
TEST_PARSER_BIN = $(BUILD_DIR)/test_parser
TEST_LSP_BIN    = $(BUILD_DIR)/test_lsp
TEST_FILE_BIN   = $(BUILD_DIR)/test_file
TEST_INTEG_BIN  = $(BUILD_DIR)/test_integration

TEST_BINS = $(TEST_EDITOR_BIN) $(TEST_PARSER_BIN) $(TEST_LSP_BIN) $(TEST_FILE_BIN) $(TEST_INTEG_BIN)

.PHONY: all clean test run

all: $(BIN)

$(BUILD_DIR):
ifeq ($(OS),Windows_NT)
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	if not exist "$(BUILD_DIR)\src" mkdir "$(BUILD_DIR)\src"
	if not exist "$(BUILD_DIR)\src\common" mkdir "$(BUILD_DIR)\src\common"
	if not exist "$(BUILD_DIR)\src\services" mkdir "$(BUILD_DIR)\src\services"
	if not exist "$(BUILD_DIR)\src\ui" mkdir "$(BUILD_DIR)\src\ui"
	if not exist "$(BUILD_DIR)\tests" mkdir "$(BUILD_DIR)\tests"
else
	mkdir -p $(BUILD_DIR)/src/common
	mkdir -p $(BUILD_DIR)/src/services
	mkdir -p $(BUILD_DIR)/src/ui
	mkdir -p $(BUILD_DIR)/tests
endif

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TREE_SITTER_C_OBJ): $(TREE_SITTER_C_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS) $(TREE_SITTER_C_OBJ)
	$(CC) $(OBJS) $(TREE_SITTER_C_OBJ) $(LDFLAGS) $(LIBS) -o $@

# Test builds
$(TEST_EDITOR_BIN): tests/test_editor.c src/services/editor.c src/services/file.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_PARSER_BIN): tests/test_parser.c src/services/parser.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ $(TREE_SITTER_C_OBJ) $(LDFLAGS) $(TEST_TREE_LIBS) -o $@

$(TEST_LSP_BIN): tests/test_lsp.c src/services/lsp.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_FILE_BIN): tests/test_file.c src/services/file.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_INTEG_BIN): tests/test_integration.c src/services/config.c src/services/file.c src/services/editor.c src/services/parser.c src/services/formatter.c src/services/linter.c src/common/memory.c $(TREE_SITTER_C_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(TEST_TREE_LIBS) -o $@

tests: $(TEST_BINS)

test: $(TEST_BINS)
	@echo "================ Running Test Suite ================"
	@$(TEST_EDITOR_BIN)
	@$(TEST_PARSER_BIN)
	@$(TEST_LSP_BIN)
	@$(TEST_FILE_BIN)
	@$(TEST_INTEG_BIN)
	@echo "================ All Tests Passed! ================"

clean:
ifeq ($(OS),Windows_NT)
		if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
else
	rm -rf $(BUILD_DIR)
endif
