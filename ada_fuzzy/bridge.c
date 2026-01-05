/*
 * bridge.c - Ada-Friendly C Bridge for Fuzzy Finder Extension
 *
 * API Version: 4 (Ada-Friendly)
 *
 * Design: C owns all mutable buffers. Ada reads immutably.
 * - bridge_prompt() stores result internally, returns status
 * - bridge_exec() stores output internally, returns status
 * - bridge_get_string() returns pointer to internal buffer
 *
 * IMPORTANT: Must call adainit() before any Ada code and adafinal() on cleanup.
 * Without this, Ada's secondary stack (used for String returns) is uninitialized.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

/* Ada runtime initialization - MUST be called before using Ada code */
extern void adainit(void);
extern void adafinal(void);

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

/* ============================================================================
 * Logging Helper
 * ============================================================================ */

#define BRIDGE_LOG(fmt, ...) do { \
    if (g_api && g_api->log_debug) \
        g_api->log_debug("fuzzy_ada: " fmt, ##__VA_ARGS__); \
} while(0)

#define BRIDGE_LOG_WARN(fmt, ...) do { \
    if (g_api && g_api->log_warn) \
        g_api->log_warn("fuzzy_ada: " fmt, ##__VA_ARGS__); \
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
    if (g_api && g_api->message) g_api->message("%s", msg);
    BRIDGE_LOG("bridge_message: EXIT");
}

// Prompt user for input
// Returns: 1 on success (result in internal buffer), 0 on cancel/error
int bridge_prompt(const char *prompt) {
    BRIDGE_LOG("bridge_prompt: ENTER prompt='%s'", prompt ? prompt : "(null)");

    if (!g_api || !g_api->prompt) {
        BRIDGE_LOG_WARN("bridge_prompt: NO API or prompt function");
        return 0;
    }

    g_string_buffer[0] = '\0';
    g_string_len = 0;
    BRIDGE_LOG("bridge_prompt: buffer cleared, calling g_api->prompt");

    int result = g_api->prompt(prompt, g_string_buffer, sizeof(g_string_buffer));
    BRIDGE_LOG("bridge_prompt: g_api->prompt returned %d", result);

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

    if (!g_api) {
        BRIDGE_LOG_WARN("bridge_exec: NO API");
        return 0;
    }

    g_string_buffer[0] = '\0';
    g_string_len = 0;

    // Use shell_command API if available
    if (g_api->shell_command) {
        BRIDGE_LOG("bridge_exec: using shell_command API");
        char *output = NULL;
        size_t output_len = 0;

        int result = g_api->shell_command(cmd, &output, &output_len);
        BRIDGE_LOG("bridge_exec: shell_command returned %d, output_len=%zu",
                   result, output_len);

        if (result && output && output_len > 0) {
            size_t copy_len = (output_len < STRING_BUFFER_SIZE - 1)
                            ? output_len
                            : STRING_BUFFER_SIZE - 1;
            memcpy(g_string_buffer, output, copy_len);
            g_string_buffer[copy_len] = '\0';
            g_string_len = copy_len;
            g_api->free(output);
            BRIDGE_LOG("bridge_exec: SUCCESS via API, copied %zu bytes", copy_len);
            return 1;
        }
        if (output) g_api->free(output);
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
    if (g_api && g_api->buffer_create) {
        bp = g_api->buffer_create(name);
    }
    BRIDGE_LOG("bridge_buffer_create: EXIT bp=%p", bp);
    return bp;
}

int bridge_buffer_switch(void *bp) {
    BRIDGE_LOG("bridge_buffer_switch: ENTER bp=%p", bp);
    int result = 0;
    if (g_api && g_api->buffer_switch) {
        result = g_api->buffer_switch(bp);
    }
    BRIDGE_LOG("bridge_buffer_switch: EXIT result=%d", result);
    return result;
}

int bridge_buffer_clear(void *bp) {
    BRIDGE_LOG("bridge_buffer_clear: ENTER bp=%p", bp);
    int result = 0;
    if (g_api && g_api->buffer_clear) {
        result = g_api->buffer_clear(bp);
    }
    BRIDGE_LOG("bridge_buffer_clear: EXIT result=%d", result);
    return result;
}

int bridge_buffer_insert(const char *text, size_t len) {
    BRIDGE_LOG("bridge_buffer_insert: ENTER len=%zu text='%.30s...'",
               len, text ? text : "(null)");
    int result = 0;
    if (g_api && g_api->buffer_insert) {
        result = g_api->buffer_insert(text, len);
    }
    BRIDGE_LOG("bridge_buffer_insert: EXIT result=%d", result);
    return result;
}

int bridge_find_file_line(const char *path, int line) {
    BRIDGE_LOG("bridge_find_file_line: ENTER path='%s' line=%d",
               path ? path : "(null)", line);
    int result = 0;
    if (g_api && g_api->find_file_line) {
        result = g_api->find_file_line(path, line);
    }
    BRIDGE_LOG("bridge_find_file_line: EXIT result=%d", result);
    return result;
}

/* ============================================================================
 * Buffer Navigation (Enter to jump to file)
 * ============================================================================ */

// Event structure (must match uemacs_event_t)
typedef struct {
    const char *name;
    void *data;
    size_t data_size;
    bool consumed;
} uemacs_event_t;

// Check if we're in a fuzzy results buffer
static bool in_fuzzy_buffer(const char **out_name) {
    if (!g_api || !g_api->current_buffer || !g_api->buffer_name) return false;

    void *bp = g_api->current_buffer();
    if (!bp) return false;

    const char *name = g_api->buffer_name(bp);
    if (!name) return false;

    if (out_name) *out_name = name;

    return (strcmp(name, "*fuzzy-find*") == 0 ||
            strcmp(name, "*fuzzy-grep*") == 0);
}

// Jump to file from current line
static bool do_fuzzy_goto(void) {
    BRIDGE_LOG("do_fuzzy_goto: ENTER");

    if (!g_api || !g_api->get_current_line) {
        BRIDGE_LOG_WARN("do_fuzzy_goto: NO API");
        return false;
    }

    const char *bufname = NULL;
    if (!in_fuzzy_buffer(&bufname)) {
        BRIDGE_LOG("do_fuzzy_goto: not in fuzzy buffer");
        return false;
    }

    BRIDGE_LOG("do_fuzzy_goto: in buffer '%s'", bufname);

    char *line = g_api->get_current_line();
    if (!line || line[0] == '\0') {
        BRIDGE_LOG("do_fuzzy_goto: empty or null line");
        if (line) g_api->free(line);
        g_api->message("No file on this line");
        return false;
    }

    // Trim trailing whitespace/newline
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

    BRIDGE_LOG("do_fuzzy_goto: line='%.80s' len=%zu", line, len);

    if (len == 0) {
        g_api->free(line);
        g_api->message("Empty line");
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

    g_api->free(line);

    BRIDGE_LOG("do_fuzzy_goto: filepath='%s' line_num=%d", filepath, line_num);

    if (filepath[0] == '\0') {
        g_api->message("No file path found");
        return false;
    }

    if (g_api->find_file_line && g_api->find_file_line(filepath, line_num)) {
        g_api->message("%s:%d", filepath, line_num);
        BRIDGE_LOG("do_fuzzy_goto: SUCCESS opened file");
        return true;
    } else {
        g_api->message("Failed to open: %s", filepath);
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

static int fuzzy_init_c(uemacs_api *api) {
    g_api = api;

    BRIDGE_LOG("fuzzy_init_c: ENTER api=%p", (void*)api);

    /* Initialize Ada runtime - CRITICAL for secondary stack */
    BRIDGE_LOG("fuzzy_init_c: calling adainit()");
    adainit();
    BRIDGE_LOG("fuzzy_init_c: adainit() complete");

    if (g_api->register_command) {
        BRIDGE_LOG("fuzzy_init_c: registering commands");
        g_api->register_command("fuzzy-find", cmd_fuzzy_find);
        g_api->register_command("fuzzy-grep", cmd_fuzzy_grep);
    }

    // Register key handler for Enter in fuzzy buffers
    if (g_api->on) {
        BRIDGE_LOG("fuzzy_init_c: registering key handler");
        g_api->on("input:key", (event_fn_t)fuzzy_key_handler, NULL, 0);
    }

    if (g_api->log_info) {
        g_api->log_info("fuzzy_ada: Loaded (v4.1, instrumented with logging)");
    }

    BRIDGE_LOG("fuzzy_init_c: EXIT success");
    return 0;
}

static void fuzzy_cleanup_c(void) {
    BRIDGE_LOG("fuzzy_cleanup_c: ENTER");
    if (g_api) {
        if (g_api->unregister_command) {
            g_api->unregister_command("fuzzy-find");
            g_api->unregister_command("fuzzy-grep");
        }
        if (g_api->off) {
            g_api->off("input:key", (event_fn_t)fuzzy_key_handler);
        }
    }

    /* Finalize Ada runtime */
    BRIDGE_LOG("fuzzy_cleanup_c: calling adafinal()");
    adafinal();

    BRIDGE_LOG("fuzzy_cleanup_c: EXIT");
}

static uemacs_extension ext = {
    .api_version = 3,
    .name = "ada_fuzzy",
    .version = "4.1.0",
    .description = "Fuzzy file finder (Ada with instrumented C bridge)",
    .init = fuzzy_init_c,
    .cleanup = fuzzy_cleanup_c,
};

uemacs_extension* uemacs_extension_entry(void) {
    return &ext;
}
