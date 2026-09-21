/**
 * @file parser.h
 * @brief Tree-sitter AST parser and syntax highlighting service.
 */

#ifndef TH_PARSER_H
#define TH_PARSER_H

#include "../common/types.h"
#include <tree_sitter/api.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TSParser *ts_parser;
    TSTree *ts_tree;
    const TSLanguage *current_ts_lang;
    void *dl_handle;
    ThLanguageId current_lang_id;

    /* Cached line highlighting */
    ThHighlightLine *highlight_lines;
    size_t line_count;
    size_t line_capacity;

    /* Current source copy for AST node queries */
    char *source_text;
    size_t source_len;
} ThParserService;

void th_parser_init(ThParserService *service);
void th_parser_shutdown(ThParserService *service);

/**
 * @brief Set current language for parser (loads tree-sitter grammar if available).
 */
bool th_parser_set_language(ThParserService *service, ThLanguageId lang_id);

/**
 * @brief Parse source text and rebuild syntax highlights.
 */
bool th_parser_parse_buffer(ThParserService *service, const char *source, size_t length, size_t line_count);

/**
 * @brief Retrieve highlight spans for a specific line (0-indexed).
 */
const ThHighlightLine *th_parser_get_line_highlights(const ThParserService *service, size_t line_idx);

/**
 * @brief Retrieve current enclosing scope (function/struct/class) at line.
 */
bool th_parser_get_enclosing_scope(const ThParserService *service, uint32_t line, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* TH_PARSER_H */
