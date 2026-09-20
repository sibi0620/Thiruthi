/**
 * @file file.c
 * @brief Cross-platform file service implementation.
 */

#include "file.h"
#include "../common/memory.h"
#include "../common/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TH_PLATFORM_WINDOWS
    #include <windows.h>
    #include <io.h>
#else
    #include <dirent.h>
    #include <unistd.h>
    #include <sys/stat.h>
#endif

void th_file_service_init(ThFileService *service) {
    if (!service) return;
    if (!th_getcwd(service->current_dir, sizeof(service->current_dir))) {
        strncpy(service->current_dir, ".", sizeof(service->current_dir));
    }
}

void th_file_service_shutdown(ThFileService *service) {
    (void)service;
}

char *th_file_read(const char *path, size_t *out_size) {
    if (out_size) *out_size = 0;
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buffer = (char *)th_malloc(file_size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, file_size, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    if (out_size) {
        *out_size = read_bytes;
    }

    return buffer;
}

bool th_file_write_atomic(const char *path, const char *content, size_t size) {
    if (!path || !content) return false;

#if TH_PLATFORM_WINDOWS
    static unsigned long s_counter = 0;
    char temp_path[1024];
    snprintf(temp_path, sizeof(temp_path), "%s.th_tmp_%lu_%lu", path, (unsigned long)GetCurrentProcessId(), ++s_counter);

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        /* Fallback: write directly */
        FILE *direct_f = fopen(path, "wb");
        if (!direct_f) return false;
        size_t written = fwrite(content, 1, size, direct_f);
        fclose(direct_f);
        return (written == size);
    }

    size_t written = fwrite(content, 1, size, f);
    fflush(f);
    int fd = _fileno(f);
    if (fd >= 0) {
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        if (h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h);
        }
    }
    fclose(f);

    if (written != size) {
        th_unlink(temp_path);
        return false;
    }

    /* MoveFileEx with MOVEFILE_REPLACE_EXISTING can atomically replace destination */
    if (!MoveFileExA(temp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        /* If MoveFileEx failed, try unlink + rename */
        th_unlink(path);
        if (rename(temp_path, path) != 0) {
            th_unlink(temp_path);
            return false;
        }
    }
    return true;

#else /* POSIX */
    char temp_path[1024];
    snprintf(temp_path, sizeof(temp_path), "%s.th_tmp.XXXXXX", path);

    int fd = mkstemp(temp_path);
    if (fd == -1) {
        /* Fallback if temp file in same directory cannot be created */
        FILE *direct_f = fopen(path, "wb");
        if (!direct_f) return false;
        size_t written = fwrite(content, 1, size, direct_f);
        fclose(direct_f);
        return written == size;
    }

    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(temp_path);
        return false;
    }

    size_t written = fwrite(content, 1, size, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (written != size) {
        unlink(temp_path);
        return false;
    }

    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return false;
    }

    return true;
#endif
}

bool th_file_exists(const char *path) {
    if (!path) return false;
#if TH_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES);
#else
    return access(path, F_OK) == 0;
#endif
}

bool th_file_is_directory(const char *path) {
    if (!path) return false;
#if TH_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

int64_t th_file_get_mtime(const char *path) {
    if (!path) return 0;
#if TH_PLATFORM_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    /* Convert 100-nanosecond intervals since Jan 1, 1601 to Unix epoch seconds */
    return (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
#endif
}

const char *th_file_get_extension(const char *path) {
    if (!path) return "";
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}

void th_file_get_basename(const char *path, char *out_basename, size_t max_len) {
    if (!out_basename || max_len == 0) return;
    out_basename[0] = '\0';
    if (!path) return;

    const char *last_sep = th_find_last_sep(path);
    const char *name = last_sep ? last_sep + 1 : path;
    strncpy(out_basename, name, max_len - 1);
    out_basename[max_len - 1] = '\0';
}

void th_file_get_dirname(const char *path, char *out_dirname, size_t max_len) {
    if (!out_dirname || max_len == 0) return;
    out_dirname[0] = '\0';
    if (!path) return;

    const char *last_sep = th_find_last_sep(path);
    if (last_sep) {
        size_t len = (size_t)(last_sep - path);
        if (len == 0) len = 1; /* Root slash '/' */
        if (len >= max_len) len = max_len - 1;
        memcpy(out_dirname, path, len);
        out_dirname[len] = '\0';
    } else {
        strncpy(out_dirname, ".", max_len - 1);
        out_dirname[max_len - 1] = '\0';
    }
}

bool th_file_list_dir(const char *dirpath, ThDirList *out_list) {
    if (!out_list) return false;
    memset(out_list, 0, sizeof(ThDirList));

    const char *target = (dirpath && dirpath[0]) ? dirpath : ".";

    size_t capacity = 32;
    ThDirEntry *entries = (ThDirEntry *)th_malloc(sizeof(ThDirEntry) * capacity);
    if (!entries) return false;
    size_t count = 0;

#if TH_PLATFORM_WINDOWS
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", target);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        th_free(entries);
        return false;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            ThDirEntry *new_entries = (ThDirEntry *)th_realloc(entries, sizeof(ThDirEntry) * capacity);
            if (!new_entries) {
                break;
            }
            entries = new_entries;
        }

        ThDirEntry *entry = &entries[count];
        strncpy(entry->name, fd.cFileName, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';

        snprintf(entry->path, sizeof(entry->path), "%s\\%s", target, fd.cFileName);
        entry->is_directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ULARGE_INTEGER sz;
        sz.LowPart = fd.nFileSizeLow;
        sz.HighPart = fd.nFileSizeHigh;
        entry->size = (size_t)sz.QuadPart;
        count++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

#else /* POSIX */
    DIR *dir = opendir(target);
    if (!dir) {
        th_free(entries);
        return false;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            ThDirEntry *new_entries = (ThDirEntry *)th_realloc(entries, sizeof(ThDirEntry) * capacity);
            if (!new_entries) {
                break;
            }
            entries = new_entries;
        }

        ThDirEntry *entry = &entries[count];
        strncpy(entry->name, de->d_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';

        snprintf(entry->path, sizeof(entry->path), "%s/%s", target, de->d_name);

        struct stat st;
        if (stat(entry->path, &st) == 0) {
            entry->is_directory = S_ISDIR(st.st_mode);
            entry->size = st.st_size;
        } else {
            entry->is_directory = false;
            entry->size = 0;
        }
        count++;
    }

    closedir(dir);
#endif

    out_list->entries = entries;
    out_list->count = count;
    out_list->capacity = capacity;
    return true;
}

void th_file_dir_list_free(ThDirList *list) {
    if (!list) return;
    if (list->entries) {
        th_free(list->entries);
        list->entries = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}
