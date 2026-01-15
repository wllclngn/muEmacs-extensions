/*
 * bridge.c - C Bridge for Pascal Text Utilities Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides C wrappers for Pascal FFI interop.
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
typedef char *(*buffer_contents_fn)(void *, size_t *);
typedef char *(*get_current_line_fn)(void);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void (*message_fn)(const char*, ...);
typedef void *(*alloc_fn)(size_t);
typedef void (*log_fn)(const char*, ...);
typedef int (*kill_line_fn)(int, int);

/* Our local API - only the functions we actually use */
static struct {
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_contents_fn buffer_contents;
    get_current_line_fn get_current_line;
    get_point_fn get_point;
    set_point_fn set_point;
    buffer_insert_fn buffer_insert;
    message_fn message;
    alloc_fn alloc;
    log_fn log_info;
    kill_line_fn kill_line;
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

char* bridge_buffer_contents(void *bp, size_t *len) {
    if (api.buffer_contents) return api.buffer_contents(bp, len);
    return NULL;
}

char* bridge_get_current_line(void) {
    if (api.get_current_line) return api.get_current_line();
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

void bridge_delete_line(void) {
    /* Kill from beginning of line to end (including newline) */
    if (api.kill_line) {
        api.kill_line(0, 1);  /* Kill one line */
    }
}

// Pascal command implementations
extern int pascal_txt_stats(int f, int n);
extern int pascal_txt_base64_enc(int f, int n);
extern int pascal_txt_base64_dec(int f, int n);
extern int pascal_txt_rot13(int f, int n);
extern int pascal_txt_upper(int f, int n);
extern int pascal_txt_lower(int f, int n);
extern int pascal_txt_reverse(int f, int n);

static int cmd_txt_stats(int f, int n) { return pascal_txt_stats(f, n); }
static int cmd_txt_base64_enc(int f, int n) { return pascal_txt_base64_enc(f, n); }
static int cmd_txt_base64_dec(int f, int n) { return pascal_txt_base64_dec(f, n); }
static int cmd_txt_rot13(int f, int n) { return pascal_txt_rot13(f, n); }
static int cmd_txt_upper(int f, int n) { return pascal_txt_upper(f, n); }
static int cmd_txt_lower(int f, int n) { return pascal_txt_lower(f, n); }
static int cmd_txt_reverse(int f, int n) { return pascal_txt_reverse(f, n); }

static int textutils_init_c(void *editor_api_raw) {
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    if (!editor_api->get_function) {
        fprintf(stderr, "pascal_textutils: Requires get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_contents = (buffer_contents_fn)LOOKUP(buffer_contents);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.message = (message_fn)LOOKUP(message);
    api.alloc = (alloc_fn)LOOKUP(alloc);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.kill_line = (kill_line_fn)LOOKUP(kill_line);

    #undef LOOKUP

    if (!api.register_command) {
        fprintf(stderr, "pascal_textutils: Failed to look up register_command\n");
        return -1;
    }

    api.register_command("txt-stats", cmd_txt_stats);
    api.register_command("txt-base64-enc", cmd_txt_base64_enc);
    api.register_command("txt-base64-dec", cmd_txt_base64_dec);
    api.register_command("txt-rot13", cmd_txt_rot13);
    api.register_command("txt-upper", cmd_txt_upper);
    api.register_command("txt-lower", cmd_txt_lower);
    api.register_command("txt-reverse", cmd_txt_reverse);

    if (api.log_info) api.log_info("pascal_textutils: Loaded (7 commands)");
    return 0;
}

static void textutils_cleanup_c(void) {
    if (api.unregister_command) {
        api.unregister_command("txt-stats");
        api.unregister_command("txt-base64-enc");
        api.unregister_command("txt-base64-dec");
        api.unregister_command("txt-rot13");
        api.unregister_command("txt-upper");
        api.unregister_command("txt-lower");
        api.unregister_command("txt-reverse");
    }
}

static uemacs_extension ext = {
    .api_version = UEMACS_API_VERSION_BUILD,
    .name = "pascal_textutils",
    .version = "1.0.0",
    .description = "Text utilities: stats, encoding, transformations (Pascal)",
    .init = textutils_init_c,
    .cleanup = textutils_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
