/*
 * bridge.c - C/CGO Bridge for Go sam Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * This bridge provides C wrappers for Go functions and uses
 * get_function() for ABI-stable API access.
 *
 * Implements Rob Pike's structural regular expressions from sam.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <uep/extension_api.h>
#include "_cgo_export.h"

/*
 * Minimal types needed for CGO interop.
 */
typedef int (*cmd_fn_t)(int, int);

/*
 * Function pointer types for the API functions we use
 */
typedef void (*message_fn)(const char*, ...);
typedef void (*log_fn)(const char*, ...);
typedef void *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(void*);
typedef const char *(*buffer_name_fn)(void*);
typedef char *(*buffer_contents_fn)(void*, size_t*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef void (*free_fn)(void*);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);

/*
 * Local API struct - only the functions we actually use
 */
static struct {
    message_fn message;
    log_fn log_info;
    log_fn log_error;
    current_buffer_fn current_buffer;
    buffer_filename_fn buffer_filename;
    buffer_name_fn buffer_name;
    buffer_contents_fn buffer_contents;
    get_point_fn get_point;
    set_point_fn set_point;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    prompt_fn prompt;
    free_fn free;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
} api;

/* ============================================================================
 * API wrappers for Go
 * ============================================================================ */

void api_message(const char *msg) {
    if (api.message) api.message("%s", msg);
}

void api_log_info(const char *msg) {
    if (api.log_info) api.log_info("%s", msg);
}

void api_log_error(const char *msg) {
    if (api.log_error) api.log_error("%s", msg);
}

void* api_current_buffer(void) {
    if (api.current_buffer) return api.current_buffer();
    return NULL;
}

const char* api_buffer_filename(void *bp) {
    if (api.buffer_filename) return api.buffer_filename(bp);
    return NULL;
}

const char* api_buffer_name(void *bp) {
    if (api.buffer_name) return api.buffer_name(bp);
    return NULL;
}

char* api_buffer_contents(void *bp, size_t *len) {
    if (api.buffer_contents) return api.buffer_contents(bp, len);
    return NULL;
}

void api_get_point(int *line, int *col) {
    if (api.get_point) api.get_point(line, col);
}

void api_set_point(int line, int col) {
    if (api.set_point) api.set_point(line, col);
}

void* api_buffer_create(const char *name) {
    if (api.buffer_create) return api.buffer_create(name);
    return NULL;
}

int api_buffer_switch(void *bp) {
    if (api.buffer_switch) return api.buffer_switch(bp);
    return 0;
}

int api_buffer_clear(void *bp) {
    if (api.buffer_clear) return api.buffer_clear(bp);
    return 0;
}

int api_buffer_insert(const char *text, size_t len) {
    if (api.buffer_insert) return api.buffer_insert(text, len);
    return 0;
}

int api_prompt(const char *prompt, char *buf, size_t buflen) {
    if (api.prompt) return api.prompt(prompt, buf, buflen);
    return -1;
}

void api_free(void *ptr) {
    if (api.free) api.free(ptr);
}

int api_delete_chars(int n) {
    /* Delete n characters at current position - handled via buffer_clear + buffer_insert */
    (void)n;
    return 0;
}

/* ============================================================================
 * Command wrappers (call Go functions)
 * ============================================================================ */

static int cmd_sam_x(int f, int n) { return go_sam_x(f, n); }
static int cmd_sam_y(int f, int n) { return go_sam_y(f, n); }
static int cmd_sam_g(int f, int n) { return go_sam_g(f, n); }
static int cmd_sam_v(int f, int n) { return go_sam_v(f, n); }
static int cmd_sam_edit(int f, int n) { return go_sam_edit(f, n); }
static int cmd_sam_pipe(int f, int n) { return go_sam_pipe(f, n); }
static int cmd_sam_help(int f, int n) { return go_sam_help(f, n); }

/* ============================================================================
 * Extension lifecycle
 * ============================================================================ */

typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(void*);
    void (*cleanup)(void);
} uemacs_extension;

static int sam_init_c(void *editor_api_raw) {
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    /*
     * Use get_function() for ABI stability.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "go_sam: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.message = (message_fn)LOOKUP(message);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_contents = (buffer_contents_fn)LOOKUP(buffer_contents);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.free = (free_fn)LOOKUP(free);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.register_command || !api.log_info) {
        fprintf(stderr, "go_sam: Missing critical API functions\n");
        return -1;
    }

    /* Initialize Go side */
    sam_init(editor_api_raw);

    /* Register commands */
    api.register_command("sam-x", cmd_sam_x);
    api.register_command("sam-y", cmd_sam_y);
    api.register_command("sam-g", cmd_sam_g);
    api.register_command("sam-v", cmd_sam_v);
    api.register_command("sam-edit", cmd_sam_edit);
    api.register_command("sam-pipe", cmd_sam_pipe);
    api.register_command("sam-help", cmd_sam_help);

    api.log_info("go_sam: Structural regex extension loaded (Pike's sam commands)");
    return 0;
}

static void sam_cleanup_c(void) {
    /* Unregister commands */
    if (api.unregister_command) {
        api.unregister_command("sam-x");
        api.unregister_command("sam-y");
        api.unregister_command("sam-g");
        api.unregister_command("sam-v");
        api.unregister_command("sam-edit");
        api.unregister_command("sam-pipe");
        api.unregister_command("sam-help");
    }
}

/* ============================================================================
 * Extension entry point
 * ============================================================================ */

static uemacs_extension ext = {
    .api_version = 4,
    .name = "go_sam",
    .version = "2.0.0",
    .description = "Rob Pike's structural regular expressions (sam)",
    .init = sam_init_c,
    .cleanup = sam_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
