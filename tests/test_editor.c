/**
 * @file test_editor.c
 * @brief Unit tests for editor service (buffer, cursor, selection, undo/redo).
 */

#include "../src/services/editor.h"
#include "../src/common/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_editor_init_and_load(void) {
    ThEditor ed;
    th_editor_init(&ed);

    assert(ed.line_count == 1);
    assert(ed.cursor.line == 0 && ed.cursor.col == 0);

    const char *sample = "Hello World\nSecond Line\nThird Line";
    bool ok = th_editor_load_text(&ed, sample, strlen(sample));
    assert(ok);
    assert(ed.line_count == 3);

    size_t full_len = 0;
    char *full = th_editor_get_full_text(&ed, &full_len);
    assert(full != NULL);
    assert(strcmp(full, sample) == 0);
    th_free(full);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_init_and_load\n");
}

static void test_editor_insert_and_backspace(void) {
    ThEditor ed;
    th_editor_init(&ed);

    th_editor_insert_text(&ed, "abc");
    assert(ed.line_count == 1);
    assert(ed.cursor.col == 3);

    th_editor_insert_newline(&ed);
    assert(ed.line_count == 2);
    assert(ed.cursor.line == 1 && ed.cursor.col == 0);

    th_editor_insert_text(&ed, "def");
    assert(ed.cursor.line == 1 && ed.cursor.col == 3);

    /* Backspace characters */
    th_editor_backspace(&ed);
    assert(ed.cursor.col == 2);

    /* Backspace to merge lines */
    th_editor_backspace(&ed); // deletes 'e'
    th_editor_backspace(&ed); // deletes 'd'
    assert(ed.cursor.line == 1 && ed.cursor.col == 0);

    th_editor_backspace(&ed); // merges line 1 into line 0
    assert(ed.line_count == 1);
    assert(ed.cursor.line == 0 && ed.cursor.col == 3);

    size_t len = 0;
    char *text = th_editor_get_full_text(&ed, &len);
    assert(strcmp(text, "abc") == 0);
    th_free(text);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_insert_and_backspace\n");
}

static void test_editor_selection_and_delete(void) {
    ThEditor ed;
    th_editor_init(&ed);

    th_editor_load_text(&ed, "Alpha Beta Gamma", 16);
    assert(!th_editor_has_selection(&ed));

    /* Select "Beta " */
    th_editor_set_cursor(&ed, 0, 6, false);
    th_editor_set_cursor(&ed, 0, 11, true);
    assert(th_editor_has_selection(&ed));

    char *sel_text = th_editor_get_selected_text(&ed);
    assert(sel_text != NULL);
    assert(strcmp(sel_text, "Beta ") == 0);
    th_free(sel_text);

    /* Delete selection */
    th_editor_delete_selection(&ed);
    assert(!th_editor_has_selection(&ed));

    size_t len = 0;
    char *text = th_editor_get_full_text(&ed, &len);
    assert(strcmp(text, "Alpha Gamma") == 0);
    th_free(text);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_selection_and_delete\n");
}

static void test_editor_undo_redo(void) {
    ThEditor ed;
    th_editor_init(&ed);

    th_editor_insert_text(&ed, "Initial text");
    assert(ed.cursor.col == 12);

    /* Grouped transaction */
    th_editor_begin_transaction(&ed);
    th_editor_insert_text(&ed, " added");
    th_editor_end_transaction(&ed);

    size_t len = 0;
    char *t1 = th_editor_get_full_text(&ed, &len);
    assert(strcmp(t1, "Initial text added") == 0);
    th_free(t1);

    /* Undo transaction */
    bool undo_ok = th_editor_undo(&ed);
    assert(undo_ok);

    char *t2 = th_editor_get_full_text(&ed, &len);
    assert(strcmp(t2, "Initial text") == 0);
    th_free(t2);

    /* Redo transaction */
    bool redo_ok = th_editor_redo(&ed);
    assert(redo_ok);

    char *t3 = th_editor_get_full_text(&ed, &len);
    assert(strcmp(t3, "Initial text added") == 0);
    th_free(t3);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_undo_redo\n");
}

static void test_editor_indent_dedent(void) {
    ThEditor ed;
    th_editor_init(&ed);

    th_editor_load_text(&ed, "line one\nline two", 17);
    th_editor_select_all(&ed);

    /* Indent */
    th_editor_indent(&ed, false);
    size_t len = 0;
    char *t1 = th_editor_get_full_text(&ed, &len);
    assert(strncmp(t1, "    line one", 12) == 0);
    th_free(t1);

    /* Dedent */
    th_editor_indent(&ed, true);
    char *t2 = th_editor_get_full_text(&ed, &len);
    assert(strncmp(t2, "line one", 8) == 0);
    th_free(t2);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_indent_dedent\n");
}

static void test_editor_find_replace(void) {
    ThEditor ed;
    th_editor_init(&ed);

    th_editor_load_text(&ed, "hello foo world foo bar foo", 27);

    /* Find next */
    bool found = th_editor_find_next(&ed, "foo", true);
    assert(found);
    assert(th_editor_has_selection(&ed));
    char *sel = th_editor_get_selected_text(&ed);
    assert(strcmp(sel, "foo") == 0);
    th_free(sel);

    /* Replace current */
    bool rep = th_editor_replace_current(&ed, "foo", "qux", true);
    assert(rep);

    /* Replace all remaining */
    size_t count = th_editor_replace_all(&ed, "foo", "baz", true);
    assert(count == 2);

    size_t len = 0;
    char *full = th_editor_get_full_text(&ed, &len);
    assert(strcmp(full, "hello qux world baz bar baz") == 0);
    th_free(full);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_find_replace\n");
}

static void test_editor_autopair_and_brackets(void) {
    ThEditor ed;
    th_editor_init(&ed);

    /* 1. Typing '(' produces '()' with cursor inside */
    th_editor_insert_char(&ed, '(');
    assert(ed.cursor.col == 1);
    size_t len = 0;
    const char *line = th_editor_get_line(&ed, 0, &len);
    assert(strcmp(line, "()") == 0);

    /* 2. Skip-over ')' advances cursor */
    th_editor_insert_char(&ed, ')');
    assert(ed.cursor.col == 2);

    /* 3. Empty pair backspace deletes both */
    th_editor_insert_char(&ed, '{');
    line = th_editor_get_line(&ed, 0, &len);
    assert(strcmp(line, "(){}") == 0);
    assert(ed.cursor.col == 3);
    th_editor_backspace(&ed);
    line = th_editor_get_line(&ed, 0, &len);
    assert(strcmp(line, "()") == 0);

    /* 4. Bracket matching */
    ThPosition match;
    bool found = th_editor_find_matching_bracket(&ed, (ThPosition){0, 0}, &match);
    assert(found);
    assert(match.line == 0 && match.col == 1);

    th_editor_shutdown(&ed);
    printf("  [PASS] test_editor_autopair_and_brackets\n");
}

int main(void) {
    printf("Running Editor Service Tests...\n");
    th_memory_init();

    test_editor_init_and_load();
    test_editor_insert_and_backspace();
    test_editor_selection_and_delete();
    test_editor_undo_redo();
    test_editor_indent_dedent();
    test_editor_find_replace();
    test_editor_autopair_and_brackets();

    ThMemoryStats stats = th_memory_get_stats();
    printf("Editor tests memory remaining: %zu bytes\n", stats.current_allocated_bytes);
    assert(stats.current_allocated_bytes == 0);

    th_memory_shutdown();
    printf("All Editor Service Tests Passed Successfully!\n\n");
    return 0;
}
