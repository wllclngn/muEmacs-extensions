/*
 * bridge.c - C/CGO Bridge for Go DFS Extension
 *
 * API Version: 3 (Event Bus)
 *
 * Provides concurrent DFS file traversal for μEmacs.
 * Uses work-stealing deques for optimal parallelism.
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

typedef struct line_tokens line_tokens_t;

struct buffer;
struct window;
struct syntax_language;

typedef lexer_state_t (*syntax_lex_fn)(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
);

// Full API struct (must match extension_api.h v3)
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
    return -1;
}

void api_free(void *ptr) {
    if (g_api && g_api->free) g_api->free(ptr);
}

void api_update_display(void) {
    if (g_api && g_api->update_display) g_api->update_display();
}

int api_find_file_line(const char *path, int line) {
    if (g_api && g_api->find_file_line) return g_api->find_file_line(path, line);
    return 0;
}

// ============================================================================
// Command wrappers (call Go functions)
// ============================================================================

static int cmd_dfs_find(int f, int n) { return go_dfs_find(f, n); }
static int cmd_dfs_grep(int f, int n) { return go_dfs_grep(f, n); }
static int cmd_dfs_count(int f, int n) { return go_dfs_count(f, n); }

// ============================================================================
// Extension lifecycle
// ============================================================================

static int dfs_init_c(uemacs_api *api) {
    g_api = api;

    // Initialize Go side
    dfs_init(api);

    // Register commands
    if (g_api->register_command) {
        g_api->register_command("dfs-find", cmd_dfs_find);
        g_api->register_command("dfs-grep", cmd_dfs_grep);
        g_api->register_command("dfs-count", cmd_dfs_count);
    }

    api_log_info("go_dfs: Concurrent DFS extension loaded (work-stealing traversal)");
    return 0;
}

static void dfs_cleanup_c(void) {
    if (g_api) {
        if (g_api->unregister_command) {
            g_api->unregister_command("dfs-find");
            g_api->unregister_command("dfs-grep");
            g_api->unregister_command("dfs-count");
        }
    }
}

// ============================================================================
// Extension entry point
// ============================================================================

static uemacs_extension ext = {
    .api_version = 3,
    .name = "go_dfs",
    .version = "1.0.0",
    .description = "Concurrent DFS file traversal (work-stealing)",
    .init = dfs_init_c,
    .cleanup = dfs_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
