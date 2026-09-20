CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -D_GNU_SOURCE -I./src -I/usr/local/include -I/usr/include
LDFLAGS ?= -L/usr/local/lib -L/usr/lib
LIBS = -lraylib -ltree-sitter -ltree-sitter-c -lm -lpthread -ldl

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
       src/services/renderer.c \
       src/ui/layout.c

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
	mkdir -p $(BUILD_DIR)/src/common
	mkdir -p $(BUILD_DIR)/src/services
	mkdir -p $(BUILD_DIR)/src/ui
	mkdir -p $(BUILD_DIR)/tests

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

# Test builds
$(TEST_EDITOR_BIN): tests/test_editor.c src/services/editor.c src/services/file.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_PARSER_BIN): tests/test_parser.c src/services/parser.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -ltree-sitter -ltree-sitter-c -ldl -o $@

$(TEST_LSP_BIN): tests/test_lsp.c src/services/lsp.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_FILE_BIN): tests/test_file.c src/services/file.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_INTEG_BIN): tests/test_integration.c src/services/config.c src/services/file.c src/services/editor.c src/services/parser.c src/services/formatter.c src/services/linter.c src/common/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -ltree-sitter -ltree-sitter-c -ldl -o $@

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
	rm -rf $(BUILD_DIR)
