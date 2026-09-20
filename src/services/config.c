/**
 * @file config.c
 * @brief Configuration service implementation.
 */

#include "config.h"
#include "../common/platform.h"
#include <string.h>

static const char *s_c_extensions[] = {".c", ".h"};
static const char *s_cpp_extensions[] = {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".C"};
static const char *s_python_extensions[] = {".py", ".pyw"};
static const char *s_js_extensions[] = {".js", ".mjs", ".cjs", ".jsx"};
static const char *s_ts_extensions[] = {".ts", ".tsx", ".mts", ".cts"};
static const char *s_html_extensions[] = {".html", ".htm"};
static const char *s_css_extensions[] = {".css", ".scss", ".sass"};
static const char *s_markdown_extensions[] = {".md", ".markdown"};

#if TH_PLATFORM_WINDOWS
    #define SO_EXT ".dll"
#else
    #define SO_EXT ".so"
#endif

void th_config_init(ThConfigService *config) {
    if (!config) return;
    memset(config, 0, sizeof(ThConfigService));

    /* Editor defaults */
    config->editor.tab_size = 4;
    config->editor.use_spaces = true;
    config->editor.format_on_save = true;
    config->editor.lint_on_save = true;
    config->editor.font_size = 17;
    config->editor.line_height = 24;
    config->editor.show_line_numbers = true;
    config->editor.line_number_mode = TH_LINE_NUMBERS_STATIC;
    config->editor.cursor_style = TH_CURSOR_BAR;
    config->editor.cursor_blink = true;
    config->editor.cursor_blink_rate = 0.5f;
#if TH_PLATFORM_WINDOWS
    strncpy(config->editor.font_path, "C:\\Windows\\Fonts\\consola.ttf", sizeof(config->editor.font_path) - 1);
#else
    config->editor.font_path[0] = '\0';
#endif
    config->editor.show_status_bar = true;
    config->editor.show_sidebar = false;
    config->editor.show_problems_panel = false;

    /* Theme: Gruber Darker palette */
    config->editor.theme = (ThTheme){
        .bg_main           = {24, 24, 24, 255},     /* Deep Background: #181818 */
        .bg_surface        = {40, 40, 40, 255},     /* Surface / Panel: #282828 */
        .bg_gutter         = {16, 16, 16, 255},     /* Gutter Background: #101010 */
        .bg_active_line    = {45, 45, 45, 160},     /* Active line highlight */
        .bg_selection      = {72, 72, 72, 200},     /* Selection: #484848 */
        .text_normal       = {228, 228, 239, 255},  /* Foreground Text: #e4e4ef */
        .text_gutter       = {95, 98, 127, 255},    /* Niagara-Dim: #5f627f */
        .text_gutter_active= {255, 221, 51, 255},   /* Yellow: #ffdd33 */
        .cursor            = {255, 221, 51, 255},   /* Gruber Darker Yellow: #ffdd33 */
        .border            = {69, 61, 65, 255},     /* #453d41 */
        .accent            = {255, 221, 51, 255},   /* Yellow: #ffdd33 */
        .diag_error        = {244, 56, 65, 255},    /* Red: #f43841 */
        .diag_warning      = {255, 221, 51, 255},   /* Yellow: #ffdd33 */
        .diag_info         = {150, 166, 200, 255},  /* Niagara: #96a6c8 */
        .diag_hint         = {149, 169, 159, 255},  /* Quartz: #95a99f */

        /* Syntax colors */
        .token_keyword     = {255, 221, 51, 255},   /* Yellow: #ffdd33 */
        .token_type        = {149, 169, 159, 255},  /* Quartz: #95a99f */
        .token_function    = {150, 166, 200, 255},  /* Niagara: #96a6c8 */
        .token_string      = {115, 201, 54, 255},   /* Green: #73c936 */
        .token_number      = {158, 149, 199, 255},  /* Wisteria: #9e95c7 */
        .token_comment     = {204, 140, 60, 255},   /* Brown / Caramel: #cc8c3c */
        .token_operator    = {149, 169, 159, 255},  /* Quartz: #95a99f */
        .token_preprocessor= {204, 140, 60, 255},   /* Caramel / Quartz */
        .token_variable    = {228, 228, 239, 255},  /* Foreground Text: #e4e4ef */
        .token_punctuation = {149, 169, 159, 255}   /* Quartz */
    };

    /* Language configs */

    /* Unknown / Plaintext */
    config->languages[TH_LANG_UNKNOWN] = (ThLanguageConfig){
        .id = TH_LANG_UNKNOWN,
        .name = "Plain Text",
        .extensions = NULL,
        .extension_count = 0,
        .lsp_command = NULL,
        .formatter_command = NULL,
        .linter_command = NULL,
        .tree_sitter_library = NULL,
        .tree_sitter_symbol = NULL
    };

    /* C */
    config->languages[TH_LANG_C] = (ThLanguageConfig){
        .id = TH_LANG_C,
        .name = "C",
        .extensions = s_c_extensions,
        .extension_count = sizeof(s_c_extensions) / sizeof(s_c_extensions[0]),
        .lsp_command = "clangd --background-index",
        .formatter_command = "clang-format",
        .linter_command = "clang-tidy",
        .tree_sitter_library = "libtree-sitter-c" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_c"
    };

    /* C++ */
    config->languages[TH_LANG_CPP] = (ThLanguageConfig){
        .id = TH_LANG_CPP,
        .name = "C++",
        .extensions = s_cpp_extensions,
        .extension_count = sizeof(s_cpp_extensions) / sizeof(s_cpp_extensions[0]),
        .lsp_command = "clangd --background-index",
        .formatter_command = "clang-format",
        .linter_command = "clang-tidy",
        .tree_sitter_library = "libtree-sitter-cpp" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_cpp"
    };

    /* Python */
    config->languages[TH_LANG_PYTHON] = (ThLanguageConfig){
        .id = TH_LANG_PYTHON,
        .name = "Python",
        .extensions = s_python_extensions,
        .extension_count = sizeof(s_python_extensions) / sizeof(s_python_extensions[0]),
        .lsp_command = "pylsp",
        .formatter_command = "black -",
        .linter_command = "pylint",
        .tree_sitter_library = "libtree-sitter-python" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_python"
    };

    /* JavaScript */
    config->languages[TH_LANG_JAVASCRIPT] = (ThLanguageConfig){
        .id = TH_LANG_JAVASCRIPT,
        .name = "JavaScript",
        .extensions = s_js_extensions,
        .extension_count = sizeof(s_js_extensions) / sizeof(s_js_extensions[0]),
        .lsp_command = "typescript-language-server --stdio",
        .formatter_command = "prettier --stdin-filepath input.js",
        .linter_command = "eslint -f unix",
        .tree_sitter_library = "libtree-sitter-javascript" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_javascript"
    };

    /* TypeScript */
    config->languages[TH_LANG_TYPESCRIPT] = (ThLanguageConfig){
        .id = TH_LANG_TYPESCRIPT,
        .name = "TypeScript",
        .extensions = s_ts_extensions,
        .extension_count = sizeof(s_ts_extensions) / sizeof(s_ts_extensions[0]),
        .lsp_command = "typescript-language-server --stdio",
        .formatter_command = "prettier --stdin-filepath input.ts",
        .linter_command = "eslint -f unix",
        .tree_sitter_library = "libtree-sitter-typescript" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_typescript"
    };

    /* HTML */
    config->languages[TH_LANG_HTML] = (ThLanguageConfig){
        .id = TH_LANG_HTML,
        .name = "HTML",
        .extensions = s_html_extensions,
        .extension_count = sizeof(s_html_extensions) / sizeof(s_html_extensions[0]),
        .lsp_command = "vscode-html-language-server --stdio",
        .formatter_command = "prettier --stdin-filepath input.html",
        .linter_command = NULL,
        .tree_sitter_library = "libtree-sitter-html" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_html"
    };

    /* CSS */
    config->languages[TH_LANG_CSS] = (ThLanguageConfig){
        .id = TH_LANG_CSS,
        .name = "CSS",
        .extensions = s_css_extensions,
        .extension_count = sizeof(s_css_extensions) / sizeof(s_css_extensions[0]),
        .lsp_command = "vscode-css-language-server --stdio",
        .formatter_command = "prettier --stdin-filepath input.css",
        .linter_command = NULL,
        .tree_sitter_library = "libtree-sitter-css" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_css"
    };

    /* Markdown */
    config->languages[TH_LANG_MARKDOWN] = (ThLanguageConfig){
        .id = TH_LANG_MARKDOWN,
        .name = "Markdown",
        .extensions = s_markdown_extensions,
        .extension_count = sizeof(s_markdown_extensions) / sizeof(s_markdown_extensions[0]),
        .lsp_command = NULL,
        .formatter_command = "prettier --stdin-filepath input.md",
        .linter_command = NULL,
        .tree_sitter_library = "libtree-sitter-markdown" SO_EXT,
        .tree_sitter_symbol = "tree_sitter_markdown"
    };
}

void th_config_shutdown(ThConfigService *config) {
    (void)config;
}

ThLanguageId th_config_detect_language(const ThConfigService *config, const char *filepath) {
    if (!config || !filepath) return TH_LANG_UNKNOWN;

    const char *dot = strrchr(filepath, '.');
    if (!dot) return TH_LANG_UNKNOWN;

    for (int i = 1; i < TH_LANG_COUNT; i++) {
        const ThLanguageConfig *lang = &config->languages[i];
        for (size_t j = 0; j < lang->extension_count; j++) {
            if (th_strcasecmp(dot, lang->extensions[j]) == 0) {
                return (ThLanguageId)i;
            }
        }
    }

    return TH_LANG_UNKNOWN;
}

const ThLanguageConfig *th_config_get_language(const ThConfigService *config, ThLanguageId lang) {
    if (!config || lang < 0 || lang >= TH_LANG_COUNT) {
        return &config->languages[TH_LANG_UNKNOWN];
    }
    return &config->languages[lang];
}

const ThTheme *th_config_get_theme(const ThConfigService *config) {
    if (!config) return NULL;
    return &config->editor.theme;
}

ThColor th_config_get_token_color(const ThTheme *theme, ThTokenType token_type) {
    if (!theme) return (ThColor){255, 255, 255, 255};

    switch (token_type) {
        case TH_TOKEN_KEYWORD:      return theme->token_keyword;
        case TH_TOKEN_TYPE:         return theme->token_type;
        case TH_TOKEN_FUNCTION:     return theme->token_function;
        case TH_TOKEN_STRING:       return theme->token_string;
        case TH_TOKEN_NUMBER:       return theme->token_number;
        case TH_TOKEN_COMMENT:      return theme->token_comment;
        case TH_TOKEN_OPERATOR:     return theme->token_operator;
        case TH_TOKEN_PREPROCESSOR: return theme->token_preprocessor;
        case TH_TOKEN_VARIABLE:     return theme->token_variable;
        case TH_TOKEN_PUNCTUATION:  return theme->token_punctuation;
        default:                    return theme->text_normal;
    }
}
