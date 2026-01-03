// C bridge for Fortran Git extension
// Handles the complex API struct access

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Command function type
typedef int (*cmd_fn_t)(int, int);

// API struct (must match uEmacs exactly)
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
static uemacs_api *g_api = NULL;

// API wrappers for Fortran
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

void bridge_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
    else { *line = 1; *col = 0; }
}

// Fortran command implementations (linked from Fortran)
extern int fortran_git_status(int f, int n);
extern int fortran_git_diff(int f, int n);
extern int fortran_git_log(int f, int n);
extern int fortran_git_blame(int f, int n);
extern int fortran_git_add(int f, int n);

// C wrappers
static int cmd_git_status(int f, int n) { return fortran_git_status(f, n); }
static int cmd_git_diff(int f, int n) { return fortran_git_diff(f, n); }
static int cmd_git_log(int f, int n) { return fortran_git_log(f, n); }
static int cmd_git_blame(int f, int n) { return fortran_git_blame(f, n); }
static int cmd_git_add(int f, int n) { return fortran_git_add(f, n); }

// Init
static int git_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("fgit-status", cmd_git_status);
        g_api->register_command("fgit-diff", cmd_git_diff);
        g_api->register_command("fgit-log", cmd_git_log);
        g_api->register_command("fgit-blame", cmd_git_blame);
        g_api->register_command("fgit-add", cmd_git_add);
    }
    if (g_api->log_info) g_api->log_info("git_fortran: Loaded (fgit-* commands)");
    return 0;
}

// Cleanup
static void git_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("fgit-status");
        g_api->unregister_command("fgit-diff");
        g_api->unregister_command("fgit-log");
        g_api->unregister_command("fgit-blame");
        g_api->unregister_command("fgit-add");
    }
}

// Extension descriptor
static uemacs_extension ext = {
    .api_version = 2,
    .name = "git_fortran",
    .version = "1.0.0",
    .description = "Git integration (Fortran)",
    .init = git_init_c,
    .cleanup = git_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
