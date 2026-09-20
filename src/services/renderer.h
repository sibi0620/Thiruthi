/**
 * @file renderer.h
 * @brief Raylib rendering and Clay layout integration service.
 */

#ifndef TH_RENDERER_H
#define TH_RENDERER_H

#include "../common/types.h"
#include "config.h"
#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int window_width;
    int window_height;
    float scale_factor;
    Font editor_font;
    Font ui_font;
    bool font_loaded;

    /* Clay memory arena */
    void *clay_memory;
    size_t clay_memory_size;

    /* Cursor blink state */
    double last_cursor_blink_time;
    bool cursor_visible;
} ThRendererService;

#include "editor.h"
#include "parser.h"

typedef struct {
    const ThEditor *editor;
    const ThConfigService *config;
    const ThParserService *parser;
    const ThRendererService *renderer;
    const ThTheme *theme;
} ThCodeCanvasRenderData;

void th_renderer_init(ThRendererService *service, int width, int height, const char *title);
void th_renderer_shutdown(ThRendererService *service);

bool th_renderer_should_close(const ThRendererService *service);
void th_renderer_begin_frame(ThRendererService *service);
void th_renderer_end_frame(ThRendererService *service);

void th_renderer_handle_resize(ThRendererService *service);
bool th_renderer_load_font(ThRendererService *service, const char *font_path, int font_size);
void th_renderer_reset_cursor_blink(ThRendererService *service);

/* Conversion utilities */
Color th_color_to_raylib(ThColor color);

#ifdef __cplusplus
}
#endif

#endif /* TH_RENDERER_H */
