// C bridge for Haskell Project Management extension

#include <stdlib.h>
#include <string.h>

typedef int (*cmd_fn_t)(int, int);

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

static uemacs_api *g_api = NULL;

// API wrappers for Haskell
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

int bridge_find_file_line(const char *path, int line) {
    if (g_api && g_api->find_file_line) return g_api->find_file_line(path, line);
    return 0;
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

static int project_init_c(uemacs_api *api) {
    g_api = api;

    // Initialize Haskell runtime
    static int argc = 1;
    static char *argv[] = { "project_haskell", NULL };
    static char **pargv = argv;
    hs_init(&argc, &pargv);

    if (g_api->register_command) {
        g_api->register_command("project-root", cmd_project_root);
        g_api->register_command("project-files", cmd_project_files);
        g_api->register_command("project-find", cmd_project_find);
    }
    if (g_api->log_info) g_api->log_info("project_haskell: Loaded");
    return 0;
}

static void project_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("project-root");
        g_api->unregister_command("project-files");
        g_api->unregister_command("project-find");
    }
    hs_exit();
}

static uemacs_extension ext = {
    .api_version = 2,
    .name = "project_haskell",
    .version = "1.0.0",
    .description = "Project management (Haskell)",
    .init = project_init_c,
    .cleanup = project_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
