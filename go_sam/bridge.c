/*
 * bridge.c - C/CGO Bridge for Go sam Extension
 *
 * API Version: 3 (Event Bus)
 *
 * This bridge provides C wrappers for Go functions and mirrors the
 * μEmacs extension API struct for CGO interop.
 *
 * Implements Rob Pike's structural regular expressions from sam.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "_cgo_export.h"

// Command function type
typedef int (*cmd_fn_t)(int, int);

// Event handler type (API v3)
typedef bool (*event_fn_t)(void*, void*);

// Lexer state (must match extension_api.h)
typedef struct {
    int mode;
    int nest_depth;
    char string_delim;
    uint32_t state_hash;
} lexer_state_t;

// Line tokens opaque type
typedef struct line_tokens line_tokens_t;

// Opaque types from μEmacs
struct buffer;
struct window;
struct syntax_language;

// Lexer callback signature
typedef lexer_state_t (*syntax_lex_fn)(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
);

// Full API struct (must match extension_api.h v3 exactly)
typedef struct {
    int api_version;

    // Event Bus (API v3)
    int (*on)(const char*, event_fn_t, void*, int);
    int (*off)(const char*, event_fn_t);
    bool (*emit)(const char*, void*);

    // Configuration Access
    int (*config_int)(const char*, const char*, int);
    bool (*config_bool)(const char*, const char*, bool);
    const char *(*config_string)(const char*, const char*, const char*);

    // Command Registration
    int (*register_command)(const char*, cmd_fn_t);
    int (*unregister_command)(const char*);

    // Buffer Operations
    void *(*current_buffer)(void);
    void *(*find_buffer)(const char*);
    char *(*buffer_contents)(void*, size_t*);
    const char *(*buffer_filename)(void*);
    const char *(*buffer_name)(void*);
    bool (*buffer_modified)(void*);
    int (*buffer_insert)(const char*, size_t);
    int (*buffer_insert_at)(void*, int, int, const char*, size_t);
    void *(*buffer_create)(const char*);
    int (*buffer_switch)(void*);
    int (*buffer_clear)(void*);

    // Cursor/Point Operations
    void (*get_point)(int*, int*);
    void (*set_point)(int, int);
    int (*get_line_count)(void*);
    char *(*get_word_at_point)(void);
    char *(*get_current_line)(void);

    // Window Operations
    struct window *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(void*, int);
    struct window *(*window_at_row)(int);
    int (*window_switch)(void*);

    // Mouse/Cursor Helpers
    int (*screen_to_buffer_pos)(void*, int, int, int*, int*);
    int (*set_mark)(void);
    int (*scroll_up)(int);
    int (*scroll_down)(int);

    // User Interface
    void (*message)(const char*, ...);
    void (*vmessage)(const char*, void*);
    int (*prompt)(const char*, char*, size_t);
    int (*prompt_yn)(const char*);
    void (*update_display)(void);

    // File Operations
    int (*find_file_line)(const char*, int);

    // Shell Integration
    int (*shell_command)(const char*, char**, size_t*);

    // Memory Helpers
    void *(*alloc)(size_t);
    void (*free)(void*);
    char *(*strdup)(const char*);

    // Logging
    void (*log_info)(const char*, ...);
    void (*log_warn)(const char*, ...);
    void (*log_error)(const char*, ...);
    void (*log_debug)(const char*, ...);

    // Syntax Highlighting
    int (*syntax_register_lexer)(const char*, const char**, syntax_lex_fn, void*);
    int (*syntax_unregister_lexer)(const char*);
    int (*syntax_add_token)(line_tokens_t*, int, int);
    void (*syntax_invalidate_buffer)(struct buffer*);
} uemacs_api;

typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(uemacs_api*);
    void (*cleanup)(void);
} uemacs_extension;

// Global API pointer
static uemacs_api *g_api = NULL;

// ============================================================================
// API wrappers for Go
// ============================================================================

void api_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void api_log_info(const char *msg) {
    if (g_api && g_api->log_info) g_api->log_info("%s", msg);
}

void api_log_error(const char *msg) {
    if (g_api && g_api->log_error) g_api->log_error("%s", msg);
}

void* api_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

const char* api_buffer_filename(void *bp) {
    if (g_api && g_api->buffer_filename) return g_api->buffer_filename(bp);
    return NULL;
}

const char* api_buffer_name(void *bp) {
    if (g_api && g_api->buffer_name) return g_api->buffer_name(bp);
    return NULL;
}

char* api_buffer_contents(void *bp, size_t *len) {
    if (g_api && g_api->buffer_contents) return g_api->buffer_contents(bp, len);
    return NULL;
}

void api_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
}

void api_set_point(int line, int col) {
    if (g_api && g_api->set_point) g_api->set_point(line, col);
}

void* api_buffer_create(const char *name) {
    if (g_api && g_api->buffer_create) return g_api->buffer_create(name);
    return NULL;
}

int api_buffer_switch(void *bp) {
    if (g_api && g_api->buffer_switch) return g_api->buffer_switch(bp);
    return 0;
}

int api_buffer_clear(void *bp) {
    if (g_api && g_api->buffer_clear) return g_api->buffer_clear(bp);
    return 0;
}

int api_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
    return 0;
}

int api_prompt(const char *prompt, char *buf, size_t buflen) {
    if (g_api && g_api->prompt) return g_api->prompt(prompt, buf, buflen);
    return -1;  // Failure
}

void api_free(void *ptr) {
    if (g_api && g_api->free) g_api->free(ptr);
}

int api_delete_chars(int n) {
    // Delete n characters at current position by inserting empty
    // For now, this is a no-op - we handle deletion via buffer_clear + buffer_insert
    (void)n;
    return 0;
}

// ============================================================================
// Command wrappers (call Go functions)
// ============================================================================

static int cmd_sam_x(int f, int n) { return go_sam_x(f, n); }
static int cmd_sam_y(int f, int n) { return go_sam_y(f, n); }
static int cmd_sam_g(int f, int n) { return go_sam_g(f, n); }
static int cmd_sam_v(int f, int n) { return go_sam_v(f, n); }
static int cmd_sam_edit(int f, int n) { return go_sam_edit(f, n); }
static int cmd_sam_pipe(int f, int n) { return go_sam_pipe(f, n); }
static int cmd_sam_help(int f, int n) { return go_sam_help(f, n); }

// ============================================================================
// Extension lifecycle
// ============================================================================

static int sam_init_c(uemacs_api *api) {
    g_api = api;

    // Initialize Go side
    sam_init(api);

    // Register commands
    if (g_api->register_command) {
        g_api->register_command("sam-x", cmd_sam_x);
        g_api->register_command("sam-y", cmd_sam_y);
        g_api->register_command("sam-g", cmd_sam_g);
        g_api->register_command("sam-v", cmd_sam_v);
        g_api->register_command("sam-edit", cmd_sam_edit);
        g_api->register_command("sam-pipe", cmd_sam_pipe);
        g_api->register_command("sam-help", cmd_sam_help);
    }

    api_log_info("go_sam: Structural regex extension loaded (Pike's sam commands)");
    return 0;
}

static void sam_cleanup_c(void) {
    if (g_api) {
        // Unregister commands
        if (g_api->unregister_command) {
            g_api->unregister_command("sam-x");
            g_api->unregister_command("sam-y");
            g_api->unregister_command("sam-g");
            g_api->unregister_command("sam-v");
            g_api->unregister_command("sam-edit");
            g_api->unregister_command("sam-pipe");
            g_api->unregister_command("sam-help");
        }
    }
}

// ============================================================================
// Extension entry point
// ============================================================================

static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "go_sam",
    .version = "1.0.0",
    .description = "Rob Pike's structural regular expressions (sam)",
    .init = sam_init_c,
    .cleanup = sam_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
