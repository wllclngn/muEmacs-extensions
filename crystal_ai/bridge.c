/*
 * bridge.c - C Bridge for Crystal NEUROXUS AI Agent Extension
 *
 * API Version: 3 (Event Bus)
 *
 * Provides C wrappers for Crystal FFI interop.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(void*, void*);

// API struct (must match extension_api.h v3)
typedef struct {
    int api_version;

    // Event Bus (API v3)
    int (*on)(const char*, event_fn_t, void*, int);
    int (*off)(const char*, event_fn_t);
    bool (*emit)(const char*, void*);

    // Configuration Access
    int (*config_int)(const char*, const char*, int);
    bool (*config_bool)(const char*, const char*, bool);
    const char *(*config_string)(const char*, const char*, const char*);

    // Command Registration
    int (*register_command)(const char*, cmd_fn_t);
    int (*unregister_command)(const char*);

    // Buffer Operations
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

    // Cursor/Point Operations
    void (*get_point)(int*, int*);
    void (*set_point)(int, int);
    int (*get_line_count)(void*);
    char *(*get_word_at_point)(void);
    char *(*get_current_line)(void);

    // Window Operations
    void *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(void*, int);
    void *(*window_at_row)(int);
    int (*window_switch)(void*);

    // Mouse/Cursor Helpers
    int (*screen_to_buffer_pos)(void*, int, int, int*, int*);
    int (*set_mark)(void);
    int (*scroll_up)(int);
    int (*scroll_down)(int);

    // User Interface
    void (*message)(const char*, ...);
    void (*vmessage)(const char*, void*);
    int (*prompt)(const char*, char*, size_t);
    int (*prompt_yn)(const char*);
    void (*update_display)(void);

    // File Operations
    int (*find_file_line)(const char*, int);

    // Shell Integration
    int (*shell_command)(const char*, char**, size_t*);

    // Memory Helpers
    void *(*alloc)(size_t);
    void (*free)(void*);
    char *(*strdup)(const char*);

    // Logging
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

// API wrappers for Crystal
void bridge_message(const char *msg) {
    if (g_api && g_api->message) g_api->message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (g_api && g_api->current_buffer) return g_api->current_buffer();
    return NULL;
}

char* bridge_buffer_contents(void *bp, size_t *len) {
    if (g_api && g_api->buffer_contents) return g_api->buffer_contents(bp, len);
    *len = 0;
    return NULL;
}

const char* bridge_buffer_filename(void *bp) {
    if (g_api && g_api->buffer_filename) return g_api->buffer_filename(bp);
    return NULL;
}

void bridge_get_point(int *line, int *col) {
    if (g_api && g_api->get_point) g_api->get_point(line, col);
    else { *line = 1; *col = 0; }
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (g_api && g_api->buffer_insert) return g_api->buffer_insert(text, len);
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

int bridge_prompt(const char *prompt, char *buf, int max) {
    if (g_api && g_api->prompt) return g_api->prompt(prompt, buf, max);
    return 0;
}

void bridge_free(void *ptr) {
    if (g_api && g_api->free) g_api->free(ptr);
}

// Crystal command implementations (new NEUROXUS commands)
extern int crystal_ai_spawn(int f, int n);
extern int crystal_ai_status(int f, int n);
extern int crystal_ai_output(int f, int n);
extern int crystal_ai_kill(int f, int n);
extern int crystal_ai_poll(int f, int n);

// Legacy compatibility commands
extern int crystal_ai_complete(int f, int n);
extern int crystal_ai_explain(int f, int n);
extern int crystal_ai_fix(int f, int n);

// Crystal cleanup (kills agents, runs GC)
extern void crystal_cleanup(void);

// Command wrappers
static int cmd_ai_spawn(int f, int n) { return crystal_ai_spawn(f, n); }
static int cmd_ai_status(int f, int n) { return crystal_ai_status(f, n); }
static int cmd_ai_output(int f, int n) { return crystal_ai_output(f, n); }
static int cmd_ai_kill(int f, int n) { return crystal_ai_kill(f, n); }
static int cmd_ai_poll(int f, int n) { return crystal_ai_poll(f, n); }
static int cmd_ai_complete(int f, int n) { return crystal_ai_complete(f, n); }
static int cmd_ai_explain(int f, int n) { return crystal_ai_explain(f, n); }
static int cmd_ai_fix(int f, int n) { return crystal_ai_fix(f, n); }

static int ai_init_c(uemacs_api *api) {
    g_api = api;
    if (g_api->register_command) {
        // NEUROXUS agent commands
        g_api->register_command("ai-spawn", cmd_ai_spawn);
        g_api->register_command("ai-status", cmd_ai_status);
        g_api->register_command("ai-output", cmd_ai_output);
        g_api->register_command("ai-kill", cmd_ai_kill);
        g_api->register_command("ai-poll", cmd_ai_poll);

        // Legacy compatibility
        g_api->register_command("ai-complete", cmd_ai_complete);
        g_api->register_command("ai-explain", cmd_ai_explain);
        g_api->register_command("ai-fix", cmd_ai_fix);
    }
    if (g_api->log_info) g_api->log_info("ai_crystal: NEUROXUS agent system loaded");
    return 0;
}

static void ai_cleanup_c(void) {
    // Clean up Crystal runtime first (kills agents, frees memory)
    crystal_cleanup();

    if (g_api && g_api->unregister_command) {
        g_api->unregister_command("ai-spawn");
        g_api->unregister_command("ai-status");
        g_api->unregister_command("ai-output");
        g_api->unregister_command("ai-kill");
        g_api->unregister_command("ai-poll");
        g_api->unregister_command("ai-complete");
        g_api->unregister_command("ai-explain");
        g_api->unregister_command("ai-fix");
    }
}

static uemacs_extension ext = {
    .api_version = 3,  /* Event bus API */
    .name = "crystal_ai",
    .version = "3.0.0",  /* v3: API v3 migration */
    .description = "NEUROXUS AI Agent System (Crystal)",
    .init = ai_init_c,
    .cleanup = ai_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
