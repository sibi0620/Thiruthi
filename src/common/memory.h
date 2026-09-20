/**
 * @file memory.h
 * @brief Explicit memory tracking, safe wrappers, and arena allocators.
 */

#ifndef TH_MEMORY_H
#define TH_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Memory Tracking Statistics --- */

typedef struct {
    size_t current_allocated_bytes;
    size_t peak_allocated_bytes;
    size_t total_allocations;
    size_t total_frees;
} ThMemoryStats;

void th_memory_init(void);
void th_memory_shutdown(void);
ThMemoryStats th_memory_get_stats(void);
void th_memory_report(void);

/* --- Safe Allocation Wrappers --- */

void *th_malloc_impl(size_t size, const char *file, int line);
void *th_calloc_impl(size_t count, size_t size, const char *file, int line);
void *th_realloc_impl(void *ptr, size_t new_size, const char *file, int line);
void th_free_impl(void *ptr, const char *file, int line);
char *th_strdup_impl(const char *s, const char *file, int line);

#define th_malloc(size) th_malloc_impl((size), __FILE__, __LINE__)
#define th_calloc(count, size) th_calloc_impl((count), (size), __FILE__, __LINE__)
#define th_realloc(ptr, new_size) th_realloc_impl((ptr), (new_size), __FILE__, __LINE__)
#define th_free(ptr) th_free_impl((ptr), __FILE__, __LINE__)
#define th_strdup(s) th_strdup_impl((s), __FILE__, __LINE__)

/* --- Arena Allocator --- */

typedef struct ThArenaChunk ThArenaChunk;

struct ThArenaChunk {
    size_t capacity;
    size_t offset;
    ThArenaChunk *next;
    uint8_t data[];
};

typedef struct {
    ThArenaChunk *first;
    ThArenaChunk *current;
    size_t default_chunk_size;
    size_t total_allocated;
} ThArena;

void th_arena_init(ThArena *arena, size_t initial_chunk_size);
void *th_arena_alloc(ThArena *arena, size_t size);
void *th_arena_alloc_zero(ThArena *arena, size_t size);
char *th_arena_strdup(ThArena *arena, const char *s);
void th_arena_reset(ThArena *arena);
void th_arena_free(ThArena *arena);

#ifdef __cplusplus
}
#endif

#endif /* TH_MEMORY_H */
