/**
 * @file layout.c
 * @brief UI Layout building and state management using Clay.
 */

#include "layout.h"
#include "../common/memory.h"
#include "clay.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* =====================================================================
 *  Frame Dynamic String Arena
 * ===================================================================== */

static char s_frame_strings[65536];
static size_t s_frame_str_offset = 0;

static Clay_String ui_dyn_str(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *dest = s_frame_strings + s_frame_str_offset;
    size_t rem = sizeof(s_frame_strings) - s_frame_str_offset;
    if (rem < 16) {
        va_end(args);
        return (Clay_String){ .isStaticallyAllocated = false, .length = 0, .chars = "" };
    }
    int written = vsnprintf(dest, rem, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= rem) {
        return (Clay_String){ .isStaticallyAllocated = false, .length = 0, .chars = "" };
    }
    s_frame_str_offset += (size_t)written + 1;
    return (Clay_String){ .isStaticallyAllocated = false, .length = (int32_t)written, .chars = dest };
}

/* =====================================================================
 *  UI State Management
 * ===================================================================== */

void th_ui_init(ThUIState *ui) {
    if (!ui) return;
    memset(ui, 0, sizeof(ThUIState));
    ui->status_type = TH_STATUS_INFO;
}

void th_ui_shutdown(ThUIState *ui) {
    if (!ui) return;
    if (ui->diagnostics.items) {
        th_free(ui->diagnostics.items);
        ui->diagnostics.items = NULL;
    }
    ui->diagnostics.count = 0;
    ui->diagnostics.capacity = 0;

    if (ui->completion_list.items) {
        th_free(ui->completion_list.items);
        ui->completion_list.items = NULL;
    }
    ui->completion_list.count = 0;
    ui->completion_list.capacity = 0;
}

void th_ui_set_status(ThUIState *ui, const char *msg, ThStatusLevel type) {
    if (!ui) return;
    if (msg) {
        strncpy(ui->status_message, msg, sizeof(ui->status_message) - 1);
        ui->status_message[sizeof(ui->status_message) - 1] = '\0';
    } else {
        ui->status_message[0] = '\0';
    }
    ui->status_type = type;
    ui->status_time = 0;
}

/* =====================================================================
 *  Helper: Clay_String from literal C string
 * ===================================================================== */

static Clay_String clay_str(const char *s) {
    Clay_String cs;
    cs.isStaticallyAllocated = false;
    cs.length = (int32_t)(s ? strlen(s) : 0);
    cs.chars = s ? s : "";
    return cs;
}

/* =====================================================================
 *  Header Bar
 * ===================================================================== */

static void build_header(const ThUIState *ui, const ThEditor *editor, const ThTheme *theme) {
    (void)ui;
    CLAY(CLAY_ID("Header"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(42) },
            .padding = { .left = 12, .right = 12, .top = 6, .bottom = 6 },
            .childGap = 12,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, theme->bg_surface.a }
    }) {
        /* File name */
        const char *dirty = editor->is_dirty ? " ●" : "";
        const char *fname = editor->filepath[0] ? editor->filepath : "Untitled";
        Clay_String title_str = ui_dyn_str("  %s%s", fname, dirty);
        CLAY_TEXT(title_str, {
            .fontSize = 15,
            .textColor = { theme->text_normal.r, theme->text_normal.g, theme->text_normal.b, 255 }
        });

        /* Spacer to push buttons to right */
        CLAY(CLAY_ID("HeaderSpacer"), {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1) } }
        }) {}

        /* Action Buttons */
        CLAY(CLAY_ID("BtnSave"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(74), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 8, .right = 8 },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            CLAY_TEXT(clay_str("Save"), {
                .fontSize = 14,
                .textColor = { 225, 225, 235, 255 }
            });
        }

        CLAY(CLAY_ID("BtnFormat"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(84), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 8, .right = 8 },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            CLAY_TEXT(clay_str("Format"), {
                .fontSize = 14,
                .textColor = { 225, 225, 235, 255 }
            });
        }

        CLAY(CLAY_ID("BtnLint"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(74), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 8, .right = 8 },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            CLAY_TEXT(clay_str("Lint"), {
                .fontSize = 14,
                .textColor = { 225, 225, 235, 255 }
            });
        }

        CLAY(CLAY_ID("BtnProblems"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 8, .right = 8 },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = ui->problems_panel_open
                ? (Clay_Color){ theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                : (Clay_Color){ theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            CLAY_TEXT(clay_str("Problems"), {
                .fontSize = 14,
                .textColor = ui->problems_panel_open
                    ? (Clay_Color){ 20, 20, 28, 255 }
                    : (Clay_Color){ 225, 225, 235, 255 }
            });
        }

        /* Settings Button */
        CLAY(CLAY_ID("SettingsButton"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 8, .right = 8 },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = ui->settings_open
                ? (Clay_Color){ theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                : (Clay_Color){ theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            CLAY_TEXT(clay_str("Settings"), {
                .fontSize = 14,
                .textColor = ui->settings_open
                    ? (Clay_Color){ 20, 20, 28, 255 }
                    : (Clay_Color){ 225, 225, 235, 255 }
            });
        }
    }
}

/* =====================================================================
 *  Status Bar
 * ===================================================================== */

static void build_status_bar(const ThUIState *ui, const ThEditor *editor, const ThConfigService *config, const ThTheme *theme) {
    CLAY(CLAY_ID("StatusBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(32) },
            .padding = { .left = 16, .right = 16, .top = 4, .bottom = 4 },
            .childGap = 20,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        },
        .backgroundColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
    }) {
        /* Cursor position */
        Clay_String pos_str = ui_dyn_str("Ln %u, Col %u", editor->cursor.line + 1, editor->cursor.col + 1);
        CLAY_TEXT(pos_str, {
            .fontSize = 14,
            .textColor = { 20, 20, 28, 255 }
        });

        /* Line numbers mode indicator */
        const char *lmode = "Static Lines";
        if (config->editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) lmode = "Relative Lines";
        else if (config->editor.line_number_mode == TH_LINE_NUMBERS_NONE) lmode = "No Line Numbers";
        Clay_String mode_str = ui_dyn_str("[%s]", lmode);
        CLAY_TEXT(mode_str, {
            .fontSize = 13,
            .textColor = { 45, 45, 58, 235 }
        });

        /* Status message */
        if (ui->status_message[0]) {
            Clay_String msg_str = ui_dyn_str("%s", ui->status_message);
            CLAY_TEXT(msg_str, {
                .fontSize = 14,
                .textColor = { 20, 20, 28, 255 }
            });
        }

        /* Line count */
        Clay_String lines_str = ui_dyn_str("%zu lines", editor->line_count);
        CLAY_TEXT(lines_str, {
            .fontSize = 13,
            .textColor = { 45, 45, 58, 235 }
        });
    }
}

/* =====================================================================
 *  Line Number Gutter
 * ===================================================================== */

static void build_gutter(const ThEditor *editor, const ThConfigService *config, const ThTheme *theme) {
    if (config->editor.line_number_mode == TH_LINE_NUMBERS_NONE) {
        return;
    }

    int line_h = config->editor.line_height;
    int visible = editor->visible_lines > 0 ? editor->visible_lines : 30;
    int scroll_line = (int)editor->scroll_y;

    CLAY(CLAY_ID("Gutter"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(64), .height = CLAY_SIZING_GROW(0) },
            .padding = { .right = 10, .top = 4 },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { theme->bg_gutter.r, theme->bg_gutter.g, theme->bg_gutter.b, theme->bg_gutter.a }
    }) {
        for (int i = 0; i < visible && (scroll_line + i) < (int)editor->line_count; i++) {
            int line_idx = scroll_line + i;
            bool is_active = ((uint32_t)line_idx == editor->cursor.line);

            Clay_String num_str;
            if (config->editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) {
                if (is_active) {
                    num_str = ui_dyn_str("%4d", line_idx + 1);
                } else {
                    int dist = abs(line_idx - (int)editor->cursor.line);
                    num_str = ui_dyn_str("%4d", dist);
                }
            } else {
                /* Static absolute line numbers */
                num_str = ui_dyn_str("%4d", line_idx + 1);
            }

            CLAY(CLAY_IDI("GutterLine", i), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED((float)line_h) },
                    .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER }
                }
            }) {
                ThColor col = is_active ? theme->text_gutter_active : theme->text_gutter;
                CLAY_TEXT(num_str, {
                    .fontSize = (uint16_t)config->editor.font_size,
                    .textColor = { col.r, col.g, col.b, col.a }
                });
            }
        }
    }
}

/* =====================================================================
 *  Breadcrumbs / Scope Bar
 * ===================================================================== */

static void build_breadcrumbs_bar(
    const ThEditor *editor,
    const ThParserService *parser,
    const ThTheme *theme
) {
    char scope_buf[128] = {0};
    bool has_scope = false;
    if (parser && editor) {
        has_scope = th_parser_get_enclosing_scope(parser, editor->cursor.line, scope_buf, sizeof(scope_buf));
    }

    const char *fname = (editor && editor->filepath[0]) ? editor->filepath : "Untitled";
    const char *slash = strrchr(fname, '/');
    if (!slash) slash = strrchr(fname, '\\');
    const char *base_name = slash ? (slash + 1) : fname;

    CLAY(CLAY_ID("BreadcrumbsBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(28) },
            .padding = { .left = 14, .right = 14 },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .childGap = 8
        },
        .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, 255 }
    }) {
        Clay_String file_item = ui_dyn_str("📁 %s", base_name);
        CLAY_TEXT(file_item, {
            .fontSize = 13,
            .textColor = { theme->text_gutter.r, theme->text_gutter.g, theme->text_gutter.b, 255 }
        });

        if (has_scope && scope_buf[0]) {
            CLAY_TEXT(clay_str("›"), {
                .fontSize = 13,
                .textColor = { theme->text_gutter.r, theme->text_gutter.g, theme->text_gutter.b, 180 }
            });

            Clay_String scope_item = ui_dyn_str("⚡ %s", scope_buf);
            CLAY_TEXT(scope_item, {
                .fontSize = 13,
                .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
            });
        }
    }
}

/* =====================================================================
 *  Code Canvas
 * ===================================================================== */

static void build_code_canvas(
    const ThUIState *ui,
    const ThEditor *editor,
    const ThConfigService *config,
    const ThParserService *parser,
    const ThRendererService *renderer,
    const ThTheme *theme
) {
    static ThCodeCanvasRenderData s_canvas_data;
    s_canvas_data.editor = editor;
    s_canvas_data.config = config;
    s_canvas_data.parser = parser;
    s_canvas_data.renderer = renderer;
    s_canvas_data.theme = theme;
    s_canvas_data.ui = ui;

    CLAY(CLAY_ID("CodeCanvas"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }
        },
        .custom = {
            .customData = &s_canvas_data
        }
    }) {
    }
}

/* =====================================================================
 *  Problems Panel
 * ===================================================================== */

static void build_problems_panel(const ThUIState *ui, const ThTheme *theme) {
    if (!ui->problems_panel_open) return;

    CLAY(CLAY_ID("ProblemsPanel"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(150) },
            .padding = CLAY_PADDING_ALL(8),
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childGap = 2
        },
        .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, 255 }
    }) {
        char header[64];
        snprintf(header, sizeof(header), "Problems (%zu)", ui->diagnostics.count);
        CLAY_TEXT(clay_str(header), {
            .fontSize = 13,
            .textColor = { theme->text_normal.r, theme->text_normal.g, theme->text_normal.b, 255 }
        });

        for (size_t d = 0; d < ui->diagnostics.count && d < 20; d++) {
            const ThDiagnostic *diag = &ui->diagnostics.items[d];
            char diag_buf[256];
            const char *sev = "info";
            if (diag->severity == TH_DIAG_ERROR) sev = "error";
            else if (diag->severity == TH_DIAG_WARNING) sev = "warning";

            snprintf(diag_buf, sizeof(diag_buf), "  [%s] Ln %u: %.180s",
                     sev, diag->range.start.line + 1, diag->message);

            ThColor col = theme->text_normal;
            if (diag->severity == TH_DIAG_ERROR) col = theme->diag_error;
            else if (diag->severity == TH_DIAG_WARNING) col = theme->diag_warning;

            CLAY_TEXT(clay_str(diag_buf), {
                .fontSize = 12,
                .textColor = { col.r, col.g, col.b, col.a }
            });
        }
    }
}

/* =====================================================================
 *  Find & Replace Bar
 * ===================================================================== */

static void build_find_replace_bar(const ThUIState *ui, const ThTheme *theme) {
    CLAY(CLAY_ID("FindReplaceBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40) },
            .padding = { .left = 16, .right = 16 },
            .childGap = 16,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, 255 }
    }) {
        char find_display[160];
        snprintf(find_display, sizeof(find_display), "Find: %s%s",
                 ui->find_replace.find_text[0] ? ui->find_replace.find_text : "(type to search)",
                 ui->find_replace.active_field == 0 ? "_" : "");
        CLAY(CLAY_ID("FindField"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(240), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 10, .right = 10 },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = ui->find_replace.active_field == 0
                ? (Clay_Color){ theme->bg_selection.r, theme->bg_selection.g, theme->bg_selection.b, 255 }
                : (Clay_Color){ theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(4)
        }) {
            CLAY_TEXT(clay_str(find_display), {
                .fontSize = 13,
                .textColor = { theme->text_normal.r, theme->text_normal.g, theme->text_normal.b, 255 }
            });
        }

        if (ui->find_replace.is_replace_mode) {
            char repl_display[160];
            snprintf(repl_display, sizeof(repl_display), "Replace: %s%s",
                     ui->find_replace.replace_text[0] ? ui->find_replace.replace_text : "(replacement)",
                     ui->find_replace.active_field == 1 ? "_" : "");
            CLAY(CLAY_ID("ReplaceField"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIXED(240), .height = CLAY_SIZING_FIXED(30) },
                    .padding = { .left = 10, .right = 10 },
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = ui->find_replace.active_field == 1
                    ? (Clay_Color){ theme->bg_selection.r, theme->bg_selection.g, theme->bg_selection.b, 255 }
                    : (Clay_Color){ theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(4)
            }) {
                CLAY_TEXT(clay_str(repl_display), {
                    .fontSize = 13,
                    .textColor = { theme->text_normal.r, theme->text_normal.g, theme->text_normal.b, 255 }
                });
            }
        }

        const char *hint = ui->find_replace.is_replace_mode
            ? "Enter: Replace & Next | Tab: Switch field | Esc: Close"
            : "Enter: Next | Shift+Enter: Prev | Esc: Close";
        CLAY_TEXT(clay_str(hint), {
            .fontSize = 12,
            .textColor = { theme->text_gutter.r, theme->text_gutter.g, theme->text_gutter.b, 255 }
        });
    }
}

static void build_goto_line_bar(const ThUIState *ui, const ThTheme *theme) {
    CLAY(CLAY_ID("GotoLineBar"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40) },
            .padding = { .left = 16, .right = 16 },
            .childGap = 16,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, 255 }
    }) {
        char goto_buf[64];
        snprintf(goto_buf, sizeof(goto_buf), "Go to Line: %s_", ui->goto_line.line_input);
        CLAY(CLAY_ID("GotoField"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(180), .height = CLAY_SIZING_FIXED(30) },
                .padding = { .left = 10, .right = 10 },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = { theme->bg_selection.r, theme->bg_selection.g, theme->bg_selection.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(4)
        }) {
            CLAY_TEXT(clay_str(goto_buf), {
                .fontSize = 13,
                .textColor = { theme->text_normal.r, theme->text_normal.g, theme->text_normal.b, 255 }
            });
        }

        CLAY_TEXT(clay_str("Press Enter to jump, Esc to cancel"), {
            .fontSize = 12,
            .textColor = { theme->text_gutter.r, theme->text_gutter.g, theme->text_gutter.b, 255 }
        });
    }
}

static void build_scrollbar(const ThEditor *editor, const ThTheme *theme) {
    if (!editor || editor->line_count <= 0) return;

    float total_lines = (float)editor->line_count;
    float visible = editor->visible_lines > 0 ? (float)editor->visible_lines : 30.0f;
    float scroll_y = editor->scroll_y;

    float thumb_ratio = visible / total_lines;
    if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
    if (thumb_ratio < 0.05f) thumb_ratio = 0.05f;

    float max_scroll = (total_lines > visible) ? (total_lines - visible) : 1.0f;
    float track_top_ratio = scroll_y / max_scroll;
    if (track_top_ratio < 0.0f) track_top_ratio = 0.0f;
    if (track_top_ratio > 1.0f) track_top_ratio = 1.0f;

    CLAY(CLAY_ID("ScrollbarTrack"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(8), .height = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { theme->bg_gutter.r, theme->bg_gutter.g, theme->bg_gutter.b, 60 }
    }) {
        float spacer_ratio = track_top_ratio * (1.0f - thumb_ratio);
        if (spacer_ratio > 0.01f) {
            CLAY(CLAY_ID("ScrollSpacer"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_PERCENT(spacer_ratio) }
                }
            }) {}
        }

        CLAY(CLAY_ID("ScrollThumb"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_PERCENT(thumb_ratio) }
            },
            .backgroundColor = { theme->bg_selection.r, theme->bg_selection.g, theme->bg_selection.b, 180 },
            .cornerRadius = CLAY_CORNER_RADIUS(4)
        }) {}
    }
}

/* =====================================================================
 *  Settings Modal Dialog
 * ===================================================================== */

static void build_settings_modal(const ThUIState *ui, const ThConfigService *config, const ThTheme *theme) {
    if (!ui->settings_open) return;

    /* Full-screen semi-transparent overlay */
    CLAY(CLAY_ID("SettingsOverlay"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 0, 0, 0, 180 }
    }) {
        /* Modal Window Box */
        CLAY(CLAY_ID("SettingsDialog"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(720), .height = CLAY_SIZING_FIXED(560) },
                .padding = CLAY_PADDING_ALL(24),
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 10
            },
            .backgroundColor = { theme->bg_surface.r, theme->bg_surface.g, theme->bg_surface.b, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(10)
        }) {
            /* Title */
            CLAY(CLAY_ID("SettingsTitleRow"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(36) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                }
            }) {
                CLAY_TEXT(clay_str("⚙ Settings"), {
                    .fontSize = 24,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 1: Line Numbers Mode */
            const char *mode_str = "Static (1, 2, 3...)";
            if (config->editor.line_number_mode == TH_LINE_NUMBERS_RELATIVE) {
                mode_str = "Relative (Vim-style)";
            } else if (config->editor.line_number_mode == TH_LINE_NUMBERS_NONE) {
                mode_str = "None (Hidden)";
            }

            CLAY(CLAY_ID("SettingRowLines"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Line Numbers:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String val = ui_dyn_str("[ %s ]  (Press 'L' to cycle: Static / Relative / None)", mode_str);
                CLAY_TEXT(val, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 2: Theme */
            CLAY(CLAY_ID("SettingRowTheme"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Theme:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String theme_value = ui_dyn_str("[ %s ]  (Press Ctrl+T to cycle)",
                                                     th_config_get_theme_name(config->editor.theme_id));
                CLAY_TEXT(theme_value, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 3: Font Selection */
            const char *font_display = "Default";
            if (strstr(config->editor.font_path, "iosevka")) font_display = "Iosevka (TTF)";
            else if (strstr(config->editor.font_path, "JetBrains")) font_display = "JetBrains Mono (TTF)";
            else if (strstr(config->editor.font_path, "Cascadia")) font_display = "Cascadia Mono";
            else if (strstr(config->editor.font_path, "consola")) font_display = "Consolas";
            else if (strstr(config->editor.font_path, "cour")) font_display = "Courier New";
            else if (config->editor.font_path[0]) {
                const char *s = strrchr(config->editor.font_path, '\\');
                if (!s) s = strrchr(config->editor.font_path, '/');
                font_display = s ? (s + 1) : config->editor.font_path;
            }

            CLAY(CLAY_ID("SettingRowFont"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Editor Font:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String fval = ui_dyn_str("[ %s ]  (Press 'F' to cycle fonts)", font_display);
                CLAY_TEXT(fval, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 4: Font Size */
            CLAY(CLAY_ID("SettingRowSize"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Font Size:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String sval = ui_dyn_str("[ %d px ]  (Press '+' or '-' to adjust)", config->editor.font_size);
                CLAY_TEXT(sval, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 5: Tab Size */
            CLAY(CLAY_ID("SettingRowTab"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Tab Size:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String tval = ui_dyn_str("[ %d spaces ]  (Press 'T' to cycle: 2, 4, 8)", config->editor.tab_size);
                CLAY_TEXT(tval, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 6: Cursor Style */
            const char *cur_style_str = "Bar (|)";
            if (config->editor.cursor_style == TH_CURSOR_BLOCK) {
                cur_style_str = "Block (█)";
            } else if (config->editor.cursor_style == TH_CURSOR_UNDERLINE) {
                cur_style_str = "Underline (_)";
            }

            CLAY(CLAY_ID("SettingRowCursorStyle"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Cursor Style:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String csval = ui_dyn_str("[ %s ]  (Press 'C' to cycle: Bar / Block / Underline)", cur_style_str);
                CLAY_TEXT(csval, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Option 7: Cursor Blink */
            const char *blink_str = config->editor.cursor_blink ? "Enabled (Blinking)" : "Disabled (Solid)";
            CLAY(CLAY_ID("SettingRowCursorBlink"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(46) },
                    .padding = { .left = 16, .right = 16 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .childGap = 16
                },
                .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                CLAY_TEXT(clay_str("Cursor Blink:"), {
                    .fontSize = 15,
                    .textColor = { 240, 240, 245, 255 }
                });

                Clay_String cbval = ui_dyn_str("[ %s ]  (Press 'B' to toggle)", blink_str);
                CLAY_TEXT(cbval, {
                    .fontSize = 14,
                    .textColor = { theme->accent.r, theme->accent.g, theme->accent.b, 255 }
                });
            }

            /* Footer hints */
            CLAY(CLAY_ID("SettingsFooter"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM }
                }
            }) {
                CLAY_TEXT(clay_str("Press Esc or Ctrl+, to close | Ctrl+T: Theme | L: Lines | F: Font | +/-: Size | T: Tab | C: Cursor | B: Blink"), {
                    .fontSize = 13,
                    .textColor = { 180, 180, 195, 255 }
                });
            }
        }
    }
}

/* =====================================================================
 *  Main Layout Builder
 * ===================================================================== */

void th_ui_build_layout(
    ThUIState *ui,
    const ThEditor *editor,
    const ThConfigService *config,
    const ThParserService *parser,
    const ThRendererService *renderer
) {
    if (!ui || !editor || !config) return;

    /* Reset frame dynamic string offset */
    s_frame_str_offset = 0;

    const ThTheme *theme = th_config_get_theme(config);
    if (!theme) return;

    /* Root container — fills entire window */
    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { theme->bg_main.r, theme->bg_main.g, theme->bg_main.b, 255 }
    }) {
        /* 1. Header */
        build_header(ui, editor, theme);

        /* 1.1 Find & Replace Panel (if active) */
        if (ui->find_replace.active) {
            build_find_replace_bar(ui, theme);
        }

        /* 1.2 Go to line Prompt (if active) */
        if (ui->goto_line.active) {
            build_goto_line_bar(ui, theme);
        }

        /* 1.3 Scope Breadcrumbs Bar */
        build_breadcrumbs_bar(editor, parser, theme);

        /* 2. Main Body (gutter + code + scrollbar) */
        CLAY(CLAY_ID("MainBody"), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            build_gutter(editor, config, theme);
            build_code_canvas(ui, editor, config, parser, renderer, theme);
            build_scrollbar(editor, theme);
        }

        /* 3. Problems Panel */
        build_problems_panel(ui, theme);

        /* 4. Status Bar */
        build_status_bar(ui, editor, config, theme);

        /* 5. Settings Modal (if active) */
        if (ui->settings_open) {
            build_settings_modal(ui, config, theme);
        }
    }
}
