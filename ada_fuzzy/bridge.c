/*
 * bridge.c - Ada-Friendly C Bridge for Fuzzy Finder Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Design: C owns all mutable buffers. Ada reads immutably.
 * - bridge_prompt() stores result internally, returns status
 * - bridge_exec() stores output internally, returns status
 * - bridge_get_string() returns pointer to internal buffer
 *
 * IMPORTANT: Must call adainit() before any Ada code and adafinal() on cleanup.
 * Without this, Ada's secondary stack (used for String returns) is uninitialized.
 *
 * Uses get_function() for ABI stability - immune to API struct layout changes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <uep/extension_api.h>
#include <uep/extension.h>

/* Ada runtime initialization - MUST be called before using Ada code */
extern void adainit(void);
extern void adafinal(void);

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(void*, void*);
typedef int (*on_fn)(const char*, event_fn_t, void*, int);
typedef int (*off_fn)(const char*, event_fn_t);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*current_buffer_fn)(void);
typedef const char *(*buffer_name_fn)(void*);
typedef void (*message_fn)(const char*, ...);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef int (*shell_command_fn)(const char*, char**, size_t*);
typedef void *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef int (*find_file_line_fn)(const char*, int);
typedef char *(*get_current_line_fn)(void);
typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);
typedef void (*(*get_function_fn)(const char*))(void);

/* Our local API - only the functions we actually use */
static struct {
    on_fn on;
    off_fn off;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_name_fn buffer_name;
    message_fn message;
    prompt_fn prompt;
    shell_command_fn shell_command;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    find_file_line_fn find_file_line;
    get_current_line_fn get_current_line;
    alloc_fn alloc;
    free_fn free;
    log_fn log_info;
    log_fn log_warn;
    log_fn log_debug;
} api;

/* Extension descriptor - use uemacs_extension from header */

/* ============================================================================
 * Logging Helper
 * ============================================================================ */

#define BRIDGE_LOG(fmt, ...) do { \
    if (api.log_debug) \
        api.log_debug("fuzzy_ada: " fmt, ##__VA_ARGS__); \
} while(0)

#define BRIDGE_LOG_WARN(fmt, ...) do { \
    if (api.log_warn) \
        api.log_warn("fuzzy_ada: " fmt, ##__VA_ARGS__); \
} while(0)

/* ============================================================================
 * Internal Buffers - C owns these, Ada reads immutably
 * ============================================================================ */

#define STRING_BUFFER_SIZE 65536

static char g_string_buffer[STRING_BUFFER_SIZE];
static size_t g_string_len = 0;

/* ============================================================================
 * Ada-Friendly Bridge API
 * ============================================================================ */

// Simple message - no buffer needed
void bridge_message(const char *msg) {
    BRIDGE_LOG("bridge_message: ENTER msg='%.50s...'", msg ? msg : "(null)");
    if (api.message) api.message("%s", msg);
    BRIDGE_LOG("bridge_message: EXIT");
}

// Prompt user for input
// Returns: 1 on success (result in internal buffer), 0 on cancel/error
int bridge_prompt(const char *prompt_text) {
    BRIDGE_LOG("bridge_prompt: ENTER prompt='%s'", prompt_text ? prompt_text : "(null)");

    if (!api.prompt) {
        BRIDGE_LOG_WARN("bridge_prompt: NO prompt function");
        return 0;
    }

    g_string_buffer[0] = '\0';
    g_string_len = 0;
    BRIDGE_LOG("bridge_prompt: buffer cleared, calling api.prompt");

    int result = api.prompt(prompt_text, g_string_buffer, sizeof(g_string_buffer));
    BRIDGE_LOG("bridge_prompt: api.prompt returned %d", result);

    if (result == 1) {
        g_string_len = strlen(g_string_buffer);
        BRIDGE_LOG("bridge_prompt: SUCCESS len=%zu content='%.50s'",
                   g_string_len, g_string_buffer);
        return 1;
    }

    BRIDGE_LOG("bridge_prompt: CANCELLED or FAILED");
    return 0;
}

// Execute shell command
// Returns: 1 on success (output in internal buffer), 0 on failure
int bridge_exec(const char *cmd) {
    BRIDGE_LOG("bridge_exec: ENTER cmd='%.80s...'", cmd ? cmd : "(null)");

    g_string_buffer[0] = '\0';
    g_string_len = 0;

    // Use shell_command API if available
    if (api.shell_command) {
        BRIDGE_LOG("bridge_exec: using shell_command API");
        char *output = NULL;
        size_t output_len = 0;

        int result = api.shell_command(cmd, &output, &output_len);
        BRIDGE_LOG("bridge_exec: shell_command returned %d, output_len=%zu",
                   result, output_len);

        if (result && output && output_len > 0) {
            size_t copy_len = (output_len < STRING_BUFFER_SIZE - 1)
                            ? output_len
                            : STRING_BUFFER_SIZE - 1;
            memcpy(g_string_buffer, output, copy_len);
            g_string_buffer[copy_len] = '\0';
            g_string_len = copy_len;
            if (api.free) api.free(output);
            BRIDGE_LOG("bridge_exec: SUCCESS via API, copied %zu bytes", copy_len);
            return 1;
        }
        if (output && api.free) api.free(output);
        BRIDGE_LOG("bridge_exec: shell_command failed, trying popen fallback");
    }

    // Fallback: use popen
    BRIDGE_LOG("bridge_exec: using popen fallback");
    char full_cmd[4096];
    snprintf(full_cmd, sizeof(full_cmd), "%s 2>/dev/null", cmd);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        BRIDGE_LOG_WARN("bridge_exec: popen FAILED");
        return 0;
    }

    size_t total = 0;
    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), fp) && total < STRING_BUFFER_SIZE - 1) {
        size_t len = strlen(chunk);
        size_t avail = STRING_BUFFER_SIZE - 1 - total;
        size_t copy = (len < avail) ? len : avail;
        memcpy(g_string_buffer + total, chunk, copy);
        total += copy;
    }
    g_string_buffer[total] = '\0';
    g_string_len = total;

    pclose(fp);
    BRIDGE_LOG("bridge_exec: popen result, total=%zu bytes", total);
    return (total > 0) ? 1 : 0;
}

// Get last string result - Ada reads this immutably
const char* bridge_get_string(size_t *out_len) {
    BRIDGE_LOG("bridge_get_string: ENTER, current len=%zu", g_string_len);
    if (out_len) *out_len = g_string_len;
    BRIDGE_LOG("bridge_get_string: EXIT returning buffer ptr=%p", (void*)g_string_buffer);
    return g_string_buffer;
}

// Get string length only
size_t bridge_get_string_length(void) {
    BRIDGE_LOG("bridge_get_string_length: returning %zu", g_string_len);
    return g_string_len;
}

// Debug checkpoint - Ada calls this to confirm it's still running
void bridge_checkpoint(const char *label) {
    BRIDGE_LOG("CHECKPOINT: %s", label ? label : "(null)");
}

// Buffer operations - pass-through to API
void* bridge_buffer_create(const char *name) {
    BRIDGE_LOG("bridge_buffer_create: ENTER name='%s'", name ? name : "(null)");
    void *bp = NULL;
    if (api.buffer_create) {
        bp = api.buffer_create(name);
    }
    BRIDGE_LOG("bridge_buffer_create: EXIT bp=%p", bp);
    return bp;
}

int bridge_buffer_switch(void *bp) {
    BRIDGE_LOG("bridge_buffer_switch: ENTER bp=%p", bp);
    int result = 0;
    if (api.buffer_switch) {
        result = api.buffer_switch(bp);
    }
    BRIDGE_LOG("bridge_buffer_switch: EXIT result=%d", result);
    return result;
}

int bridge_buffer_clear(void *bp) {
    BRIDGE_LOG("bridge_buffer_clear: ENTER bp=%p", bp);
    int result = 0;
    if (api.buffer_clear) {
        result = api.buffer_clear(bp);
    }
    BRIDGE_LOG("bridge_buffer_clear: EXIT result=%d", result);
    return result;
}

int bridge_buffer_insert(const char *text, size_t len) {
    BRIDGE_LOG("bridge_buffer_insert: ENTER len=%zu text='%.30s...'",
               len, text ? text : "(null)");
    int result = 0;
    if (api.buffer_insert) {
        result = api.buffer_insert(text, len);
    }
    BRIDGE_LOG("bridge_buffer_insert: EXIT result=%d", result);
    return result;
}

int bridge_find_file_line(const char *path, int line) {
    BRIDGE_LOG("bridge_find_file_line: ENTER path='%s' line=%d",
               path ? path : "(null)", line);
    int result = 0;
    if (api.find_file_line) {
        result = api.find_file_line(path, line);
    }
    BRIDGE_LOG("bridge_find_file_line: EXIT result=%d", result);
    return result;
}

/* ============================================================================
 * Buffer Navigation (Enter to jump to file)
 * ============================================================================ */

/* uemacs_event_t is defined in <uep/extension_api.h> */

// Check if we're in a fuzzy results buffer
static bool in_fuzzy_buffer(const char **out_name) {
    if (!api.current_buffer || !api.buffer_name) return false;

    void *bp = api.current_buffer();
    if (!bp) return false;

    const char *name = api.buffer_name(bp);
    if (!name) return false;

    if (out_name) *out_name = name;

    return (strcmp(name, "*fuzzy-find*") == 0 ||
            strcmp(name, "*fuzzy-grep*") == 0);
}

// Jump to file from current line
static bool do_fuzzy_goto(void) {
    BRIDGE_LOG("do_fuzzy_goto: ENTER");

    if (!api.get_current_line) {
        BRIDGE_LOG_WARN("do_fuzzy_goto: NO get_current_line");
        return false;
    }

    const char *bufname = NULL;
    if (!in_fuzzy_buffer(&bufname)) {
        BRIDGE_LOG("do_fuzzy_goto: not in fuzzy buffer");
        return false;
    }

    BRIDGE_LOG("do_fuzzy_goto: in buffer '%s'", bufname);

    char *line = api.get_current_line();
    if (!line || line[0] == '\0') {
        BRIDGE_LOG("do_fuzzy_goto: empty or null line");
        if (line && api.free) api.free(line);
        if (api.message) api.message("No file on this line");
        return false;
    }

    // Trim trailing whitespace/newline
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

    BRIDGE_LOG("do_fuzzy_goto: line='%.80s' len=%zu", line, len);

    if (len == 0) {
        if (api.free) api.free(line);
        if (api.message) api.message("Empty line");
        return false;
    }

    char filepath[1024] = {0};
    int line_num = 1;

    if (strcmp(bufname, "*fuzzy-find*") == 0) {
        strncpy(filepath, line, sizeof(filepath) - 1);
    } else if (strcmp(bufname, "*fuzzy-grep*") == 0) {
        char *first_colon = strchr(line, ':');
        if (first_colon) {
            size_t pathlen = first_colon - line;
            if (pathlen < sizeof(filepath)) {
                strncpy(filepath, line, pathlen);
                filepath[pathlen] = '\0';

                char *p = first_colon + 1;
                line_num = 0;
                while (*p && isdigit(*p)) {
                    line_num = line_num * 10 + (*p - '0');
                    p++;
                }
                if (line_num == 0) line_num = 1;
            }
        } else {
            strncpy(filepath, line, sizeof(filepath) - 1);
        }
    }

    if (api.free) api.free(line);

    BRIDGE_LOG("do_fuzzy_goto: filepath='%s' line_num=%d", filepath, line_num);

    if (filepath[0] == '\0') {
        if (api.message) api.message("No file path found");
        return false;
    }

    if (api.find_file_line && api.find_file_line(filepath, line_num)) {
        if (api.message) api.message("%s:%d", filepath, line_num);
        BRIDGE_LOG("do_fuzzy_goto: SUCCESS opened file");
        return true;
    } else {
        if (api.message) api.message("Failed to open: %s", filepath);
        BRIDGE_LOG_WARN("do_fuzzy_goto: FAILED to open file");
        return false;
    }
}

// Key event handler - intercept Enter in fuzzy buffers
static bool fuzzy_key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    int key = *(int *)event->data;

    if (key != '\r' && key != '\n') return false;
    if (!in_fuzzy_buffer(NULL)) return false;

    BRIDGE_LOG("fuzzy_key_handler: Enter pressed in fuzzy buffer");
    do_fuzzy_goto();
    return true;
}

/* ============================================================================
 * Extension Entry Point
 * ============================================================================ */

// Ada command implementations
extern int ada_fuzzy_find(int f, int n);
extern int ada_fuzzy_grep(int f, int n);

static int cmd_fuzzy_find(int f, int n) {
    BRIDGE_LOG("cmd_fuzzy_find: ENTER f=%d n=%d", f, n);
    BRIDGE_LOG("cmd_fuzzy_find: calling ada_fuzzy_find");
    int result = ada_fuzzy_find(f, n);
    BRIDGE_LOG("cmd_fuzzy_find: EXIT result=%d", result);
    return result;
}

static int cmd_fuzzy_grep(int f, int n) {
    BRIDGE_LOG("cmd_fuzzy_grep: ENTER f=%d n=%d", f, n);
    BRIDGE_LOG("cmd_fuzzy_grep: calling ada_fuzzy_grep");
    int result = ada_fuzzy_grep(f, n);
    BRIDGE_LOG("cmd_fuzzy_grep: EXIT result=%d", result);
    return result;
}

static int fuzzy_init_c(struct uemacs_api *editor_api) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */

    if (!editor_api->get_function) {
        fprintf(stderr, "ada_fuzzy: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.message = (message_fn)LOOKUP(message);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.shell_command = (shell_command_fn)LOOKUP(shell_command);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.alloc = (alloc_fn)LOOKUP(alloc);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_warn = (log_fn)LOOKUP(log_warn);
    api.log_debug = (log_fn)LOOKUP(log_debug);

    #undef LOOKUP

    /* Verify critical functions */
    if (!api.register_command || !api.message) {
        fprintf(stderr, "ada_fuzzy: Failed to look up critical API functions\n");
        return -1;
    }

    BRIDGE_LOG("fuzzy_init_c: ENTER api=%p", editor_api);

    /* Initialize Ada runtime - CRITICAL for secondary stack */
    BRIDGE_LOG("fuzzy_init_c: calling adainit()");
    adainit();
    BRIDGE_LOG("fuzzy_init_c: adainit() complete");

    BRIDGE_LOG("fuzzy_init_c: registering commands");
    api.register_command("fuzzy-find", cmd_fuzzy_find);
    api.register_command("fuzzy-grep", cmd_fuzzy_grep);

    // Register key handler for Enter in fuzzy buffers
    if (api.on) {
        BRIDGE_LOG("fuzzy_init_c: registering key handler");
        api.on("input:key", (event_fn_t)fuzzy_key_handler, NULL, 0);
    }

    if (api.log_info) {
        api.log_info("fuzzy_ada: Loaded (v4.2, ABI-stable)");
    }

    BRIDGE_LOG("fuzzy_init_c: EXIT success");
    return 0;
}

static void fuzzy_cleanup_c(void) {
    BRIDGE_LOG("fuzzy_cleanup_c: ENTER");

    if (api.unregister_command) {
        api.unregister_command("fuzzy-find");
        api.unregister_command("fuzzy-grep");
    }
    if (api.off) {
        api.off("input:key", (event_fn_t)fuzzy_key_handler);
    }

    /* Finalize Ada runtime */
    BRIDGE_LOG("fuzzy_cleanup_c: calling adafinal()");
    adafinal();

    BRIDGE_LOG("fuzzy_cleanup_c: EXIT");
}

static struct uemacs_extension ext = {
    .api_version = 4,
    .name = "ada_fuzzy",
    .version = "4.2.0",
    .description = "Fuzzy file finder (Ada with ABI-stable C bridge)",
    .init = fuzzy_init_c,
    .cleanup = fuzzy_cleanup_c,
};

struct uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
