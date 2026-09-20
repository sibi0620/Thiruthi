/**
 * @file lsp.c
 * @brief Asynchronous Language Server Protocol client implementation.
 */

#include "lsp.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TH_PLATFORM_POSIX
    #include <unistd.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <errno.h>
    #include <sys/wait.h>
    #include <sys/poll.h>
#endif

#define LSP_READ_BUFFER_INIT_CAP (64 * 1024)

/* --- JSON Escape and Helper Functions --- */

static char *json_escape_string(const char *s) {
    if (!s) return th_strdup("");
    size_t len = strlen(s);
    /* Worst case every char needs 2 chars */
    char *out = (char *)th_malloc(len * 2 + 1);
    if (!out) return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out[o++] = '\\'; out[o++] = '"'; break;
            case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
            case '\b': out[o++] = '\\'; out[o++] = 'b'; break;
            case '\f': out[o++] = '\\'; out[o++] = 'f'; break;
            case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
            case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
            case '\t': out[o++] = '\\'; out[o++] = 't'; break;
            default:
                if (c < 32) {
                    o += snprintf(out + o, 7, "\\u%04x", c);
                } else {
                    out[o++] = (char)c;
                }
                break;
        }
    }
    out[o] = '\0';
    return out;
}

void th_lsp_path_to_uri(const char *path, char *out_uri, size_t max_len) {
    if (!path || !out_uri || max_len == 0) return;

#if TH_PLATFORM_WINDOWS
    char full_path[MAX_PATH];
    if (GetFullPathNameA(path, MAX_PATH, full_path, NULL) == 0) {
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }

    /* Convert backslashes to forward slashes */
    for (char *p = full_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* URI format: file:///C:/path/to/file */
    snprintf(out_uri, max_len, "file:///%s", full_path);
#else
    char resolved[1024];
    if (path[0] != '/' && realpath(path, resolved)) {
        path = resolved;
    }
    snprintf(out_uri, max_len, "file://%s", path);
#endif
}

void th_lsp_uri_to_path(const char *uri, char *out_path, size_t max_len) {
    if (!uri || !out_path || max_len == 0) return;

#if TH_PLATFORM_WINDOWS
    if (strncmp(uri, "file:///", 8) == 0) {
        strncpy(out_path, uri + 8, max_len - 1);
    } else if (strncmp(uri, "file://", 7) == 0) {
        strncpy(out_path, uri + 7, max_len - 1);
    } else {
        strncpy(out_path, uri, max_len - 1);
    }
    out_path[max_len - 1] = '\0';

    /* Convert forward slashes to backslashes on Windows */
    for (char *p = out_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#else
    if (strncmp(uri, "file://", 7) == 0) {
        strncpy(out_path, uri + 7, max_len - 1);
    } else {
        strncpy(out_path, uri, max_len - 1);
    }
    out_path[max_len - 1] = '\0';
#endif
}

/* --- Sending JSON-RPC Messages --- */

static bool lsp_send_raw(ThLspClient *client, const char *json_payload) {
    if (!client || !client->is_running) return false;

    size_t len = strlen(json_payload);
    char header[128];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);

#if TH_PLATFORM_WINDOWS
    if (client->stdin_pipe == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    if (!WriteFile(client->stdin_pipe, header, (DWORD)header_len, &written, NULL) || (int)written != header_len) {
        return false;
    }
    if (!WriteFile(client->stdin_pipe, json_payload, (DWORD)len, &written, NULL) || (size_t)written != len) {
        return false;
    }
    return true;
#else
    if (client->stdin_fd == -1) return false;
    ssize_t written = write(client->stdin_fd, header, header_len);
    if (written < 0) return false;

    written = write(client->stdin_fd, json_payload, len);
    return written == (ssize_t)len;
#endif
}

/* --- Client Lifecycle --- */

void th_lsp_init(ThLspClient *client) {
    if (!client) return;
    memset(client, 0, sizeof(ThLspClient));

#if TH_PLATFORM_WINDOWS
    client->h_process = NULL;
    client->h_thread = NULL;
    client->stdin_pipe = INVALID_HANDLE_VALUE;
    client->stdout_pipe = INVALID_HANDLE_VALUE;
    client->stderr_pipe = INVALID_HANDLE_VALUE;
#else
    client->stdin_fd = -1;
    client->stdout_fd = -1;
    client->stderr_fd = -1;
#endif
    client->next_request_id = 1;
    client->read_buffer_cap = LSP_READ_BUFFER_INIT_CAP;
    client->read_buffer = (char *)th_malloc(client->read_buffer_cap);
    client->read_buffer_len = 0;
}

void th_lsp_shutdown(ThLspClient *client) {
    if (!client) return;
    th_lsp_stop(client);

    if (client->read_buffer) {
        th_free(client->read_buffer);
        client->read_buffer = NULL;
    }
    client->read_buffer_len = 0;
    client->read_buffer_cap = 0;
}

#if TH_PLATFORM_POSIX
static void parse_args(char *cmd, char **argv, int max_args) {
    int count = 0;
    char *p = cmd;

    while (*p && count < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv[count++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p++ = '\0';
        }
    }
    argv[count] = NULL;
}
#endif

bool th_lsp_start(ThLspClient *client, const char *command, const char *root_dir) {
    if (!client || !command || client->is_running) return false;

#if TH_PLATFORM_WINDOWS
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE stdin_rd = INVALID_HANDLE_VALUE, stdin_wr = INVALID_HANDLE_VALUE;
    HANDLE stdout_rd = INVALID_HANDLE_VALUE, stdout_wr = INVALID_HANDLE_VALUE;
    HANDLE stderr_rd = INVALID_HANDLE_VALUE, stderr_wr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&stdin_rd, &stdin_wr, &sa, 0) ||
        !CreatePipe(&stdout_rd, &stdout_wr, &sa, 0) ||
        !CreatePipe(&stderr_rd, &stderr_wr, &sa, 0)) {
        if (stdin_rd != INVALID_HANDLE_VALUE) CloseHandle(stdin_rd);
        if (stdin_wr != INVALID_HANDLE_VALUE) CloseHandle(stdin_wr);
        if (stdout_rd != INVALID_HANDLE_VALUE) CloseHandle(stdout_rd);
        if (stdout_wr != INVALID_HANDLE_VALUE) CloseHandle(stdout_wr);
        if (stderr_rd != INVALID_HANDLE_VALUE) CloseHandle(stderr_rd);
        if (stderr_wr != INVALID_HANDLE_VALUE) CloseHandle(stderr_wr);
        return false;
    }

    SetHandleInformation(stdin_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = stdin_rd;
    si.hStdOutput = stdout_wr;
    si.hStdError = stderr_wr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    char cmd_copy[2048];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    BOOL ok = CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root_dir, &si, &pi);

    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);
    CloseHandle(stderr_wr);

    if (!ok) {
        CloseHandle(stdin_wr);
        CloseHandle(stdout_rd);
        CloseHandle(stderr_rd);
        return false;
    }

    client->h_process = pi.hProcess;
    client->h_thread = pi.hThread;
    client->stdin_pipe = stdin_wr;
    client->stdout_pipe = stdout_rd;
    client->stderr_pipe = stderr_rd;
    client->is_running = true;
    client->is_initialized = false;
    client->read_buffer_len = 0;

#else /* POSIX */
    int stdin_p[2];
    int stdout_p[2];
    int stderr_p[2];

    if (pipe(stdin_p) < 0 || pipe(stdout_p) < 0 || pipe(stderr_p) < 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_p[0]); close(stdin_p[1]);
        close(stdout_p[0]); close(stdout_p[1]);
        close(stderr_p[0]); close(stderr_p[1]);
        return false;
    }

    if (pid == 0) {
        /* Child process */
        close(stdin_p[1]);
        close(stdout_p[0]);
        close(stderr_p[0]);

        dup2(stdin_p[0], STDIN_FILENO);
        dup2(stdout_p[1], STDOUT_FILENO);
        dup2(stderr_p[1], STDERR_FILENO);

        close(stdin_p[0]);
        close(stdout_p[1]);
        close(stderr_p[1]);

        char cmd_copy[1024];
        strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        char *argv[64];
        parse_args(cmd_copy, argv, 64);

        execvp(argv[0], argv);
        _exit(127);
    }

    /* Parent process */
    close(stdin_p[0]);
    close(stdout_p[1]);
    close(stderr_p[1]);

    client->pid = pid;
    client->stdin_fd = stdin_p[1];
    client->stdout_fd = stdout_p[0];
    client->stderr_fd = stderr_p[0];
    client->is_running = true;
    client->is_initialized = false;
    client->read_buffer_len = 0;

    /* Set non-blocking */
    int flags = fcntl(client->stdout_fd, F_GETFL, 0);
    fcntl(client->stdout_fd, F_SETFL, flags | O_NONBLOCK);
#endif

    /* Send initialize request */
    char root_uri[1024];
    th_lsp_path_to_uri(root_dir ? root_dir : ".", root_uri, sizeof(root_uri));

    char init_payload[2048];
    int req_id = client->next_request_id++;
    snprintf(init_payload, sizeof(init_payload),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
        "\"processId\":%d,\"rootUri\":\"%s\","
        "\"capabilities\":{\"textDocument\":{\"completion\":{\"completionItem\":{\"snippetSupport\":true}},"
        "\"hover\":{},\"publishDiagnostics\":{\"relatedInformation\":true}}}}}" ,
        req_id, (int)th_getpid(), root_uri);

    lsp_send_raw(client, init_payload);
    return true;
}

void th_lsp_stop(ThLspClient *client) {
    if (!client || !client->is_running) return;

    /* Send shutdown request and exit notification */
    char shutdown_payload[128];
    snprintf(shutdown_payload, sizeof(shutdown_payload),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\"}", client->next_request_id++);
    lsp_send_raw(client, shutdown_payload);
    lsp_send_raw(client, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}");

#if TH_PLATFORM_WINDOWS
    if (client->stdin_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(client->stdin_pipe);
        client->stdin_pipe = INVALID_HANDLE_VALUE;
    }

    if (client->h_process) {
        if (WaitForSingleObject(client->h_process, 500) == WAIT_TIMEOUT) {
            TerminateProcess(client->h_process, 0);
            WaitForSingleObject(client->h_process, 500);
        }
        CloseHandle(client->h_process);
        client->h_process = NULL;
    }
    if (client->h_thread) {
        CloseHandle(client->h_thread);
        client->h_thread = NULL;
    }
    if (client->stdout_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(client->stdout_pipe);
        client->stdout_pipe = INVALID_HANDLE_VALUE;
    }
    if (client->stderr_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(client->stderr_pipe);
        client->stderr_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (client->stdin_fd != -1) { close(client->stdin_fd); client->stdin_fd = -1; }
    if (client->stdout_fd != -1) { close(client->stdout_fd); client->stdout_fd = -1; }
    if (client->stderr_fd != -1) { close(client->stderr_fd); client->stderr_fd = -1; }

    /* Wait with non-blocking check, then SIGTERM if needed */
    int status;
    pid_t res = waitpid(client->pid, &status, WNOHANG);
    if (res == 0) {
        kill(client->pid, SIGTERM);
        th_usleep(10000);
        waitpid(client->pid, &status, WNOHANG);
    }
#endif

    client->is_running = false;
    client->is_initialized = false;
}

/* --- Protocol Notifications & Requests --- */

void th_lsp_did_open(ThLspClient *client, const char *filepath, ThLanguageId lang_id, const char *text) {
    if (!client || !client->is_running || !filepath) return;

    th_lsp_path_to_uri(filepath, client->current_uri, sizeof(client->current_uri));

    const char *lang_str = "plaintext";
    switch (lang_id) {
        case TH_LANG_C: lang_str = "c"; break;
        case TH_LANG_CPP: lang_str = "cpp"; break;
        case TH_LANG_PYTHON: lang_str = "python"; break;
        case TH_LANG_JAVASCRIPT: lang_str = "javascript"; break;
        case TH_LANG_TYPESCRIPT: lang_str = "typescript"; break;
        case TH_LANG_HTML: lang_str = "html"; break;
        case TH_LANG_CSS: lang_str = "css"; break;
        case TH_LANG_MARKDOWN: lang_str = "markdown"; break;
        default: break;
    }

    char *escaped_text = json_escape_string(text ? text : "");
    if (!escaped_text) return;

    size_t cap = strlen(escaped_text) + 512;
    char *payload = (char *)th_malloc(cap);
    if (payload) {
        snprintf(payload, cap,
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
            "\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"%s\",\"version\":1,\"text\":\"%s\"}}}",
            client->current_uri, lang_str, escaped_text);
        lsp_send_raw(client, payload);
        th_free(payload);
    }
    th_free(escaped_text);
}

void th_lsp_did_change(ThLspClient *client, const char *filepath, uint32_t version, const char *full_text) {
    if (!client || !client->is_running || !filepath) return;

    char uri[1024];
    th_lsp_path_to_uri(filepath, uri, sizeof(uri));

    char *escaped_text = json_escape_string(full_text ? full_text : "");
    if (!escaped_text) return;

    size_t cap = strlen(escaped_text) + 512;
    char *payload = (char *)th_malloc(cap);
    if (payload) {
        snprintf(payload, cap,
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
            "\"textDocument\":{\"uri\":\"%s\",\"version\":%u},"
            "\"contentChanges\":[{\"text\":\"%s\"}]}}",
            uri, version, escaped_text);
        lsp_send_raw(client, payload);
        th_free(payload);
    }
    th_free(escaped_text);
}

void th_lsp_did_save(ThLspClient *client, const char *filepath) {
    if (!client || !client->is_running || !filepath) return;

    char uri[1024];
    th_lsp_path_to_uri(filepath, uri, sizeof(uri));

    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
        "\"textDocument\":{\"uri\":\"%s\"}}}", uri);
    lsp_send_raw(client, payload);
}

void th_lsp_did_close(ThLspClient *client, const char *filepath) {
    if (!client || !client->is_running || !filepath) return;

    char uri[1024];
    th_lsp_path_to_uri(filepath, uri, sizeof(uri));

    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
        "\"textDocument\":{\"uri\":\"%s\"}}}", uri);
    lsp_send_raw(client, payload);
}

void th_lsp_request_completion(ThLspClient *client, const char *filepath, ThPosition pos) {
    if (!client || !client->is_running || !filepath) return;

    char uri[1024];
    th_lsp_path_to_uri(filepath, uri, sizeof(uri));

    int req_id = client->next_request_id++;
    client->pending_completion_id = req_id;

    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/completion\",\"params\":{"
        "\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%u,\"character\":%u}}}",
        req_id, uri, pos.line, pos.col);
    lsp_send_raw(client, payload);
}

void th_lsp_request_hover(ThLspClient *client, const char *filepath, ThPosition pos) {
    if (!client || !client->is_running || !filepath) return;

    char uri[1024];
    th_lsp_path_to_uri(filepath, uri, sizeof(uri));

    int req_id = client->next_request_id++;
    client->pending_hover_id = req_id;

    char payload[2048];
    snprintf(payload, sizeof(payload),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/hover\",\"params\":{"
        "\"textDocument\":{\"uri\":\"%s\"},\"position\":{\"line\":%u,\"character\":%u}}}",
        req_id, uri, pos.line, pos.col);
    lsp_send_raw(client, payload);
}

/* --- Callbacks Registration --- */

void th_lsp_set_diagnostics_callback(ThLspClient *client, ThLspDiagnosticsCb cb, void *user_data) {
    if (!client) return;
    client->diagnostics_cb = cb;
    client->diagnostics_cb_data = user_data;
}

void th_lsp_set_completions_callback(ThLspClient *client, ThLspCompletionsCb cb, void *user_data) {
    if (!client) return;
    client->completions_cb = cb;
    client->completions_cb_data = user_data;
}

void th_lsp_set_hover_callback(ThLspClient *client, ThLspHoverCb cb, void *user_data) {
    if (!client) return;
    client->hover_cb = cb;
    client->hover_cb_data = user_data;
}

/* --- JSON Parsing Helpers for LSP responses --- */

static const char *json_find_key(const char *json, const char *key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (!pos) return NULL;
    pos += strlen(needle);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;
    return pos;
}

static bool json_extract_int(const char *json, const char *key, int *out_val) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;
    *out_val = atoi(pos);
    return true;
}

static bool json_extract_string(const char *json, const char *key, char *out_buf, size_t max_len) {
    const char *pos = json_find_key(json, key);
    if (!pos || *pos != '"') return false;
    pos++;

    size_t o = 0;
    while (*pos && *pos != '"' && o < max_len - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case 'n': out_buf[o++] = '\n'; break;
                case 'r': out_buf[o++] = '\r'; break;
                case 't': out_buf[o++] = '\t'; break;
                case '"': out_buf[o++] = '"'; break;
                case '\\': out_buf[o++] = '\\'; break;
                default: out_buf[o++] = *pos; break;
            }
            pos++;
        } else {
            out_buf[o++] = *pos++;
        }
    }
    out_buf[o] = '\0';
    return true;
}

static void parse_diagnostics_message(ThLspClient *client, const char *json) {
    char uri[1024] = {0};
    json_extract_string(json, "uri", uri, sizeof(uri));

    const char *diags_arr = json_find_key(json, "diagnostics");
    if (!diags_arr || *diags_arr != '[') return;

    ThDiagnostic items[256];
    size_t count = 0;

    const char *p = diags_arr + 1;
    while (*p && *p != ']' && count < 256) {
        const char *obj = strchr(p, '{');
        if (!obj) break;

        /* Find matching closing brace */
        int depth = 1;
        const char *obj_end = obj + 1;
        while (*obj_end && depth > 0) {
            if (*obj_end == '{') depth++;
            else if (*obj_end == '}') depth--;
            obj_end++;
        }

        ThDiagnostic diag = {0};
        diag.severity = TH_DIAG_ERROR;
        strncpy(diag.source, "lsp", sizeof(diag.source) - 1);

        /* Parse severity */
        int sev = 1;
        if (json_extract_int(obj, "severity", &sev)) {
            diag.severity = (ThDiagnosticSeverity)sev;
        }

        /* Parse message */
        json_extract_string(obj, "message", diag.message, sizeof(diag.message));

        /* Parse range start */
        const char *start_pos = strstr(obj, "\"start\"");
        if (start_pos) {
            int line = 0, ch = 0;
            json_extract_int(start_pos, "line", &line);
            json_extract_int(start_pos, "character", &ch);
            diag.range.start.line = line >= 0 ? (uint32_t)line : 0;
            diag.range.start.col = ch >= 0 ? (uint32_t)ch : 0;
        }

        /* Parse range end */
        const char *end_pos = strstr(obj, "\"end\"");
        if (end_pos) {
            int line = 0, ch = 0;
            json_extract_int(end_pos, "line", &line);
            json_extract_int(end_pos, "character", &ch);
            diag.range.end.line = line >= 0 ? (uint32_t)line : 0;
            diag.range.end.col = ch >= 0 ? (uint32_t)ch : 0;
        }

        items[count++] = diag;
        p = obj_end;
    }

    if (client->diagnostics_cb) {
        ThDiagnosticList list = {
            .items = items,
            .count = count,
            .capacity = count
        };
        client->diagnostics_cb(uri, &list, client->diagnostics_cb_data);
    }
}

static void parse_completions_message(ThLspClient *client, const char *json) {
    const char *items_arr = json_find_key(json, "items");
    if (!items_arr || *items_arr != '[') {
        items_arr = json_find_key(json, "result");
        if (!items_arr || *items_arr != '[') return;
    }

    ThCompletionItem items[64];
    size_t count = 0;

    const char *p = items_arr + 1;
    while (*p && *p != ']' && count < 64) {
        const char *obj = strchr(p, '{');
        if (!obj) break;

        int depth = 1;
        const char *obj_end = obj + 1;
        while (*obj_end && depth > 0) {
            if (*obj_end == '{') depth++;
            else if (*obj_end == '}') depth--;
            obj_end++;
        }

        ThCompletionItem item = {0};
        json_extract_string(obj, "label", item.label, sizeof(item.label));
        json_extract_string(obj, "detail", item.detail, sizeof(item.detail));
        json_extract_string(obj, "documentation", item.documentation, sizeof(item.documentation));
        if (!json_extract_string(obj, "insertText", item.insert_text, sizeof(item.insert_text))) {
            snprintf(item.insert_text, sizeof(item.insert_text), "%s", item.label);
        }
        json_extract_int(obj, "kind", &item.kind);

        if (item.label[0] != '\0') {
            items[count++] = item;
        }
        p = obj_end;
    }

    if (client->completions_cb) {
        ThCompletionList list = {
            .items = items,
            .count = count,
            .capacity = count
        };
        client->completions_cb(&list, client->completions_cb_data);
    }
}

static void parse_hover_message(ThLspClient *client, const char *json) {
    const char *contents = json_find_key(json, "contents");
    if (!contents) return;

    ThHoverInfo hover = {0};
    hover.active = true;

    if (*contents == '"') {
        json_extract_string(json, "contents", hover.contents, sizeof(hover.contents));
    } else if (*contents == '{') {
        json_extract_string(contents, "value", hover.contents, sizeof(hover.contents));
    }

    if (client->hover_cb && hover.contents[0] != '\0') {
        client->hover_cb(&hover, client->hover_cb_data);
    }
}

static void process_lsp_message(ThLspClient *client, const char *json) {
    /* Check for publishDiagnostics notification */
    if (strstr(json, "\"method\":\"textDocument/publishDiagnostics\"")) {
        parse_diagnostics_message(client, json);
        return;
    }

    /* Check request responses by id */
    int id = -1;
    if (json_extract_int(json, "id", &id)) {
        if (id == client->pending_completion_id) {
            parse_completions_message(client, json);
            client->pending_completion_id = 0;
            return;
        }
        if (id == client->pending_hover_id) {
            parse_hover_message(client, json);
            client->pending_hover_id = 0;
            return;
        }
        if (!client->is_initialized && strstr(json, "\"capabilities\"")) {
            client->is_initialized = true;
            /* Send initialized notification */
            lsp_send_raw(client, "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
            return;
        }
    }
}

void th_lsp_poll(ThLspClient *client) {
    if (!client || !client->is_running) return;

#if TH_PLATFORM_WINDOWS
    if (client->stdout_pipe != INVALID_HANDLE_VALUE) {
        DWORD bytes_avail = 0;
        while (PeekNamedPipe(client->stdout_pipe, NULL, 0, NULL, &bytes_avail, NULL) && bytes_avail > 0) {
            char temp[4096];
            DWORD to_read = (bytes_avail < sizeof(temp)) ? bytes_avail : (DWORD)sizeof(temp);
            DWORD bytes_read = 0;
            if (!ReadFile(client->stdout_pipe, temp, to_read, &bytes_read, NULL) || bytes_read == 0) {
                break;
            }

            if (client->read_buffer_len + bytes_read >= client->read_buffer_cap) {
                size_t new_cap = client->read_buffer_cap * 2;
                if (new_cap < client->read_buffer_len + bytes_read + 1) {
                    new_cap = client->read_buffer_len + bytes_read + 4096;
                }
                client->read_buffer = (char *)th_realloc(client->read_buffer, new_cap);
                client->read_buffer_cap = new_cap;
            }
            memcpy(client->read_buffer + client->read_buffer_len, temp, bytes_read);
            client->read_buffer_len += bytes_read;
            client->read_buffer[client->read_buffer_len] = '\0';
        }
    }
#else
    /* Non-blocking read from server pipe if connected */
    if (client->stdout_fd != -1) {
        char temp[4096];
        ssize_t bytes_read = 0;

        while ((bytes_read = read(client->stdout_fd, temp, sizeof(temp))) > 0) {
            if (client->read_buffer_len + bytes_read >= client->read_buffer_cap) {
                size_t new_cap = client->read_buffer_cap * 2;
                if (new_cap < client->read_buffer_len + bytes_read + 1) {
                    new_cap = client->read_buffer_len + bytes_read + 4096;
                }
                client->read_buffer = (char *)th_realloc(client->read_buffer, new_cap);
                client->read_buffer_cap = new_cap;
            }
            memcpy(client->read_buffer + client->read_buffer_len, temp, bytes_read);
            client->read_buffer_len += bytes_read;
            client->read_buffer[client->read_buffer_len] = '\0';
        }
    }
#endif

    /* Process complete messages in buffer */
    while (client->read_buffer_len > 0) {
        const char *cl_header = "Content-Length:";
        char *header_pos = strstr(client->read_buffer, cl_header);
        if (!header_pos) {
            break;
        }

        char *body_sep = strstr(header_pos, "\r\n\r\n");
        if (!body_sep) {
            break;
        }

        size_t content_length = (size_t)atoi(header_pos + strlen(cl_header));
        char *body_start = body_sep + 4;
        size_t header_and_sep_len = body_start - client->read_buffer;

        if (client->read_buffer_len < header_and_sep_len + content_length) {
            /* Message not fully received yet */
            break;
        }

        /* Null terminate the JSON payload temporarily */
        char saved = body_start[content_length];
        body_start[content_length] = '\0';

        process_lsp_message(client, body_start);

        body_start[content_length] = saved;

        /* Shift remainder */
        size_t total_msg_len = header_and_sep_len + content_length;
        size_t remainder = client->read_buffer_len - total_msg_len;
        if (remainder > 0) {
            memmove(client->read_buffer, client->read_buffer + total_msg_len, remainder);
        }
        client->read_buffer_len = remainder;
        client->read_buffer[client->read_buffer_len] = '\0';
    }
}
