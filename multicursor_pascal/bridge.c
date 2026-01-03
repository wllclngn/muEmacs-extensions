// C bridge for Pascal Multiple Cursors extension

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

// API wrappers for Pascal
void bridge_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

void bridge_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
    else { *line = 1; *col = 0; }
}

void bridge_set_point(int line, int col) {
    if (g_api && g_api->set_point) g_api->set_point(line, col);
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
    return 0;
}

// Pascal command implementations
extern int pascal_mc_add(int f, int n);
extern int pascal_mc_clear(int f, int n);
extern int pascal_mc_next(int f, int n);
extern int pascal_mc_insert(int f, int n);

static int cmd_mc_add(int f, int n) { return pascal_mc_add(f, n); }
static int cmd_mc_clear(int f, int n) { return pascal_mc_clear(f, n); }
static int cmd_mc_next(int f, int n) { return pascal_mc_next(f, n); }
static int cmd_mc_insert(int f, int n) { return pascal_mc_insert(f, n); }

static int mc_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("mc-add", cmd_mc_add);
        g_api->register_command("mc-clear", cmd_mc_clear);
        g_api->register_command("mc-next", cmd_mc_next);
        g_api->register_command("mc-insert", cmd_mc_insert);
    }
    if (g_api->log_info) g_api->log_info("multicursor_pascal: Loaded");
    return 0;
}

static void mc_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("mc-add");
        g_api->unregister_command("mc-clear");
        g_api->unregister_command("mc-next");
        g_api->unregister_command("mc-insert");
    }
}

static uemacs_extension ext = {
    .api_version = 2,
    .name = "multicursor_pascal",
    .version = "1.0.0",
    .description = "Multiple cursors (Pascal)",
    .init = mc_init_c,
    .cleanup = mc_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
