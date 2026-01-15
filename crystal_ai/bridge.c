/*
 * bridge.c - C Bridge for Crystal NEUROXUS AI Agent Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides C wrappers for Crystal FFI interop.
 * Uses get_function() for ABI stability - immune to API struct layout changes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <uep/extension_api.h>

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*current_buffer_fn)(void);
typedef char *(*buffer_contents_fn)(void*, size_t*);
typedef const char *(*buffer_filename_fn)(void*);
typedef void (*get_point_fn)(int*, int*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef void (*message_fn)(const char*, ...);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);

/* Our local API - only the functions we actually use */
static struct {
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_contents_fn buffer_contents;
    buffer_filename_fn buffer_filename;
    get_point_fn get_point;
    buffer_insert_fn buffer_insert;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    prompt_fn prompt;
    message_fn message;
    free_fn free;
    log_fn log_info;
} api;

/* Extension descriptor */
typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(void*);
    void (*cleanup)(void);
} uemacs_extension;

// API wrappers for Crystal
void bridge_message(const char *msg) {
    if (api.message) api.message("%s", msg);
}

void* bridge_current_buffer(void) {
    if (api.current_buffer) return api.current_buffer();
    return NULL;
}

char* bridge_buffer_contents(void *bp, size_t *len) {
    if (api.buffer_contents) return api.buffer_contents(bp, len);
    *len = 0;
    return NULL;
}

const char* bridge_buffer_filename(void *bp) {
    if (api.buffer_filename) return api.buffer_filename(bp);
    return NULL;
}

void bridge_get_point(int *line, int *col) {
    if (api.get_point) api.get_point(line, col);
    else { *line = 1; *col = 0; }
}

int bridge_buffer_insert(const char *text, size_t len) {
    if (api.buffer_insert) return api.buffer_insert(text, len);
    return 0;
}

void* bridge_buffer_create(const char *name) {
    if (api.buffer_create) return api.buffer_create(name);
    return NULL;
}

int bridge_buffer_switch(void *bp) {
    if (api.buffer_switch) return api.buffer_switch(bp);
    return 0;
}

int bridge_buffer_clear(void *bp) {
    if (api.buffer_clear) return api.buffer_clear(bp);
    return 0;
}

int bridge_prompt(const char *prompt_text, char *buf, int max) {
    if (api.prompt) return api.prompt(prompt_text, buf, max);
    return 0;
}

void bridge_free(void *ptr) {
    if (api.free) api.free(ptr);
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

static int ai_init_c(void *editor_api_raw) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    struct uemacs_api *editor_api = (struct uemacs_api *)editor_api_raw;

    if (!editor_api->get_function) {
        fprintf(stderr, "crystal_ai: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_contents = (buffer_contents_fn)LOOKUP(buffer_contents);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.message = (message_fn)LOOKUP(message);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);

    #undef LOOKUP

    /* Verify critical functions */
    if (!api.register_command) {
        fprintf(stderr, "crystal_ai: Failed to look up register_command\n");
        return -1;
    }

    // NEUROXUS agent commands
    api.register_command("ai-spawn", cmd_ai_spawn);
    api.register_command("ai-status", cmd_ai_status);
    api.register_command("ai-output", cmd_ai_output);
    api.register_command("ai-kill", cmd_ai_kill);
    api.register_command("ai-poll", cmd_ai_poll);

    // Legacy compatibility
    api.register_command("ai-complete", cmd_ai_complete);
    api.register_command("ai-explain", cmd_ai_explain);
    api.register_command("ai-fix", cmd_ai_fix);

    if (api.log_info) api.log_info("ai_crystal: NEUROXUS agent system loaded (ABI-stable)");
    return 0;
}

static void ai_cleanup_c(void) {
    // Clean up Crystal runtime first (kills agents, frees memory)
    crystal_cleanup();

    if (api.unregister_command) {
        api.unregister_command("ai-spawn");
        api.unregister_command("ai-status");
        api.unregister_command("ai-output");
        api.unregister_command("ai-kill");
        api.unregister_command("ai-poll");
        api.unregister_command("ai-complete");
        api.unregister_command("ai-explain");
        api.unregister_command("ai-fix");
    }
}

static uemacs_extension ext = {
    .api_version = 4,  /* ABI-stable API */
    .name = "crystal_ai",
    .version = "4.0.0",  /* v4: ABI-stable migration */
    .description = "NEUROXUS AI Agent System (Crystal)",
    .init = ai_init_c,
    .cleanup = ai_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
