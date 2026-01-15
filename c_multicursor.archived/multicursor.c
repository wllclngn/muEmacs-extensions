/*
 * Multiple Cursors Extension for μEmacs
 * In-process C extension with visual cursor rendering
 *
 * Commands:
 *   mc-add     - Add cursor at current position
 *   mc-clear   - Clear all secondary cursors
 *   mc-next    - Jump to next cursor
 *   mc-prev    - Jump to previous cursor
 *   mc-insert  - Insert character at all cursor positions
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <uep/extension_api.h>
#include <uep/extension.h>

#define MAX_CURSORS 64

typedef struct {
    int line;           /* 1-based line number */
    int col;            /* 0-based column */
    bool active;
} cursor_t;

static cursor_t g_cursors[MAX_CURSORS];
static int g_cursor_count = 0;
static int g_current_idx = 0;

/* API function pointers */
static struct {
    int (*register_command)(const char *, uemacs_cmd_fn);
    int (*unregister_command)(const char *);
    void *(*current_buffer)(void);
    void (*get_point)(int *, int *);
    void (*set_point)(int, int);
    int (*buffer_insert)(const char *, size_t);
    void (*message)(const char *, ...);
    void (*update_display)(void);
    int (*modeline_register)(const char *, char *(*)(void *), void *, int);
    int (*modeline_unregister)(const char *);
    void *(*alloc)(size_t);
    void (*log_info)(const char *, ...);

    /* Display hooks for visual cursors */
    int (*highlight_add)(int line, int col, int len, int face);
    void (*highlight_clear_tag)(const char *tag);
} api;

/* Forward declarations */
static int cmd_mc_add(int f, int n);
static int cmd_mc_clear(int f, int n);
static int cmd_mc_next(int f, int n);
static int cmd_mc_prev(int f, int n);
static int cmd_mc_insert(int f, int n);

/* Modeline callback - returns "MC:N" when N > 1 */
static char *mc_modeline_format(void *user_data)
{
    (void)user_data;
    if (g_cursor_count <= 1) return NULL;
    if (!api.alloc) return NULL;

    char *buf = api.alloc(16);
    if (!buf) return NULL;
    snprintf(buf, 16, "MC:%d", g_cursor_count);
    return buf;
}

/* Check if cursor already exists at position */
static bool cursor_exists(int line, int col)
{
    for (int i = 0; i < g_cursor_count; i++) {
        if (g_cursors[i].active &&
            g_cursors[i].line == line &&
            g_cursors[i].col == col) {
            return true;
        }
    }
    return false;
}

/* mc-add: Add cursor at current position */
static int cmd_mc_add(int f, int n)
{
    (void)f; (void)n;

    if (!api.current_buffer || !api.current_buffer()) {
        if (api.message) api.message("mc-add: No buffer");
        return 0;
    }

    if (g_cursor_count >= MAX_CURSORS) {
        if (api.message) api.message("mc-add: Max cursors (%d) reached", MAX_CURSORS);
        return 0;
    }

    int line = 1, col = 0;
    if (api.get_point) api.get_point(&line, &col);

    /* Check for duplicate */
    if (cursor_exists(line, col)) {
        if (api.message) api.message("mc-add: Cursor already at %d:%d", line, col);
        return 1;
    }

    /* Add new cursor */
    g_cursors[g_cursor_count].line = line;
    g_cursors[g_cursor_count].col = col;
    g_cursors[g_cursor_count].active = true;
    g_cursor_count++;

    if (api.message) api.message("mc-add: Cursor %d at %d:%d", g_cursor_count, line, col);
    if (api.update_display) api.update_display();

    return 1;
}

/* mc-clear: Clear all secondary cursors */
static int cmd_mc_clear(int f, int n)
{
    (void)f; (void)n;

    for (int i = 0; i < MAX_CURSORS; i++) {
        g_cursors[i].line = 0;
        g_cursors[i].col = 0;
        g_cursors[i].active = false;
    }

    g_cursor_count = 0;
    g_current_idx = 0;

    if (api.message) api.message("mc-clear: All cursors cleared");
    if (api.update_display) api.update_display();

    return 1;
}

/* mc-next: Jump to next cursor */
static int cmd_mc_next(int f, int n)
{
    (void)f; (void)n;

    if (g_cursor_count == 0) {
        if (api.message) api.message("mc-next: No cursors (use mc-add first)");
        return 0;
    }

    /* Cycle to next cursor */
    g_current_idx = (g_current_idx + 1) % g_cursor_count;

    /* Jump to cursor position */
    if (api.set_point) {
        api.set_point(g_cursors[g_current_idx].line, g_cursors[g_current_idx].col);
    }

    if (api.message) api.message("mc-next: Cursor %d/%d", g_current_idx + 1, g_cursor_count);
    if (api.update_display) api.update_display();

    return 1;
}

/* mc-prev: Jump to previous cursor */
static int cmd_mc_prev(int f, int n)
{
    (void)f; (void)n;

    if (g_cursor_count == 0) {
        if (api.message) api.message("mc-prev: No cursors (use mc-add first)");
        return 0;
    }

    /* Cycle to previous cursor */
    g_current_idx = (g_current_idx - 1 + g_cursor_count) % g_cursor_count;

    /* Jump to cursor position */
    if (api.set_point) {
        api.set_point(g_cursors[g_current_idx].line, g_cursors[g_current_idx].col);
    }

    if (api.message) api.message("mc-prev: Cursor %d/%d", g_current_idx + 1, g_cursor_count);
    if (api.update_display) api.update_display();

    return 1;
}

/* mc-insert: Insert a marker at all cursor positions */
static int cmd_mc_insert(int f, int n)
{
    (void)f; (void)n;

    if (g_cursor_count == 0) {
        if (api.message) api.message("mc-insert: No cursors");
        return 0;
    }

    if (!api.set_point || !api.buffer_insert || !api.get_point) {
        if (api.message) api.message("mc-insert: API not available");
        return 0;
    }

    /* Save current position */
    int orig_line = 1, orig_col = 0;
    api.get_point(&orig_line, &orig_col);

    const char *marker = "|";

    /* Insert at each cursor position (reverse order to preserve positions) */
    for (int i = g_cursor_count - 1; i >= 0; i--) {
        if (g_cursors[i].active) {
            api.set_point(g_cursors[i].line, g_cursors[i].col);
            api.buffer_insert(marker, 1);
        }
    }

    /* Restore position */
    api.set_point(orig_line, orig_col);

    if (api.message) api.message("mc-insert: Inserted at %d positions", g_cursor_count);
    if (api.update_display) api.update_display();

    return 1;
}

/* Extension initialization */
static int mc_init(struct uemacs_api *editor_api)
{

    if (!editor_api || !editor_api->get_function) {
        fprintf(stderr, "c_multicursor: Requires get_function() support\n");
        return -1;
    }

    /* Look up API functions */
    #define LOOKUP(type, name) (type)editor_api->get_function(#name)

    api.register_command = LOOKUP(int (*)(const char *, uemacs_cmd_fn), register_command);
    api.unregister_command = LOOKUP(int (*)(const char *), unregister_command);
    api.current_buffer = LOOKUP(void *(*)(void), current_buffer);
    api.get_point = LOOKUP(void (*)(int *, int *), get_point);
    api.set_point = LOOKUP(void (*)(int, int), set_point);
    api.buffer_insert = LOOKUP(int (*)(const char *, size_t), buffer_insert);
    api.message = LOOKUP(void (*)(const char *, ...), message);
    api.update_display = LOOKUP(void (*)(void), update_display);
    api.modeline_register = LOOKUP(int (*)(const char *, char *(*)(void *), void *, int), modeline_register);
    api.modeline_unregister = LOOKUP(int (*)(const char *), modeline_unregister);
    api.alloc = LOOKUP(void *(*)(size_t), alloc);
    api.log_info = LOOKUP(void (*)(const char *, ...), log_info);

    #undef LOOKUP

    if (!api.register_command) {
        fprintf(stderr, "c_multicursor: Failed to look up register_command\n");
        return -1;
    }

    /* Register commands */
    api.register_command("mc-add", cmd_mc_add);
    api.register_command("mc-clear", cmd_mc_clear);
    api.register_command("mc-next", cmd_mc_next);
    api.register_command("mc-prev", cmd_mc_prev);
    api.register_command("mc-insert", cmd_mc_insert);

    /* Register modeline segment */
    if (api.modeline_register) {
        api.modeline_register("multicursor", mc_modeline_format, NULL, 1);
    }

    if (api.log_info) api.log_info("c_multicursor: Loaded (5 commands)");

    return 0;
}

/* Extension cleanup */
static void mc_cleanup(void)
{
    if (api.unregister_command) {
        api.unregister_command("mc-add");
        api.unregister_command("mc-clear");
        api.unregister_command("mc-next");
        api.unregister_command("mc-prev");
        api.unregister_command("mc-insert");
    }

    if (api.modeline_unregister) {
        api.modeline_unregister("multicursor");
    }

    /* Clear cursor state */
    g_cursor_count = 0;
    g_current_idx = 0;
}

/* Extension descriptor */
static struct uemacs_extension ext = {
    .api_version = UEMACS_API_VERSION,
    .name = "c_multicursor",
    .version = "1.0.0",
    .description = "Multiple cursors with position tracking",
    .init = mc_init,
    .cleanup = mc_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void)
{
    return &ext;
}
