/*
 * bridge.c - C Bridge for Fortran Git Extension
 *
 * API Version: 3 (Event Bus)
 *
 * Provides C wrappers for Fortran ISO_C_BINDING interop.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Command function type
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

// API wrappers for Fortran
void bridge_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

const char* bridge_buffer_filename(void *bp) {
    if (g_api && g_api->buffer_filename) return g_api->buffer_filename(bp);
    return NULL;
}

void* bridge_buffer_create(const char *name) {
    if (g_api && g_api->buffer_create) return g_api->buffer_create(name);
    return NULL;
}

int bridge_buffer_switch(void *bp) {
    if (g_api && g_api->buffer_switch) return g_api->buffer_switch(bp);
    return 0;
}

int bridge_buffer_clear(void *bp) {
    if (g_api && g_api->buffer_clear) return g_api->buffer_clear(bp);
    return 0;
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
    return 0;
}

void bridge_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
    else { *line = 1; *col = 0; }
}

// Fortran command implementations (linked from Fortran)
extern int fortran_git_status(int f, int n);
extern int fortran_git_diff(int f, int n);
extern int fortran_git_log(int f, int n);
extern int fortran_git_blame(int f, int n);
extern int fortran_git_add(int f, int n);

// C wrappers
static int cmd_git_status(int f, int n) { return fortran_git_status(f, n); }
static int cmd_git_diff(int f, int n) { return fortran_git_diff(f, n); }
static int cmd_git_log(int f, int n) { return fortran_git_log(f, n); }
static int cmd_git_blame(int f, int n) { return fortran_git_blame(f, n); }
static int cmd_git_add(int f, int n) { return fortran_git_add(f, n); }

// Init
static int git_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("fgit-status", cmd_git_status);
        g_api->register_command("fgit-diff", cmd_git_diff);
        g_api->register_command("fgit-log", cmd_git_log);
        g_api->register_command("fgit-blame", cmd_git_blame);
        g_api->register_command("fgit-add", cmd_git_add);
    }
    if (g_api->log_info) g_api->log_info("git_fortran: Loaded (fgit-* commands)");
    return 0;
}

// Cleanup
static void git_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("fgit-status");
        g_api->unregister_command("fgit-diff");
        g_api->unregister_command("fgit-log");
        g_api->unregister_command("fgit-blame");
        g_api->unregister_command("fgit-add");
    }
}

// Extension descriptor
static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "git_fortran",
    .version = "2.0.0",  /* v2: API v3 migration */
    .description = "Git integration (Fortran)",
    .init = git_init_c,
    .cleanup = git_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
