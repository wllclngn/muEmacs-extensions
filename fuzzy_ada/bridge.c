// C bridge for Ada Fuzzy Finder extension

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    void (*vmessage)(const char*, void*);
    int (*prompt)(const char*, char*, size_t);
    void *_pad5[2];
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

// API wrappers for Ada
void bridge_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

int bridge_prompt(const char *prompt, char *buf, size_t buflen) {
    if (g_api && g_api->prompt) return g_api->prompt(prompt, buf, buflen);
    return 0;
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

// Ada command implementations
extern int ada_fuzzy_find(int f, int n);
extern int ada_fuzzy_grep(int f, int n);

static int cmd_fuzzy_find(int f, int n) { return ada_fuzzy_find(f, n); }
static int cmd_fuzzy_grep(int f, int n) { return ada_fuzzy_grep(f, n); }

static int fuzzy_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        g_api->register_command("fuzzy-find", cmd_fuzzy_find);
        g_api->register_command("fuzzy-grep", cmd_fuzzy_grep);
    }
    if (g_api->log_info) g_api->log_info("fuzzy_ada: Loaded");
    return 0;
}

static void fuzzy_cleanup_c(void) {
    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("fuzzy-find");
        g_api->unregister_command("fuzzy-grep");
    }
}

static uemacs_extension ext = {
    .api_version = 2,
    .name = "fuzzy_ada",
    .version = "1.0.0",
    .description = "Fuzzy file finder (Ada)",
    .init = fuzzy_init_c,
    .cleanup = fuzzy_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
