/**
 * @file lsp.h
 * @brief Asynchronous Language Server Protocol (LSP) client service.
 */

#ifndef TH_LSP_H
#define TH_LSP_H

#include "../common/types.h"
#include "../common/platform.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ThLspDiagnosticsCb)(const char *uri, const ThDiagnosticList *diags, void *user_data);
typedef void (*ThLspCompletionsCb)(const ThCompletionList *completions, void *user_data);
typedef void (*ThLspHoverCb)(const ThHoverInfo *hover, void *user_data);

typedef struct {
#if TH_PLATFORM_WINDOWS
    HANDLE h_process;
    HANDLE h_thread;
    HANDLE stdin_pipe;
    HANDLE stdout_pipe;
    HANDLE stderr_pipe;
#else
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
#endif
    bool is_running;
    bool is_initialized;

    /* Message buffer */
    char *read_buffer;
    size_t read_buffer_len;
    size_t read_buffer_cap;

    int next_request_id;
    int pending_completion_id;
    int pending_hover_id;

    /* Callbacks */
    ThLspDiagnosticsCb diagnostics_cb;
    void *diagnostics_cb_data;

    ThLspCompletionsCb completions_cb;
    void *completions_cb_data;

    ThLspHoverCb hover_cb;
    void *hover_cb_data;

    /* Current file URI */
    char current_uri[1024];
} ThLspClient;

void th_lsp_init(ThLspClient *client);
void th_lsp_shutdown(ThLspClient *client);

bool th_lsp_start(ThLspClient *client, const char *command, const char *root_dir);
void th_lsp_stop(ThLspClient *client);

/* Non-blocking poll for incoming LSP messages */
void th_lsp_poll(ThLspClient *client);

/* Document lifecycle */
void th_lsp_did_open(ThLspClient *client, const char *filepath, ThLanguageId lang_id, const char *text);
void th_lsp_did_change(ThLspClient *client, const char *filepath, uint32_t version, const char *full_text);
void th_lsp_did_save(ThLspClient *client, const char *filepath);
void th_lsp_did_close(ThLspClient *client, const char *filepath);

/* Language features */
void th_lsp_request_completion(ThLspClient *client, const char *filepath, ThPosition pos);
void th_lsp_request_hover(ThLspClient *client, const char *filepath, ThPosition pos);

/* Callback registration */
void th_lsp_set_diagnostics_callback(ThLspClient *client, ThLspDiagnosticsCb cb, void *user_data);
void th_lsp_set_completions_callback(ThLspClient *client, ThLspCompletionsCb cb, void *user_data);
void th_lsp_set_hover_callback(ThLspClient *client, ThLspHoverCb cb, void *user_data);

/* Helpers */
void th_lsp_path_to_uri(const char *path, char *out_uri, size_t max_len);
void th_lsp_uri_to_path(const char *uri, char *out_path, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* TH_LSP_H */
