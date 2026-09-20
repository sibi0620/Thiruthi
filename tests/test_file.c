/**
 * @file test_file.c
 * @brief Unit tests for file service (atomic writes, reads, directory listing).
 */

#include "../src/services/file.h"
#include "../src/common/memory.h"
#include "../src/common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_file_read_write_atomic(void) {
#if TH_PLATFORM_WINDOWS
    const char *test_path = "thiruthi_test_file.txt";
#else
    const char *test_path = "/tmp/thiruthi_test_file.txt";
#endif
    const char *payload = "Thiruthi atomic file test content.\nLine 2.\nLine 3.";

    bool write_ok = th_file_write_atomic(test_path, payload, strlen(payload));
    assert(write_ok);
    assert(th_file_exists(test_path));

    size_t read_size = 0;
    char *content = th_file_read(test_path, &read_size);
    assert(content != NULL);
    assert(read_size == strlen(payload));
    assert(strcmp(content, payload) == 0);

    th_free(content);
    th_unlink(test_path);
    assert(!th_file_exists(test_path));

    printf("  [PASS] test_file_read_write_atomic\n");
}

static void test_file_path_helpers(void) {
#if TH_PLATFORM_WINDOWS
    const char *path = "C:\\Users\\dev\\workspace\\thiruthi\\src\\main.c";
#else
    const char *path = "/home/developer/workspace/thiruthi/src/main.c";
#endif

    const char *ext = th_file_get_extension(path);
    assert(strcmp(ext, ".c") == 0);

    char base[256];
    th_file_get_basename(path, base, sizeof(base));
    assert(strcmp(base, "main.c") == 0);

    char dir[256];
    th_file_get_dirname(path, dir, sizeof(dir));
#if TH_PLATFORM_WINDOWS
    assert(strcmp(dir, "C:\\Users\\dev\\workspace\\thiruthi\\src") == 0);
#else
    assert(strcmp(dir, "/home/developer/workspace/thiruthi/src") == 0);
#endif

    printf("  [PASS] test_file_path_helpers\n");
}

static void test_file_list_dir(void) {
    ThDirList list;
    bool ok = th_file_list_dir(".", &list);
    assert(ok);
    assert(list.count > 0);
    assert(list.entries != NULL);

    th_file_dir_list_free(&list);
    assert(list.count == 0);
    assert(list.entries == NULL);

    printf("  [PASS] test_file_list_dir\n");
}

int main(void) {
    printf("Running File Service Tests...\n");
    th_memory_init();

    test_file_read_write_atomic();
    test_file_path_helpers();
    test_file_list_dir();

    ThMemoryStats stats = th_memory_get_stats();
    printf("File tests memory remaining: %zu bytes\n", stats.current_allocated_bytes);
    assert(stats.current_allocated_bytes == 0);

    th_memory_shutdown();
    printf("All File Service Tests Passed Successfully!\n\n");
    return 0;
}
