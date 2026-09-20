/**
 * @file test_lsp.c
 * @brief Unit tests for LSP client service (JSON-RPC protocol, URI helpers, parsing).
 */

#include "../src/services/lsp.h"
#include "../src/common/memory.h"
#include "../src/common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_lsp_uri_helpers(void) {
    char uri[1024];
    char path[1024];

#if TH_PLATFORM_WINDOWS
    th_lsp_path_to_uri("C:\\Users\\test\\main.c", uri, sizeof(uri));
    /* Should produce file:///C:/Users/test/main.c */
    assert(strncmp(uri, "file:///", 8) == 0);

    th_lsp_uri_to_path(uri, path, sizeof(path));
    /* Should contain the path with backslashes on Windows */
    assert(strstr(path, "main.c") != NULL);
#else
    th_lsp_path_to_uri("/home/user/project/main.c", uri, sizeof(uri));
    assert(strcmp(uri, "file:///home/user/project/main.c") == 0);

    th_lsp_uri_to_path(uri, path, sizeof(path));
    assert(strcmp(path, "/home/user/project/main.c") == 0);
#endif

    printf("  [PASS] test_lsp_uri_helpers\n");
}

static bool s_diag_received = false;
static size_t s_diag_count = 0;

static void mock_diag_cb(const char *uri, const ThDiagnosticList *diags, void *user_data) {
    (void)uri;
    (void)user_data;
    s_diag_received = true;
    s_diag_count = diags->count;
}

static void test_lsp_protocol_parsing(void) {
    ThLspClient client;
    th_lsp_init(&client);
    th_lsp_set_diagnostics_callback(&client, mock_diag_cb, NULL);

    /* Construct sample JSON-RPC publishDiagnostics message */
    const char *diag_json =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
        "\"uri\":\"file:///test.c\",\"diagnostics\":["
        "{\"range\":{\"start\":{\"line\":10,\"character\":4},\"end\":{\"line\":10,\"character\":12}},"
        "\"severity\":1,\"message\":\"Undefined symbol 'foo'\"},"
        "{\"range\":{\"start\":{\"line\":20,\"character\":0},\"end\":{\"line\":20,\"character\":5}},"
        "\"severity\":2,\"message\":\"Unused variable 'bar'\"}"
        "]}}";

    size_t len = strlen(diag_json);
    char full_packet[4096];
    int pkt_len = snprintf(full_packet, sizeof(full_packet), "Content-Length: %zu\r\n\r\n%s", len, diag_json);

    /* Feed into client read buffer */
    memcpy(client.read_buffer, full_packet, pkt_len);
    client.read_buffer_len = pkt_len;
    client.is_running = true;

    s_diag_received = false;
    s_diag_count = 0;

    /* Poll will process the buffer */
    th_lsp_poll(&client);

    assert(s_diag_received);
    assert(s_diag_count == 2);
    assert(client.read_buffer_len == 0);

    client.is_running = false;
    th_lsp_shutdown(&client);
    printf("  [PASS] test_lsp_protocol_parsing\n");
}

static bool s_compl_received = false;
static size_t s_compl_count = 0;

static void mock_compl_cb(const ThCompletionList *completions, void *user_data) {
    (void)user_data;
    s_compl_received = true;
    s_compl_count = completions->count;
}

static void test_lsp_completions_parsing(void) {
    ThLspClient client;
    th_lsp_init(&client);
    th_lsp_set_completions_callback(&client, mock_compl_cb, NULL);

    const char *compl_json =
        "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{\"items\":["
        "{\"label\":\"printf\",\"detail\":\"int printf(const char *format, ...)\",\"insertText\":\"printf\"},"
        "{\"label\":\"putchar\",\"detail\":\"int putchar(int char)\",\"insertText\":\"putchar\"}"
        "]}}";

    size_t len = strlen(compl_json);
    char packet[4096];
    int pkt_len = snprintf(packet, sizeof(packet), "Content-Length: %zu\r\n\r\n%s", len, compl_json);

    memcpy(client.read_buffer, packet, pkt_len);
    client.read_buffer_len = pkt_len;
    client.is_running = true;
    client.pending_completion_id = 42;

    s_compl_received = false;
    s_compl_count = 0;

    th_lsp_poll(&client);

    assert(s_compl_received);
    assert(s_compl_count == 2);

    client.is_running = false;
    th_lsp_shutdown(&client);
    printf("  [PASS] test_lsp_completions_parsing\n");
}

int main(void) {
    printf("Running LSP Service Tests...\n");
    th_memory_init();

    test_lsp_uri_helpers();
    test_lsp_protocol_parsing();
    test_lsp_completions_parsing();

    ThMemoryStats stats = th_memory_get_stats();
    printf("LSP tests memory remaining: %zu bytes\n", stats.current_allocated_bytes);
    assert(stats.current_allocated_bytes == 0);

    th_memory_shutdown();
    printf("All LSP Service Tests Passed Successfully!\n\n");
    return 0;
}
