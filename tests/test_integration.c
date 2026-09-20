/**
 * @file test_integration.c
 * @brief Integration tests for inter-service communication and end-to-end editing lifecycle.
 */

#include "../src/services/config.h"
#include "../src/services/file.h"
#include "../src/services/editor.h"
#include "../src/services/parser.h"
#include "../src/services/formatter.h"
#include "../src/services/linter.h"
#include "../src/common/memory.h"
#include "../src/common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_end_to_end_editing_flow(void) {
    printf("  Testing end-to-end service orchestration...\n");

    /* 1. Services setup */
    ThConfigService config;
    th_config_init(&config);

    ThFileService file_service;
    th_file_service_init(&file_service);

    ThEditor editor;
    th_editor_init(&editor);

    ThParserService parser;
    th_parser_init(&parser);

    ThFormatterService formatter;
    th_formatter_init(&formatter);

    ThLinterService linter;
    th_linter_init(&linter);

    /* 2. File creation & language detection */
#if TH_PLATFORM_WINDOWS
    const char *test_path = "thiruthi_integration_test.c";
#else
    const char *test_path = "/tmp/thiruthi_integration_test.c";
#endif
    const char *raw_c =
        "#include <stdio.h>\n"
        "int   add(int a,int b){return a+b;}\n"
        "int main(void){\n"
        "int res=add(1,2);\n"
        "printf(\"result: %d\\n\",res);\n"
        "return 0;\n"
        "}\n";

    bool written = th_file_write_atomic(test_path, raw_c, strlen(raw_c));
    assert(written);

    ThLanguageId lang_id = th_config_detect_language(&config, test_path);
    assert(lang_id == TH_LANG_C);

    const ThLanguageConfig *lcfg = th_config_get_language(&config, lang_id);
    assert(lcfg != NULL);
    assert(strcmp(lcfg->name, "C") == 0);

    /* 3. Load into editor */
    bool loaded = th_editor_load_file(&editor, test_path);
    assert(loaded);
    assert(editor.line_count >= 7);
    assert(!editor.is_dirty);

    /* 4. Parse with tree-sitter */
    bool set_lang = th_parser_set_language(&parser, lang_id);
    assert(set_lang);

    size_t full_len = 0;
    char *full_text = th_editor_get_full_text(&editor, &full_len);
    assert(full_text != NULL);

    bool parsed = th_parser_parse_buffer(&parser, full_text, full_len, editor.line_count);
    assert(parsed);
    th_free(full_text);

    /* Line 0 should have preprocessor highlight */
    const ThHighlightLine *hl0 = th_parser_get_line_highlights(&parser, 0);
    assert(hl0 != NULL && hl0->count > 0);

    /* 5. Perform editing action */
    th_editor_set_cursor(&editor, 1, 0, false);
    th_editor_insert_text(&editor, "// Helper addition function\n");
    assert(editor.is_dirty);
    assert(editor.line_count >= 8);

    /* 6. Format using clang-format */
    char *formatted = NULL;
    size_t formatted_len = 0;
    full_text = th_editor_get_full_text(&editor, &full_len);

    bool fmt_ok = th_formatter_format_code(
        &formatter,
        lcfg->formatter_command,
        test_path,
        full_text,
        full_len,
        &formatted,
        &formatted_len
    );
    th_free(full_text);

    if (fmt_ok && formatted) {
        th_editor_load_text(&editor, formatted, formatted_len);
        th_free(formatted);
    }

    /* 7. Save file atomically */
    bool saved = th_editor_save_file(&editor, test_path);
    assert(saved);
    assert(!editor.is_dirty);

    /* 8. Run linter parsing */
    char mock_linter_output[256];
    snprintf(mock_linter_output, sizeof(mock_linter_output),
             "%s:4:5: warning: unused variable 'unused' [-Wunused-variable]\n", test_path);
    th_linter_parse_output(&linter, mock_linter_output, test_path);

    const ThDiagnosticList *diags = th_linter_get_diagnostics(&linter);
    assert(diags != NULL);
    assert(diags->count == 1);
    assert(diags->items[0].severity == TH_DIAG_WARNING);
    assert(diags->items[0].range.start.line == 3);

    /* 9. Cleanup services */
    th_unlink(test_path);
    th_linter_shutdown(&linter);
    th_formatter_shutdown(&formatter);
    th_parser_shutdown(&parser);
    th_editor_shutdown(&editor);
    th_file_service_shutdown(&file_service);
    th_config_shutdown(&config);

    printf("  [PASS] test_end_to_end_editing_flow\n");
}

int main(void) {
    printf("Running Integration Tests...\n");
    th_memory_init();

    test_end_to_end_editing_flow();

    ThMemoryStats stats = th_memory_get_stats();
    printf("Integration tests memory remaining: %zu bytes\n", stats.current_allocated_bytes);
    assert(stats.current_allocated_bytes == 0);

    th_memory_shutdown();
    printf("All Integration Tests Passed Successfully!\n\n");
    return 0;
}
