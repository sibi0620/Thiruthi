/**
 * @file linter.h
 * @brief External linter integration service (clang-tidy, pylint, eslint).
 */

#ifndef TH_LINTER_H
#define TH_LINTER_H

#include "../common/types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ThDiagnosticList diagnostics;
    char last_error[512];
    bool is_running;
} ThLinterService;

void th_linter_init(ThLinterService *service);
void th_linter_shutdown(ThLinterService *service);

/**
 * @brief Run linter on target file and populate diagnostics list.
 */
bool th_linter_run(ThLinterService *service, const char *linter_cmd, const char *filepath);

/**
 * @brief Parse diagnostic output string into ThDiagnosticList.
 */
void th_linter_parse_output(ThLinterService *service, const char *output, const char *target_file);

const ThDiagnosticList *th_linter_get_diagnostics(const ThLinterService *service);
void th_linter_clear_diagnostics(ThLinterService *service);

#ifdef __cplusplus
}
#endif

#endif /* TH_LINTER_H */
