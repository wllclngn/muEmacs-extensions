/*
 * bridge.c - C/CGO Bridge for Go Sudoku Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides a Sudoku game with constraint-propagation solver.
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
typedef void *(*find_buffer_fn)(const char*);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef void (*update_display_fn)(void);
typedef int (*prompt_yn_fn)(const char*);
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
    find_buffer_fn find_buffer;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    get_point_fn get_point;
    set_point_fn set_point;
    update_display_fn update_display;
    prompt_yn_fn prompt_yn;
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

void *api_current_buffer(void) {
    if (api.current_buffer) return api.current_buffer();
    return NULL;
}

void *api_find_buffer(const char *name) {
    if (api.find_buffer) return api.find_buffer(name);
    return NULL;
}

void *api_buffer_create(const char *name) {
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

void api_get_point(int *line, int *col) {
    if (api.get_point) api.get_point(line, col);
}

void api_set_point(int line, int col) {
    if (api.set_point) api.set_point(line, col);
}

void api_update_display(void) {
    if (api.update_display) api.update_display();
}

int api_prompt_yn(const char *prompt) {
    if (api.prompt_yn) return api.prompt_yn(prompt);
    return 0;
}

/* ============================================================================
 * Command wrappers (call into Go)
 * ============================================================================ */

static int cmd_sudoku_new(int f, int n) {
    return GoSudokuNew(f, n);
}

static int cmd_sudoku_check(int f, int n) {
    return GoSudokuCheck(f, n);
}

static int cmd_sudoku_hint(int f, int n) {
    return GoSudokuHint(f, n);
}

static int cmd_sudoku_solve(int f, int n) {
    return GoSudokuSolve(f, n);
}

static int cmd_sudoku_reset(int f, int n) {
    return GoSudokuReset(f, n);
}

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

static int sudoku_init(void *editor_api_raw) {
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    /*
     * Use get_function() for ABI stability.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "go_sudoku: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.message = (message_fn)LOOKUP(message);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.find_buffer = (find_buffer_fn)LOOKUP(find_buffer);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.update_display = (update_display_fn)LOOKUP(update_display);
    api.prompt_yn = (prompt_yn_fn)LOOKUP(prompt_yn);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.register_command || !api.log_info) {
        fprintf(stderr, "go_sudoku: Missing critical API functions\n");
        return -1;
    }

    /* Register commands */
    api.register_command("sudoku-new", cmd_sudoku_new);
    api.register_command("sudoku-check", cmd_sudoku_check);
    api.register_command("sudoku-hint", cmd_sudoku_hint);
    api.register_command("sudoku-solve", cmd_sudoku_solve);
    api.register_command("sudoku-reset", cmd_sudoku_reset);

    /* Initialize Go side */
    GoSudokuInit();

    api.log_info("go_sudoku: Extension v2.0.0 loaded");
    api.log_info("  Commands: sudoku-new, sudoku-check, sudoku-hint");
    api.log_info("            sudoku-solve, sudoku-reset");

    return 0;
}

static void sudoku_cleanup(void) {
    /* Unregister commands */
    if (api.unregister_command) {
        api.unregister_command("sudoku-new");
        api.unregister_command("sudoku-check");
        api.unregister_command("sudoku-hint");
        api.unregister_command("sudoku-solve");
        api.unregister_command("sudoku-reset");
    }

    if (api.log_info) {
        api.log_info("go_sudoku: Extension unloaded");
    }

    /* Cleanup Go side */
    GoSudokuCleanup();
}

/* Extension descriptor */
static uemacs_extension sudoku_ext = {
    .api_version = 4,
    .name = "go_sudoku",
    .version = "2.0.0",
    .description = "Sudoku game with constraint-propagation solver",
    .init = sudoku_init,
    .cleanup = sudoku_cleanup,
};

/* Entry point */
uemacs_extension *uemacs_extension_entry(void) {
    return &sudoku_ext;
}
