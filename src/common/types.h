/**
 * @file types.h
 * @brief Common types, enumerations, and data structures for Thiruthi.
 */

#ifndef TH_TYPES_H
#define TH_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 2D text buffer position (0-indexed).
 */
typedef struct {
    uint32_t line;
    uint32_t col;
} ThPosition;

/**
 * @brief Text range in buffer.
 */
typedef struct {
    ThPosition start;
    ThPosition end;
} ThRange;

/**
 * @brief Diagnostic severity levels matching LSP specification.
 */
typedef enum {
    TH_DIAG_ERROR = 1,
    TH_DIAG_WARNING = 2,
    TH_DIAG_INFO = 3,
    TH_DIAG_HINT = 4
} ThDiagnosticSeverity;

/**
 * @brief Diagnostic entry (from LSP or external linter).
 */
typedef struct {
    ThRange range;
    ThDiagnosticSeverity severity;
    char source[64];
    char message[512];
} ThDiagnostic;

/**
 * @brief List of diagnostics.
 */
typedef struct {
    ThDiagnostic *items;
    size_t count;
    size_t capacity;
} ThDiagnosticList;

/**
 * @brief LSP completion item.
 */
typedef struct {
    char label[128];
    char detail[128];
    char documentation[256];
    char insert_text[128];
    int kind;
} ThCompletionItem;

/**
 * @brief List of completion items.
 */
typedef struct {
    ThCompletionItem *items;
    size_t count;
    size_t capacity;
} ThCompletionList;

/**
 * @brief Hover tooltip information.
 */
typedef struct {
    ThPosition position;
    char contents[1024];
    bool active;
} ThHoverInfo;

/**
 * @brief Supported languages in Thiruthi.
 */
typedef enum {
    TH_LANG_UNKNOWN = 0,
    TH_LANG_C,
    TH_LANG_CPP,
    TH_LANG_PYTHON,
    TH_LANG_JAVASCRIPT,
    TH_LANG_TYPESCRIPT,
    TH_LANG_HTML,
    TH_LANG_CSS,
    TH_LANG_MARKDOWN,
    TH_LANG_COUNT
} ThLanguageId;

/**
 * @brief Syntax token classification.
 */
typedef enum {
    TH_TOKEN_DEFAULT = 0,
    TH_TOKEN_KEYWORD,
    TH_TOKEN_TYPE,
    TH_TOKEN_FUNCTION,
    TH_TOKEN_STRING,
    TH_TOKEN_NUMBER,
    TH_TOKEN_COMMENT,
    TH_TOKEN_OPERATOR,
    TH_TOKEN_PREPROCESSOR,
    TH_TOKEN_VARIABLE,
    TH_TOKEN_PUNCTUATION,
    TH_TOKEN_COUNT
} ThTokenType;

/**
 * @brief Highlight span within a line.
 */
typedef struct {
    uint32_t start_col;
    uint32_t end_col;
    ThTokenType type;
} ThHighlightSpan;

/**
 * @brief Line highlight token spans.
 */
typedef struct {
    ThHighlightSpan *spans;
    uint32_t count;
    uint32_t capacity;
} ThHighlightLine;

/**
 * @brief RGBA color representation.
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} ThColor;

/**
 * @brief Status notification level.
 */
typedef enum {
    TH_STATUS_INFO,
    TH_STATUS_SUCCESS,
    TH_STATUS_WARNING,
    TH_STATUS_ERROR
} ThStatusLevel;

/**
 * @brief Status message displayed on the status bar.
 */
typedef struct {
    char text[256];
    ThStatusLevel level;
    double timestamp;
} ThStatusMessage;

/**
 * @brief Inter-service event types for decoupled message passing.
 */
typedef enum {
    TH_EVENT_NONE = 0,
    TH_EVENT_FILE_OPENED,
    TH_EVENT_FILE_SAVED,
    TH_EVENT_BUFFER_MODIFIED,
    TH_EVENT_CURSOR_MOVED,
    TH_EVENT_DIAGNOSTICS_UPDATED,
    TH_EVENT_COMPLETIONS_READY,
    TH_EVENT_HOVER_READY,
    TH_EVENT_FORMAT_REQUESTED,
    TH_EVENT_LINT_REQUESTED,
    TH_EVENT_STATUS_MESSAGE
} ThEventType;

/**
 * @brief Generic event payload passed through the service bus.
 */
typedef struct {
    ThEventType type;
    const char *filepath;
    ThLanguageId language;
    ThPosition cursor;
    void *data;
} ThEvent;

typedef void (*ThEventCallback)(const ThEvent *event, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TH_TYPES_H */
