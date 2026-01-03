#include <stdlib.h>
#include <string.h>
#include "_cgo_export.h"

// Command function type
typedef int (*cmd_fn_t)(int, int);

// Minimal API struct
typedef struct {
    int api_version;
    int (*register_command)(const char*, cmd_fn_t);
    int (*unregister_command)(const char*);
    void *_pad1[10];
    void *(*current_buffer)(void);
    void *(*find_buffer)(const char*);
    char *(*buffer_contents)(void*, size_t*);
    const char *(*buffer_filename)(void*);
    const char *(*buffer_name)(void*);
    int (*buffer_modified)(void*);
    int (*buffer_insert)(const char*, size_t);
    void *_pad2;
    void *(*buffer_create)(const char*);
    int (*buffer_switch)(void*);
    int (*buffer_clear)(void*);
    void (*get_point)(int*, int*);
    void (*set_point)(int, int);
    void *_pad3[3];
    void *_pad4[3];
    void (*message)(const char*, ...);
    void *_pad5[4];
    int (*find_file_line)(const char*, int);
    void *_pad6;
    void *(*alloc)(size_t);
    void (*free)(void*);
    void *_pad7;
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
uemacs_api *g_api = NULL;

// API wrappers
void api_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void api_log_info(const char *msg) {
    if (g_api && g_api->log_info) g_api->log_info("%s", msg);
}

void* api_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

const char* api_buffer_filename(void *bp) {
    if (g_api && g_api->buffer_filename) return g_api->buffer_filename(bp);
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

// Command wrappers
static int cmd_lsp_start(int f, int n) { return go_lsp_start(f, n); }
static int cmd_lsp_stop(int f, int n) { return go_lsp_stop(f, n); }
static int cmd_lsp_hover(int f, int n) { return go_lsp_hover(f, n); }
static int cmd_lsp_definition(int f, int n) { return go_lsp_definition(f, n); }
static int cmd_lsp_references(int f, int n) { return go_lsp_references(f, n); }

static int lsp_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("lsp-start", cmd_lsp_start);
        g_api->register_command("lsp-stop", cmd_lsp_stop);
        g_api->register_command("lsp-hover", cmd_lsp_hover);
        g_api->register_command("lsp-definition", cmd_lsp_definition);
        g_api->register_command("lsp-references", cmd_lsp_references);
    }
    api_log_info("lsp_client: Go extension loaded");
    return 0;
}

static void lsp_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("lsp-start");
        g_api->unregister_command("lsp-stop");
        g_api->unregister_command("lsp-hover");
        g_api->unregister_command("lsp-definition");
        g_api->unregister_command("lsp-references");
    }
}

static uemacs_extension ext = {
    .api_version = 2,
    .name = "lsp_client",
    .version = "1.0.0",
    .description = "LSP client (Go)",
    .init = lsp_init_c,
    .cleanup = lsp_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
