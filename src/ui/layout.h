/**
 * @file layout.h
 * @brief UI Layout Engine and State Management using Clay.
 */

#ifndef TH_LAYOUT_H
#define TH_LAYOUT_H

#include "../common/types.h"
#include "../services/config.h"
#include "../services/editor.h"
#include "../services/parser.h"
#include "../services/renderer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    bool is_replace_mode;
    char find_text[128];
    char replace_text[128];
    int match_count;
    int current_match;
    int active_field; /* 0 = find, 1 = replace */
} ThFindReplaceState;

typedef struct {
    bool active;
    char line_input[32];
} ThGoToLineState;

typedef struct {
    ThDiagnosticList diagnostics;
    ThCompletionList completion_list;
    bool completion_popup_active;
    int completion_selected_idx;

    ThHoverInfo hover_info;
    bool hover_popup_active;

    bool sidebar_open;
    bool problems_panel_open;

    char status_message[256];
    ThStatusLevel status_type;
    double status_time;

    ThFindReplaceState find_replace;
    ThGoToLineState goto_line;

    /* Settings Dialog */
    bool settings_open;
    int settings_selected_option; /* 0: Font Size, 1: Line Numbers Mode, 2: Tab Size, 3: Font Choice */
} ThUIState;

void th_ui_init(ThUIState *ui);
void th_ui_shutdown(ThUIState *ui);

void th_ui_set_status(ThUIState *ui, const char *msg, ThStatusLevel type);

void th_ui_build_layout(
    ThUIState *ui,
    const ThEditor *editor,
    const ThConfigService *config,
    const ThParserService *parser,
    const ThRendererService *renderer
);

#ifdef __cplusplus
}
#endif

#endif /* TH_LAYOUT_H */
