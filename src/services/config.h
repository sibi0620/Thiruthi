/**
 * @file config.h
 * @brief Configuration service: language detection, tool registry, and theme settings.
 */

#ifndef TH_CONFIG_H
#define TH_CONFIG_H

#include "../common/types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ThLanguageId id;
    const char *name;
    const char **extensions;
    size_t extension_count;
    const char *lsp_command;
    const char *formatter_command;
    const char *linter_command;
    const char *tree_sitter_library;
    const char *tree_sitter_symbol;
} ThLanguageConfig;

typedef struct {
    ThColor bg_main;
    ThColor bg_surface;
    ThColor bg_gutter;
    ThColor bg_active_line;
    ThColor bg_selection;
    ThColor text_normal;
    ThColor text_gutter;
    ThColor text_gutter_active;
    ThColor cursor;
    ThColor border;
    ThColor accent;
    ThColor diag_error;
    ThColor diag_warning;
    ThColor diag_info;
    ThColor diag_hint;

    /* Syntax token colors */
    ThColor token_keyword;
    ThColor token_type;
    ThColor token_function;
    ThColor token_string;
    ThColor token_number;
    ThColor token_comment;
    ThColor token_operator;
    ThColor token_preprocessor;
    ThColor token_variable;
    ThColor token_punctuation;
} ThTheme;

typedef enum {
    TH_LINE_NUMBERS_STATIC = 0,    /* Absolute: 1, 2, 3 ... */
    TH_LINE_NUMBERS_RELATIVE = 1,  /* Vim-style relative numbers */
    TH_LINE_NUMBERS_NONE = 2       /* Hidden */
} ThLineNumberMode;

typedef enum {
    TH_CURSOR_BAR = 0,       /* Vertical bar: | */
    TH_CURSOR_BLOCK = 1,     /* Full character cell block: █ */
    TH_CURSOR_UNDERLINE = 2  /* Bottom underline: _ */
} ThCursorStyle;

typedef struct {
    int tab_size;
    bool use_spaces;
    bool format_on_save;
    bool lint_on_save;
    int font_size;
    int line_height;
    bool show_line_numbers;
    ThLineNumberMode line_number_mode;
    ThCursorStyle cursor_style;
    bool cursor_blink;
    float cursor_blink_rate;
    char font_path[256];
    bool show_status_bar;
    bool show_sidebar;
    bool show_problems_panel;
    ThTheme theme;
} ThEditorConfig;

typedef struct {
    ThEditorConfig editor;
    ThLanguageConfig languages[TH_LANG_COUNT];
} ThConfigService;

void th_config_init(ThConfigService *config);
void th_config_shutdown(ThConfigService *config);

ThLanguageId th_config_detect_language(const ThConfigService *config, const char *filepath);
const ThLanguageConfig *th_config_get_language(const ThConfigService *config, ThLanguageId lang);
const ThTheme *th_config_get_theme(const ThConfigService *config);
ThColor th_config_get_token_color(const ThTheme *theme, ThTokenType token_type);

#ifdef __cplusplus
}
#endif

#endif /* TH_CONFIG_H */
