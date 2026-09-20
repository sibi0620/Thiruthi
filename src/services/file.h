/**
 * @file file.h
 * @brief File service: safe file I/O, atomic saves, and directory operations.
 */

#ifndef TH_FILE_H
#define TH_FILE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[256];
    char path[1024];
    bool is_directory;
    size_t size;
} ThDirEntry;

typedef struct {
    ThDirEntry *entries;
    size_t count;
    size_t capacity;
} ThDirList;

typedef struct {
    char current_dir[1024];
} ThFileService;

void th_file_service_init(ThFileService *service);
void th_file_service_shutdown(ThFileService *service);

/**
 * @brief Reads entire file into a null-terminated memory buffer.
 * Caller owns the returned buffer and must free it via th_free().
 */
char *th_file_read(const char *path, size_t *out_size);

/**
 * @brief Writes buffer content atomically using a temp file and rename.
 */
bool th_file_write_atomic(const char *path, const char *content, size_t size);

bool th_file_exists(const char *path);
bool th_file_is_directory(const char *path);
int64_t th_file_get_mtime(const char *path);

const char *th_file_get_extension(const char *path);
void th_file_get_basename(const char *path, char *out_basename, size_t max_len);
void th_file_get_dirname(const char *path, char *out_dirname, size_t max_len);

/**
 * @brief List files in a directory.
 */
bool th_file_list_dir(const char *dirpath, ThDirList *out_list);
void th_file_dir_list_free(ThDirList *list);

#ifdef __cplusplus
}
#endif

#endif /* TH_FILE_H */
