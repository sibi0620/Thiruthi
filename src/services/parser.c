/**
 * @file parser.c
 * @brief Tree-sitter integration and syntax highlighting implementation.
 */

#include "parser.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if TH_PLATFORM_WINDOWS
    #define SO_EXT ".dll"
#else
    #define SO_EXT ".so"
#endif

/* Declaration for linked tree-sitter-c */
extern const TSLanguage *tree_sitter_c(void);

static void highlight_line_free(ThHighlightLine *hl) {
    if (hl->spans) {
        th_free(hl->spans);
        hl->spans = NULL;
    }
    hl->count = 0;
    hl->capacity = 0;
}

static void highlight_line_add_span(ThHighlightLine *hl, uint32_t start, uint32_t end, ThTokenType type) {
    if (start >= end) return;
    if (hl->count >= hl->capacity) {
        size_t new_cap = (hl->capacity == 0) ? 8 : hl->capacity * 2;
        hl->spans = (ThHighlightSpan *)th_realloc(hl->spans, sizeof(ThHighlightSpan) * new_cap);
        hl->capacity = new_cap;
    }
    hl->spans[hl->count++] = (ThHighlightSpan){
        .start_col = start,
        .end_col = end,
        .type = type
    };
}

void th_parser_init(ThParserService *service) {
    if (!service) return;
    memset(service, 0, sizeof(ThParserService));

    service->ts_parser = ts_parser_new();
    service->current_lang_id = TH_LANG_UNKNOWN;
}

void th_parser_shutdown(ThParserService *service) {
    if (!service) return;

    if (service->ts_tree) {
        ts_tree_delete(service->ts_tree);
        service->ts_tree = NULL;
    }
    if (service->ts_parser) {
        ts_parser_delete(service->ts_parser);
        service->ts_parser = NULL;
    }
    if (service->dl_handle) {
        th_dlclose((th_dl_handle_t)service->dl_handle);
        service->dl_handle = NULL;
    }

    if (service->highlight_lines) {
        for (size_t i = 0; i < service->line_count; i++) {
            highlight_line_free(&service->highlight_lines[i]);
        }
        th_free(service->highlight_lines);
        service->highlight_lines = NULL;
    }
    service->line_count = 0;
    service->line_capacity = 0;
}

bool th_parser_set_language(ThParserService *service, ThLanguageId lang_id) {
    if (!service) return false;
    service->current_lang_id = lang_id;

    if (service->dl_handle) {
        th_dlclose((th_dl_handle_t)service->dl_handle);
        service->dl_handle = NULL;
    }
    service->current_ts_lang = NULL;

    if (lang_id == TH_LANG_C || lang_id == TH_LANG_CPP) {
        service->current_ts_lang = tree_sitter_c();
    } else {
        /* Try dynamic loading */
        const char *lib_name = NULL;
        const char *sym_name = NULL;
        switch (lang_id) {
            case TH_LANG_PYTHON:
                lib_name = "libtree-sitter-python" SO_EXT;
                sym_name = "tree_sitter_python";
                break;
            case TH_LANG_JAVASCRIPT:
                lib_name = "libtree-sitter-javascript" SO_EXT;
                sym_name = "tree_sitter_javascript";
                break;
            case TH_LANG_TYPESCRIPT:
                lib_name = "libtree-sitter-typescript" SO_EXT;
                sym_name = "tree_sitter_typescript";
                break;
            default:
                break;
        }

        if (lib_name && sym_name) {
            th_dl_handle_t handle = th_dlopen(lib_name);
            if (handle) {
                typedef const TSLanguage *(*LangFunc)(void);
                LangFunc fn = (LangFunc)th_dlsym(handle, sym_name);
                if (fn) {
                    service->dl_handle = (void *)handle;
                    service->current_ts_lang = fn();
                } else {
                    th_dlclose(handle);
                }
            }
        }
    }

    if (service->current_ts_lang && service->ts_parser) {
        ts_parser_set_language(service->ts_parser, service->current_ts_lang);
        return true;
    }

    return false;
}

static void ensure_highlight_lines(ThParserService *service, size_t count) {
    if (count > service->line_capacity) {
        size_t new_cap = service->line_capacity == 0 ? 64 : service->line_capacity * 2;
        if (new_cap < count) new_cap = count;

        service->highlight_lines = (ThHighlightLine *)th_realloc(
            service->highlight_lines, sizeof(ThHighlightLine) * new_cap);

        for (size_t i = service->line_capacity; i < new_cap; i++) {
            service->highlight_lines[i] = (ThHighlightLine){0};
        }
        service->line_capacity = new_cap;
    }

    for (size_t i = 0; i < service->line_count; i++) {
        highlight_line_free(&service->highlight_lines[i]);
    }
    service->line_count = count;
}

/* --- AST Node Mapping --- */

static ThTokenType map_node_to_token(const char *type, const char *parent_type) {
    if (!type) return TH_TOKEN_DEFAULT;

    if (strstr(type, "comment")) return TH_TOKEN_COMMENT;
    if (strstr(type, "string") || strstr(type, "char_literal")) return TH_TOKEN_STRING;
    if (strstr(type, "number") || strstr(type, "integer") || strstr(type, "float")) return TH_TOKEN_NUMBER;
    if (strstr(type, "preproc")) return TH_TOKEN_PREPROCESSOR;
    if (strstr(type, "type") || strcmp(type, "primitive_type") == 0 || strcmp(type, "type_identifier") == 0) {
        return TH_TOKEN_TYPE;
    }

    if (strcmp(type, "identifier") == 0) {
        if (parent_type && (strstr(parent_type, "call") || strstr(parent_type, "function_declarator"))) {
            return TH_TOKEN_FUNCTION;
        }
        return TH_TOKEN_VARIABLE;
    }

    /* Keywords */
    if (strcmp(type, "return") == 0 || strcmp(type, "if") == 0 || strcmp(type, "else") == 0 ||
        strcmp(type, "for") == 0 || strcmp(type, "while") == 0 || strcmp(type, "do") == 0 ||
        strcmp(type, "switch") == 0 || strcmp(type, "case") == 0 || strcmp(type, "break") == 0 ||
        strcmp(type, "continue") == 0 || strcmp(type, "default") == 0 || strcmp(type, "struct") == 0 ||
        strcmp(type, "union") == 0 || strcmp(type, "enum") == 0 || strcmp(type, "typedef") == 0 ||
        strcmp(type, "sizeof") == 0 || strcmp(type, "const") == 0 || strcmp(type, "static") == 0 ||
        strcmp(type, "extern") == 0 || strcmp(type, "inline") == 0 || strcmp(type, "volatile") == 0 ||
        strcmp(type, "class") == 0 || strcmp(type, "def") == 0 || strcmp(type, "import") == 0 ||
        strcmp(type, "from") == 0 || strcmp(type, "as") == 0 || strcmp(type, "try") == 0 ||
        strcmp(type, "except") == 0 || strcmp(type, "finally") == 0 || strcmp(type, "with") == 0 ||
        strcmp(type, "lambda") == 0 || strcmp(type, "yield") == 0 || strcmp(type, "pass") == 0 ||
        strcmp(type, "raise") == 0 || strcmp(type, "function") == 0 || strcmp(type, "let") == 0 ||
        strcmp(type, "var") == 0 || strcmp(type, "const") == 0 || strcmp(type, "new") == 0 ||
        strcmp(type, "delete") == 0 || strcmp(type, "this") == 0 || strcmp(type, "async") == 0 ||
        strcmp(type, "await") == 0) {
        return TH_TOKEN_KEYWORD;
    }

    return TH_TOKEN_DEFAULT;
}

/* Recursive AST Walker to populate highlight spans */
static void walk_ast(ThParserService *service, TSNode node) {
    uint32_t child_count = ts_node_child_count(node);
    const char *type = ts_node_type(node);

    if (child_count == 0 || strstr(type, "comment") || strstr(type, "string")) {
        TSPoint start = ts_node_start_point(node);
        TSPoint end = ts_node_end_point(node);

        TSNode parent = ts_node_parent(node);
        const char *parent_type = ts_node_is_null(parent) ? NULL : ts_node_type(parent);
        ThTokenType token = map_node_to_token(type, parent_type);

        if (token != TH_TOKEN_DEFAULT) {
            for (uint32_t row = start.row; row <= end.row && row < service->line_count; row++) {
                uint32_t sc = (row == start.row) ? start.column : 0;
                uint32_t ec = (row == end.row) ? end.column : 1000;
                highlight_line_add_span(&service->highlight_lines[row], sc, ec, token);
            }
        }
        return;
    }

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        walk_ast(service, child);
    }
}

/* --- Fallback Tokenizer --- */

static bool is_type(const char *word, size_t len, ThLanguageId lang) {
    static const char *c_types[] = {
        "int", "char", "float", "double", "void", "bool", "size_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "intptr_t", "uintptr_t", "ptrdiff_t", "ssize_t",
        "long", "short", "unsigned", "signed", "auto",
        "FILE", "Font", "Color", "Vector2", "Rectangle",
        "string", "vector", "map", "set", "unordered_map", "unordered_set",
        "pair", "tuple", "unique_ptr", "shared_ptr", "weak_ptr"
    };

    static const char *py_types[] = {
        "int", "str", "float", "bool", "list", "dict", "set", "tuple",
        "bytes", "bytearray", "object", "type", "Any", "Optional", "Union",
        "List", "Dict", "Set", "Tuple"
    };

    static const char *js_types[] = {
        "number", "string", "boolean", "any", "void", "never", "unknown",
        "object", "symbol", "bigint", "Array", "Promise", "Record", "Map", "Set"
    };

    const char **list = c_types;
    size_t count = sizeof(c_types) / sizeof(c_types[0]);

    if (lang == TH_LANG_PYTHON) {
        list = py_types;
        count = sizeof(py_types) / sizeof(py_types[0]);
    } else if (lang == TH_LANG_JAVASCRIPT || lang == TH_LANG_TYPESCRIPT) {
        list = js_types;
        count = sizeof(js_types) / sizeof(js_types[0]);
    }

    for (size_t i = 0; i < count; i++) {
        if (strlen(list[i]) == len && strncmp(list[i], word, len) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_keyword(const char *word, size_t len, ThLanguageId lang) {
    static const char *c_kw[] = {
        "break", "case", "const", "continue", "default", "do",
        "else", "enum", "extern", "for", "goto", "if",
        "register", "return", "sizeof", "static",
        "struct", "switch", "typedef", "union", "volatile", "while",
        "inline", "restrict", "true", "false", "NULL",
        "class", "public", "private", "protected", "virtual", "override",
        "template", "typename", "namespace", "using", "new", "delete", "nullptr",
        "try", "catch", "throw", "constexpr", "explicit"
    };

    static const char *py_kw[] = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "False", "finally", "for",
        "from", "global", "if", "import", "in", "is", "lambda", "None",
        "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
        "while", "with", "yield", "self"
    };

    static const char *js_kw[] = {
        "break", "case", "catch", "class", "const", "continue", "debugger",
        "default", "delete", "do", "else", "export", "extends", "finally",
        "for", "function", "if", "import", "in", "instanceof", "new", "return",
        "super", "switch", "this", "throw", "try", "typeof", "var", "void",
        "while", "with", "yield", "let", "static", "enum", "await", "async",
        "null", "true", "false", "undefined", "interface", "type", "implements",
        "declare", "readonly"
    };

    static const char *html_kw[] = {
        "html", "head", "body", "title", "meta", "link", "script", "style",
        "div", "span", "p", "a", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "table", "tr", "td", "th", "form", "input",
        "button", "select", "option", "textarea", "label", "img", "nav",
        "header", "footer", "main", "section", "article", "aside", "DOCTYPE"
    };

    static const char *css_kw[] = {
        "color", "background", "margin", "padding", "border", "display",
        "position", "width", "height", "top", "left", "right", "bottom",
        "font-family", "font-size", "font-weight", "line-height", "text-align",
        "flex", "flex-direction", "justify-content", "align-items", "gap",
        "grid", "overflow", "z-index", "cursor", "opacity", "transition",
        "none", "block", "inline", "relative", "absolute", "fixed", "solid",
        "inherit", "initial", "auto", "center", "hidden"
    };

    const char **list = c_kw;
    size_t count = sizeof(c_kw) / sizeof(c_kw[0]);

    if (lang == TH_LANG_PYTHON) {
        list = py_kw;
        count = sizeof(py_kw) / sizeof(py_kw[0]);
    } else if (lang == TH_LANG_JAVASCRIPT || lang == TH_LANG_TYPESCRIPT) {
        list = js_kw;
        count = sizeof(js_kw) / sizeof(js_kw[0]);
    } else if (lang == TH_LANG_HTML) {
        list = html_kw;
        count = sizeof(html_kw) / sizeof(html_kw[0]);
    } else if (lang == TH_LANG_CSS) {
        list = css_kw;
        count = sizeof(css_kw) / sizeof(css_kw[0]);
    }

    for (size_t i = 0; i < count; i++) {
        if (strlen(list[i]) == len && strncmp(list[i], word, len) == 0) {
            return true;
        }
    }
    return false;
}

static void tokenize_line_fallback(ThParserService *service, size_t line_idx, const char *line, size_t len) {
    ThHighlightLine *hl = &service->highlight_lines[line_idx];
    size_t i = 0;

    /* Markdown syntax highlighting */
    if (service->current_lang_id == TH_LANG_MARKDOWN) {
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i < len && line[i] == '#') {
            size_t hash_start = i;
            while (i < len && line[i] == '#') i++;
            highlight_line_add_span(hl, (uint32_t)hash_start, (uint32_t)i, TH_TOKEN_KEYWORD);
            if (i < len && line[i] == ' ') i++;
            highlight_line_add_span(hl, (uint32_t)i, (uint32_t)len, TH_TOKEN_FUNCTION);
            return;
        }
        if (i < len && (line[i] == '>' || line[i] == '-' || line[i] == '*')) {
            highlight_line_add_span(hl, (uint32_t)i, (uint32_t)(i + 1), TH_TOKEN_OPERATOR);
            i++;
        }
    }

    /* Check preprocessor directive (e.g. #include, #define) */
    if (service->current_lang_id == TH_LANG_C || service->current_lang_id == TH_LANG_CPP) {
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i < len && line[i] == '#') {
            size_t pp_start = i;
            while (i < len && !isspace((unsigned char)line[i])) i++;
            highlight_line_add_span(hl, (uint32_t)pp_start, (uint32_t)i, TH_TOKEN_PREPROCESSOR);

            /* Check header like <stdio.h> or "header.h" */
            while (i < len && isspace((unsigned char)line[i])) i++;
            if (i < len && (line[i] == '<' || line[i] == '"')) {
                char close_ch = (line[i] == '<') ? '>' : '"';
                size_t inc_start = i++;
                while (i < len && line[i] != close_ch) i++;
                if (i < len) i++;
                highlight_line_add_span(hl, (uint32_t)inc_start, (uint32_t)i, TH_TOKEN_STRING);
            }
            return;
        }
        i = 0;
    }

    while (i < len) {
        char c = line[i];

        /* Whitespace */
        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }

        /* Line Comments */
        if ((service->current_lang_id == TH_LANG_PYTHON && c == '#') ||
            ((service->current_lang_id != TH_LANG_PYTHON) && c == '/' && i + 1 < len && line[i + 1] == '/')) {
            highlight_line_add_span(hl, (uint32_t)i, (uint32_t)len, TH_TOKEN_COMMENT);
            break;
        }

        /* C/C++/CSS block comment */
        if (c == '/' && i + 1 < len && line[i + 1] == '*') {
            size_t start = i;
            i += 2;
            while (i + 1 < len && !(line[i] == '*' && line[i + 1] == '/')) i++;
            if (i + 1 < len) i += 2;
            else i = len;
            highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_COMMENT);
            continue;
        }

        /* HTML/XML comments: <!-- ... --> */
        if (c == '<' && i + 3 < len && line[i + 1] == '!' && line[i + 2] == '-' && line[i + 3] == '-') {
            size_t start = i;
            i += 4;
            while (i + 2 < len && !(line[i] == '-' && line[i + 1] == '-' && line[i + 2] == '>')) i++;
            if (i + 2 < len) i += 3;
            else i = len;
            highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_COMMENT);
            continue;
        }

        /* Strings */
        if (c == '"' || c == '\'' || c == '`') {
            size_t start = i++;
            char quote = c;
            while (i < len && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < len) i += 2;
                else i++;
            }
            if (i < len) i++; /* include closing quote */
            highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_STRING);
            continue;
        }

        /* Numbers (hex, float, decimal) */
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < len && isdigit((unsigned char)line[i + 1]))) {
            size_t start = i;
            if (c == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                i += 2;
                while (i < len && isxdigit((unsigned char)line[i])) i++;
            } else {
                while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '.' || line[i] == '_')) i++;
            }
            highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_NUMBER);
            continue;
        }

        /* Identifiers / Keywords / Types */
        if (isalpha((unsigned char)c) || c == '_' || (service->current_lang_id == TH_LANG_CSS && c == '-')) {
            size_t start = i;
            while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_' || line[i] == '-')) {
                i++;
            }
            size_t id_len = i - start;

            if (is_type(line + start, id_len, service->current_lang_id)) {
                highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_TYPE);
            } else if (is_keyword(line + start, id_len, service->current_lang_id)) {
                highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_KEYWORD);
            } else {
                /* Check if function call (followed by '(') */
                size_t lookahead = i;
                while (lookahead < len && (line[lookahead] == ' ' || line[lookahead] == '\t')) lookahead++;
                if (lookahead < len && line[lookahead] == '(') {
                    highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_FUNCTION);
                } else if (c >= 'A' && c <= 'Z') {
                    highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_TYPE);
                } else {
                    highlight_line_add_span(hl, (uint32_t)start, (uint32_t)i, TH_TOKEN_VARIABLE);
                }
            }
            continue;
        }

        /* Operators / Punctuation */
        if (strchr("+-*/%=<>!&|^~?:;,.{}[]()@#$", c)) {
            highlight_line_add_span(hl, (uint32_t)i, (uint32_t)(i + 1), TH_TOKEN_OPERATOR);
            i++;
            continue;
        }

        i++;
    }
}

bool th_parser_parse_buffer(ThParserService *service, const char *source, size_t length, size_t line_count) {
    if (!service) return false;
    ensure_highlight_lines(service, line_count > 0 ? line_count : 1);

    if (!source || length == 0) {
        return true;
    }

    bool tree_sitter_success = false;
    if (service->ts_parser && service->current_ts_lang) {
        if (service->ts_tree) {
            ts_tree_delete(service->ts_tree);
            service->ts_tree = NULL;
        }

        service->ts_tree = ts_parser_parse_string(service->ts_parser, NULL, source, length);
        if (service->ts_tree) {
            TSNode root = ts_tree_root_node(service->ts_tree);
            walk_ast(service, root);
            tree_sitter_success = true;
        }
    }

    /* If tree-sitter was not active or produced zero spans, execute fallback tokenizer */
    bool has_any_span = false;
    for (size_t i = 0; i < service->line_count; i++) {
        if (service->highlight_lines[i].count > 0) {
            has_any_span = true;
            break;
        }
    }

    if (!tree_sitter_success || !has_any_span) {
        const char *p = source;
        const char *end = source + length;
        size_t l_idx = 0;

        while (p < end && l_idx < service->line_count) {
            const char *l_start = p;
            while (p < end && *p != '\n' && *p != '\r') p++;
            size_t l_len = p - l_start;

            tokenize_line_fallback(service, l_idx, l_start, l_len);
            l_idx++;

            if (p < end && *p == '\r') p++;
            if (p < end && *p == '\n') p++;
        }
    }

    return true;
}

const ThHighlightLine *th_parser_get_line_highlights(const ThParserService *service, size_t line_idx) {
    if (!service || line_idx >= service->line_count) return NULL;
    return &service->highlight_lines[line_idx];
}
