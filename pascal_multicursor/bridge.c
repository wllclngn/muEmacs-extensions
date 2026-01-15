/*
 * bridge.c - C Bridge for Pascal Multiple Cursors Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides C wrappers for Pascal FFI interop.
 * Uses get_function() for ABI stability - immune to API struct layout changes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <uep/extension_api.h>

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*current_buffer_fn)(void);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void (*message_fn)(const char*, ...);
typedef void *(*alloc_fn)(size_t);
typedef void (*log_fn)(const char*, ...);
typedef int (*modeline_register_fn)(const char*, char* (*)(void*), void*, int);
typedef int (*modeline_unregister_fn)(const char*);

/* Our local API - only the functions we actually use */
static struct {
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    get_point_fn get_point;
    set_point_fn set_point;
    buffer_insert_fn buffer_insert;
    message_fn message;
    alloc_fn alloc;
    log_fn log_info;
    modeline_register_fn modeline_register;
    modeline_unregister_fn modeline_unregister;
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

// API wrappers for Pascal
void bridge_message(const char *msg) {
    if (api.message) api.message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (api.current_buffer) return api.current_buffer();
    return NULL;
}

void bridge_get_point(int *line, int *col) {
    if (api.get_point) api.get_point(line, col);
    else { *line = 1; *col = 0; }
}

void bridge_set_point(int line, int col) {
    if (api.set_point) api.set_point(line, col);
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (api.buffer_insert) return api.buffer_insert(text, len);
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
    if (!api.alloc) return NULL;
    char *buf = api.alloc(16);
    if (!buf) return NULL;
    snprintf(buf, 16, "MC:%d", count);
    return buf;
}

static int mc_init_c(void *editor_api_raw) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    if (!editor_api->get_function) {
        fprintf(stderr, "pascal_multicursor: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.message = (message_fn)LOOKUP(message);
    api.alloc = (alloc_fn)LOOKUP(alloc);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.modeline_register = (modeline_register_fn)LOOKUP(modeline_register);
    api.modeline_unregister = (modeline_unregister_fn)LOOKUP(modeline_unregister);

    #undef LOOKUP

    /* Verify critical functions */
    if (!api.register_command) {
        fprintf(stderr, "pascal_multicursor: Failed to look up register_command\n");
        return -1;
    }

    api.register_command("mc-add", cmd_mc_add);
    api.register_command("mc-clear", cmd_mc_clear);
    api.register_command("mc-next", cmd_mc_next);
    api.register_command("mc-insert", cmd_mc_insert);

    // Register modeline segment (high urgency - it's a mode indicator)
    if (api.modeline_register) {
        api.modeline_register("multicursor", mc_modeline_format, NULL, 1);
    }

    if (api.log_info) api.log_info("multicursor_pascal: Loaded (v4.0, ABI-stable)");
    return 0;
}

static void mc_cleanup_c(void) {
    if (api.unregister_command) {
        api.unregister_command("mc-add");
        api.unregister_command("mc-clear");
        api.unregister_command("mc-next");
        api.unregister_command("mc-insert");
    }
    if (api.modeline_unregister) {
        api.modeline_unregister("multicursor");
    }
}

static uemacs_extension ext = {
    .api_version = 4,  /* ABI-stable API */
    .name = "pascal_multicursor",
    .version = "4.0.0",  /* v4: ABI-stable migration */
    .description = "Multiple cursors with modeline indicator (Pascal)",
    .init = mc_init_c,
    .cleanup = mc_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
