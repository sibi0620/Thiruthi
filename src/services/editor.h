/**
 * @file editor.h
 * @brief Core text buffer, cursor management, selection, and undo/redo service.
 */

#ifndef TH_EDITOR_H
#define TH_EDITOR_H

#include "../common/types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *chars;
    size_t length;
    size_t capacity;
} ThLine;

typedef enum {
    TH_UNDO_INSERT,
    TH_UNDO_DELETE
} ThUndoType;

typedef struct {
    ThUndoType type;
    ThPosition pos;
    char *text;
    uint32_t group_id;
} ThUndoRecord;

typedef struct {
    ThUndoRecord *items;
    size_t count;
    size_t capacity;
} ThUndoStack;

typedef struct {
    ThLine *lines;
    size_t line_count;
    size_t line_capacity;

    ThPosition cursor;
    uint32_t preferred_col;

    bool has_selection;
    ThPosition selection_anchor;

    ThUndoStack undo_stack;
    ThUndoStack redo_stack;
    uint32_t current_group_id;
    bool in_transaction;

    char filepath[1024];
    ThLanguageId language;
    bool is_dirty;
    uint32_t version;

    /* Scroll offset in lines and columns */
    float scroll_y;
    float scroll_x;
    int visible_lines;
} ThEditor;

void th_editor_init(ThEditor *editor);
void th_editor_shutdown(ThEditor *editor);

/* Buffer content operations */
bool th_editor_load_text(ThEditor *editor, const char *text, size_t len);
bool th_editor_load_file(ThEditor *editor, const char *filepath);
bool th_editor_save_file(ThEditor *editor, const char *filepath);
char *th_editor_get_full_text(const ThEditor *editor, size_t *out_len);
const char *th_editor_get_line(const ThEditor *editor, size_t line_idx, size_t *out_len);
size_t th_editor_get_line_count(const ThEditor *editor);

/* Editing operations */
void th_editor_begin_transaction(ThEditor *editor);
void th_editor_end_transaction(ThEditor *editor);
void th_editor_insert_char(ThEditor *editor, char c);
void th_editor_insert_text(ThEditor *editor, const char *text);
void th_editor_insert_newline(ThEditor *editor);
void th_editor_backspace(ThEditor *editor);
void th_editor_delete(ThEditor *editor);
void th_editor_delete_selection(ThEditor *editor);
void th_editor_indent(ThEditor *editor, bool dedent);

/* Undo / Redo */
bool th_editor_undo(ThEditor *editor);
bool th_editor_redo(ThEditor *editor);

/* Cursor and Selection */
void th_editor_move_cursor(ThEditor *editor, int delta_line, int delta_col, bool selecting);
void th_editor_set_cursor(ThEditor *editor, uint32_t line, uint32_t col, bool selecting);
void th_editor_move_to_line_start(ThEditor *editor, bool selecting);
void th_editor_move_to_line_end(ThEditor *editor, bool selecting);
void th_editor_move_to_buffer_start(ThEditor *editor, bool selecting);
void th_editor_move_to_buffer_end(ThEditor *editor, bool selecting);
void th_editor_move_word(ThEditor *editor, int direction, bool selecting);
void th_editor_select_all(ThEditor *editor);
void th_editor_clear_selection(ThEditor *editor);
bool th_editor_has_selection(const ThEditor *editor);
ThRange th_editor_get_selection_range(const ThEditor *editor);
char *th_editor_get_selected_text(const ThEditor *editor);

/* Clamping & Utilities */
void th_editor_clamp_cursor(ThEditor *editor);

/* Word Prefix & Completion */
bool th_editor_get_word_prefix(const ThEditor *editor, char *out_prefix, size_t max_len, uint32_t *out_start_col);
void th_editor_apply_completion(ThEditor *editor, uint32_t start_col, const char *insert_text);

/* Bracket Matching */
bool th_editor_find_matching_bracket(const ThEditor *editor, ThPosition pos, ThPosition *out_match);

/* Search & Replace */
bool th_editor_find_next(ThEditor *editor, const char *pattern, bool case_sensitive);
bool th_editor_replace_current(ThEditor *editor, const char *pattern, const char *replacement, bool case_sensitive);
size_t th_editor_replace_all(ThEditor *editor, const char *pattern, const char *replacement, bool case_sensitive);

#ifdef __cplusplus
}
#endif

#endif /* TH_EDITOR_H */
