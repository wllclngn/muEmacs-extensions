/*
 * bridge.c - C/CGO Bridge for Go Chess Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides work-stealing parallel chess engine for μEmacs.
 * Human vs AI with configurable search depth.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <uep/extension_api.h>
#include "_cgo_export.h"

typedef int (*cmd_fn_t)(int, int);

/*
 * Function pointer types for the API functions we use
 */
typedef void (*message_fn)(const char*, ...);
typedef void (*log_fn)(const char*, ...);
typedef void *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(void*);
typedef char *(*buffer_contents_fn)(void*, size_t*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef void (*buffer_set_unmodified_fn)(void*);
typedef void (*buffer_set_scratch_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef void (*free_fn)(void*);
typedef void (*update_display_fn)(void);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef int (*config_int_fn)(const char*, const char*, int);
typedef bool (*config_bool_fn)(const char*, const char*, bool);

/*
 * Local API struct - only the functions we actually use
 */
static struct {
    message_fn message;
    log_fn log_info;
    log_fn log_error;
    current_buffer_fn current_buffer;
    buffer_filename_fn buffer_filename;
    buffer_contents_fn buffer_contents;
    get_point_fn get_point;
    set_point_fn set_point;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_set_unmodified_fn buffer_set_unmodified;
    buffer_set_scratch_fn buffer_set_scratch;
    buffer_insert_fn buffer_insert;
    prompt_fn prompt;
    free_fn free;
    update_display_fn update_display;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    config_int_fn config_int;
    config_bool_fn config_bool;
} api;

/* Extension name for config lookups */
static const char *EXT_NAME = "go_chess";

/* ============================================================================
 * API wrappers for Go (these are called from Go via CGO)
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

void api_buffer_set_unmodified(void *bp) {
    if (api.buffer_set_unmodified) api.buffer_set_unmodified(bp);
}

void api_buffer_set_scratch(void *bp) {
    if (api.buffer_set_scratch) api.buffer_set_scratch(bp);
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

void api_update_display(void) {
    if (api.update_display) api.update_display();
}

int api_config_int(const char *key, int default_val) {
    if (api.config_int) return api.config_int(EXT_NAME, key, default_val);
    return default_val;
}

bool api_config_bool(const char *key, bool default_val) {
    if (api.config_bool) return api.config_bool(EXT_NAME, key, default_val);
    return default_val;
}

/* ============================================================================
 * Command wrappers (call Go functions)
 * ============================================================================ */

static int cmd_chess(int f, int n) { return go_chess_new(f, n); }
static int cmd_chess_move(int f, int n) { return go_chess_move(f, n); }
static int cmd_chess_undo(int f, int n) { return go_chess_undo(f, n); }
static int cmd_chess_depth(int f, int n) { return go_chess_depth(f, n); }
static int cmd_chess_eval(int f, int n) { return go_chess_eval(f, n); }
static int cmd_chess_hint(int f, int n) { return go_chess_hint(f, n); }
static int cmd_chess_flip(int f, int n) { return go_chess_flip(f, n); }
static int cmd_chess_fen(int f, int n) { return go_chess_fen(f, n); }
static int cmd_chess_auto(int f, int n) { return go_chess_auto(f, n); }
static int cmd_chess_stop(int f, int n) { return go_chess_stop(f, n); }

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

static int chess_init_c(void *editor_api_raw) {
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "go_chess: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.message = (message_fn)LOOKUP(message);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_contents = (buffer_contents_fn)LOOKUP(buffer_contents);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_set_unmodified = (buffer_set_unmodified_fn)LOOKUP(buffer_set_unmodified);
    api.buffer_set_scratch = (buffer_set_scratch_fn)LOOKUP(buffer_set_scratch);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.free = (free_fn)LOOKUP(free);
    api.update_display = (update_display_fn)LOOKUP(update_display);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.config_int = (config_int_fn)LOOKUP(config_int);
    api.config_bool = (config_bool_fn)LOOKUP(config_bool);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.register_command) {
        fprintf(stderr, "go_chess: register_command not found!\n");
        return -1;
    }
    if (!api.log_info) {
        fprintf(stderr, "go_chess: log_info not found!\n");
        return -1;
    }

    /* Initialize Go side */
    chess_init(editor_api_raw);

    /* Register commands */
    api.register_command("chess", cmd_chess);
    api.register_command("chess-move", cmd_chess_move);
    api.register_command("chess-undo", cmd_chess_undo);
    api.register_command("chess-depth", cmd_chess_depth);
    api.register_command("chess-eval", cmd_chess_eval);
    api.register_command("chess-hint", cmd_chess_hint);
    api.register_command("chess-flip", cmd_chess_flip);
    api.register_command("chess-fen", cmd_chess_fen);
    api.register_command("chess-auto", cmd_chess_auto);
    api.register_command("chess-stop", cmd_chess_stop);

    api.log_info("go_chess: Work-stealing chess engine loaded (parallel alpha-beta)");
    return 0;
}

static void chess_cleanup_c(void) {
    /* Stop any running goroutines FIRST - critical for clean shutdown */
    go_chess_cleanup();

    if (api.unregister_command) {
        api.unregister_command("chess");
        api.unregister_command("chess-move");
        api.unregister_command("chess-undo");
        api.unregister_command("chess-depth");
        api.unregister_command("chess-eval");
        api.unregister_command("chess-hint");
        api.unregister_command("chess-flip");
        api.unregister_command("chess-fen");
        api.unregister_command("chess-auto");
        api.unregister_command("chess-stop");
    }
}

/* ============================================================================
 * Extension entry point
 * ============================================================================ */

static uemacs_extension ext = {
    .api_version = 4,
    .name = "go_chess",
    .version = "1.0.0",
    .description = "Work-stealing chess engine (parallel alpha-beta)",
    .init = chess_init_c,
    .cleanup = chess_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
