/*
 * bridge.c - C Bridge for Fortran Git Extension
 *
 * This bridge provides the glue between μEmacs API and Fortran implementation.
 * All git logic is in git_ext.f90; this file just handles API access.
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <uep/extension.h>
#include <uep/extension_api.h>

/* ============================================================================
 * API Function Pointers (looked up at init time for ABI stability)
 * ============================================================================ */

typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef int (*config_int_fn)(const char*, const char*, int);
typedef bool (*config_bool_fn)(const char*, const char*, bool);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct buffer *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(struct buffer*);
typedef const char *(*buffer_name_fn)(struct buffer*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef struct buffer *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(struct buffer*);
typedef int (*buffer_clear_fn)(struct buffer*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef char *(*get_current_line_fn)(void);
typedef void (*message_fn)(const char*, ...);
typedef void (*update_display_fn)(void);
typedef int (*shell_command_fn)(const char*, char**, size_t*);
typedef int (*find_file_line_fn)(const char*, int);
typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);
typedef int (*prompt_fn)(const char*, char*, size_t);

static struct {
    on_fn on;
    off_fn off;
    config_int_fn config_int;
    config_bool_fn config_bool;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_filename_fn buffer_filename;
    buffer_name_fn buffer_name;
    buffer_insert_fn buffer_insert;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    get_point_fn get_point;
    set_point_fn set_point;
    get_current_line_fn get_current_line;
    message_fn message;
    update_display_fn update_display;
    shell_command_fn shell_command;
    find_file_line_fn find_file_line;
    prompt_fn prompt;
    alloc_fn alloc;
    free_fn free;
    log_fn log_info;
    log_fn log_error;
    log_fn log_debug;
} api;

/* Git root for path resolution */
static char g_git_root[512] = {0};

/* Buffer names */
#define GIT_STATUS_BUFFER "*git-status*"
#define GIT_LOG_BUFFER    "*git-log*"
#define GIT_DIFF_BUFFER   "*git-diff*"

/* ============================================================================
 * Bridge Functions - Called from Fortran via bind(C)
 * ============================================================================ */

/* Display a message in the minibuffer */
void bridge_message(const char *msg) {
    if (api.message && msg) {
        api.message("%s", msg);
    }
}

/* Get current buffer pointer */
void *bridge_current_buffer(void) {
    return api.current_buffer ? api.current_buffer() : NULL;
}

/* Get filename from buffer */
const char *bridge_buffer_filename(void *bp) {
    return (api.buffer_filename && bp) ? api.buffer_filename(bp) : NULL;
}

/* Get buffer name */
const char *bridge_buffer_name(void *bp) {
    return (api.buffer_name && bp) ? api.buffer_name(bp) : NULL;
}

/* Create a named buffer */
void *bridge_buffer_create(const char *name) {
    return api.buffer_create ? api.buffer_create(name) : NULL;
}

/* Switch to buffer */
int bridge_buffer_switch(void *bp) {
    return (api.buffer_switch && bp) ? api.buffer_switch(bp) : 0;
}

/* Clear buffer contents */
int bridge_buffer_clear(void *bp) {
    return (api.buffer_clear && bp) ? api.buffer_clear(bp) : 0;
}

/* Insert text into current buffer */
int bridge_buffer_insert(const char *text, size_t len) {
    return api.buffer_insert ? api.buffer_insert(text, len) : 0;
}

/* Get cursor position */
void bridge_get_point(int *line, int *col) {
    if (api.get_point) {
        api.get_point(line, col);
    } else {
        *line = 1;
        *col = 0;
    }
}

/* Set cursor position */
void bridge_set_point(int line, int col) {
    if (api.set_point) {
        api.set_point(line, col);
    }
}

/* Get current line text (caller must free) */
char *bridge_get_current_line(void) {
    return api.get_current_line ? api.get_current_line() : NULL;
}

/* Free memory allocated by editor */
void bridge_free(void *ptr) {
    if (api.free && ptr) {
        api.free(ptr);
    }
}

/* Run shell command, return output (caller must free) */
int bridge_shell_command(const char *cmd, char **output, size_t *len) {
    if (!api.shell_command) {
        *output = NULL;
        *len = 0;
        return -1;
    }
    return api.shell_command(cmd, output, len);
}

/* Open file at line */
int bridge_find_file_line(const char *path, int line) {
    return api.find_file_line ? api.find_file_line(path, line) : 0;
}

/* Prompt user for input */
int bridge_prompt(const char *prompt_text, char *result, size_t maxlen) {
    return api.prompt ? api.prompt(prompt_text, result, maxlen) : -1;
}

/* Update display */
void bridge_update_display(void) {
    if (api.update_display) {
        api.update_display();
    }
}

/* Log functions */
void bridge_log_info(const char *msg) {
    if (api.log_info) api.log_info("%s", msg);
}

void bridge_log_debug(const char *msg) {
    if (api.log_debug) api.log_debug("%s", msg);
}

/* Check if in git repository */
int bridge_in_git_repo(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git rev-parse --is-inside-work-tree 2>/dev/null", &output, &len);
    int result = (ret == 0 && output && strncmp(output, "true", 4) == 0);
    if (output) api.free(output);
    return result;
}

/* Get and store git root */
const char *bridge_get_git_root(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git rev-parse --show-toplevel 2>/dev/null", &output, &len);
    if (ret == 0 && output && len > 0) {
        if (output[len-1] == '\n') output[len-1] = '\0';
        strncpy(g_git_root, output, sizeof(g_git_root) - 1);
        g_git_root[sizeof(g_git_root) - 1] = '\0';
        api.free(output);
        return g_git_root;
    }
    if (output) api.free(output);
    g_git_root[0] = '\0';
    return NULL;
}

/* Get stored git root */
const char *bridge_stored_git_root(void) {
    return g_git_root[0] ? g_git_root : NULL;
}

/* ============================================================================
 * Fortran Function Declarations (implemented in git_ext.f90)
 * ============================================================================ */

extern int fortran_git_status(int f, int n);
extern int fortran_git_status_full(int f, int n);
extern int fortran_git_stage(int f, int n);
extern int fortran_git_unstage(int f, int n);
extern int fortran_git_commit(int f, int n);
extern int fortran_git_diff(int f, int n);
extern int fortran_git_log(int f, int n);
extern int fortran_git_pull(int f, int n);
extern int fortran_git_push(int f, int n);
extern int fortran_git_branch(int f, int n);
extern int fortran_git_stash(int f, int n);
extern int fortran_git_stash_pop(int f, int n);
extern int fortran_git_goto(int f, int n);

/* ============================================================================
 * Command Wrappers - Register these, they call Fortran
 * ============================================================================ */

static int cmd_git_status(int f, int n) { return fortran_git_status(f, n); }
static int cmd_git_status_full(int f, int n) { return fortran_git_status_full(f, n); }
static int cmd_git_stage(int f, int n) { return fortran_git_stage(f, n); }
static int cmd_git_unstage(int f, int n) { return fortran_git_unstage(f, n); }
static int cmd_git_commit(int f, int n) { return fortran_git_commit(f, n); }
static int cmd_git_diff(int f, int n) { return fortran_git_diff(f, n); }
static int cmd_git_log(int f, int n) { return fortran_git_log(f, n); }
static int cmd_git_pull(int f, int n) { return fortran_git_pull(f, n); }
static int cmd_git_push(int f, int n) { return fortran_git_push(f, n); }
static int cmd_git_branch(int f, int n) { return fortran_git_branch(f, n); }
static int cmd_git_stash(int f, int n) { return fortran_git_stash(f, n); }
static int cmd_git_stash_pop(int f, int n) { return fortran_git_stash_pop(f, n); }

/* ============================================================================
 * Buffer Navigation - Enter key handler
 * ============================================================================ */

static bool in_git_buffer(void) {
    if (!api.current_buffer || !api.buffer_name) return false;
    void *bp = api.current_buffer();
    if (!bp) return false;
    const char *name = api.buffer_name(bp);
    if (!name) return false;
    return (strcmp(name, GIT_STATUS_BUFFER) == 0 ||
            strcmp(name, GIT_LOG_BUFFER) == 0 ||
            strcmp(name, GIT_DIFF_BUFFER) == 0);
}

static bool git_key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    int key = (int)(intptr_t)event->data;
    if (key != '\r' && key != '\n') return false;
    if (!in_git_buffer()) return false;

    /* Delegate to Fortran */
    fortran_git_goto(0, 1);
    return true;
}

/* ============================================================================
 * Extension Lifecycle
 * ============================================================================ */

static int git_init(struct uemacs_api *editor_api) {
    if (!editor_api->get_function) {
        fprintf(stderr, "fortran_git: Requires μEmacs with get_function() support\n");
        return -1;
    }

    #define LOOKUP(name) editor_api->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.config_int = (config_int_fn)LOOKUP(config_int);
    api.config_bool = (config_bool_fn)LOOKUP(config_bool);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.message = (message_fn)LOOKUP(message);
    api.update_display = (update_display_fn)LOOKUP(update_display);
    api.shell_command = (shell_command_fn)LOOKUP(shell_command);
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.alloc = (alloc_fn)LOOKUP(alloc);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.log_debug = (log_fn)LOOKUP(log_debug);

    #undef LOOKUP

    if (!api.on || !api.off || !api.register_command || !api.log_error) {
        fprintf(stderr, "fortran_git: Missing critical API functions\n");
        return -1;
    }

    /* Register all 12 commands */
    api.register_command("git-status", cmd_git_status);
    api.register_command("git-status-full", cmd_git_status_full);
    api.register_command("git-stage", cmd_git_stage);
    api.register_command("git-unstage", cmd_git_unstage);
    api.register_command("git-commit", cmd_git_commit);
    api.register_command("git-diff", cmd_git_diff);
    api.register_command("git-log", cmd_git_log);
    api.register_command("git-pull", cmd_git_pull);
    api.register_command("git-push", cmd_git_push);
    api.register_command("git-branch", cmd_git_branch);
    api.register_command("git-stash", cmd_git_stash);
    api.register_command("git-stash-pop", cmd_git_stash_pop);

    /* Register Enter key handler for buffer navigation */
    api.on("input:key", git_key_handler, NULL, 0);

    api.log_info("fortran_git v1.0.0 loaded (Fortran core, 12 commands)");

    /* Show branch if in git repo */
    if (bridge_in_git_repo()) {
        char *output = NULL;
        size_t len = 0;
        api.shell_command("git branch --show-current 2>/dev/null", &output, &len);
        if (output && len > 0) {
            if (output[len-1] == '\n') output[len-1] = '\0';
            api.message("Git: On branch '%s'", output);
            api.free(output);
        }
    }

    return 0;
}

static void git_cleanup(void) {
    api.off("input:key", git_key_handler);

    api.unregister_command("git-status");
    api.unregister_command("git-status-full");
    api.unregister_command("git-stage");
    api.unregister_command("git-unstage");
    api.unregister_command("git-commit");
    api.unregister_command("git-diff");
    api.unregister_command("git-log");
    api.unregister_command("git-pull");
    api.unregister_command("git-push");
    api.unregister_command("git-branch");
    api.unregister_command("git-stash");
    api.unregister_command("git-stash-pop");

    g_git_root[0] = '\0';
    api.log_info("fortran_git extension unloaded");
}

/* Extension descriptor */
static struct uemacs_extension git_ext = {
    .api_version = UEMACS_API_VERSION_BUILD,
    .name = "fortran_git",
    .version = "1.0.0",
    .description = "Git integration (Fortran core, 12 commands)",
    .init = git_init,
    .cleanup = git_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &git_ext;
}
