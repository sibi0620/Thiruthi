PARSER_LANG = c
BASE_SRC_DIR = /usr/src/tree-sitter/$(PARSER_LANG)/0.24.1/parser/src
OBJ_DIR ?= $(CURDIR)/debian/.ts-parser-$(PARSER_LANG)
SOURCES = $(BASE_SRC_DIR)/parser.c
OBJECTS = $(patsubst $(BASE_SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
CFLAGS += $(shell pkg-config --cflags tree-sitter) -I $(BASE_SRC_DIR)
LDFLAGS += $(shell pkg-config --libs tree-sitter)

all: $(OBJ_DIR)/$(PARSER_LANG).so

$(OBJ_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: $(BASE_SRC_DIR)/%.c $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -fPIC -c -o $@ $<

$(OBJ_DIR)/$(PARSER_LANG).so: $(OBJECTS)
	$(CC) $(CPPFLAGS) $(LDFLAGS) -fPIC -shared -o $@ $^
