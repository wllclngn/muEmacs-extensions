/*
 * bridge.c - C/CGO Bridge for Go LSP Client Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * This bridge provides C wrappers for Go functions and uses
 * get_function() for ABI-stable API access.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <uep/extension_api.h>
#include "_cgo_export.h"

/* lsp_diag_entry_t is defined in _cgo_export.h from Go's CGO preamble */

/*
 * Minimal types needed for CGO interop.
 */
typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(void*, void*);

/*
 * Function pointer types for the API functions we use
 */
typedef void (*message_fn)(const char*, ...);
typedef void (*log_fn)(const char*, ...);
typedef void *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(void*);
typedef char *(*buffer_contents_fn)(void*, size_t*);
typedef void (*get_point_fn)(int*, int*);
typedef int (*find_file_line_fn)(const char*, int);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void (*free_fn)(void*);
typedef int (*syntax_add_token_fn)(uemacs_line_tokens_t*, int, int);
typedef void (*syntax_invalidate_buffer_fn)(struct buffer*);
typedef bool (*emit_fn)(const char*, void*);
typedef int (*on_fn)(const char*, event_fn_t, void*, int);
typedef int (*off_fn)(const char*, event_fn_t);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef int (*syntax_register_lexer_fn)(const char*, const char**, uemacs_syntax_lex_fn, void*);
typedef int (*syntax_unregister_lexer_fn)(const char*);

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
    find_file_line_fn find_file_line;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    free_fn free;
    syntax_add_token_fn syntax_add_token;
    syntax_invalidate_buffer_fn syntax_invalidate_buffer;
    emit_fn emit;
    on_fn on;
    off_fn off;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    syntax_register_lexer_fn syntax_register_lexer;
    syntax_unregister_lexer_fn syntax_unregister_lexer;
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

char* api_buffer_contents(void *bp, size_t *len) {
    if (api.buffer_contents) return api.buffer_contents(bp, len);
    return NULL;
}

void api_get_point(int *line, int *col) {
    if (api.get_point) api.get_point(line, col);
}

int api_find_file_line(const char *path, int line) {
    if (api.find_file_line) return api.find_file_line(path, line);
    return 0;
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

void api_free(void *ptr) {
    if (api.free) api.free(ptr);
}

int api_syntax_add_token(void *tokens, int end_col, int face) {
    if (api.syntax_add_token)
        return api.syntax_add_token((uemacs_line_tokens_t*)tokens, end_col, face);
    return -1;
}

void api_syntax_invalidate_buffer(void *bp) {
    if (api.syntax_invalidate_buffer)
        api.syntax_invalidate_buffer((struct buffer*)bp);
}

int api_emit(const char *event, void *data) {
    if (api.emit) return api.emit(event, data);
    return 0;
}

/* Emit diagnostics event - called from Go */
void api_emit_diagnostics(const char *uri, lsp_diag_entry_t *diags, int count) {
    if (!api.emit) return;

    struct {
        const char *uri;
        lsp_diag_entry_t *diags;
        int count;
    } event = {
        .uri = uri,
        .diags = diags,
        .count = count
    };
    api.emit("lsp:diagnostics", &event);
}

/* Lexer callback wrapper - calls Go function */
static uemacs_lexer_state_t lsp_lexer_callback(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    uemacs_lexer_state_t prev_state,
    uemacs_line_tokens_t *out
) {
    (void)lang;
    (void)prev_state;

    go_lsp_lex_line(NULL, buffer, line_num, (char*)line, len, out);

    uemacs_lexer_state_t result = {0, 0, 0, 0};
    return result;
}

/* ============================================================================
 * Command wrappers
 * ============================================================================ */

static int cmd_lsp_start(int f, int n) { return go_lsp_start(f, n); }
static int cmd_lsp_stop(int f, int n) { return go_lsp_stop(f, n); }
static int cmd_lsp_hover(int f, int n) { return go_lsp_hover(f, n); }
static int cmd_lsp_definition(int f, int n) { return go_lsp_definition(f, n); }
static int cmd_lsp_references(int f, int n) { return go_lsp_references(f, n); }
static int cmd_lsp_refresh_tokens(int f, int n) { return go_lsp_refresh_tokens(f, n); }
static int cmd_lsp_completion(int f, int n) { return go_lsp_completion(f, n); }
static int cmd_lsp_diagnostics(int f, int n) { return go_lsp_diagnostics(f, n); }
static int cmd_lsp_code_action(int f, int n) { return go_lsp_code_action(f, n); }
static int cmd_lsp_document_symbols(int f, int n) { return go_lsp_document_symbols(f, n); }
static int cmd_lsp_workspace_symbols(int f, int n) { return go_lsp_workspace_symbols(f, n); }
static int cmd_lsp_did_save(int f, int n) { return go_lsp_did_save(f, n); }
static int cmd_lsp_did_close(int f, int n) { return go_lsp_did_close(f, n); }

/* Event handlers for automatic didSave/didClose */
static bool on_buffer_saved(void *buffer, void *user_data) {
    (void)user_data;
    (void)buffer;
    go_lsp_did_save(0, 1);
    return true;
}

static bool on_buffer_closed(void *buffer, void *user_data) {
    (void)user_data;
    (void)buffer;
    go_lsp_did_close(0, 1);
    return true;
}

/* Language patterns for lexer registration */
static const char *py_patterns[] = {"*.py", NULL};
static const char *go_patterns[] = {"*.go", NULL};
static const char *rs_patterns[] = {"*.rs", NULL};
static const char *c_patterns[] = {"*.c", "*.h", "*.cpp", "*.hpp", NULL};
static const char *js_patterns[] = {"*.js", "*.ts", NULL};
static const char *zig_patterns[] = {"*.zig", NULL};

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

static int lsp_init_c(void *editor_api_raw) {
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    /*
     * Use get_function() for ABI stability.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "go_lsp: Requires μEmacs with get_function() support\n");
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
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.free = (free_fn)LOOKUP(free);
    api.syntax_add_token = (syntax_add_token_fn)LOOKUP(syntax_add_token);
    api.syntax_invalidate_buffer = (syntax_invalidate_buffer_fn)LOOKUP(syntax_invalidate_buffer);
    api.emit = (emit_fn)LOOKUP(emit);
    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.syntax_register_lexer = (syntax_register_lexer_fn)LOOKUP(syntax_register_lexer);
    api.syntax_unregister_lexer = (syntax_unregister_lexer_fn)LOOKUP(syntax_unregister_lexer);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.register_command || !api.log_info) {
        fprintf(stderr, "go_lsp: Missing critical API functions\n");
        return -1;
    }

    /* Register commands */
    api.register_command("lsp-start", cmd_lsp_start);
    api.register_command("lsp-stop", cmd_lsp_stop);
    api.register_command("lsp-hover", cmd_lsp_hover);
    api.register_command("lsp-definition", cmd_lsp_definition);
    api.register_command("lsp-references", cmd_lsp_references);
    api.register_command("lsp-refresh-tokens", cmd_lsp_refresh_tokens);
    api.register_command("lsp-completion", cmd_lsp_completion);
    api.register_command("lsp-diagnostics", cmd_lsp_diagnostics);
    api.register_command("lsp-code-action", cmd_lsp_code_action);
    api.register_command("lsp-document-symbols", cmd_lsp_document_symbols);
    api.register_command("lsp-workspace-symbols", cmd_lsp_workspace_symbols);

    /* Register as lexer for supported languages */
    if (api.syntax_register_lexer) {
        api.syntax_register_lexer("lsp-python", py_patterns, lsp_lexer_callback, NULL);
        api.syntax_register_lexer("lsp-go", go_patterns, lsp_lexer_callback, NULL);
        api.syntax_register_lexer("lsp-rust", rs_patterns, lsp_lexer_callback, NULL);
        api.syntax_register_lexer("lsp-c", c_patterns, lsp_lexer_callback, NULL);
        api.syntax_register_lexer("lsp-js", js_patterns, lsp_lexer_callback, NULL);
        api.syntax_register_lexer("lsp-zig", zig_patterns, lsp_lexer_callback, NULL);
    }

    /* Register event handlers for automatic didSave/didClose */
    if (api.on) {
        api.on("buffer:saved", on_buffer_saved, NULL, 0);
        api.on("buffer:closed", on_buffer_closed, NULL, 0);
    }

    api.log_info("lsp_client: Go extension loaded (v5.0, ABI-stable)");
    return 0;
}

static void lsp_cleanup_c(void) {
    /* Unregister commands */
    if (api.unregister_command) {
        api.unregister_command("lsp-start");
        api.unregister_command("lsp-stop");
        api.unregister_command("lsp-hover");
        api.unregister_command("lsp-definition");
        api.unregister_command("lsp-references");
        api.unregister_command("lsp-refresh-tokens");
        api.unregister_command("lsp-completion");
        api.unregister_command("lsp-diagnostics");
        api.unregister_command("lsp-code-action");
        api.unregister_command("lsp-document-symbols");
        api.unregister_command("lsp-workspace-symbols");
    }

    /* Unregister lexers */
    if (api.syntax_unregister_lexer) {
        api.syntax_unregister_lexer("lsp-python");
        api.syntax_unregister_lexer("lsp-go");
        api.syntax_unregister_lexer("lsp-rust");
        api.syntax_unregister_lexer("lsp-c");
        api.syntax_unregister_lexer("lsp-js");
        api.syntax_unregister_lexer("lsp-zig");
    }

    /* Unregister event handlers */
    if (api.off) {
        api.off("buffer:saved", on_buffer_saved);
        api.off("buffer:closed", on_buffer_closed);
    }
}

static uemacs_extension ext = {
    .api_version = 4,
    .name = "go_lsp",
    .version = "5.0.0",
    .description = "LSP client with semantic tokens (Go, ABI-stable)",
    .init = lsp_init_c,
    .cleanup = lsp_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
