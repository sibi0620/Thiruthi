/**
 * @file main.c
 * @brief Thiruthi Code Editor - Entry point and service orchestrator.
 */

#include "common/types.h"
#include "common/memory.h"
#include "common/platform.h"
#include "services/config.h"
#include "services/file.h"
#include "services/editor.h"
#include "services/parser.h"
#include "services/lsp.h"
#include "services/formatter.h"
#include "services/linter.h"
#include "services/completion.h"
#include "services/renderer.h"
#include "ui/layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raylib.h>

/* Global Application State */
typedef struct {
    ThConfigService config;
    ThFileService file_service;
    ThEditor editor;
    ThParserService parser;
    ThLspClient lsp;
    ThFormatterService formatter;
    ThLinterService linter;
    ThRendererService renderer;
    ThUIState ui;

    uint32_t last_parsed_version;
    double last_edit_time;
} ThiruthiApp;

static ThiruthiApp g_app;

/* --- LSP Callbacks --- */

static void on_lsp_diagnostics(const char *uri, const ThDiagnosticList *diags, void *user_data) {
    ThiruthiApp *app = (ThiruthiApp *)user_data;
    if (!app || !diags) return;
    (void)uri;

    /* Merge or replace diagnostics */
    if (app->ui.diagnostics.items) {
        th_free(app->ui.diagnostics.items);
    }
    app->ui.diagnostics.items = (ThDiagnostic *)th_malloc(sizeof(ThDiagnostic) * (diags->count + 1));
    if (app->ui.diagnostics.items) {
        memcpy(app->ui.diagnostics.items, diags->items, sizeof(ThDiagnostic) * diags->count);
        app->ui.diagnostics.count = diags->count;
        app->ui.diagnostics.capacity = diags->count;
    }

    char status[128];
    snprintf(status, sizeof(status), "Diagnostics updated (%zu issues)", diags->count);
    th_ui_set_status(&app->ui, status, diags->count > 0 ? TH_STATUS_WARNING : TH_STATUS_SUCCESS);
}

static void on_lsp_completions(const ThCompletionList *completions, void *user_data) {
    ThiruthiApp *app = (ThiruthiApp *)user_data;
    if (!app || !completions) return;

    if (completions->count > 0) {
        app->ui.completion_list = *completions;
        app->ui.completion_popup_active = true;
        app->ui.completion_selected_idx = 0;
    } else {
        app->ui.completion_popup_active = false;
    }
}

static void on_lsp_hover(const ThHoverInfo *hover, void *user_data) {
    ThiruthiApp *app = (ThiruthiApp *)user_data;
    if (!app || !hover) return;

    app->ui.hover_info = *hover;
    app->ui.hover_popup_active = hover->active;
}

/* --- Trigger Operations --- */

static void trigger_format(ThiruthiApp *app) {
    const ThLanguageConfig *lang = th_config_get_language(&app->config, app->editor.language);
    if (!lang || !lang->formatter_command) {
        th_ui_set_status(&app->ui, "No formatter configured for this language", TH_STATUS_WARNING);
        return;
    }

    size_t in_len = 0;
    char *full_text = th_editor_get_full_text(&app->editor, &in_len);
    if (!full_text) return;

    char *formatted = NULL;
    size_t formatted_len = 0;

    bool ok = th_formatter_format_code(
        &app->formatter,
        lang->formatter_command,
        app->editor.filepath,
        full_text,
        in_len,
        &formatted,
        &formatted_len
    );

    th_free(full_text);

    if (ok && formatted) {
        ThPosition old_cursor = app->editor.cursor;
        th_editor_load_text(&app->editor, formatted, formatted_len);
        app->editor.cursor = old_cursor;
        th_editor_clamp_cursor(&app->editor);
        th_free(formatted);

        th_ui_set_status(&app->ui, "Code formatted successfully", TH_STATUS_SUCCESS);
    } else {
        const char *err = th_formatter_get_last_error(&app->formatter);
        char msg[256];
        snprintf(msg, sizeof(msg), "Format failed: %.200s", err);
        th_ui_set_status(&app->ui, msg, TH_STATUS_ERROR);
    }
}

static void trigger_lint(ThiruthiApp *app) {
    const ThLanguageConfig *lang = th_config_get_language(&app->config, app->editor.language);
    if (!lang || !lang->linter_command) {
        th_ui_set_status(&app->ui, "No linter configured for this language", TH_STATUS_INFO);
        return;
    }

    if (!app->editor.filepath[0]) {
        th_ui_set_status(&app->ui, "Save file before running linter", TH_STATUS_WARNING);
        return;
    }

    th_ui_set_status(&app->ui, "Running linter...", TH_STATUS_INFO);
    bool ok = th_linter_run(&app->linter, lang->linter_command, app->editor.filepath);
    if (ok) {
        const ThDiagnosticList *linter_diags = th_linter_get_diagnostics(&app->linter);
        if (linter_diags) {
            on_lsp_diagnostics(app->editor.filepath, linter_diags, app);
        }
    }
}

static void trigger_save(ThiruthiApp *app) {
    if (!app->editor.filepath[0]) {
        strncpy(app->editor.filepath, "scratch.c", sizeof(app->editor.filepath) - 1);
    }

    if (app->config.editor.format_on_save) {
        trigger_format(app);
    }

    bool ok = th_editor_save_file(&app->editor, app->editor.filepath);
    if (ok) {
        th_lsp_did_save(&app->lsp, app->editor.filepath);
        th_ui_set_status(&app->ui, "File saved successfully", TH_STATUS_SUCCESS);

        if (app->config.editor.lint_on_save) {
            trigger_lint(app);
        }
    } else {
        th_ui_set_status(&app->ui, "Failed to save file!", TH_STATUS_ERROR);
    }
}

/* --- Input Handling --- */

static char keycode_to_ascii(int key, bool shift) {
    if (key >= KEY_A && key <= KEY_Z) {
        char letter = (char)('a' + (key - KEY_A));
        bool caps = IsKeyDown(KEY_CAPS_LOCK);
        if (shift != caps) letter = (char)(letter - ('a' - 'A'));
        return letter;
    }

    if (key >= KEY_ZERO && key <= KEY_NINE) {
        static const char shifted_digits[] = ")!@#$%^&*(";
        return shift ? shifted_digits[key - KEY_ZERO] : (char)('0' + key - KEY_ZERO);
    }

    if (key == KEY_SPACE) return ' ';
    if (key == KEY_APOSTROPHE) return shift ? '"' : '\'';
    if (key == KEY_COMMA) return shift ? '<' : ',';
    if (key == KEY_MINUS) return shift ? '_' : '-';
    if (key == KEY_PERIOD) return shift ? '>' : '.';
    if (key == KEY_SLASH) return shift ? '?' : '/';
    if (key == KEY_SEMICOLON) return shift ? ':' : ';';
    if (key == KEY_EQUAL) return shift ? '+' : '=';
    if (key == KEY_LEFT_BRACKET) return shift ? '{' : '[';
    if (key == KEY_BACKSLASH) return shift ? '|' : '\\';
    if (key == KEY_RIGHT_BRACKET) return shift ? '}' : ']';
    if (key == KEY_GRAVE) return shift ? '~' : '`';
    return '\0';
}

static void trigger_completion(ThiruthiApp *app) {
    char prefix[64] = {0};
    uint32_t start_col = 0;
    if (th_editor_get_word_prefix(&app->editor, prefix, sizeof(prefix), &start_col)) {
        if (strlen(prefix) >= 1) {
            th_completion_populate(&app->ui.completion_list, &app->editor, prefix);
            if (app->ui.completion_list.count > 0) {
                app->ui.completion_popup_active = true;
                app->ui.completion_selected_idx = 0;
                return;
            }
        }
    }
    app->ui.completion_popup_active = false;
}

static void handle_text_input(ThiruthiApp *app, bool shift, bool ctrl, bool alt, bool super_key) {
    if (ctrl || alt || super_key) return;

    bool received_character = false;
    int character;

    while ((character = GetCharPressed()) > 0) {
        received_character = true;
        if (character >= 32 && character <= 126) {
            th_editor_insert_char(&app->editor, (char)character);
            app->last_edit_time = GetTime();
            if (character == '.' || character == '>') {
                th_lsp_request_completion(&app->lsp, app->editor.filepath, app->editor.cursor);
            }
        }
    }

    /* Some Windows/MinGW Raylib builds do not populate the character queue.
       Fallback: convert key codes to ASCII, but skip keys that are handled
       elsewhere as editing/navigation commands (arrows, enter, backspace, etc.)
       to prevent double-processing. */
    if (!received_character) {
        int key;
        while ((key = GetKeyPressed()) > 0) {
            /* Skip non-printable / editing / navigation keys */
            if (key == KEY_BACKSPACE || key == KEY_DELETE || key == KEY_ENTER ||
                key == KEY_TAB || key == KEY_ESCAPE ||
                key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT ||
                key == KEY_HOME || key == KEY_END || key == KEY_PAGE_UP || key == KEY_PAGE_DOWN ||
                key == KEY_INSERT || key == KEY_CAPS_LOCK ||
                key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT ||
                key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL ||
                key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT ||
                key == KEY_LEFT_SUPER || key == KEY_RIGHT_SUPER ||
                (key >= KEY_F1 && key <= KEY_F12)) {
                continue;
            }
            char ascii = keycode_to_ascii(key, shift);
            if (ascii != '\0') {
                received_character = true;
                th_editor_insert_char(&app->editor, ascii);
                app->last_edit_time = GetTime();
                if (ascii == '.' || ascii == '>') {
                    th_lsp_request_completion(&app->lsp, app->editor.filepath, app->editor.cursor);
                }
            }
        }
    }

    if (received_character) {
        trigger_completion(app);
    }
}

static void handle_keyboard_input(ThiruthiApp *app) {
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    bool super_key = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    /* Autocomplete Popup Keyboard Navigation */
    if (app->ui.completion_popup_active && app->ui.completion_list.count > 0) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            app->ui.completion_popup_active = false;
            return;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            app->ui.completion_selected_idx = (app->ui.completion_selected_idx + 1) % app->ui.completion_list.count;
            return;
        }
        if (IsKeyPressed(KEY_UP)) {
            app->ui.completion_selected_idx = (app->ui.completion_selected_idx - 1 + app->ui.completion_list.count) % app->ui.completion_list.count;
            return;
        }
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ENTER)) {
            int sel_idx = app->ui.completion_selected_idx;
            if (sel_idx >= 0 && sel_idx < (int)app->ui.completion_list.count) {
                char prefix[64] = {0};
                uint32_t start_col = 0;
                th_editor_get_word_prefix(&app->editor, prefix, sizeof(prefix), &start_col);
                th_editor_apply_completion(&app->editor, start_col, app->ui.completion_list.items[sel_idx].insert_text);
                app->last_edit_time = GetTime();
            }
            app->ui.completion_popup_active = false;
            while (GetCharPressed() > 0) {}
            return;
        }
    }

    /* Ctrl+Space: Manually trigger Autocomplete */
    if (ctrl && IsKeyPressed(KEY_SPACE)) {
        trigger_completion(app);
        return;
    }

    /* Direct shortcut for Settings: Ctrl+, or Ctrl+; */
    if (ctrl && (IsKeyPressed(KEY_COMMA) || IsKeyPressed(KEY_SEMICOLON))) {
        app->ui.settings_open = !app->ui.settings_open;
        if (app->ui.settings_open) {
            th_ui_set_status(&app->ui, "Settings opened (L: Line Numbers, F: Font, +/-: Size, T: Tabs, Esc: Close)", TH_STATUS_INFO);
        }
        return;
    }

    if (ctrl && IsKeyPressed(KEY_T)) {
        ThThemeId next_theme = (ThThemeId)((app->config.editor.theme_id + 1) % TH_THEME_COUNT);
        th_config_set_theme(&app->config, next_theme);
        th_ui_set_status(&app->ui, th_config_get_theme_name(next_theme), TH_STATUS_INFO);
        return;
    }

    /* F3: Quick toggle Line Numbers mode anytime */
    if (IsKeyPressed(KEY_F3)) {
        if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_STATIC) {
            app->config.editor.line_number_mode = TH_LINE_NUMBERS_RELATIVE;
            th_ui_set_status(&app->ui, "Line numbers: Relative (Vim-style)", TH_STATUS_INFO);
        } else if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) {
            app->config.editor.line_number_mode = TH_LINE_NUMBERS_NONE;
            th_ui_set_status(&app->ui, "Line numbers: Hidden", TH_STATUS_INFO);
        } else {
            app->config.editor.line_number_mode = TH_LINE_NUMBERS_STATIC;
            th_ui_set_status(&app->ui, "Line numbers: Static", TH_STATUS_INFO);
        }
        return;
    }

    /* When settings modal is open, handle modal controls and intercept text editing */
    if (app->ui.settings_open) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            app->ui.settings_open = false;
            return;
        }

        /* L: Cycle Line Number Mode */
        if (IsKeyPressed(KEY_L)) {
            if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_STATIC) {
                app->config.editor.line_number_mode = TH_LINE_NUMBERS_RELATIVE;
                th_ui_set_status(&app->ui, "Line numbers: Relative (Vim-style)", TH_STATUS_INFO);
            } else if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) {
                app->config.editor.line_number_mode = TH_LINE_NUMBERS_NONE;
                th_ui_set_status(&app->ui, "Line numbers: Hidden", TH_STATUS_INFO);
            } else {
                app->config.editor.line_number_mode = TH_LINE_NUMBERS_STATIC;
                th_ui_set_status(&app->ui, "Line numbers: Static", TH_STATUS_INFO);
            }
            return;
        }

        /* F: Cycle Editor Font */
        if (IsKeyPressed(KEY_F)) {
            static int s_font_idx = 0;
            const char *fonts[] = {
                "C:\\Windows\\Fonts\\CascadiaMono.ttf",
                "C:\\Windows\\Fonts\\consola.ttf",
                "C:\\Windows\\Fonts\\cour.ttf"
            };
            const char *font_names[] = {
                "Cascadia Mono",
                "Consolas",
                "Courier New"
            };
            s_font_idx = (s_font_idx + 1) % 3;
            strncpy(app->config.editor.font_path, fonts[s_font_idx], sizeof(app->config.editor.font_path) - 1);
            th_renderer_load_font(&app->renderer, app->config.editor.font_path, app->config.editor.font_size);
            char fmsg[128];
            snprintf(fmsg, sizeof(fmsg), "Font changed to: %s", font_names[s_font_idx]);
            th_ui_set_status(&app->ui, fmsg, TH_STATUS_INFO);
            return;
        }

        /* '+' or '=': Increase Font Size */
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
            if (app->config.editor.font_size < 36) {
                app->config.editor.font_size += 2;
                app->config.editor.line_height = app->config.editor.font_size + 8;
                th_renderer_load_font(&app->renderer, app->config.editor.font_path, app->config.editor.font_size);
                char smsg[64];
                snprintf(smsg, sizeof(smsg), "Font size: %d px", app->config.editor.font_size);
                th_ui_set_status(&app->ui, smsg, TH_STATUS_INFO);
            }
            return;
        }

        /* '-' or '_': Decrease Font Size */
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
            if (app->config.editor.font_size > 10) {
                app->config.editor.font_size -= 2;
                app->config.editor.line_height = app->config.editor.font_size + 8;
                th_renderer_load_font(&app->renderer, app->config.editor.font_path, app->config.editor.font_size);
                char smsg[64];
                snprintf(smsg, sizeof(smsg), "Font size: %d px", app->config.editor.font_size);
                th_ui_set_status(&app->ui, smsg, TH_STATUS_INFO);
            }
            return;
        }

        /* T: Cycle Tab Size */
        if (IsKeyPressed(KEY_T)) {
            if (app->config.editor.tab_size == 2) app->config.editor.tab_size = 4;
            else if (app->config.editor.tab_size == 4) app->config.editor.tab_size = 8;
            else app->config.editor.tab_size = 2;
            char tmsg[64];
            snprintf(tmsg, sizeof(tmsg), "Tab size: %d spaces", app->config.editor.tab_size);
            th_ui_set_status(&app->ui, tmsg, TH_STATUS_INFO);
            return;
        }

        /* C: Cycle Cursor Style */
        if (IsKeyPressed(KEY_C)) {
            if (app->config.editor.cursor_style == TH_CURSOR_BAR) {
                app->config.editor.cursor_style = TH_CURSOR_BLOCK;
                th_ui_set_status(&app->ui, "Cursor style: Block (█)", TH_STATUS_INFO);
            } else if (app->config.editor.cursor_style == TH_CURSOR_BLOCK) {
                app->config.editor.cursor_style = TH_CURSOR_UNDERLINE;
                th_ui_set_status(&app->ui, "Cursor style: Underline (_)", TH_STATUS_INFO);
            } else {
                app->config.editor.cursor_style = TH_CURSOR_BAR;
                th_ui_set_status(&app->ui, "Cursor style: Bar (|)", TH_STATUS_INFO);
            }
            th_renderer_reset_cursor_blink(&app->renderer);
            return;
        }

        /* B: Toggle Cursor Blink */
        if (IsKeyPressed(KEY_B)) {
            app->config.editor.cursor_blink = !app->config.editor.cursor_blink;
            char bmsg[64];
            snprintf(bmsg, sizeof(bmsg), "Cursor blink: %s", app->config.editor.cursor_blink ? "Enabled" : "Disabled");
            th_ui_set_status(&app->ui, bmsg, TH_STATUS_INFO);
            th_renderer_reset_cursor_blink(&app->renderer);
            return;
        }

        /* Consume text input while in modal */
        while (GetCharPressed() > 0) {}
        return;
    }

    /* Completion pop-up active navigation */
    if (app->ui.completion_popup_active) {
        if (IsKeyPressed(KEY_UP)) {
            if (app->ui.completion_selected_idx > 0) app->ui.completion_selected_idx--;
            return;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            if ((size_t)app->ui.completion_selected_idx + 1 < app->ui.completion_list.count) {
                app->ui.completion_selected_idx++;
            }
            return;
        }
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ENTER)) {
            if (app->ui.completion_selected_idx >= 0 &&
                (size_t)app->ui.completion_selected_idx < app->ui.completion_list.count) {
                ThCompletionItem *item = &app->ui.completion_list.items[app->ui.completion_selected_idx];
                th_editor_insert_text(&app->editor, item->insert_text);
            }
            app->ui.completion_popup_active = false;
            return;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            app->ui.completion_popup_active = false;
            return;
        }
    }

    /* Shortcuts with Ctrl */
    if (ctrl) {
        if (IsKeyPressed(KEY_S)) {
            trigger_save(app);
            return;
        }
        if (IsKeyPressed(KEY_F)) {
            trigger_format(app);
            return;
        }
        if (IsKeyPressed(KEY_H)) {
            /* Toggle Find & Replace panel */
            app->ui.find_replace.active = !app->ui.find_replace.active;
            if (app->ui.find_replace.active) {
                app->ui.find_replace.is_replace_mode = true;
                app->ui.find_replace.active_field = 0;
                th_ui_set_status(&app->ui, "Find & Replace (Esc to close)", TH_STATUS_INFO);
            }
            return;
        }
        if (IsKeyPressed(KEY_G)) {
            /* Toggle Go-to-line dialog */
            app->ui.goto_line.active = !app->ui.goto_line.active;
            if (app->ui.goto_line.active) {
                app->ui.goto_line.line_input[0] = '\0';
                th_ui_set_status(&app->ui, "Go to line (Enter to jump, Esc to cancel)", TH_STATUS_INFO);
            }
            return;
        }
        if (IsKeyPressed(KEY_L)) {
            trigger_lint(app);
            return;
        }
        if (IsKeyPressed(KEY_B)) {
            app->ui.sidebar_open = !app->ui.sidebar_open;
            return;
        }
        if (IsKeyPressed(KEY_P)) {
            app->ui.problems_panel_open = !app->ui.problems_panel_open;
            return;
        }
        if (IsKeyPressed(KEY_Z)) {
            if (shift) {
                th_editor_redo(&app->editor);
            } else {
                th_editor_undo(&app->editor);
            }
            return;
        }
        if (IsKeyPressed(KEY_Y)) {
            th_editor_redo(&app->editor);
            return;
        }
        if (IsKeyPressed(KEY_A)) {
            th_editor_select_all(&app->editor);
            return;
        }
        if (IsKeyPressed(KEY_C)) {
            char *sel = th_editor_get_selected_text(&app->editor);
            if (sel) {
                SetClipboardText(sel);
                th_free(sel);
                th_ui_set_status(&app->ui, "Copied to clipboard", TH_STATUS_INFO);
            }
            return;
        }
        if (IsKeyPressed(KEY_X)) {
            char *sel = th_editor_get_selected_text(&app->editor);
            if (sel) {
                SetClipboardText(sel);
                th_free(sel);
                th_editor_delete_selection(&app->editor);
                th_ui_set_status(&app->ui, "Cut to clipboard", TH_STATUS_INFO);
            }
            return;
        }
        if (IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) {
                th_editor_insert_text(&app->editor, clip);
            }
            return;
        }
        if (IsKeyPressed(KEY_SPACE)) {
            /* Request completions from LSP */
            th_lsp_request_completion(&app->lsp, app->editor.filepath, app->editor.cursor);
            th_ui_set_status(&app->ui, "Requested LSP completions...", TH_STATUS_INFO);
            return;
        }
        if (IsKeyPressed(KEY_LEFT)) {
            th_editor_move_word(&app->editor, -1, shift);
            return;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            th_editor_move_word(&app->editor, 1, shift);
            return;
        }
        if (IsKeyPressed(KEY_HOME)) {
            th_editor_move_to_buffer_start(&app->editor, shift);
            return;
        }
        if (IsKeyPressed(KEY_END)) {
            th_editor_move_to_buffer_end(&app->editor, shift);
            return;
        }
    }

    /* Go-to-line input handling */
    if (app->ui.goto_line.active) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            app->ui.goto_line.active = false;
            return;
        }
        if (IsKeyPressed(KEY_ENTER)) {
            int target = atoi(app->ui.goto_line.line_input);
            if (target > 0) {
                th_editor_set_cursor(&app->editor, (uint32_t)(target - 1), 0, false);
                char msg[64];
                snprintf(msg, sizeof(msg), "Jumped to line %d", target);
                th_ui_set_status(&app->ui, msg, TH_STATUS_SUCCESS);
            }
            app->ui.goto_line.active = false;
            return;
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t len = strlen(app->ui.goto_line.line_input);
            if (len > 0) app->ui.goto_line.line_input[len - 1] = '\0';
            return;
        }
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch >= '0' && ch <= '9') {
                size_t len = strlen(app->ui.goto_line.line_input);
                if (len < sizeof(app->ui.goto_line.line_input) - 1) {
                    app->ui.goto_line.line_input[len] = (char)ch;
                    app->ui.goto_line.line_input[len + 1] = '\0';
                }
            }
        }
        return;
    }

    /* Find & Replace input handling */
    if (app->ui.find_replace.active) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            app->ui.find_replace.active = false;
            return;
        }
        if (IsKeyPressed(KEY_TAB)) {
            if (app->ui.find_replace.is_replace_mode) {
                app->ui.find_replace.active_field = (app->ui.find_replace.active_field == 0) ? 1 : 0;
            }
            return;
        }
        if (IsKeyPressed(KEY_ENTER)) {
            if (app->ui.find_replace.is_replace_mode && app->ui.find_replace.active_field == 1) {
                bool replaced = th_editor_replace_current(&app->editor,
                                                          app->ui.find_replace.find_text,
                                                          app->ui.find_replace.replace_text,
                                                          false);
                if (replaced) {
                    th_ui_set_status(&app->ui, "Replaced occurrence", TH_STATUS_SUCCESS);
                } else {
                    th_ui_set_status(&app->ui, "Pattern not found", TH_STATUS_WARNING);
                }
            } else {
                bool found = th_editor_find_next(&app->editor, app->ui.find_replace.find_text, false);
                if (found) {
                    th_ui_set_status(&app->ui, "Found match", TH_STATUS_SUCCESS);
                } else {
                    th_ui_set_status(&app->ui, "Pattern not found", TH_STATUS_WARNING);
                }
            }
            return;
        }

        char *target_str = (app->ui.find_replace.active_field == 0)
            ? app->ui.find_replace.find_text
            : app->ui.find_replace.replace_text;
        size_t max_len = (app->ui.find_replace.active_field == 0)
            ? sizeof(app->ui.find_replace.find_text) - 1
            : sizeof(app->ui.find_replace.replace_text) - 1;

        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t len = strlen(target_str);
            if (len > 0) target_str[len - 1] = '\0';
            return;
        }

        int ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch >= 32 && ch <= 126) {
                size_t len = strlen(target_str);
                if (len < max_len) {
                    target_str[len] = (char)ch;
                    target_str[len + 1] = '\0';
                    if (app->ui.find_replace.active_field == 0) {
                        th_editor_find_next(&app->editor, app->ui.find_replace.find_text, false);
                    }
                }
            }
        }
        return;
    }

    /* Standard Navigation Keys */
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        th_editor_move_cursor(&app->editor, -1, 0, shift);
        app->ui.completion_popup_active = false;
        app->ui.hover_popup_active = false;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        th_editor_move_cursor(&app->editor, 1, 0, shift);
        app->ui.completion_popup_active = false;
        app->ui.hover_popup_active = false;
    }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        th_editor_move_cursor(&app->editor, 0, -1, shift);
        app->ui.completion_popup_active = false;
        app->ui.hover_popup_active = false;
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        th_editor_move_cursor(&app->editor, 0, 1, shift);
        app->ui.completion_popup_active = false;
        app->ui.hover_popup_active = false;
    }

    if (IsKeyPressed(KEY_HOME)) {
        th_editor_move_to_line_start(&app->editor, shift);
    }
    if (IsKeyPressed(KEY_END)) {
        th_editor_move_to_line_end(&app->editor, shift);
    }
    if (IsKeyPressed(KEY_PAGE_UP)) {
        th_editor_move_cursor(&app->editor, -app->editor.visible_lines, 0, shift);
    }
    if (IsKeyPressed(KEY_PAGE_DOWN)) {
        th_editor_move_cursor(&app->editor, app->editor.visible_lines, 0, shift);
    }

    /* Editing Keys — must return early to prevent handle_text_input from
       consuming the key queue and swallowing real text keystrokes. */
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        th_editor_backspace(&app->editor);
        app->last_edit_time = GetTime();
        th_renderer_reset_cursor_blink(&app->renderer);
        trigger_completion(app);
        return;
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
        th_editor_delete(&app->editor);
        app->last_edit_time = GetTime();
        th_renderer_reset_cursor_blink(&app->renderer);
        trigger_completion(app);
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) {
        th_editor_insert_newline(&app->editor);
        app->last_edit_time = GetTime();
        /* Drain any char queue entries produced by Enter key */
        while (GetCharPressed() > 0) {}
        th_renderer_reset_cursor_blink(&app->renderer);
        return;
    }
    if (IsKeyPressed(KEY_TAB)) {
        th_editor_indent(&app->editor, shift);
        app->last_edit_time = GetTime();
        th_renderer_reset_cursor_blink(&app->renderer);
        return;
    }

    /* Character Input */
    handle_text_input(app, shift, ctrl, alt, super_key);

    th_renderer_reset_cursor_blink(&app->renderer);

    /* Scroll with cursor */
    if (app->editor.cursor.line < (size_t)app->editor.scroll_y) {
        app->editor.scroll_y = (float)app->editor.cursor.line;
    } else if (app->editor.cursor.line >= (size_t)app->editor.scroll_y + app->editor.visible_lines) {
        app->editor.scroll_y = (float)(app->editor.cursor.line - app->editor.visible_lines + 1);
    }
}

static void handle_mouse_input(ThiruthiApp *app) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        app->editor.scroll_y -= wheel * 3;
        if (app->editor.scroll_y < 0) app->editor.scroll_y = 0;
        if (app->editor.scroll_y >= app->editor.line_count) {
            app->editor.scroll_y = (float)(app->editor.line_count > 0 ? app->editor.line_count - 1 : 0);
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();

        /* Settings Modal Click Handling */
        if (app->ui.settings_open) {
            float mw = 620.0f;
            float mh = 480.0f;
            float mx = ((float)app->renderer.window_width - mw) / 2.0f;
            float my = ((float)app->renderer.window_height - mh) / 2.0f;

            /* Click outside modal closes it */
            if (mouse.x < mx || mouse.x > mx + mw || mouse.y < my || mouse.y > my + mh) {
                app->ui.settings_open = false;
                return;
            }

            /* Row 1: Line Numbers Mode */
            if (mouse.y >= my + 55 && mouse.y <= my + 105) {
                if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_STATIC) {
                    app->config.editor.line_number_mode = TH_LINE_NUMBERS_RELATIVE;
                    th_ui_set_status(&app->ui, "Line numbers: Relative (Vim-style)", TH_STATUS_INFO);
                } else if (app->config.editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) {
                    app->config.editor.line_number_mode = TH_LINE_NUMBERS_NONE;
                    th_ui_set_status(&app->ui, "Line numbers: Hidden", TH_STATUS_INFO);
                } else {
                    app->config.editor.line_number_mode = TH_LINE_NUMBERS_STATIC;
                    th_ui_set_status(&app->ui, "Line numbers: Static", TH_STATUS_INFO);
                }
                return;
            }

            /* Row 2: Theme */
            if (mouse.y >= my + 105 && mouse.y <= my + 155) {
                ThThemeId next_theme = (ThThemeId)((app->config.editor.theme_id + 1) % TH_THEME_COUNT);
                th_config_set_theme(&app->config, next_theme);
                th_ui_set_status(&app->ui, th_config_get_theme_name(next_theme), TH_STATUS_INFO);
                return;
            }

            /* Row 3: Editor Font */
            if (mouse.y >= my + 155 && mouse.y <= my + 205) {
                static int s_mouse_font_idx = 0;
                const char *fonts[] = {
                    "C:\\Windows\\Fonts\\CascadiaMono.ttf",
                    "C:\\Windows\\Fonts\\consola.ttf",
                    "C:\\Windows\\Fonts\\cour.ttf",
                    ""
                };
                const char *font_names[] = {
                    "Cascadia Mono",
                    "Consolas",
                    "Courier New",
                    "Raylib Default"
                };
                s_mouse_font_idx = (s_mouse_font_idx + 1) % 4;
                strncpy(app->config.editor.font_path, fonts[s_mouse_font_idx], sizeof(app->config.editor.font_path) - 1);
                th_renderer_load_font(&app->renderer, app->config.editor.font_path, app->config.editor.font_size);
                char fmsg[128];
                snprintf(fmsg, sizeof(fmsg), "Font changed to: %s", font_names[s_mouse_font_idx]);
                th_ui_set_status(&app->ui, fmsg, TH_STATUS_INFO);
                return;
            }

            /* Row 4: Font Size (+2 px, cycles at 26) */
            if (mouse.y >= my + 205 && mouse.y <= my + 255) {
                app->config.editor.font_size += 2;
                if (app->config.editor.font_size > 26) app->config.editor.font_size = 12;
                app->config.editor.line_height = app->config.editor.font_size + 8;
                th_renderer_load_font(&app->renderer, app->config.editor.font_path, app->config.editor.font_size);
                char smsg[64];
                snprintf(smsg, sizeof(smsg), "Font size: %d px", app->config.editor.font_size);
                th_ui_set_status(&app->ui, smsg, TH_STATUS_INFO);
                return;
            }

            /* Row 5: Tab Size */
            if (mouse.y >= my + 255 && mouse.y <= my + 305) {
                if (app->config.editor.tab_size == 2) app->config.editor.tab_size = 4;
                else if (app->config.editor.tab_size == 4) app->config.editor.tab_size = 8;
                else app->config.editor.tab_size = 2;
                char tmsg[64];
                snprintf(tmsg, sizeof(tmsg), "Tab size: %d spaces", app->config.editor.tab_size);
                th_ui_set_status(&app->ui, tmsg, TH_STATUS_INFO);
                return;
            }

            /* Row 6: Cursor Style */
            if (mouse.y >= my + 305 && mouse.y <= my + 355) {
                if (app->config.editor.cursor_style == TH_CURSOR_BAR) {
                    app->config.editor.cursor_style = TH_CURSOR_BLOCK;
                    th_ui_set_status(&app->ui, "Cursor style: Block (█)", TH_STATUS_INFO);
                } else if (app->config.editor.cursor_style == TH_CURSOR_BLOCK) {
                    app->config.editor.cursor_style = TH_CURSOR_UNDERLINE;
                    th_ui_set_status(&app->ui, "Cursor style: Underline (_)", TH_STATUS_INFO);
                } else {
                    app->config.editor.cursor_style = TH_CURSOR_BAR;
                    th_ui_set_status(&app->ui, "Cursor style: Bar (|)", TH_STATUS_INFO);
                }
                th_renderer_reset_cursor_blink(&app->renderer);
                return;
            }

            /* Row 7: Cursor Blink */
            if (mouse.y >= my + 355 && mouse.y <= my + 405) {
                app->config.editor.cursor_blink = !app->config.editor.cursor_blink;
                char bmsg[64];
                snprintf(bmsg, sizeof(bmsg), "Cursor blink: %s", app->config.editor.cursor_blink ? "Enabled" : "Disabled");
                th_ui_set_status(&app->ui, bmsg, TH_STATUS_INFO);
                th_renderer_reset_cursor_blink(&app->renderer);
                return;
            }

            return;
        }

        /* Click Header Buttons */
        if (mouse.y >= 0 && mouse.y <= 38) {
            float right_offset = (float)app->renderer.window_width;
            if (mouse.x >= right_offset - 105 && mouse.x <= right_offset - 8) {
                app->ui.settings_open = !app->ui.settings_open;
                if (app->ui.settings_open) {
                    th_ui_set_status(&app->ui, "Settings opened", TH_STATUS_INFO);
                }
            } else if (mouse.x >= right_offset - 203 && mouse.x < right_offset - 105) {
                app->ui.problems_panel_open = !app->ui.problems_panel_open;
            } else if (mouse.x >= right_offset - 276 && mouse.x < right_offset - 203) {
                trigger_lint(app);
            } else if (mouse.x >= right_offset - 359 && mouse.x < right_offset - 276) {
                trigger_format(app);
            } else if (mouse.x >= right_offset - 432 && mouse.x < right_offset - 359) {
                trigger_save(app);
            }
            return;
        }

        /* Click within code canvas */
        float gutter_w = 56.0f;
        float sidebar_w = app->ui.sidebar_open ? 220.0f : 0.0f;
        float code_x = sidebar_w + gutter_w;
        float code_y = 38.0f;

        if (mouse.x >= code_x && mouse.y >= code_y) {
            int line_h = app->config.editor.line_height;
            int clicked_line = (int)app->editor.scroll_y + (int)((mouse.y - code_y) / line_h);
            if (clicked_line >= 0 && (size_t)clicked_line < app->editor.line_count) {
                /* Approximate char width: ~9 pixels */
                float col_offset = mouse.x - code_x - 8.0f;
                int clicked_col = col_offset > 0 ? (int)(col_offset / 9.5f) : 0;
                th_editor_set_cursor(&app->editor, clicked_line, clicked_col, IsKeyDown(KEY_LEFT_SHIFT));
                th_renderer_reset_cursor_blink(&app->renderer);

                /* Request hover tooltip at clicked location */
                th_lsp_request_hover(&app->lsp, app->editor.filepath, app->editor.cursor);
            }
        }
    }
}

/* --- Service Update --- */

static void update_services(ThiruthiApp *app) {
    /* Poll LSP messages */
    th_lsp_poll(&app->lsp);

    /* Reparse tree-sitter AST if buffer modified */
    if (app->editor.version != app->last_parsed_version) {
        size_t len = 0;
        char *full_text = th_editor_get_full_text(&app->editor, &len);
        if (full_text) {
            th_parser_parse_buffer(&app->parser, full_text, len, app->editor.line_count);
            th_lsp_did_change(&app->lsp, app->editor.filepath, app->editor.version, full_text);
            th_free(full_text);
        }
        app->last_parsed_version = app->editor.version;
    }
}

/* --- Main Entry Point --- */

int main(int argc, char **argv) {
    printf("Starting Thiruthi Code Editor...\n");

    /* 1. Initialize Memory Service */
    th_memory_init();

    /* 2. Initialize Config Service */
    th_config_init(&g_app.config);

    /* 3. Initialize File Service */
    th_file_service_init(&g_app.file_service);

    /* 4. Initialize Editor Service */
    th_editor_init(&g_app.editor);

    /* 5. Initialize Parser Service (Tree-sitter) */
    th_parser_init(&g_app.parser);

    /* 6. Initialize LSP Client */
    th_lsp_init(&g_app.lsp);
    th_lsp_set_diagnostics_callback(&g_app.lsp, on_lsp_diagnostics, &g_app);
    th_lsp_set_completions_callback(&g_app.lsp, on_lsp_completions, &g_app);
    th_lsp_set_hover_callback(&g_app.lsp, on_lsp_hover, &g_app);

    /* 7. Initialize Formatter & Linter */
    th_formatter_init(&g_app.formatter);
    th_linter_init(&g_app.linter);

    /* 8. Initialize UI State */
    th_ui_init(&g_app.ui);

    /* 9. Load Target File or Default Buffer */
    const char *target_file = (argc > 1) ? argv[1] : "sample.c";
    bool loaded = th_editor_load_file(&g_app.editor, target_file);
    if (!loaded || g_app.editor.line_count <= 1) {
        const char *welcome_c =
            "#include <stdio.h>\n"
            "#include <stdlib.h>\n"
            "\n"
            "// Welcome to Thiruthi - Production Code Editor in C\n"
            "// Raylib Rendering + Clay UI + Tree-sitter + LSP\n"
            "\n"
            "int main(int argc, char **argv) {\n"
            "    printf(\"Hello from Thiruthi!\\n\");\n"
            "    return 0;\n"
            "}\n";
        th_editor_load_text(&g_app.editor, welcome_c, strlen(welcome_c));
        strncpy(g_app.editor.filepath, target_file, sizeof(g_app.editor.filepath) - 1);
        g_app.editor.is_dirty = false;
    }

    /* 10. Detect Language & Configure Parser and LSP */
    g_app.editor.language = th_config_detect_language(&g_app.config, g_app.editor.filepath);
    const ThLanguageConfig *lcfg = th_config_get_language(&g_app.config, g_app.editor.language);

    th_parser_set_language(&g_app.parser, g_app.editor.language);

    /* Initial AST parse */
    size_t initial_len = 0;
    char *initial_text = th_editor_get_full_text(&g_app.editor, &initial_len);
    if (initial_text) {
        th_parser_parse_buffer(&g_app.parser, initial_text, initial_len, g_app.editor.line_count);
        th_free(initial_text);
    }
    g_app.last_parsed_version = g_app.editor.version;

    /* Start LSP Server if configured */
    if (lcfg && lcfg->lsp_command) {
        bool lsp_ok = th_lsp_start(&g_app.lsp, lcfg->lsp_command, ".");
        if (lsp_ok) {
            size_t doc_len = 0;
            char *doc_text = th_editor_get_full_text(&g_app.editor, &doc_len);
            th_lsp_did_open(&g_app.lsp, g_app.editor.filepath, g_app.editor.language, doc_text);
            if (doc_text) th_free(doc_text);
            th_ui_set_status(&g_app.ui, "LSP server initialized", TH_STATUS_SUCCESS);
        }
    }

    /* 11. Initialize Renderer & Raylib Window */
    th_renderer_init(&g_app.renderer, 1280, 800, "Thiruthi - Code Editor");

    /* Load configured system font (Consolas / Cascadia Mono / TTF) */
    th_renderer_load_font(&g_app.renderer, g_app.config.editor.font_path, g_app.config.editor.font_size);

    /* 12. Main Application Loop */
    while (!th_renderer_should_close(&g_app.renderer)) {
        /* NOTE: Do NOT call PollInputEvents() here. In Raylib 6.x,
           EndDrawing() already calls PollInputEvents() at the end of
           each frame, buffering events for the next iteration.
           Calling it again here would overwrite those buffered events
           with an empty poll, causing typed characters to be lost. */

        /* Process User Inputs */
        handle_keyboard_input(&g_app);
        handle_mouse_input(&g_app);

        /* Update Services (LSP polling, Tree-sitter) */
        update_services(&g_app);

        /* Begin Frame Layout */
        th_renderer_begin_frame(&g_app.renderer, &g_app.config.editor);

        /* Build Clay Layout Tree */
        th_ui_build_layout(&g_app.ui, &g_app.editor, &g_app.config, &g_app.parser, &g_app.renderer);

        /* End Frame & Raylib Render */
        th_renderer_end_frame(&g_app.renderer, &g_app.config.editor.theme);
    }

    /* 13. Graceful Microservice Shutdown */
    printf("Shutting down Thiruthi services...\n");
    th_renderer_shutdown(&g_app.renderer);
    th_ui_shutdown(&g_app.ui);
    th_linter_shutdown(&g_app.linter);
    th_formatter_shutdown(&g_app.formatter);
    th_lsp_shutdown(&g_app.lsp);
    th_parser_shutdown(&g_app.parser);
    th_editor_shutdown(&g_app.editor);
    th_file_service_shutdown(&g_app.file_service);
    th_config_shutdown(&g_app.config);
    th_memory_report();
    th_memory_shutdown();

    printf("Thiruthi terminated cleanly.\n");
    return 0;
}
