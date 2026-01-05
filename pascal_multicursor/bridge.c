/*
 * bridge.c - C Bridge for Pascal Multiple Cursors Extension
 *
 * API Version: 3 (Event Bus)
 *
 * Provides C wrappers for Pascal FFI interop.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(void*, void*);

// API struct (must match extension_api.h v3)
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
    void *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(void*, int);
    void *(*window_at_row)(int);
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

    // Syntax Highlighting (not used by multicursor)
    int (*syntax_register_lexer)(const char*, const char**, void*, void*);
    int (*syntax_unregister_lexer)(const char*);
    int (*syntax_add_token)(void*, int, int);
    void (*syntax_invalidate_buffer)(void*);

    // Modeline Extension Segments
    int (*modeline_register)(const char*, char* (*)(void*), void*, int);
    int (*modeline_unregister)(const char*);
    void (*modeline_refresh)(void);
} uemacs_api;

typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(uemacs_api*);
    void (*cleanup)(void);
} uemacs_extension;

static uemacs_api *g_api = NULL;

// API wrappers for Pascal
void bridge_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

void bridge_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
    else { *line = 1; *col = 0; }
}

void bridge_set_point(int line, int col) {
    if (g_api && g_api->set_point) g_api->set_point(line, col);
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
    return 0;
}

// Pascal command implementations
extern int pascal_mc_add(int f, int n);
extern int pascal_mc_clear(int f, int n);
extern int pascal_mc_next(int f, int n);
extern int pascal_mc_insert(int f, int n);
extern int pascal_mc_get_count(void);  // Query cursor count for modeline

static int cmd_mc_add(int f, int n) { return pascal_mc_add(f, n); }
static int cmd_mc_clear(int f, int n) { return pascal_mc_clear(f, n); }
static int cmd_mc_next(int f, int n) { return pascal_mc_next(f, n); }
static int cmd_mc_insert(int f, int n) { return pascal_mc_insert(f, n); }

// Modeline callback - returns "MC:N" when N > 1, NULL otherwise
static char* mc_modeline_format(void *user_data) {
    (void)user_data;
    int count = pascal_mc_get_count();
    if (count <= 1) return NULL;  // Hide when single cursor
    char *buf = g_api->alloc(16);
    if (!buf) return NULL;
    snprintf(buf, 16, "MC:%d", count);
    return buf;
}

static int mc_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("mc-add", cmd_mc_add);
        g_api->register_command("mc-clear", cmd_mc_clear);
        g_api->register_command("mc-next", cmd_mc_next);
        g_api->register_command("mc-insert", cmd_mc_insert);
    }
    // Register modeline segment (high urgency - it's a mode indicator)
    if (g_api->modeline_register) {
        g_api->modeline_register("multicursor", mc_modeline_format, NULL, 1);
    }
    if (g_api->log_info) g_api->log_info("multicursor_pascal: Loaded (v3.0, modeline)");
    return 0;
}

static void mc_cleanup_c(void) {
    if (g_api) {
        if (g_api->unregister_command) {
            g_api->unregister_command("mc-add");
            g_api->unregister_command("mc-clear");
            g_api->unregister_command("mc-next");
            g_api->unregister_command("mc-insert");
        }
        if (g_api->modeline_unregister) {
            g_api->modeline_unregister("multicursor");
        }
    }
}

static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "pascal_multicursor",
    .version = "3.0.0",  /* v3: Modeline indicator */
    .description = "Multiple cursors with modeline indicator (Pascal)",
    .init = mc_init_c,
    .cleanup = mc_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
