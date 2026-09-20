/**
 * @file memory.c
 * @brief Memory tracking and arena allocation implementation.
 */

#include "memory.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TH_MEM_MAGIC 0x54484D45 /* "THME" */

typedef struct {
    uint32_t magic;
    size_t size;
    const char *file;
    int line;
} ThMemHeader;

static ThMemoryStats g_mem_stats = {0};
static th_mutex_t g_mem_lock;
static bool g_mem_initialized = false;

void th_memory_init(void) {
    if (!g_mem_initialized) {
        th_mutex_init(&g_mem_lock);
        g_mem_initialized = true;
    }
    th_mutex_lock(&g_mem_lock);
    memset(&g_mem_stats, 0, sizeof(g_mem_stats));
    th_mutex_unlock(&g_mem_lock);
}

void th_memory_shutdown(void) {
    th_mutex_lock(&g_mem_lock);
    if (g_mem_stats.current_allocated_bytes > 0) {
        fprintf(stderr, "[TH_MEMORY] Warning: Memory leak detected! %zu bytes unfreed (%zu allocs vs %zu frees)\n",
                g_mem_stats.current_allocated_bytes,
                g_mem_stats.total_allocations,
                g_mem_stats.total_frees);
    }
    th_mutex_unlock(&g_mem_lock);

    if (g_mem_initialized) {
        th_mutex_destroy(&g_mem_lock);
        g_mem_initialized = false;
    }
}

ThMemoryStats th_memory_get_stats(void) {
    th_mutex_lock(&g_mem_lock);
    ThMemoryStats stats = g_mem_stats;
    th_mutex_unlock(&g_mem_lock);
    return stats;
}

void th_memory_report(void) {
    th_mutex_lock(&g_mem_lock);
    printf("--- Thiruthi Memory Report ---\n");
    printf("Current Allocated: %zu bytes\n", g_mem_stats.current_allocated_bytes);
    printf("Peak Allocated:    %zu bytes\n", g_mem_stats.peak_allocated_bytes);
    printf("Total Allocations: %zu\n", g_mem_stats.total_allocations);
    printf("Total Frees:       %zu\n", g_mem_stats.total_frees);
    printf("------------------------------\n");
    th_mutex_unlock(&g_mem_lock);
}

void *th_malloc_impl(size_t size, const char *file, int line) {
    if (size == 0) return NULL;

    size_t total_size = sizeof(ThMemHeader) + size;
    ThMemHeader *header = (ThMemHeader *)malloc(total_size);
    if (!header) {
        fprintf(stderr, "[TH_MEMORY] Out of memory at %s:%d (requested %zu bytes)\n", file, line, size);
        return NULL;
    }

    header->magic = TH_MEM_MAGIC;
    header->size = size;
    header->file = file;
    header->line = line;

    th_mutex_lock(&g_mem_lock);
    g_mem_stats.current_allocated_bytes += size;
    if (g_mem_stats.current_allocated_bytes > g_mem_stats.peak_allocated_bytes) {
        g_mem_stats.peak_allocated_bytes = g_mem_stats.current_allocated_bytes;
    }
    g_mem_stats.total_allocations++;
    th_mutex_unlock(&g_mem_lock);

    return (void *)((uint8_t *)header + sizeof(ThMemHeader));
}

void *th_calloc_impl(size_t count, size_t size, const char *file, int line) {
    size_t total = count * size;
    void *ptr = th_malloc_impl(total, file, line);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *th_realloc_impl(void *ptr, size_t new_size, const char *file, int line) {
    if (!ptr) {
        return th_malloc_impl(new_size, file, line);
    }
    if (new_size == 0) {
        th_free_impl(ptr, file, line);
        return NULL;
    }

    ThMemHeader *old_header = (ThMemHeader *)((uint8_t *)ptr - sizeof(ThMemHeader));
    if (old_header->magic != TH_MEM_MAGIC) {
        fprintf(stderr, "[TH_MEMORY] Invalid pointer passed to realloc at %s:%d\n", file, line);
        return realloc(ptr, new_size);
    }

    size_t old_size = old_header->size;
    void *new_ptr = th_malloc_impl(new_size, file, line);
    if (!new_ptr) return NULL;

    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    th_free_impl(ptr, file, line);

    return new_ptr;
}

void th_free_impl(void *ptr, const char *file, int line) {
    if (!ptr) return;

    ThMemHeader *header = (ThMemHeader *)((uint8_t *)ptr - sizeof(ThMemHeader));
    if (header->magic != TH_MEM_MAGIC) {
        fprintf(stderr, "[TH_MEMORY] Warning: Freeing untracked or corrupted pointer at %s:%d\n", file, line);
        free(ptr);
        return;
    }

    header->magic = 0; /* Invalidate magic to detect double-free */

    th_mutex_lock(&g_mem_lock);
    if (g_mem_stats.current_allocated_bytes >= header->size) {
        g_mem_stats.current_allocated_bytes -= header->size;
    } else {
        g_mem_stats.current_allocated_bytes = 0;
    }
    g_mem_stats.total_frees++;
    th_mutex_unlock(&g_mem_lock);

    free(header);
}

char *th_strdup_impl(const char *s, const char *file, int line) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = (char *)th_malloc_impl(len + 1, file, line);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

/* --- Arena Implementation --- */

void th_arena_init(ThArena *arena, size_t initial_chunk_size) {
    if (!arena) return;
    if (initial_chunk_size < 1024) {
        initial_chunk_size = 1024 * 64; /* 64 KB default chunk */
    }
    arena->default_chunk_size = initial_chunk_size;
    arena->total_allocated = 0;

    ThArenaChunk *chunk = (ThArenaChunk *)th_malloc(sizeof(ThArenaChunk) + initial_chunk_size);
    if (chunk) {
        chunk->capacity = initial_chunk_size;
        chunk->offset = 0;
        chunk->next = NULL;
    }
    arena->first = chunk;
    arena->current = chunk;
}

static inline size_t align8(size_t n) {
    return (n + 7) & ~7;
}

void *th_arena_alloc(ThArena *arena, size_t size) {
    if (!arena || size == 0) return NULL;
    size = align8(size);

    if (!arena->current) {
        th_arena_init(arena, arena->default_chunk_size ? arena->default_chunk_size : 64 * 1024);
        if (!arena->current) return NULL;
    }

    if (arena->current->offset + size > arena->current->capacity) {
        size_t next_capacity = arena->default_chunk_size;
        if (size > next_capacity) {
            next_capacity = size * 2;
        }

        ThArenaChunk *new_chunk = (ThArenaChunk *)th_malloc(sizeof(ThArenaChunk) + next_capacity);
        if (!new_chunk) return NULL;

        new_chunk->capacity = next_capacity;
        new_chunk->offset = 0;
        new_chunk->next = NULL;

        arena->current->next = new_chunk;
        arena->current = new_chunk;
    }

    void *ptr = arena->current->data + arena->current->offset;
    arena->current->offset += size;
    arena->total_allocated += size;
    return ptr;
}

void *th_arena_alloc_zero(ThArena *arena, size_t size) {
    void *ptr = th_arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char *th_arena_strdup(ThArena *arena, const char *s) {
    if (!arena || !s) return NULL;
    size_t len = strlen(s);
    char *dup = (char *)th_arena_alloc(arena, len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

void th_arena_reset(ThArena *arena) {
    if (!arena) return;
    ThArenaChunk *chunk = arena->first;
    while (chunk) {
        chunk->offset = 0;
        chunk = chunk->next;
    }
    arena->current = arena->first;
    arena->total_allocated = 0;
}

void th_arena_free(ThArena *arena) {
    if (!arena) return;
    ThArenaChunk *chunk = arena->first;
    while (chunk) {
        ThArenaChunk *next = chunk->next;
        th_free(chunk);
        chunk = next;
    }
    arena->first = NULL;
    arena->current = NULL;
    arena->total_allocated = 0;
}
