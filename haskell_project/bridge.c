/*
 * bridge.c - C Bridge for Haskell Project Management Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides C wrappers for Haskell FFI interop.
 * Uses get_function() for ABI stability - immune to API struct layout changes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <uep/extension_api.h>

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(void*, void*);
typedef int (*on_fn)(const char*, event_fn_t, void*, int);
typedef int (*off_fn)(const char*, event_fn_t);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*current_buffer_fn)(void);
typedef const char *(*buffer_name_fn)(void*);
typedef const char *(*buffer_filename_fn)(void*);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef int (*find_file_line_fn)(const char*, int);
typedef char *(*get_current_line_fn)(void);
typedef void (*message_fn)(const char*, ...);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);

/* Our local API - only the functions we actually use */
static struct {
    on_fn on;
    off_fn off;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_name_fn buffer_name;
    buffer_filename_fn buffer_filename;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    find_file_line_fn find_file_line;
    get_current_line_fn get_current_line;
    message_fn message;
    free_fn free;
    log_fn log_info;
} api;

/* Extension descriptor */
typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(void*);
    void (*cleanup)(void);
} uemacs_extension;

// API wrappers for Haskell
void bridge_message(const char *msg) {
    if (api.message) api.message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (api.current_buffer) return api.current_buffer();
    return NULL;
}

const char* bridge_buffer_filename(void *bp) {
    if (api.buffer_filename) return api.buffer_filename(bp);
    return NULL;
}

void* bridge_buffer_create(const char *name) {
    if (api.buffer_create) return api.buffer_create(name);
    return NULL;
}

int bridge_buffer_switch(void *bp) {
    if (api.buffer_switch) return api.buffer_switch(bp);
    return 0;
}

int bridge_buffer_clear(void *bp) {
    if (api.buffer_clear) return api.buffer_clear(bp);
    return 0;
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (api.buffer_insert) return api.buffer_insert(text, len);
    return 0;
}

int bridge_find_file_line(const char *path, int line) {
    if (api.find_file_line) return api.find_file_line(path, line);
    return 0;
}

/* ============================================================================
 * Buffer Navigation (Enter to jump to file)
 * ============================================================================ */

// uemacs_event_t is defined in <uep/extension_api.h>

// Check if we're in a project results buffer
static bool in_project_buffer(void) {
    if (!api.current_buffer || !api.buffer_name) return false;

    void *bp = api.current_buffer();
    if (!bp) return false;

    const char *name = api.buffer_name(bp);
    if (!name) return false;

    return (strcmp(name, "*project-files*") == 0 ||
            strcmp(name, "*project-find*") == 0);
}

// Jump to file from current line
static bool do_project_goto(void) {
    if (!api.get_current_line) return false;

    char *line = api.get_current_line();
    if (!line || line[0] == '\0') {
        if (api.message) api.message("No file on this line");
        if (line && api.free) api.free(line);
        return false;
    }

    // Trim trailing whitespace/newline
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

    if (len == 0) {
        if (api.message) api.message("Empty line");
        if (api.free) api.free(line);
        return false;
    }

    // The line IS the filepath (project-files outputs one file per line)
    if (api.find_file_line && api.find_file_line(line, 1)) {
        if (api.message) api.message("%s", line);
        if (api.free) api.free(line);
        return true;
    } else {
        if (api.message) api.message("Failed to open: %s", line);
        if (api.free) api.free(line);
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

static int project_init_c(void *editor_api_raw) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    if (!editor_api->get_function) {
        fprintf(stderr, "haskell_project: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.message = (message_fn)LOOKUP(message);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);

    #undef LOOKUP

    /* Verify critical functions */
    if (!api.register_command) {
        fprintf(stderr, "haskell_project: Failed to look up register_command\n");
        return -1;
    }

    // Initialize Haskell runtime
    static int argc = 1;
    static char *argv[] = { "project_haskell", NULL };
    static char **pargv = argv;
    hs_init(&argc, &pargv);

    api.register_command("project-root", cmd_project_root);
    api.register_command("project-files", cmd_project_files);
    api.register_command("project-find", cmd_project_find);

    // Register key handler for Enter in project buffers
    if (api.on) {
        api.on("input:key", (event_fn_t)project_key_handler, NULL, 0);
    }

    if (api.log_info) api.log_info("project_haskell: Loaded (v4.0, ABI-stable)");
    return 0;
}

static void project_cleanup_c(void) {
    if (api.unregister_command) {
        api.unregister_command("project-root");
        api.unregister_command("project-files");
        api.unregister_command("project-find");
    }
    if (api.off) {
        api.off("input:key", (event_fn_t)project_key_handler);
    }
    hs_exit();
}

static uemacs_extension ext = {
    .api_version = 4,  /* ABI-stable API */
    .name = "haskell_project",
    .version = "4.0.0",  /* v4: ABI-stable migration */
    .description = "Project management with file navigation (Haskell)",
    .init = project_init_c,
    .cleanup = project_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
