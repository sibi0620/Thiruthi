/**
 * @file renderer.c
 * @brief Raylib rendering and Clay layout implementation.
 */

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include "renderer.h"
#include "editor.h"
#include "parser.h"
#include "../common/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define CLAY_RECTANGLE_TO_RAYLIB_RECTANGLE(r) ((Rectangle){ .x = (r).x, .y = (r).y, .width = (r).width, .height = (r).height })
#define CLAY_COLOR_TO_RAYLIB_COLOR(c) ((Color){ .r = (unsigned char)roundf((c).r), .g = (unsigned char)roundf((c).g), .b = (unsigned char)roundf((c).b), .a = (unsigned char)roundf((c).a) })

Color th_color_to_raylib(ThColor c) {
    return (Color){c.r, c.g, c.b, c.a};
}

/* --- Clay MeasureText callback --- */

static Clay_Dimensions Raylib_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    Clay_Dimensions size = {0, 0};
    Font *font = (Font *)userData;
    Font fontToUse = (font && font->glyphs) ? *font : GetFontDefault();

    float fontSize = (float)config->fontSize;
    if (fontSize <= 0) fontSize = 16.0f;
    float letterSpacing = (float)config->letterSpacing;

    /* Measure string using stack buffer or dynamic allocation */
    char temp[1024];
    char *dyn = NULL;
    char *str = temp;
    if (text.length >= sizeof(temp)) {
        dyn = (char *)th_malloc(text.length + 1);
        str = dyn;
    }

    if (str) {
        memcpy(str, text.chars, text.length);
        str[text.length] = '\0';
        Vector2 v = MeasureTextEx(fontToUse, str, fontSize, letterSpacing);
        size.width = v.x;
        size.height = v.y;
        if (dyn) th_free(dyn);
    }

    return size;
}

static void draw_text_segment(Font font, const char *text, size_t start, size_t end, Vector2 pos, float fontSize, Color color) {
    if (start >= end) return;
    size_t len = end - start;
    char temp[512];
    char *buf = temp;
    if (len >= sizeof(temp)) {
        buf = (char *)th_malloc(len + 1);
    }
    if (buf) {
        memcpy(buf, text + start, len);
        buf[len] = '\0';
        DrawTextEx(font, buf, pos, fontSize, 0, color);
        if (buf != temp) th_free(buf);
    }
}

static void th_render_code_canvas_custom(Rectangle bb, const ThCodeCanvasRenderData *data, Font *fonts) {
    if (!data || !data->editor || !data->config) return;

    Font fontToUse = (fonts && fonts->glyphs) ? *fonts : GetFontDefault();
    float fontSize = (float)data->config->editor.font_size;
    float line_h = (float)data->config->editor.line_height;
    int visible = data->editor->visible_lines > 0 ? data->editor->visible_lines : 30;
    int scroll_line = (int)data->editor->scroll_y;
    const ThTheme *theme = data->theme ? data->theme : &data->config->editor.theme;

    float char_w = MeasureTextEx(fontToUse, "M", fontSize, 0).x;
    if (char_w <= 0.0f) char_w = fontSize * 0.55f;

    BeginScissorMode((int)roundf(bb.x), (int)roundf(bb.y),
                     (int)roundf(bb.width), (int)roundf(bb.height));

    float pad_x = 8.0f;
    float pad_y = 4.0f;

    /* 1. Active line highlight */
    if (data->editor->cursor.line >= (size_t)scroll_line &&
        data->editor->cursor.line < (size_t)(scroll_line + visible)) {
        int cur_line_screen = (int)data->editor->cursor.line - scroll_line;
        float active_y = bb.y + pad_y + cur_line_screen * line_h;
        DrawRectangleRec((Rectangle){bb.x, active_y, bb.width, line_h},
                         th_color_to_raylib(theme->bg_active_line));
    }

    /* 2. Selection highlight */
    if (th_editor_has_selection(data->editor)) {
        ThRange sel = th_editor_get_selection_range(data->editor);
        for (int i = 0; i < visible && (scroll_line + i) < (int)data->editor->line_count; i++) {
            int li = scroll_line + i;
            if ((uint32_t)li >= sel.start.line && (uint32_t)li <= sel.end.line) {
                size_t l_len = 0;
                th_editor_get_line(data->editor, li, &l_len);
                uint32_t sc = ((uint32_t)li == sel.start.line) ? sel.start.col : 0;
                uint32_t ec = ((uint32_t)li == sel.end.line) ? sel.end.col : (uint32_t)(l_len + 1);
                if (ec > sc) {
                    float sel_x = bb.x + pad_x + sc * char_w;
                    float sel_w = (ec - sc) * char_w;
                    float sel_y = bb.y + pad_y + i * line_h;
                    DrawRectangleRec((Rectangle){sel_x, sel_y, sel_w, line_h},
                                     th_color_to_raylib(theme->bg_selection));
                }
            }
        }
    }

    /* 3. Syntax-highlighted text lines */
    for (int i = 0; i < visible && (scroll_line + i) < (int)data->editor->line_count; i++) {
        int li = scroll_line + i;
        size_t line_len = 0;
        const char *line_text = th_editor_get_line(data->editor, li, &line_len);
        if (!line_text || line_len == 0) continue;

        float line_y = bb.y + pad_y + i * line_h + (line_h - fontSize) * 0.5f;
        float start_x = bb.x + pad_x;

        const ThHighlightLine *hl = data->parser ? th_parser_get_line_highlights(data->parser, li) : NULL;
        if (!hl || hl->count == 0) {
            /* Full line in normal text color */
            draw_text_segment(fontToUse, line_text, 0, line_len,
                              (Vector2){start_x, line_y}, fontSize,
                              th_color_to_raylib(theme->text_normal));
        } else {
            uint32_t cur = 0;
            for (uint32_t s = 0; s < hl->count; s++) {
                ThHighlightSpan span = hl->spans[s];
                if (span.start_col > line_len) span.start_col = (uint32_t)line_len;
                if (span.end_col > line_len) span.end_col = (uint32_t)line_len;

                /* Plain text before span */
                if (span.start_col > cur) {
                    draw_text_segment(fontToUse, line_text, cur, span.start_col,
                                      (Vector2){start_x + cur * char_w, line_y}, fontSize,
                                      th_color_to_raylib(theme->text_normal));
                }

                /* Token text with syntax color */
                if (span.end_col > span.start_col) {
                    ThColor token_col = th_config_get_token_color(theme, span.type);
                    draw_text_segment(fontToUse, line_text, span.start_col, span.end_col,
                                      (Vector2){start_x + span.start_col * char_w, line_y}, fontSize,
                                      th_color_to_raylib(token_col));
                    cur = span.end_col;
                }
            }
            /* Remaining text after spans */
            if (cur < line_len) {
                draw_text_segment(fontToUse, line_text, cur, line_len,
                                  (Vector2){start_x + cur * char_w, line_y}, fontSize,
                                  th_color_to_raylib(theme->text_normal));
            }
        }
    }

    /* 4. Cursor Rendering */
    if (data->editor->cursor.line >= (size_t)scroll_line &&
        data->editor->cursor.line < (size_t)(scroll_line + visible)) {
        int cur_line_screen = (int)data->editor->cursor.line - scroll_line;
        float cx = bb.x + pad_x + data->editor->cursor.col * char_w;
        float cy = bb.y + pad_y + cur_line_screen * line_h;
        Color cursor_color = th_color_to_raylib(theme->cursor);

        bool show_cursor = !data->config->editor.cursor_blink || (data->renderer && data->renderer->cursor_visible);

        if (show_cursor) {
            switch (data->config->editor.cursor_style) {
                case TH_CURSOR_BAR: {
                    /* Vertical bar cursor | */
                    DrawRectangle((int)roundf(cx), (int)roundf(cy + 2.0f), 2, (int)roundf(line_h - 4.0f), cursor_color);
                    break;
                }
                case TH_CURSOR_BLOCK: {
                    /* Full character block cursor █ */
                    DrawRectangle((int)roundf(cx), (int)roundf(cy + 1.0f), (int)roundf(char_w), (int)roundf(line_h - 2.0f), cursor_color);

                    /* Invert character underneath so it is readable */
                    size_t c_len = 0;
                    const char *c_line = th_editor_get_line(data->editor, data->editor->cursor.line, &c_len);
                    if (c_line && data->editor->cursor.col < c_len) {
                        draw_text_segment(fontToUse, c_line, data->editor->cursor.col, data->editor->cursor.col + 1,
                                          (Vector2){cx, cy + (line_h - fontSize) * 0.5f}, fontSize,
                                          th_color_to_raylib(theme->bg_main));
                    }
                    break;
                }
                case TH_CURSOR_UNDERLINE: {
                    /* Horizontal underline cursor _ */
                    DrawRectangle((int)roundf(cx), (int)roundf(cy + line_h - 3.0f), (int)roundf(char_w), 3, cursor_color);
                    break;
                }
            }
        }
    }

    EndScissorMode();
}

/* --- Clay Raylib Render Dispatcher --- */

static void Clay_Raylib_Render(Clay_RenderCommandArray renderCommands, Font *fonts) {
    char *temp_str_buf = NULL;
    size_t temp_str_cap = 0;

    for (int32_t i = 0; i < renderCommands.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&renderCommands, i);
        Rectangle bb = {cmd->boundingBox.x, cmd->boundingBox.y, cmd->boundingBox.width, cmd->boundingBox.height};

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                Clay_CustomRenderData *custom = &cmd->renderData.custom;
                if (custom->customData) {
                    th_render_code_canvas_custom(bb, (const ThCodeCanvasRenderData *)custom->customData, fonts);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData *textData = &cmd->renderData.text;
                Font fontToUse = (fonts && fonts->glyphs) ? *fonts : GetFontDefault();

                size_t needed = textData->stringContents.length + 1;
                if (needed > temp_str_cap) {
                    if (temp_str_buf) th_free(temp_str_buf);
                    temp_str_cap = needed < 256 ? 256 : needed * 2;
                    temp_str_buf = (char *)th_malloc(temp_str_cap);
                }

                if (temp_str_buf) {
                    memcpy(temp_str_buf, textData->stringContents.chars, textData->stringContents.length);
                    temp_str_buf[textData->stringContents.length] = '\0';
                    DrawTextEx(fontToUse, temp_str_buf, (Vector2){bb.x, bb.y},
                               (float)textData->fontSize, (float)textData->letterSpacing,
                               CLAY_COLOR_TO_RAYLIB_COLOR(textData->textColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *rectData = &cmd->renderData.rectangle;
                Color col = CLAY_COLOR_TO_RAYLIB_COLOR(rectData->backgroundColor);
                if (rectData->cornerRadius.topLeft > 0) {
                    float radius = (rectData->cornerRadius.topLeft * 2.0f) /
                        ((bb.width > bb.height) ? bb.height : bb.width);
                    DrawRectangleRounded(bb, radius, 8, col);
                } else {
                    DrawRectangle(bb.x, bb.y, bb.width, bb.height, col);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *border = &cmd->renderData.border;
                Color col = CLAY_COLOR_TO_RAYLIB_COLOR(border->color);

                if (border->width.left > 0) {
                    DrawRectangleV((Vector2){bb.x, bb.y}, (Vector2){border->width.left, bb.height}, col);
                }
                if (border->width.right > 0) {
                    DrawRectangleV((Vector2){bb.x + bb.width - border->width.right, bb.y},
                                   (Vector2){border->width.right, bb.height}, col);
                }
                if (border->width.top > 0) {
                    DrawRectangleV((Vector2){bb.x, bb.y}, (Vector2){bb.width, border->width.top}, col);
                }
                if (border->width.bottom > 0) {
                    DrawRectangleV((Vector2){bb.x, bb.y + bb.height - border->width.bottom},
                                   (Vector2){bb.width, border->width.bottom}, col);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                BeginScissorMode((int)roundf(bb.x), (int)roundf(bb.y),
                                 (int)roundf(bb.width), (int)roundf(bb.height));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                EndScissorMode();
                break;
            }
            default:
                break;
        }
    }

    if (temp_str_buf) {
        th_free(temp_str_buf);
    }
}

static void handle_clay_error(Clay_ErrorData errorData) {
    fprintf(stderr, "[CLAY_ERROR] %.*s\n", (int)errorData.errorText.length, errorData.errorText.chars);
}

/* --- Renderer Service Implementation --- */

void th_renderer_init(ThRendererService *service, int width, int height, const char *title) {
    if (!service) return;
    memset(service, 0, sizeof(ThRendererService));

    service->window_width = width;
    service->window_height = height;
    service->scale_factor = 1.0f;
    service->cursor_visible = true;
    service->last_cursor_blink_time = 0;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(width, height, title ? title : "Thiruthi Code Editor");
    SetTargetFPS(60);

    /* Initialize Clay Arena */
    uint64_t clay_min_mem = Clay_MinMemorySize();
    service->clay_memory_size = clay_min_mem < (1024 * 1024 * 8) ? (1024 * 1024 * 8) : clay_min_mem;
    service->clay_memory = th_malloc(service->clay_memory_size);

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(service->clay_memory_size, service->clay_memory);
    Clay_Initialize(arena, (Clay_Dimensions){(float)width, (float)height}, (Clay_ErrorHandler){handle_clay_error, NULL});

    Clay_SetMeasureTextFunction(Raylib_MeasureText, &service->editor_font);

    /* Font Setup */
    service->font_loaded = false;
    service->editor_font = GetFontDefault();
    service->ui_font = GetFontDefault();

    /* Try to load crisp system font (Consolas / Cascadia Mono on Windows) */
    th_renderer_load_font(service, NULL, 17);
}

bool th_renderer_load_font(ThRendererService *service, const char *font_path, int font_size) {
    if (!service) return false;
    if (font_size <= 0) font_size = 17;

    const char *candidates[4];
    int cand_count = 0;

    if (font_path && font_path[0]) {
        candidates[cand_count++] = font_path;
    }
    candidates[cand_count++] = "C:\\Windows\\Fonts\\consola.ttf";
    candidates[cand_count++] = "C:\\Windows\\Fonts\\CascadiaMono.ttf";
    candidates[cand_count++] = "C:\\Windows\\Fonts\\cour.ttf";

    for (int i = 0; i < cand_count; i++) {
        if (FileExists(candidates[i])) {
            Font loaded = LoadFontEx(candidates[i], font_size * 2, NULL, 0);
            if (loaded.texture.id > 0) {
                SetTextureFilter(loaded.texture, 1);

                if (service->font_loaded) {
                    UnloadFont(service->editor_font);
                }
                service->editor_font = loaded;
                service->ui_font = loaded;
                service->font_loaded = true;

                Clay_SetMeasureTextFunction(Raylib_MeasureText, &service->editor_font);
                printf("[FONT] Successfully loaded font: %s (size %d)\n", candidates[i], font_size);
                return true;
            }
        }
    }

    return false;
}

void th_renderer_shutdown(ThRendererService *service) {
    if (!service) return;

    if (service->font_loaded) {
        UnloadFont(service->editor_font);
        service->font_loaded = false;
    }

    if (service->clay_memory) {
        th_free(service->clay_memory);
        service->clay_memory = NULL;
    }

    CloseWindow();
}

bool th_renderer_should_close(const ThRendererService *service) {
    (void)service;
    return WindowShouldClose();
}

void th_renderer_handle_resize(ThRendererService *service) {
    if (!service) return;
    int cur_w = GetScreenWidth();
    int cur_h = GetScreenHeight();

    if (cur_w != service->window_width || cur_h != service->window_height) {
        service->window_width = cur_w;
        service->window_height = cur_h;
        Clay_SetLayoutDimensions((Clay_Dimensions){(float)cur_w, (float)cur_h});
    }
}

void th_renderer_begin_frame(ThRendererService *service) {
    if (!service) return;
    th_renderer_handle_resize(service);

    /* Blink cursor every 500ms */
    double now = GetTime();
    if (now - service->last_cursor_blink_time >= 0.5) {
        service->cursor_visible = !service->cursor_visible;
        service->last_cursor_blink_time = now;
    }

    /* Update Clay Pointer */
    Vector2 mouse = GetMousePosition();
    Clay_SetPointerState((Clay_Vector2){mouse.x, mouse.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));

    float wheel = GetMouseWheelMove();
    Clay_UpdateScrollContainers(true, (Clay_Vector2){0, wheel * 10.0f}, GetFrameTime());

    Clay_BeginLayout();
}

void th_renderer_end_frame(ThRendererService *service) {
    if (!service) return;

    Clay_RenderCommandArray commands = Clay_EndLayout(GetFrameTime());

    BeginDrawing();
    ClearBackground((Color){24, 24, 24, 255});

    Clay_Raylib_Render(commands, &service->editor_font);

    EndDrawing();
}

void th_renderer_reset_cursor_blink(ThRendererService *service) {
    if (!service) return;
    service->cursor_visible = true;
    service->last_cursor_blink_time = GetTime();
}
