/**
 * @file test_parser.c
 * @brief Unit tests for tree-sitter parser service and syntax highlighting.
 */

#include "../src/services/parser.h"
#include "../src/common/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_parser_c_tree_sitter(void) {
    ThParserService parser;
    th_parser_init(&parser);

    bool set_lang = th_parser_set_language(&parser, TH_LANG_C);
    assert(set_lang);
    assert(parser.current_ts_lang != NULL);

    const char *c_code =
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    printf(\"Hello, Tree-sitter!\\n\");\n"
        "    return 0;\n"
        "}\n";

    bool parsed = th_parser_parse_buffer(&parser, c_code, strlen(c_code), 5);
    assert(parsed);

    /* Line 0: #include <stdio.h> (preprocessor) */
    const ThHighlightLine *hl0 = th_parser_get_line_highlights(&parser, 0);
    assert(hl0 != NULL);
    assert(hl0->count > 0);

    /* Line 3: return 0; (keyword + number) */
    const ThHighlightLine *hl3 = th_parser_get_line_highlights(&parser, 3);
    assert(hl3 != NULL);
    assert(hl3->count > 0);

    bool found_keyword = false;
    for (size_t s = 0; s < hl3->count; s++) {
        if (hl3->spans[s].type == TH_TOKEN_KEYWORD) {
            found_keyword = true;
            break;
        }
    }
    assert(found_keyword);

    th_parser_shutdown(&parser);
    printf("  [PASS] test_parser_c_tree_sitter\n");
}

static void test_parser_fallback_lexer(void) {
    ThParserService parser;
    th_parser_init(&parser);

    th_parser_set_language(&parser, TH_LANG_PYTHON);

    const char *py_code =
        "# Python test script\n"
        "def compute(value):\n"
        "    if value > 10:\n"
        "        return \"large\"\n"
        "    return 42\n";

    bool parsed = th_parser_parse_buffer(&parser, py_code, strlen(py_code), 5);
    assert(parsed);

    /* Line 0: Comment */
    const ThHighlightLine *hl0 = th_parser_get_line_highlights(&parser, 0);
    assert(hl0 != NULL && hl0->count > 0);
    assert(hl0->spans[0].type == TH_TOKEN_COMMENT);

    /* Line 1: def compute(value) -> keyword def */
    const ThHighlightLine *hl1 = th_parser_get_line_highlights(&parser, 1);
    assert(hl1 != NULL && hl1->count > 0);

    bool found_def = false;
    for (size_t s = 0; s < hl1->count; s++) {
        if (hl1->spans[s].type == TH_TOKEN_KEYWORD) {
            found_def = true;
            break;
        }
    }
    assert(found_def);

    th_parser_shutdown(&parser);
    printf("  [PASS] test_parser_fallback_lexer\n");
}

static void test_parser_syntax_error_tolerance(void) {
    ThParserService parser;
    th_parser_init(&parser);
    th_parser_set_language(&parser, TH_LANG_C);

    /* Incomplete, malformed code */
    const char *broken_c = "void broken( { return \"unterminated";
    bool parsed = th_parser_parse_buffer(&parser, broken_c, strlen(broken_c), 1);
    assert(parsed);

    const ThHighlightLine *hl = th_parser_get_line_highlights(&parser, 0);
    assert(hl != NULL);

    th_parser_shutdown(&parser);
    printf("  [PASS] test_parser_syntax_error_tolerance\n");
}

int main(void) {
    printf("Running Parser Service Tests...\n");
    th_memory_init();

    test_parser_c_tree_sitter();
    test_parser_fallback_lexer();
    test_parser_syntax_error_tolerance();

    ThMemoryStats stats = th_memory_get_stats();
    printf("Parser tests memory remaining: %zu bytes\n", stats.current_allocated_bytes);
    assert(stats.current_allocated_bytes == 0);

    th_memory_shutdown();
    printf("All Parser Service Tests Passed Successfully!\n\n");
    return 0;
}
