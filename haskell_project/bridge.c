/*
 * bridge.c - C Bridge for Haskell Project Management Extension
 *
 * API Version: 3 (Event Bus)
 *
 * Provides C wrappers for Haskell FFI interop.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

static uemacs_api *g_api = NULL;

// API wrappers for Haskell
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

int bridge_find_file_line(const char *path, int line) {
    if (g_api && g_api->find_file_line) return g_api->find_file_line(path, line);
    return 0;
}

/* ============================================================================
 * Buffer Navigation (Enter to jump to file)
 * ============================================================================ */

// Event structure (must match uemacs_event_t)
typedef struct {
    const char *name;
    void *data;
    size_t data_size;
    bool consumed;
} uemacs_event_t;

// Check if we're in a project results buffer
static bool in_project_buffer(void) {
    if (!g_api || !g_api->current_buffer || !g_api->buffer_name) return false;

    void *bp = g_api->current_buffer();
    if (!bp) return false;

    const char *name = g_api->buffer_name(bp);
    if (!name) return false;

    return (strcmp(name, "*project-files*") == 0 ||
            strcmp(name, "*project-find*") == 0);
}

// Jump to file from current line
static bool do_project_goto(void) {
    if (!g_api || !g_api->get_current_line) return false;

    char *line = g_api->get_current_line();
    if (!line || line[0] == '\0') {
        if (g_api->message) g_api->message("No file on this line");
        if (line) g_api->free(line);
        return false;
    }

    // Trim trailing whitespace/newline
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

    if (len == 0) {
        if (g_api->message) g_api->message("Empty line");
        g_api->free(line);
        return false;
    }

    // The line IS the filepath (project-files outputs one file per line)
    if (g_api->find_file_line && g_api->find_file_line(line, 1)) {
        if (g_api->message) g_api->message("%s", line);
        g_api->free(line);
        return true;
    } else {
        if (g_api->message) g_api->message("Failed to open: %s", line);
        g_api->free(line);
        return false;
    }
}

// Key event handler - intercept Enter in project buffers
static bool project_key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    // Event data is the key code
    int key = (int)(intptr_t)event->data;

    // Only handle Enter (CR = 13, LF = 10)
    if (key != '\r' && key != '\n') return false;

    // Check if we're in a project buffer
    if (!in_project_buffer()) return false;

    // Handle the jump
    do_project_goto();
    return true;  // Event consumed
}

// Haskell command implementations
extern int hs_project_root(int f, int n);
extern int hs_project_files(int f, int n);
extern int hs_project_find(int f, int n);

// Haskell runtime init/shutdown
extern void hs_init(int *argc, char ***argv);
extern void hs_exit(void);

static int cmd_project_root(int f, int n) { return hs_project_root(f, n); }
static int cmd_project_files(int f, int n) { return hs_project_files(f, n); }
static int cmd_project_find(int f, int n) { return hs_project_find(f, n); }

static int project_init_c(uemacs_api *api) {
    g_api = api;

    // Initialize Haskell runtime
    static int argc = 1;
    static char *argv[] = { "project_haskell", NULL };
    static char **pargv = argv;
    hs_init(&argc, &pargv);

    if (g_api->register_command) {
        g_api->register_command("project-root", cmd_project_root);
        g_api->register_command("project-files", cmd_project_files);
        g_api->register_command("project-find", cmd_project_find);
    }

    // Register key handler for Enter in project buffers
    if (g_api->on) {
        g_api->on("input:key", (event_fn_t)project_key_handler, NULL, 0);
    }

    if (g_api->log_info) g_api->log_info("project_haskell: Loaded (v3.0, buffer navigation)");
    return 0;
}

static void project_cleanup_c(void) {
    if (g_api) {
        if (g_api->unregister_command) {
            g_api->unregister_command("project-root");
            g_api->unregister_command("project-files");
            g_api->unregister_command("project-find");
        }
        if (g_api->off) {
            g_api->off("input:key", (event_fn_t)project_key_handler);
        }
    }
    hs_exit();
}

static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "haskell_project",
    .version = "3.0.0",  /* v3: Buffer navigation (Enter to jump) */
    .description = "Project management with file navigation (Haskell)",
    .init = project_init_c,
    .cleanup = project_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
