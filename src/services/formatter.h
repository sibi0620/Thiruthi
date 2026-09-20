/**
 * @file formatter.h
 * @brief Code formatter integration service (clang-format, black, prettier).
 */

#ifndef TH_FORMATTER_H
#define TH_FORMATTER_H

#include "../common/types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char last_error[512];
} ThFormatterService;

void th_formatter_init(ThFormatterService *service);
void th_formatter_shutdown(ThFormatterService *service);

/**
 * @brief Format source code buffer via external formatter tool.
 *
 * @param service Formatter service context
 * @param command Base formatter command (e.g., "clang-format", "black -", "prettier")
 * @param filepath Filepath used to infer language or configuration
 * @param input_code Input source code string
 * @param input_len Length of input source code
 * @param out_formatted Output allocated formatted string (caller owns and must th_free)
 * @param out_formatted_len Output length of formatted string
 * @return true on successful format, false otherwise (error stored in last_error)
 */
bool th_formatter_format_code(
    ThFormatterService *service,
    const char *command,
    const char *filepath,
    const char *input_code,
    size_t input_len,
    char **out_formatted,
    size_t *out_formatted_len
);

const char *th_formatter_get_last_error(const ThFormatterService *service);

#ifdef __cplusplus
}
#endif

#endif /* TH_FORMATTER_H */
