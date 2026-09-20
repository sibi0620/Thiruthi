/**
 * @file linter.c
 * @brief External linter service implementation.
 */

#include "linter.h"
#include "file.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void th_linter_init(ThLinterService *service) {
    if (!service) return;
    memset(service, 0, sizeof(ThLinterService));
}

void th_linter_shutdown(ThLinterService *service) {
    if (!service) return;
    th_linter_clear_diagnostics(service);
}

void th_linter_clear_diagnostics(ThLinterService *service) {
    if (!service) return;
    if (service->diagnostics.items) {
        th_free(service->diagnostics.items);
        service->diagnostics.items = NULL;
    }
    service->diagnostics.count = 0;
    service->diagnostics.capacity = 0;
}

static void add_diagnostic(ThLinterService *service, const ThDiagnostic *diag) {
    if (service->diagnostics.count >= service->diagnostics.capacity) {
        size_t new_cap = service->diagnostics.capacity == 0 ? 16 : service->diagnostics.capacity * 2;
        service->diagnostics.items = (ThDiagnostic *)th_realloc(
            service->diagnostics.items, sizeof(ThDiagnostic) * new_cap);
        service->diagnostics.capacity = new_cap;
    }
    service->diagnostics.items[service->diagnostics.count++] = *diag;
}

void th_linter_parse_output(ThLinterService *service, const char *output, const char *target_file) {
    if (!service || !output) return;

    char target_base[256] = {0};
    if (target_file) {
        th_file_get_basename(target_file, target_base, sizeof(target_base));
    }

    const char *p = output;
    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        size_t line_len = (size_t)(p - line_start);

        if (*p == '\r') p++;
        if (*p == '\n') p++;

        if (line_len < 5) continue;

        char buf[1024];
        if (line_len >= sizeof(buf)) line_len = sizeof(buf) - 1;
        memcpy(buf, line_start, line_len);
        buf[line_len] = '\0';

        /* Match: file:line:col: severity: msg or file:line: severity: msg */
        char fname[256] = {0};
        int line_num = 0;
        int col_num = 0;
        char rest[768] = {0};

        int matched = sscanf(buf, "%255[^:]:%d:%d: %767[^\n]", fname, &line_num, &col_num, rest);
        if (matched < 3) {
            matched = sscanf(buf, "%255[^:]:%d: %767[^\n]", fname, &line_num, rest);
            col_num = 1;
        }

        if (matched >= 2 && line_num > 0) {
            ThDiagnostic diag = {0};
            diag.range.start.line = line_num - 1; /* convert to 0-indexed */
            diag.range.start.col = col_num > 0 ? col_num - 1 : 0;
            diag.range.end.line = diag.range.start.line;
            diag.range.end.col = diag.range.start.col + 1;
            snprintf(diag.source, sizeof(diag.source), "linter");

            char *colon = strchr(rest, ':');
            if (colon) {
                *colon = '\0';
                char *sev_str = rest;
                char *msg_str = colon + 1;
                while (*msg_str == ' ') msg_str++;

                if (th_strcasestr(sev_str, "error") || th_strcasestr(sev_str, "fatal")) {
                    diag.severity = TH_DIAG_ERROR;
                } else if (th_strcasestr(sev_str, "warning")) {
                    diag.severity = TH_DIAG_WARNING;
                } else if (th_strcasestr(sev_str, "note") || th_strcasestr(sev_str, "info")) {
                    diag.severity = TH_DIAG_INFO;
                } else {
                    diag.severity = TH_DIAG_WARNING;
                }
                snprintf(diag.message, sizeof(diag.message), "%s", msg_str);
            } else {
                diag.severity = TH_DIAG_WARNING;
                snprintf(diag.message, sizeof(diag.message), "%s", rest);
            }

            add_diagnostic(service, &diag);
        }
    }
}

bool th_linter_run(ThLinterService *service, const char *linter_cmd, const char *filepath) {
    if (!service || !linter_cmd || !filepath) return false;
    th_linter_clear_diagnostics(service);

    char full_cmd[1024];
#if TH_PLATFORM_WINDOWS
    if (strstr(linter_cmd, "clang-tidy")) {
        snprintf(full_cmd, sizeof(full_cmd), "clang-tidy \"%s\" -- 2>&1", filepath);
    } else if (strstr(linter_cmd, "pylint")) {
        snprintf(full_cmd, sizeof(full_cmd), "pylint --output-format=text \"%s\" 2>&1", filepath);
    } else if (strstr(linter_cmd, "eslint")) {
        snprintf(full_cmd, sizeof(full_cmd), "eslint -f unix \"%s\" 2>&1", filepath);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s \"%s\" 2>&1", linter_cmd, filepath);
    }
#else
    if (strstr(linter_cmd, "clang-tidy")) {
        snprintf(full_cmd, sizeof(full_cmd), "clang-tidy '%s' -- 2>&1", filepath);
    } else if (strstr(linter_cmd, "pylint")) {
        snprintf(full_cmd, sizeof(full_cmd), "pylint --output-format=text '%s' 2>&1", filepath);
    } else if (strstr(linter_cmd, "eslint")) {
        snprintf(full_cmd, sizeof(full_cmd), "eslint -f unix '%s' 2>&1", filepath);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s '%s' 2>&1", linter_cmd, filepath);
    }
#endif

    FILE *pipe_fp = th_popen(full_cmd, "r");
    if (!pipe_fp) {
        snprintf(service->last_error, sizeof(service->last_error), "Failed to execute linter command");
        return false;
    }

    size_t out_cap = 16384;
    char *output = (char *)th_malloc(out_cap);
    size_t out_len = 0;

    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe_fp)) {
        size_t r = strlen(buf);
        if (out_len + r >= out_cap) {
            out_cap *= 2;
            output = (char *)th_realloc(output, out_cap);
        }
        memcpy(output + out_len, buf, r);
        out_len += r;
        output[out_len] = '\0';
    }

    th_pclose(pipe_fp);

    if (output && out_len > 0) {
        th_linter_parse_output(service, output, filepath);
    }
    if (output) th_free(output);

    return true;
}

const ThDiagnosticList *th_linter_get_diagnostics(const ThLinterService *service) {
    if (!service) return NULL;
    return &service->diagnostics;
}
