/**
 * @file completion.h
 * @brief Autocomplete engine for C/C++ keywords, standard library symbols, and buffer identifiers.
 */

#ifndef TH_COMPLETION_H
#define TH_COMPLETION_H

#include "../common/types.h"
#include "editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Populate completion list based on current buffer prefix.
 */
void th_completion_populate(ThCompletionList *list, const ThEditor *editor, const char *prefix);

/**
 * @brief Clear and release memory of a completion list.
 */
void th_completion_clear(ThCompletionList *list);

#ifdef __cplusplus
}
#endif

#endif /* TH_COMPLETION_H */
