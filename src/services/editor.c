/**
 * @file editor.c
 * @brief Core text buffer, cursor, selection, and undo/redo implementation.
 */

#include "editor.h"
#include "file.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_LINE_CAPACITY 32
#define INITIAL_LINE_CHARS_CAPACITY 64

/* --- Helper Line Functions --- */

static void line_init(ThLine *line, size_t initial_cap) {
    if (initial_cap < INITIAL_LINE_CHARS_CAPACITY) initial_cap = INITIAL_LINE_CHARS_CAPACITY;
    line->chars = (char *)th_malloc(initial_cap);
    line->chars[0] = '\0';
    line->length = 0;
    line->capacity = initial_cap;
}

static void line_free(ThLine *line) {
    if (line->chars) {
        th_free(line->chars);
        line->chars = NULL;
    }
    line->length = 0;
    line->capacity = 0;
}

static void line_ensure_capacity(ThLine *line, size_t needed) {
    if (needed + 1 <= line->capacity) return;
    size_t new_cap = line->capacity * 2;
    if (new_cap < needed + 1) new_cap = needed + 1;
    line->chars = (char *)th_realloc(line->chars, new_cap);
    line->capacity = new_cap;
}

static void line_insert_chars(ThLine *line, size_t col, const char *text, size_t len) {
    if (col > line->length) col = line->length;
    line_ensure_capacity(line, line->length + len);

    memmove(line->chars + col + len, line->chars + col, line->length - col + 1);
    memcpy(line->chars + col, text, len);
    line->length += len;
    line->chars[line->length] = '\0';
}

static void line_delete_chars(ThLine *line, size_t col, size_t len) {
    if (col >= line->length || len == 0) return;
    if (col + len > line->length) len = line->length - col;

    memmove(line->chars + col, line->chars + col + len, line->length - (col + len) + 1);
    line->length -= len;
    line->chars[line->length] = '\0';
}

/* --- Undo Stack Helpers --- */

static void undo_stack_init(ThUndoStack *stack) {
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void undo_stack_clear(ThUndoStack *stack) {
    for (size_t i = 0; i < stack->count; i++) {
        if (stack->items[i].text) {
            th_free(stack->items[i].text);
        }
    }
    if (stack->items) {
        th_free(stack->items);
        stack->items = NULL;
    }
    stack->count = 0;
    stack->capacity = 0;
}

static void undo_stack_push(ThUndoStack *stack, ThUndoType type, ThPosition pos, const char *text, uint32_t group_id) {
    if (stack->count >= stack->capacity) {
        size_t new_cap = (stack->capacity == 0) ? 32 : stack->capacity * 2;
        stack->items = (ThUndoRecord *)th_realloc(stack->items, sizeof(ThUndoRecord) * new_cap);
        stack->capacity = new_cap;
    }

    ThUndoRecord *rec = &stack->items[stack->count++];
    rec->type = type;
    rec->pos = pos;
    rec->text = th_strdup(text ? text : "");
    rec->group_id = group_id;
}

static bool undo_stack_pop(ThUndoStack *stack, ThUndoRecord *out_rec) {
    if (stack->count == 0) return false;
    stack->count--;
    *out_rec = stack->items[stack->count];
    return true;
}

/* --- Editor Lifecycle --- */

void th_editor_init(ThEditor *editor) {
    if (!editor) return;
    memset(editor, 0, sizeof(ThEditor));

    editor->line_capacity = INITIAL_LINE_CAPACITY;
    editor->lines = (ThLine *)th_malloc(sizeof(ThLine) * editor->line_capacity);
    editor->line_count = 1;
    line_init(&editor->lines[0], INITIAL_LINE_CHARS_CAPACITY);

    editor->cursor = (ThPosition){0, 0};
    editor->preferred_col = 0;
    editor->has_selection = false;
    editor->selection_anchor = (ThPosition){0, 0};

    undo_stack_init(&editor->undo_stack);
    undo_stack_init(&editor->redo_stack);
    editor->current_group_id = 1;
    editor->in_transaction = false;

    editor->is_dirty = false;
    editor->version = 1;
    editor->language = TH_LANG_UNKNOWN;
    editor->scroll_y = 0.0f;
    editor->scroll_x = 0.0f;
    editor->visible_lines = 30;
}

void th_editor_shutdown(ThEditor *editor) {
    if (!editor) return;

    for (size_t i = 0; i < editor->line_count; i++) {
        line_free(&editor->lines[i]);
    }
    if (editor->lines) {
        th_free(editor->lines);
        editor->lines = NULL;
    }
    editor->line_count = 0;
    editor->line_capacity = 0;

    undo_stack_clear(&editor->undo_stack);
    undo_stack_clear(&editor->redo_stack);
}

/* --- Buffer Allocation & Lines Management --- */

static void ensure_line_capacity(ThEditor *editor, size_t needed) {
    if (needed <= editor->line_capacity) return;
    size_t new_cap = editor->line_capacity * 2;
    if (new_cap < needed) new_cap = needed;
    editor->lines = (ThLine *)th_realloc(editor->lines, sizeof(ThLine) * new_cap);
    editor->line_capacity = new_cap;
}

static void insert_empty_line(ThEditor *editor, size_t index) {
    if (index > editor->line_count) index = editor->line_count;
    ensure_line_capacity(editor, editor->line_count + 1);

    if (index < editor->line_count) {
        memmove(&editor->lines[index + 1], &editor->lines[index], sizeof(ThLine) * (editor->line_count - index));
    }
    line_init(&editor->lines[index], INITIAL_LINE_CHARS_CAPACITY);
    editor->line_count++;
}

static void remove_line(ThEditor *editor, size_t index) {
    if (index >= editor->line_count) return;
    line_free(&editor->lines[index]);

    if (index + 1 < editor->line_count) {
        memmove(&editor->lines[index], &editor->lines[index + 1], sizeof(ThLine) * (editor->line_count - index - 1));
    }
    editor->line_count--;
    if (editor->line_count == 0) {
        /* Keep at least one empty line */
        editor->line_count = 1;
        line_init(&editor->lines[0], INITIAL_LINE_CHARS_CAPACITY);
    }
}

/* --- Buffer Content Operations --- */

bool th_editor_load_text(ThEditor *editor, const char *text, size_t len) {
    if (!editor) return false;

    /* Free existing lines */
    for (size_t i = 0; i < editor->line_count; i++) {
        line_free(&editor->lines[i]);
    }
    editor->line_count = 0;
    undo_stack_clear(&editor->undo_stack);
    undo_stack_clear(&editor->redo_stack);

    if (!text || len == 0) {
        editor->line_count = 1;
        line_init(&editor->lines[0], INITIAL_LINE_CHARS_CAPACITY);
        editor->cursor = (ThPosition){0, 0};
        editor->preferred_col = 0;
        editor->has_selection = false;
        editor->is_dirty = false;
        editor->version++;
        return true;
    }

    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        const char *line_start = p;
        while (p < end && *p != '\n' && *p != '\r') {
            p++;
        }
        size_t line_len = p - line_start;

        insert_empty_line(editor, editor->line_count);
        ThLine *l = &editor->lines[editor->line_count - 1];
        line_insert_chars(l, 0, line_start, line_len);

        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    if (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        /* Text ends with newline -> add trailing empty line */
        insert_empty_line(editor, editor->line_count);
    }

    if (editor->line_count == 0) {
        insert_empty_line(editor, 0);
    }

    editor->cursor = (ThPosition){0, 0};
    editor->preferred_col = 0;
    editor->has_selection = false;
    editor->is_dirty = false;
    editor->version++;
    return true;
}

bool th_editor_load_file(ThEditor *editor, const char *filepath) {
    if (!editor || !filepath) return false;

    size_t size = 0;
    char *content = th_file_read(filepath, &size);
    if (!content) {
        /* New / empty file */
        th_editor_load_text(editor, "", 0);
        strncpy(editor->filepath, filepath, sizeof(editor->filepath) - 1);
        editor->is_dirty = false;
        return true;
    }

    bool ok = th_editor_load_text(editor, content, size);
    th_free(content);

    if (ok) {
        strncpy(editor->filepath, filepath, sizeof(editor->filepath) - 1);
        editor->is_dirty = false;
    }
    return ok;
}

bool th_editor_save_file(ThEditor *editor, const char *filepath) {
    if (!editor) return false;
    const char *path = filepath ? filepath : editor->filepath;
    if (!path || path[0] == '\0') return false;

    size_t len = 0;
    char *content = th_editor_get_full_text(editor, &len);
    if (!content) return false;

    bool success = th_file_write_atomic(path, content, len);
    th_free(content);

    if (success) {
        if (path != editor->filepath) {
            snprintf(editor->filepath, sizeof(editor->filepath), "%s", path);
        }
        editor->is_dirty = false;
    }
    return success;
}

char *th_editor_get_full_text(const ThEditor *editor, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!editor) return NULL;

    size_t total_size = 0;
    for (size_t i = 0; i < editor->line_count; i++) {
        total_size += editor->lines[i].length + 1; /* +1 for newline or null */
    }

    char *buffer = (char *)th_malloc(total_size + 1);
    if (!buffer) return NULL;

    size_t offset = 0;
    for (size_t i = 0; i < editor->line_count; i++) {
        size_t l_len = editor->lines[i].length;
        if (l_len > 0) {
            memcpy(buffer + offset, editor->lines[i].chars, l_len);
            offset += l_len;
        }
        if (i + 1 < editor->line_count) {
            buffer[offset++] = '\n';
        }
    }
    buffer[offset] = '\0';

    if (out_len) *out_len = offset;
    return buffer;
}

const char *th_editor_get_line(const ThEditor *editor, size_t line_idx, size_t *out_len) {
    if (!editor || line_idx >= editor->line_count) {
        if (out_len) *out_len = 0;
        return "";
    }
    if (out_len) *out_len = editor->lines[line_idx].length;
    return editor->lines[line_idx].chars;
}

size_t th_editor_get_line_count(const ThEditor *editor) {
    return editor ? editor->line_count : 0;
}

/* --- Transactions & Grouping --- */

void th_editor_begin_transaction(ThEditor *editor) {
    if (!editor) return;
    editor->in_transaction = true;
    editor->current_group_id++;
}

void th_editor_end_transaction(ThEditor *editor) {
    if (!editor) return;
    editor->in_transaction = false;
}

static uint32_t get_action_group(ThEditor *editor) {
    if (!editor->in_transaction) {
        editor->current_group_id++;
    }
    return editor->current_group_id;
}

/* --- Primitive Buffer Edits (without undo record) --- */

static void raw_insert_text_at(ThEditor *editor, ThPosition pos, const char *text) {
    if (!text || text[0] == '\0') return;
    if (pos.line >= editor->line_count) pos.line = editor->line_count - 1;

    const char *p = text;
    uint32_t cur_line = pos.line;
    uint32_t cur_col = pos.col;

    while (*p) {
        const char *nl = strchr(p, '\n');
        if (!nl) {
            /* Single line text */
            size_t seg_len = strlen(p);
            line_insert_chars(&editor->lines[cur_line], cur_col, p, seg_len);
            cur_col += seg_len;
            break;
        } else {
            /* Insert text before newline */
            size_t seg_len = nl - p;
            line_insert_chars(&editor->lines[cur_line], cur_col, p, seg_len);
            cur_col += seg_len;

            /* Split line at cur_col */
            insert_empty_line(editor, cur_line + 1);
            ThLine *orig = &editor->lines[cur_line];
            ThLine *next = &editor->lines[cur_line + 1];

            size_t remainder = orig->length - cur_col;
            if (remainder > 0) {
                line_insert_chars(next, 0, orig->chars + cur_col, remainder);
                line_delete_chars(orig, cur_col, remainder);
            }

            cur_line++;
            cur_col = 0;
            p = nl + 1;
        }
    }

    editor->cursor = (ThPosition){cur_line, cur_col};
    editor->preferred_col = cur_col;
    editor->is_dirty = true;
    editor->version++;
}

static char *raw_delete_range(ThEditor *editor, ThRange range) {
    if (range.start.line > range.end.line ||
        (range.start.line == range.end.line && range.start.col >= range.end.col)) {
        return NULL;
    }

    if (range.start.line >= editor->line_count) range.start.line = editor->line_count - 1;
    if (range.end.line >= editor->line_count) range.end.line = editor->line_count - 1;

    /* Collect deleted text for undo */
    size_t approx_size = 0;
    for (size_t l = range.start.line; l <= range.end.line; l++) {
        approx_size += editor->lines[l].length + 2;
    }
    char *deleted_text = (char *)th_malloc(approx_size + 1);
    size_t del_offset = 0;

    if (range.start.line == range.end.line) {
        ThLine *line = &editor->lines[range.start.line];
        size_t del_len = range.end.col - range.start.col;
        if (del_len > line->length - range.start.col) del_len = line->length - range.start.col;

        memcpy(deleted_text, line->chars + range.start.col, del_len);
        del_offset = del_len;
        deleted_text[del_offset] = '\0';

        line_delete_chars(line, range.start.col, del_len);
    } else {
        /* Multi-line deletion */
        ThLine *first = &editor->lines[range.start.line];
        size_t first_del_len = first->length - range.start.col;
        memcpy(deleted_text + del_offset, first->chars + range.start.col, first_del_len);
        del_offset += first_del_len;
        deleted_text[del_offset++] = '\n';

        line_delete_chars(first, range.start.col, first_del_len);

        /* Middle lines */
        for (size_t l = range.start.line + 1; l < range.end.line; l++) {
            ThLine *mid = &editor->lines[range.start.line + 1];
            memcpy(deleted_text + del_offset, mid->chars, mid->length);
            del_offset += mid->length;
            deleted_text[del_offset++] = '\n';
            remove_line(editor, range.start.line + 1);
        }

        /* Last line */
        ThLine *last = &editor->lines[range.start.line + 1];
        size_t last_del_len = range.end.col;
        if (last_del_len > last->length) last_del_len = last->length;

        memcpy(deleted_text + del_offset, last->chars, last_del_len);
        del_offset += last_del_len;
        deleted_text[del_offset] = '\0';

        line_delete_chars(last, 0, last_del_len);

        /* Merge last line remainder into first line */
        if (last->length > 0) {
            line_insert_chars(first, first->length, last->chars, last->length);
        }
        remove_line(editor, range.start.line + 1);
    }

    editor->cursor = range.start;
    editor->preferred_col = range.start.col;
    editor->is_dirty = true;
    editor->version++;
    return deleted_text;
}

/* --- High-Level Editing Operations --- */

void th_editor_delete_selection(ThEditor *editor) {
    if (!editor || !editor->has_selection) return;

    ThRange range = th_editor_get_selection_range(editor);
    uint32_t group_id = get_action_group(editor);

    char *deleted_text = raw_delete_range(editor, range);
    if (deleted_text) {
        undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, deleted_text, group_id);
        undo_stack_clear(&editor->redo_stack);
        th_free(deleted_text);
    }

    editor->has_selection = false;
    th_editor_clamp_cursor(editor);
}

void th_editor_insert_text(ThEditor *editor, const char *text) {
    if (!editor || !text || text[0] == '\0') return;

    if (editor->has_selection) {
        th_editor_delete_selection(editor);
    }

    uint32_t group_id = get_action_group(editor);
    ThPosition pos = editor->cursor;

    raw_insert_text_at(editor, pos, text);
    undo_stack_push(&editor->undo_stack, TH_UNDO_INSERT, pos, text, group_id);
    undo_stack_clear(&editor->redo_stack);
    th_editor_clamp_cursor(editor);
}

void th_editor_insert_char(ThEditor *editor, char c) {
    char buf[2] = {c, '\0'};
    th_editor_insert_text(editor, buf);
}

void th_editor_insert_newline(ThEditor *editor) {
    th_editor_insert_text(editor, "\n");
}

void th_editor_backspace(ThEditor *editor) {
    if (!editor) return;

    if (editor->has_selection) {
        th_editor_delete_selection(editor);
        return;
    }

    if (editor->cursor.col > 0) {
        ThRange range = {
            .start = {editor->cursor.line, editor->cursor.col - 1},
            .end = editor->cursor
        };
        uint32_t group_id = get_action_group(editor);
        char *deleted = raw_delete_range(editor, range);
        if (deleted) {
            undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, deleted, group_id);
            undo_stack_clear(&editor->redo_stack);
            th_free(deleted);
        }
    } else if (editor->cursor.line > 0) {
        /* Merge with previous line */
        uint32_t prev_line = editor->cursor.line - 1;
        uint32_t prev_col = editor->lines[prev_line].length;
        ThRange range = {
            .start = {prev_line, prev_col},
            .end = editor->cursor
        };
        uint32_t group_id = get_action_group(editor);
        char *deleted = raw_delete_range(editor, range);
        if (deleted) {
            undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, deleted, group_id);
            undo_stack_clear(&editor->redo_stack);
            th_free(deleted);
        }
    }
    th_editor_clamp_cursor(editor);
}

void th_editor_delete(ThEditor *editor) {
    if (!editor) return;

    if (editor->has_selection) {
        th_editor_delete_selection(editor);
        return;
    }

    ThLine *cur = &editor->lines[editor->cursor.line];
    if (editor->cursor.col < cur->length) {
        ThRange range = {
            .start = editor->cursor,
            .end = {editor->cursor.line, editor->cursor.col + 1}
        };
        uint32_t group_id = get_action_group(editor);
        char *deleted = raw_delete_range(editor, range);
        if (deleted) {
            undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, deleted, group_id);
            undo_stack_clear(&editor->redo_stack);
            th_free(deleted);
        }
    } else if (editor->cursor.line + 1 < editor->line_count) {
        /* Delete newline at end of line */
        ThRange range = {
            .start = editor->cursor,
            .end = {editor->cursor.line + 1, 0}
        };
        uint32_t group_id = get_action_group(editor);
        char *deleted = raw_delete_range(editor, range);
        if (deleted) {
            undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, deleted, group_id);
            undo_stack_clear(&editor->redo_stack);
            th_free(deleted);
        }
    }
    th_editor_clamp_cursor(editor);
}

void th_editor_indent(ThEditor *editor, bool dedent) {
    if (!editor) return;

    if (!editor->has_selection) {
        if (dedent) {
            ThLine *l = &editor->lines[editor->cursor.line];
            size_t spaces = 0;
            while (spaces < 4 && spaces < l->length && l->chars[spaces] == ' ') {
                spaces++;
            }
            if (spaces > 0) {
                ThRange range = {
                    .start = {editor->cursor.line, 0},
                    .end = {editor->cursor.line, spaces}
                };
                uint32_t group_id = get_action_group(editor);
                char *del = raw_delete_range(editor, range);
                if (del) {
                    undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, range.start, del, group_id);
                    undo_stack_clear(&editor->redo_stack);
                    th_free(del);
                }
            }
        } else {
            th_editor_insert_text(editor, "    ");
        }
        return;
    }

    /* Multi-line indentation */
    ThRange sel = th_editor_get_selection_range(editor);
    th_editor_begin_transaction(editor);

    for (uint32_t l = sel.start.line; l <= sel.end.line; l++) {
        if (dedent) {
            ThLine *line = &editor->lines[l];
            size_t spaces = 0;
            while (spaces < 4 && spaces < line->length && line->chars[spaces] == ' ') {
                spaces++;
            }
            if (spaces > 0) {
                ThRange r = {
                    .start = {l, 0},
                    .end = {l, spaces}
                };
                char *del = raw_delete_range(editor, r);
                if (del) {
                    undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, r.start, del, editor->current_group_id);
                    th_free(del);
                }
            }
        } else {
            ThPosition pos = {l, 0};
            raw_insert_text_at(editor, pos, "    ");
            undo_stack_push(&editor->undo_stack, TH_UNDO_INSERT, pos, "    ", editor->current_group_id);
        }
    }

    th_editor_end_transaction(editor);
    undo_stack_clear(&editor->redo_stack);
    th_editor_clamp_cursor(editor);
}

/* --- Undo / Redo Execution --- */

bool th_editor_undo(ThEditor *editor) {
    if (!editor || editor->undo_stack.count == 0) return false;

    uint32_t target_group = editor->undo_stack.items[editor->undo_stack.count - 1].group_id;
    editor->has_selection = false;

    while (editor->undo_stack.count > 0 &&
           editor->undo_stack.items[editor->undo_stack.count - 1].group_id == target_group) {
        ThUndoRecord rec;
        undo_stack_pop(&editor->undo_stack, &rec);

        if (rec.type == TH_UNDO_INSERT) {
            /* Undo insertion: delete the inserted text */
            size_t text_len = strlen(rec.text);
            uint32_t end_line = rec.pos.line;
            uint32_t end_col = rec.pos.col;
            for (size_t i = 0; i < text_len; i++) {
                if (rec.text[i] == '\n') {
                    end_line++;
                    end_col = 0;
                } else {
                    end_col++;
                }
            }
            ThRange range = {rec.pos, {end_line, end_col}};
            char *del = raw_delete_range(editor, range);
            if (del) th_free(del);

            undo_stack_push(&editor->redo_stack, TH_UNDO_DELETE, rec.pos, rec.text, target_group);
        } else if (rec.type == TH_UNDO_DELETE) {
            /* Undo deletion: re-insert the deleted text */
            raw_insert_text_at(editor, rec.pos, rec.text);
            undo_stack_push(&editor->redo_stack, TH_UNDO_INSERT, rec.pos, rec.text, target_group);
        }

        if (rec.text) th_free(rec.text);
    }

    th_editor_clamp_cursor(editor);
    return true;
}

bool th_editor_redo(ThEditor *editor) {
    if (!editor || editor->redo_stack.count == 0) return false;

    uint32_t target_group = editor->redo_stack.items[editor->redo_stack.count - 1].group_id;
    editor->has_selection = false;

    while (editor->redo_stack.count > 0 &&
           editor->redo_stack.items[editor->redo_stack.count - 1].group_id == target_group) {
        ThUndoRecord rec;
        undo_stack_pop(&editor->redo_stack, &rec);

        if (rec.type == TH_UNDO_DELETE) {
            /* Redo delete */
            size_t text_len = strlen(rec.text);
            uint32_t end_line = rec.pos.line;
            uint32_t end_col = rec.pos.col;
            for (size_t i = 0; i < text_len; i++) {
                if (rec.text[i] == '\n') {
                    end_line++;
                    end_col = 0;
                } else {
                    end_col++;
                }
            }
            ThRange range = {rec.pos, {end_line, end_col}};
            char *del = raw_delete_range(editor, range);
            if (del) th_free(del);

            undo_stack_push(&editor->undo_stack, TH_UNDO_DELETE, rec.pos, rec.text, target_group);
        } else if (rec.type == TH_UNDO_INSERT) {
            /* Redo insert */
            raw_insert_text_at(editor, rec.pos, rec.text);
            undo_stack_push(&editor->undo_stack, TH_UNDO_INSERT, rec.pos, rec.text, target_group);
        }

        if (rec.text) th_free(rec.text);
    }

    th_editor_clamp_cursor(editor);
    return true;
}

/* --- Cursor and Navigation --- */

void th_editor_clamp_cursor(ThEditor *editor) {
    if (!editor) return;

    if (editor->cursor.line >= editor->line_count) {
        editor->cursor.line = editor->line_count > 0 ? editor->line_count - 1 : 0;
    }
    size_t line_len = editor->lines[editor->cursor.line].length;
    if (editor->cursor.col > line_len) {
        editor->cursor.col = line_len;
    }
}

static void update_selection_state(ThEditor *editor, bool selecting) {
    if (selecting) {
        if (!editor->has_selection) {
            editor->has_selection = true;
            editor->selection_anchor = editor->cursor;
        }
    } else {
        editor->has_selection = false;
    }
}

void th_editor_set_cursor(ThEditor *editor, uint32_t line, uint32_t col, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);

    editor->cursor.line = line;
    editor->cursor.col = col;
    th_editor_clamp_cursor(editor);
    editor->preferred_col = editor->cursor.col;
}

void th_editor_move_cursor(ThEditor *editor, int delta_line, int delta_col, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);

    if (delta_line != 0) {
        int target_line = (int)editor->cursor.line + delta_line;
        if (target_line < 0) target_line = 0;
        if ((size_t)target_line >= editor->line_count) target_line = (int)editor->line_count - 1;

        editor->cursor.line = target_line;
        size_t len = editor->lines[target_line].length;
        editor->cursor.col = (editor->preferred_col <= len) ? editor->preferred_col : len;
    }

    if (delta_col != 0) {
        int target_col = (int)editor->cursor.col + delta_col;
        if (target_col < 0) {
            if (editor->cursor.line > 0) {
                editor->cursor.line--;
                editor->cursor.col = editor->lines[editor->cursor.line].length;
            } else {
                editor->cursor.col = 0;
            }
        } else if ((size_t)target_col > editor->lines[editor->cursor.line].length) {
            if (editor->cursor.line + 1 < editor->line_count) {
                editor->cursor.line++;
                editor->cursor.col = 0;
            } else {
                editor->cursor.col = editor->lines[editor->cursor.line].length;
            }
        } else {
            editor->cursor.col = target_col;
        }
        editor->preferred_col = editor->cursor.col;
    }

    th_editor_clamp_cursor(editor);
}

void th_editor_move_to_line_start(ThEditor *editor, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);

    /* Smart Home: toggle between first non-whitespace and column 0 */
    ThLine *line = &editor->lines[editor->cursor.line];
    size_t first_non_ws = 0;
    while (first_non_ws < line->length && (line->chars[first_non_ws] == ' ' || line->chars[first_non_ws] == '\t')) {
        first_non_ws++;
    }

    if (editor->cursor.col == first_non_ws) {
        editor->cursor.col = 0;
    } else {
        editor->cursor.col = first_non_ws;
    }
    editor->preferred_col = editor->cursor.col;
}

void th_editor_move_to_line_end(ThEditor *editor, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);
    editor->cursor.col = editor->lines[editor->cursor.line].length;
    editor->preferred_col = editor->cursor.col;
}

void th_editor_move_to_buffer_start(ThEditor *editor, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);
    editor->cursor = (ThPosition){0, 0};
    editor->preferred_col = 0;
}

void th_editor_move_to_buffer_end(ThEditor *editor, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);
    editor->cursor.line = editor->line_count > 0 ? editor->line_count - 1 : 0;
    editor->cursor.col = editor->lines[editor->cursor.line].length;
    editor->preferred_col = editor->cursor.col;
}

void th_editor_move_word(ThEditor *editor, int direction, bool selecting) {
    if (!editor) return;
    update_selection_state(editor, selecting);

    ThLine *line = &editor->lines[editor->cursor.line];
    if (direction < 0) {
        /* Move backward word */
        if (editor->cursor.col == 0 && editor->cursor.line > 0) {
            editor->cursor.line--;
            editor->cursor.col = editor->lines[editor->cursor.line].length;
            editor->preferred_col = editor->cursor.col;
            return;
        }

        int col = (int)editor->cursor.col - 1;
        while (col > 0 && isspace((unsigned char)line->chars[col])) col--;
        while (col > 0 && (isalnum((unsigned char)line->chars[col - 1]) || line->chars[col - 1] == '_')) col--;
        if (col < 0) col = 0;
        editor->cursor.col = col;
    } else {
        /* Move forward word */
        if (editor->cursor.col >= line->length && editor->cursor.line + 1 < editor->line_count) {
            editor->cursor.line++;
            editor->cursor.col = 0;
            editor->preferred_col = 0;
            return;
        }

        size_t col = editor->cursor.col;
        while (col < line->length && (isalnum((unsigned char)line->chars[col]) || line->chars[col] == '_')) col++;
        while (col < line->length && isspace((unsigned char)line->chars[col])) col++;
        editor->cursor.col = col;
    }
    editor->preferred_col = editor->cursor.col;
    th_editor_clamp_cursor(editor);
}

void th_editor_select_all(ThEditor *editor) {
    if (!editor || editor->line_count == 0) return;
    editor->has_selection = true;
    editor->selection_anchor = (ThPosition){0, 0};
    editor->cursor.line = editor->line_count - 1;
    editor->cursor.col = editor->lines[editor->cursor.line].length;
    editor->preferred_col = editor->cursor.col;
}

void th_editor_clear_selection(ThEditor *editor) {
    if (!editor) return;
    editor->has_selection = false;
}

bool th_editor_has_selection(const ThEditor *editor) {
    return editor && editor->has_selection &&
           (editor->selection_anchor.line != editor->cursor.line ||
            editor->selection_anchor.col != editor->cursor.col);
}

ThRange th_editor_get_selection_range(const ThEditor *editor) {
    ThRange range = {{0, 0}, {0, 0}};
    if (!editor || !editor->has_selection) {
        if (editor) {
            range.start = editor->cursor;
            range.end = editor->cursor;
        }
        return range;
    }

    if (editor->selection_anchor.line < editor->cursor.line ||
        (editor->selection_anchor.line == editor->cursor.line &&
         editor->selection_anchor.col <= editor->cursor.col)) {
        range.start = editor->selection_anchor;
        range.end = editor->cursor;
    } else {
        range.start = editor->cursor;
        range.end = editor->selection_anchor;
    }
    return range;
}

char *th_editor_get_selected_text(const ThEditor *editor) {
    if (!th_editor_has_selection(editor)) return NULL;

    ThRange sel = th_editor_get_selection_range(editor);
    size_t approx_size = 0;
    for (size_t l = sel.start.line; l <= sel.end.line; l++) {
        approx_size += editor->lines[l].length + 2;
    }

    char *buf = (char *)th_malloc(approx_size + 1);
    if (!buf) return NULL;

    size_t offset = 0;
    if (sel.start.line == sel.end.line) {
        ThLine *line = &editor->lines[sel.start.line];
        size_t len = sel.end.col - sel.start.col;
        if (len > line->length - sel.start.col) len = line->length - sel.start.col;
        memcpy(buf, line->chars + sel.start.col, len);
        offset = len;
    } else {
        ThLine *first = &editor->lines[sel.start.line];
        size_t first_len = first->length - sel.start.col;
        memcpy(buf + offset, first->chars + sel.start.col, first_len);
        offset += first_len;
        buf[offset++] = '\n';

        for (size_t l = sel.start.line + 1; l < sel.end.line; l++) {
            ThLine *mid = &editor->lines[l];
            memcpy(buf + offset, mid->chars, mid->length);
            offset += mid->length;
            buf[offset++] = '\n';
        }

        ThLine *last = &editor->lines[sel.end.line];
        size_t last_len = sel.end.col > last->length ? last->length : sel.end.col;
        memcpy(buf + offset, last->chars, last_len);
        offset += last_len;
    }

    buf[offset] = '\0';
    return buf;
}

/* =====================================================================
 *  Search & Replace Implementation
 * ===================================================================== */

static const char *line_find_match(const char *haystack, const char *needle, bool case_sensitive) {
    if (!haystack || !needle || !*needle) return NULL;
    if (case_sensitive) {
        return strstr(haystack, needle);
    } else {
        return th_strcasestr(haystack, needle);
    }
}

bool th_editor_find_next(ThEditor *editor, const char *pattern, bool case_sensitive) {
    if (!editor || !pattern || !*pattern || editor->line_count == 0) return false;

    size_t pat_len = strlen(pattern);
    uint32_t start_line = editor->cursor.line;
    uint32_t start_col = editor->cursor.col;

    if (editor->has_selection) {
        ThRange sel = th_editor_get_selection_range(editor);
        start_line = sel.start.line;
        start_col = sel.start.col + 1;
    }

    /* 1. Search forward to end of buffer */
    for (size_t l = start_line; l < editor->line_count; l++) {
        const ThLine *line = &editor->lines[l];
        if (!line->chars || line->length == 0) continue;

        size_t search_offset = (l == start_line) ? start_col : 0;
        if (search_offset >= line->length) continue;

        const char *match = line_find_match(line->chars + search_offset, pattern, case_sensitive);
        if (match) {
            uint32_t col = (uint32_t)(match - line->chars);
            editor->selection_anchor = (ThPosition){(uint32_t)l, col};
            editor->cursor = (ThPosition){(uint32_t)l, (uint32_t)(col + pat_len)};
            editor->has_selection = true;
            editor->preferred_col = editor->cursor.col;

            if (l < (size_t)editor->scroll_y || l >= (size_t)editor->scroll_y + editor->visible_lines) {
                editor->scroll_y = (float)(l > 5 ? l - 5 : 0);
            }
            return true;
        }
    }

    /* 2. Wrap around from top */
    for (size_t l = 0; l <= start_line && l < editor->line_count; l++) {
        const ThLine *line = &editor->lines[l];
        if (!line->chars || line->length == 0) continue;

        const char *match = line_find_match(line->chars, pattern, case_sensitive);
        if (match) {
            uint32_t col = (uint32_t)(match - line->chars);
            if (l == start_line && col >= start_col) break;

            editor->selection_anchor = (ThPosition){(uint32_t)l, col};
            editor->cursor = (ThPosition){(uint32_t)l, (uint32_t)(col + pat_len)};
            editor->has_selection = true;
            editor->preferred_col = editor->cursor.col;

            if (l < (size_t)editor->scroll_y || l >= (size_t)editor->scroll_y + editor->visible_lines) {
                editor->scroll_y = (float)(l > 5 ? l - 5 : 0);
            }
            return true;
        }
    }

    return false;
}

bool th_editor_replace_current(ThEditor *editor, const char *pattern, const char *replacement, bool case_sensitive) {
    if (!editor || !pattern || !replacement) return false;

    if (th_editor_has_selection(editor)) {
        char *sel = th_editor_get_selected_text(editor);
        bool matches = false;
        if (sel) {
            if (case_sensitive) {
                matches = (strcmp(sel, pattern) == 0);
            } else {
                matches = (th_strcasecmp(sel, pattern) == 0);
            }
            th_free(sel);
        }

        if (matches) {
            th_editor_delete_selection(editor);
            th_editor_insert_text(editor, replacement);
            th_editor_find_next(editor, pattern, case_sensitive);
            return true;
        }
    }

    if (th_editor_find_next(editor, pattern, case_sensitive)) {
        th_editor_delete_selection(editor);
        th_editor_insert_text(editor, replacement);
        return true;
    }

    return false;
}

size_t th_editor_replace_all(ThEditor *editor, const char *pattern, const char *replacement, bool case_sensitive) {
    if (!editor || !pattern || !*pattern || !replacement) return 0;

    size_t count = 0;
    th_editor_begin_transaction(editor);
    th_editor_set_cursor(editor, 0, 0, false);

    while (th_editor_find_next(editor, pattern, case_sensitive)) {
        th_editor_delete_selection(editor);
        th_editor_insert_text(editor, replacement);
        count++;
    }

    th_editor_end_transaction(editor);
    return count;
}
