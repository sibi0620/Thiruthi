/**
 * @file completion.c
 * @brief Autocomplete engine implementation.
 */

#include "completion.h"
#include "../common/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *label;
    const char *detail;
    int kind; /* 14 = keyword, 2 = function, 6 = type */
} StaticSymbol;

static const StaticSymbol s_static_symbols[] = {
    {"include", "Preprocessor directive", 14},
    {"define", "Macro definition", 14},
    {"ifdef", "Conditional compilation", 14},
    {"ifndef", "Header guard check", 14},
    {"endif", "End conditional", 14},
    {"pragma", "Compiler directive", 14},
    {"int", "Primitive integer type", 6},
    {"char", "Character type", 6},
    {"void", "Void type", 6},
    {"float", "Single precision float", 6},
    {"double", "Double precision float", 6},
    {"bool", "Boolean type", 6},
    {"size_t", "Unsigned integer type", 6},
    {"uint8_t", "8-bit unsigned integer", 6},
    {"uint16_t", "16-bit unsigned integer", 6},
    {"uint32_t", "32-bit unsigned integer", 6},
    {"uint64_t", "64-bit unsigned integer", 6},
    {"int8_t", "8-bit signed integer", 6},
    {"int16_t", "16-bit signed integer", 6},
    {"int32_t", "32-bit signed integer", 6},
    {"int64_t", "64-bit signed integer", 6},
    {"const", "Constant qualifier", 14},
    {"static", "Static storage duration", 14},
    {"struct", "Structure declaration", 14},
    {"typedef", "Type definition", 14},
    {"enum", "Enumeration declaration", 14},
    {"union", "Union declaration", 14},
    {"return", "Return from function", 14},
    {"if", "Conditional branch", 14},
    {"else", "Else branch", 14},
    {"while", "While loop", 14},
    {"for", "For loop", 14},
    {"do", "Do-while loop", 14},
    {"switch", "Switch statement", 14},
    {"case", "Case label", 14},
    {"break", "Break loop/switch", 14},
    {"continue", "Continue loop", 14},
    {"default", "Default case label", 14},
    {"sizeof", "Sizeof operator", 14},
    {"NULL", "Null pointer constant", 14},
    {"true", "Boolean true", 14},
    {"false", "Boolean false", 14},
    {"printf", "Formatted output: printf(fmt, ...)", 2},
    {"snprintf", "Safe buffer output: snprintf(buf, sz, fmt, ...)", 2},
    {"malloc", "Allocate dynamic memory: malloc(size)", 2},
    {"free", "Release dynamic memory: free(ptr)", 2},
    {"realloc", "Reallocate dynamic memory: realloc(ptr, size)", 2},
    {"memcpy", "Copy memory block: memcpy(dst, src, n)", 2},
    {"memset", "Fill memory block: memset(dst, val, n)", 2},
    {"strlen", "Get string length: strlen(str)", 2},
    {"strcmp", "Compare strings: strcmp(s1, s2)", 2},
    {"strncmp", "Compare n string chars: strncmp(s1, s2, n)", 2},
    {"strcpy", "Copy string: strcpy(dst, src)", 2},
    {"strncpy", "Copy n string chars: strncpy(dst, src, n)", 2},
    {"strchr", "Find char in string: strchr(str, ch)", 2},
    {"strstr", "Find substring: strstr(str, sub)", 2}
};

static const size_t s_static_symbol_count = sizeof(s_static_symbols) / sizeof(s_static_symbols[0]);

void th_completion_clear(ThCompletionList *list) {
    if (!list) return;
    if (list->items) {
        th_free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

static void add_completion_item(ThCompletionList *list, const char *label, const char *detail, int kind) {
    if (!list || !label || !*label) return;

    /* Check duplicates */
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].label, label) == 0) {
            return;
        }
    }

    if (list->count >= list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 16 : list->capacity * 2;
        list->items = (ThCompletionItem *)th_realloc(list->items, sizeof(ThCompletionItem) * new_cap);
        list->capacity = new_cap;
    }

    ThCompletionItem *item = &list->items[list->count++];
    memset(item, 0, sizeof(ThCompletionItem));
    strncpy(item->label, label, sizeof(item->label) - 1);
    strncpy(item->insert_text, label, sizeof(item->insert_text) - 1);
    if (detail) strncpy(item->detail, detail, sizeof(item->detail) - 1);
    item->kind = kind;
}

static int compare_completions(const void *a, const void *b) {
    const ThCompletionItem *ia = (const ThCompletionItem *)a;
    const ThCompletionItem *ib = (const ThCompletionItem *)b;
    return strcmp(ia->label, ib->label);
}

void th_completion_populate(ThCompletionList *list, const ThEditor *editor, const char *prefix) {
    if (!list) return;
    th_completion_clear(list);

    if (!prefix || prefix[0] == '\0') return;
    size_t prefix_len = strlen(prefix);

    /* 1. Static Language Keywords & Standard Library */
    for (size_t i = 0; i < s_static_symbol_count; i++) {
        const StaticSymbol *sym = &s_static_symbols[i];
        if (strncasecmp(sym->label, prefix, prefix_len) == 0) {
            add_completion_item(list, sym->label, sym->detail, sym->kind);
        }
    }

    /* 2. Document Identifiers */
    if (editor) {
        size_t line_cnt = th_editor_get_line_count(editor);
        for (size_t l = 0; l < line_cnt && list->count < 32; l++) {
            size_t l_len = 0;
            const char *line = th_editor_get_line(editor, l, &l_len);
            if (!line || l_len < 3) continue;

            size_t idx = 0;
            while (idx < l_len) {
                while (idx < l_len && !(isalpha((unsigned char)line[idx]) || line[idx] == '_')) {
                    idx++;
                }
                size_t word_start = idx;
                while (idx < l_len && (isalnum((unsigned char)line[idx]) || line[idx] == '_')) {
                    idx++;
                }
                size_t w_len = idx - word_start;
                if (w_len >= 3 && w_len < 64 && w_len > prefix_len) {
                    if (strncasecmp(line + word_start, prefix, prefix_len) == 0) {
                        char word_buf[64];
                        memcpy(word_buf, line + word_start, w_len);
                        word_buf[w_len] = '\0';
                        add_completion_item(list, word_buf, "Identifier in file", 5);
                    }
                }
            }
        }
    }

    /* Sort completions alphabetically */
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(ThCompletionItem), compare_completions);
    }
}
