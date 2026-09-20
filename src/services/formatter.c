/**
 * @file formatter.c
 * @brief Code formatter integration implementation.
 */

#include "formatter.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TH_PLATFORM_POSIX
    #include <unistd.h>
    #include <sys/wait.h>
    #include <errno.h>
#endif

void th_formatter_init(ThFormatterService *service) {
    if (!service) return;
    service->last_error[0] = '\0';
}

void th_formatter_shutdown(ThFormatterService *service) {
    (void)service;
}

const char *th_formatter_get_last_error(const ThFormatterService *service) {
    return service ? service->last_error : "";
}

bool th_formatter_format_code(
    ThFormatterService *service,
    const char *command,
    const char *filepath,
    const char *input_code,
    size_t input_len,
    char **out_formatted,
    size_t *out_formatted_len
) {
    if (out_formatted) *out_formatted = NULL;
    if (out_formatted_len) *out_formatted_len = 0;
    if (!service || !command || !input_code) {
        if (service) snprintf(service->last_error, sizeof(service->last_error), "Invalid parameters");
        return false;
    }

    /* Assemble full command line */
    char full_cmd[1024];
#if TH_PLATFORM_WINDOWS
    if (strstr(command, "clang-format")) {
        if (filepath && filepath[0]) {
            snprintf(full_cmd, sizeof(full_cmd), "clang-format -assume-filename=\"%s\"", filepath);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "clang-format");
        }
    } else if (strstr(command, "black")) {
        snprintf(full_cmd, sizeof(full_cmd), "black -q -");
    } else if (strstr(command, "prettier")) {
        if (filepath && filepath[0]) {
            snprintf(full_cmd, sizeof(full_cmd), "prettier --stdin-filepath \"%s\"", filepath);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "prettier");
        }
    } else {
        strncpy(full_cmd, command, sizeof(full_cmd) - 1);
        full_cmd[sizeof(full_cmd) - 1] = '\0';
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE in_rd = INVALID_HANDLE_VALUE, in_wr = INVALID_HANDLE_VALUE;
    HANDLE out_rd = INVALID_HANDLE_VALUE, out_wr = INVALID_HANDLE_VALUE;
    HANDLE err_rd = INVALID_HANDLE_VALUE, err_wr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&in_rd, &in_wr, &sa, 0) ||
        !CreatePipe(&out_rd, &out_wr, &sa, 0) ||
        !CreatePipe(&err_rd, &err_wr, &sa, 0)) {
        if (in_rd != INVALID_HANDLE_VALUE) CloseHandle(in_rd);
        if (in_wr != INVALID_HANDLE_VALUE) CloseHandle(in_wr);
        if (out_rd != INVALID_HANDLE_VALUE) CloseHandle(out_rd);
        if (out_wr != INVALID_HANDLE_VALUE) CloseHandle(out_wr);
        if (err_rd != INVALID_HANDLE_VALUE) CloseHandle(err_rd);
        if (err_wr != INVALID_HANDLE_VALUE) CloseHandle(err_wr);
        snprintf(service->last_error, sizeof(service->last_error), "Failed to create pipes for formatter");
        return false;
    }

    SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = in_rd;
    si.hStdOutput = out_wr;
    si.hStdError = err_wr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    char exec_cmd[1200];
    snprintf(exec_cmd, sizeof(exec_cmd), "cmd.exe /c %s", full_cmd);

    BOOL ok = CreateProcessA(NULL, exec_cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(in_rd);
    CloseHandle(out_wr);
    CloseHandle(err_wr);

    if (!ok) {
        CloseHandle(in_wr);
        CloseHandle(out_rd);
        CloseHandle(err_rd);
        snprintf(service->last_error, sizeof(service->last_error), "Failed to start formatter process: %s", full_cmd);
        return false;
    }

    /* Write input to child's stdin */
    size_t written_total = 0;
    while (written_total < input_len) {
        DWORD chunk = (DWORD)(input_len - written_total);
        DWORD w = 0;
        if (!WriteFile(in_wr, input_code + written_total, chunk, &w, NULL) || w == 0) break;
        written_total += w;
    }
    CloseHandle(in_wr);

    /* Read child's stdout */
    size_t out_cap = input_len + 4096;
    char *out_buf = (char *)th_malloc(out_cap);
    size_t out_len = 0;

    char chunk[4096];
    DWORD r = 0;
    while (ReadFile(out_rd, chunk, sizeof(chunk), &r, NULL) && r > 0) {
        if (out_len + r >= out_cap) {
            out_cap = out_cap * 2 + r;
            out_buf = (char *)th_realloc(out_buf, out_cap);
        }
        memcpy(out_buf + out_len, chunk, r);
        out_len += r;
    }
    CloseHandle(out_rd);
    if (out_buf) {
        out_buf[out_len] = '\0';
    }

    /* Read child's stderr */
    char err_buf[512] = {0};
    DWORD err_r = 0;
    ReadFile(err_rd, err_buf, sizeof(err_buf) - 1, &err_r, NULL);
    if (err_r > 0) err_buf[err_r] = '\0';
    CloseHandle(err_rd);

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code == 0 && out_len > 0) {
        *out_formatted = out_buf;
        *out_formatted_len = out_len;
        service->last_error[0] = '\0';
        return true;
    }

    if (err_buf[0]) {
        snprintf(service->last_error, sizeof(service->last_error), "%s", err_buf);
    } else {
        snprintf(service->last_error, sizeof(service->last_error), "Formatter exited with code %lu", exit_code);
    }

    if (out_buf) th_free(out_buf);
    return false;

#else /* POSIX */
    if (strstr(command, "clang-format")) {
        if (filepath && filepath[0]) {
            snprintf(full_cmd, sizeof(full_cmd), "clang-format -assume-filename='%s'", filepath);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "clang-format");
        }
    } else if (strstr(command, "black")) {
        snprintf(full_cmd, sizeof(full_cmd), "black -q -");
    } else if (strstr(command, "prettier")) {
        if (filepath && filepath[0]) {
            snprintf(full_cmd, sizeof(full_cmd), "prettier --stdin-filepath '%s'", filepath);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "prettier");
        }
    } else {
        strncpy(full_cmd, command, sizeof(full_cmd) - 1);
        full_cmd[sizeof(full_cmd) - 1] = '\0';
    }

    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        snprintf(service->last_error, sizeof(service->last_error), "Failed to create pipes for formatter");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        snprintf(service->last_error, sizeof(service->last_error), "Failed to fork formatter process");
        return false;
    }

    if (pid == 0) {
        /* Child */
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);

        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[1]);

        execl("/bin/sh", "sh", "-c", full_cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    /* Write input code to child's stdin */
    size_t written_total = 0;
    while (written_total < input_len) {
        ssize_t w = write(in_pipe[1], input_code + written_total, input_len - written_total);
        if (w <= 0) break;
        written_total += w;
    }
    close(in_pipe[1]);

    /* Read child's stdout */
    size_t out_cap = input_len + 4096;
    char *out_buf = (char *)th_malloc(out_cap);
    size_t out_len = 0;

    char chunk[4096];
    ssize_t r = 0;
    while ((r = read(out_pipe[0], chunk, sizeof(chunk))) > 0) {
        if (out_len + r >= out_cap) {
            out_cap = out_cap * 2 + r;
            out_buf = (char *)th_realloc(out_buf, out_cap);
        }
        memcpy(out_buf + out_len, chunk, r);
        out_len += r;
    }
    close(out_pipe[0]);
    if (out_buf) {
        out_buf[out_len] = '\0';
    }

    /* Read child's stderr */
    char err_buf[512] = {0};
    ssize_t err_r = read(err_pipe[0], err_buf, sizeof(err_buf) - 1);
    if (err_r > 0) err_buf[err_r] = '\0';
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && out_len > 0) {
        *out_formatted = out_buf;
        *out_formatted_len = out_len;
        service->last_error[0] = '\0';
        return true;
    }

    if (err_buf[0]) {
        snprintf(service->last_error, sizeof(service->last_error), "%s", err_buf);
    } else {
        snprintf(service->last_error, sizeof(service->last_error), "Formatter exited with code %d", WEXITSTATUS(status));
    }

    if (out_buf) th_free(out_buf);
    return false;
#endif
}
