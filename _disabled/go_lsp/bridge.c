/*
 * bridge.c - C/CGO Bridge for Go LSP Client Extension
 *
 * API Version: 3 (Event Bus)
 *
 * This bridge provides C wrappers for Go functions and mirrors the
 * μEmacs extension API struct for CGO interop.
 *
 * The API struct must exactly match include/uep/extension_api.h
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "_cgo_export.h"

// Command function type
typedef int (*cmd_fn_t)(int, int);

// Event handler type (API v3)
typedef bool (*event_fn_t)(void*, void*);

// Lexer state (must match extension_api.h)
typedef struct {
    int mode;
    int nest_depth;
    char string_delim;
    uint32_t state_hash;
} lexer_state_t;

// Line tokens opaque type
typedef struct line_tokens line_tokens_t;

// Opaque types from μEmacs
struct buffer;
struct window;
struct syntax_language;

// Lexer callback signature (must match internal syntax.h API v3)
typedef lexer_state_t (*syntax_lex_fn)(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
);

// Full API struct (must match extension_api.h v3 exactly)
typedef struct {
    int api_version;

    // ─── Event Bus (API v3) ───────────────────────────────────────────
    int (*on)(const char*, event_fn_t, void*, int);
    int (*off)(const char*, event_fn_t);
    bool (*emit)(const char*, void*);

    // ─── Configuration Access ─────────────────────────────────────────
    int (*config_int)(const char*, const char*, int);
    bool (*config_bool)(const char*, const char*, bool);
    const char *(*config_string)(const char*, const char*, const char*);

    // ─── Command Registration ─────────────────────────────────────────
    int (*register_command)(const char*, cmd_fn_t);
    int (*unregister_command)(const char*);

    // ─── Buffer Operations ────────────────────────────────────────────
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

    // ─── Cursor/Point Operations ──────────────────────────────────────
    void (*get_point)(int*, int*);
    void (*set_point)(int, int);
    int (*get_line_count)(void*);
    char *(*get_word_at_point)(void);
    char *(*get_current_line)(void);

    // ─── Window Operations ────────────────────────────────────────────
    struct window *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(void*, int);
    struct window *(*window_at_row)(int);
    int (*window_switch)(void*);

    // ─── Mouse/Cursor Helpers ─────────────────────────────────────────
    int (*screen_to_buffer_pos)(void*, int, int, int*, int*);
    int (*set_mark)(void);
    int (*scroll_up)(int);
    int (*scroll_down)(int);

    // ─── User Interface ───────────────────────────────────────────────
    void (*message)(const char*, ...);
    void (*vmessage)(const char*, void*);
    int (*prompt)(const char*, char*, size_t);
    int (*prompt_yn)(const char*);
    void (*update_display)(void);

    // ─── File Operations ──────────────────────────────────────────────
    int (*find_file_line)(const char*, int);

    // ─── Shell Integration ────────────────────────────────────────────
    int (*shell_command)(const char*, char**, size_t*);

    // ─── Memory Helpers ───────────────────────────────────────────────
    void *(*alloc)(size_t);
    void (*free)(void*);
    char *(*strdup)(const char*);

    // ─── Logging ──────────────────────────────────────────────────────
    void (*log_info)(const char*, ...);
    void (*log_warn)(const char*, ...);
    void (*log_error)(const char*, ...);
    void (*log_debug)(const char*, ...);

    // ─── Syntax Highlighting ──────────────────────────────────────────
    int (*syntax_register_lexer)(const char*, const char**, syntax_lex_fn, void*);
    int (*syntax_unregister_lexer)(const char*);
    int (*syntax_add_token)(line_tokens_t*, int, int);
    void (*syntax_invalidate_buffer)(struct buffer*);
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

// API wrappers for Go
void api_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void api_log_info(const char *msg) {
    if (g_api && g_api->log_info) g_api->log_info("%s", msg);
}

void api_log_error(const char *msg) {
    if (g_api && g_api->log_error) g_api->log_error("%s", msg);
}

void* api_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

const char* api_buffer_filename(void *bp) {
    if (g_api && g_api->buffer_filename) return g_api->buffer_filename(bp);
    return NULL;
}

char* api_buffer_contents(void *bp, size_t *len) {
    if (g_api && g_api->buffer_contents) return g_api->buffer_contents(bp, len);
    return NULL;
}

void api_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
}

int api_find_file_line(const char *path, int line) {
    if (g_api && g_api->find_file_line) return g_api->find_file_line(path, line);
    return 0;
}

void* api_buffer_create(const char *name) {
    if (g_api && g_api->buffer_create) return g_api->buffer_create(name);
    return NULL;
}

int api_buffer_switch(void *bp) {
    if (g_api && g_api->buffer_switch) return g_api->buffer_switch(bp);
    return 0;
}

int api_buffer_clear(void *bp) {
    if (g_api && g_api->buffer_clear) return g_api->buffer_clear(bp);
    return 0;
}

int api_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
    return 0;
}

void api_free(void *ptr) {
    if (g_api && g_api->free) g_api->free(ptr);
}

int api_syntax_add_token(void *tokens, int end_col, int face) {
    if (g_api && g_api->syntax_add_token)
        return g_api->syntax_add_token((line_tokens_t*)tokens, end_col, face);
    return -1;
}

void api_syntax_invalidate_buffer(void *bp) {
    if (g_api && g_api->syntax_invalidate_buffer)
        g_api->syntax_invalidate_buffer((struct buffer*)bp);
}

int api_emit(const char *event, void *data) {
    if (g_api && g_api->emit) return g_api->emit(event, data);
    return 0;
}

// Emit diagnostics event - called from Go
// Uses lsp_diag_entry_t defined in _cgo_export.h
void api_emit_diagnostics(const char *uri, lsp_diag_entry_t *diags, int count) {
    if (!g_api || !g_api->emit) return;

    // Create event structure on stack
    struct {
        const char *uri;
        lsp_diag_entry_t *diags;
        int count;
    } event = {
        .uri = uri,
        .diags = diags,
        .count = count
    };
    g_api->emit("lsp:diagnostics", &event);
}

// Lexer callback wrapper - calls Go function
// Signature matches internal syntax_lex_fn (API v3)
static lexer_state_t lsp_lexer_callback(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
) {
    (void)lang;  // We use global g_api
    (void)prev_state;

    // Call Go function (pass NULL for user_data - LSP doesn't need it)
    go_lsp_lex_line(NULL, buffer, line_num, (char*)line, len, out);

    // Return empty state (LSP doesn't use state machine)
    lexer_state_t result = {0, 0, 0, 0};
    return result;
}

// Command wrappers
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

// Event handlers for automatic didSave/didClose
static bool on_buffer_saved(void *buffer, void *user_data) {
    (void)user_data;
    (void)buffer;
    // Call the Go function to notify LSP server
    go_lsp_did_save(0, 1);
    return true; // Continue propagation
}

static bool on_buffer_closed(void *buffer, void *user_data) {
    (void)user_data;
    (void)buffer;
    // Call the Go function to notify LSP server
    go_lsp_did_close(0, 1);
    return true; // Continue propagation
}

// Language patterns for lexer registration
static const char *py_patterns[] = {"*.py", NULL};
static const char *go_patterns[] = {"*.go", NULL};
static const char *rs_patterns[] = {"*.rs", NULL};
static const char *c_patterns[] = {"*.c", "*.h", "*.cpp", "*.hpp", NULL};
static const char *js_patterns[] = {"*.js", "*.ts", NULL};
static const char *zig_patterns[] = {"*.zig", NULL};

static int lsp_init_c(uemacs_api *api) {
    g_api = api;

    // Register commands
    if (g_api->register_command) {
        g_api->register_command("lsp-start", cmd_lsp_start);
        g_api->register_command("lsp-stop", cmd_lsp_stop);
        g_api->register_command("lsp-hover", cmd_lsp_hover);
        g_api->register_command("lsp-definition", cmd_lsp_definition);
        g_api->register_command("lsp-references", cmd_lsp_references);
        g_api->register_command("lsp-refresh-tokens", cmd_lsp_refresh_tokens);
        g_api->register_command("lsp-completion", cmd_lsp_completion);
        g_api->register_command("lsp-diagnostics", cmd_lsp_diagnostics);
        g_api->register_command("lsp-code-action", cmd_lsp_code_action);
        g_api->register_command("lsp-document-symbols", cmd_lsp_document_symbols);
        g_api->register_command("lsp-workspace-symbols", cmd_lsp_workspace_symbols);
    }

    // Register as lexer for supported languages
    // These will override built-in lexers when LSP provides semantic tokens
    if (g_api->syntax_register_lexer) {
        g_api->syntax_register_lexer("lsp-python", py_patterns, lsp_lexer_callback, NULL);
        g_api->syntax_register_lexer("lsp-go", go_patterns, lsp_lexer_callback, NULL);
        g_api->syntax_register_lexer("lsp-rust", rs_patterns, lsp_lexer_callback, NULL);
        g_api->syntax_register_lexer("lsp-c", c_patterns, lsp_lexer_callback, NULL);
        g_api->syntax_register_lexer("lsp-js", js_patterns, lsp_lexer_callback, NULL);
        g_api->syntax_register_lexer("lsp-zig", zig_patterns, lsp_lexer_callback, NULL);
    }

    // Register event handlers for automatic didSave/didClose
    if (g_api->on) {
        g_api->on("buffer:saved", on_buffer_saved, NULL, 0);
        g_api->on("buffer:closed", on_buffer_closed, NULL, 0);
    }

    api_log_info("lsp_client: Go extension loaded (v4.0, lock-free concurrent)");
    return 0;
}

static void lsp_cleanup_c(void) {
    if (g_api) {
        // Unregister commands
        if (g_api->unregister_command) {
            g_api->unregister_command("lsp-start");
            g_api->unregister_command("lsp-stop");
            g_api->unregister_command("lsp-hover");
            g_api->unregister_command("lsp-definition");
            g_api->unregister_command("lsp-references");
            g_api->unregister_command("lsp-refresh-tokens");
            g_api->unregister_command("lsp-completion");
            g_api->unregister_command("lsp-diagnostics");
            g_api->unregister_command("lsp-code-action");
            g_api->unregister_command("lsp-document-symbols");
            g_api->unregister_command("lsp-workspace-symbols");
        }

        // Unregister lexers
        if (g_api->syntax_unregister_lexer) {
            g_api->syntax_unregister_lexer("lsp-python");
            g_api->syntax_unregister_lexer("lsp-go");
            g_api->syntax_unregister_lexer("lsp-rust");
            g_api->syntax_unregister_lexer("lsp-c");
            g_api->syntax_unregister_lexer("lsp-js");
            g_api->syntax_unregister_lexer("lsp-zig");
        }

        // Unregister event handlers
        if (g_api->off) {
            g_api->off("buffer:saved", on_buffer_saved);
            g_api->off("buffer:closed", on_buffer_closed);
        }
    }
}

static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "go_lsp",
    .version = "4.0.0",  /* v4: Lock-free concurrency, full LSP feature set */
    .description = "LSP client with semantic tokens (Go, lock-free concurrent)",
    .init = lsp_init_c,
    .cleanup = lsp_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
