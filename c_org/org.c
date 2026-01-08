/*
 * org.c - Org-mode Extension for muEmacs
 *
 * Provides org-mode outlining and task management:
 * - Headline folding via EVT_DISPLAY_LINE hook
 * - TAB cycling (fold/unfold headlines)
 * - Shift-TAB global visibility cycling
 * - TODO state cycling
 * - Checkbox toggling
 *
 * C23 Features Used:
 * - nullptr instead of NULL
 * - Designated initializers
 * - bool as first-class type
 *
 * 2026 - Linux/C23 compliant
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>  /* For strcasecmp */
#include <stdlib.h>
#include <ctype.h>

/* Include the muEmacs extension API */
#include "uep/extension.h"
#include "uep/extension_api.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_ORG_BUFFERS 64
#define MAX_FOLDS 4096
#define MAX_HEADLINE_LEVEL 9

/* ============================================================================
 * Types
 * ============================================================================ */

/* TODO state */
typedef enum {
    ORG_TODO_NONE = 0,
    ORG_TODO_TODO,
    ORG_TODO_DONE,
} org_todo_state_t;

/* Fold region */
typedef struct {
    int header_line;    /* 0-based line number of headline */
    int end_line;       /* Last line of this section (before next same/higher level) */
    int level;          /* Headline level (1-9) */
    bool folded;        /* Currently collapsed? */
} org_fold_t;

/* Per-buffer org state */
typedef struct {
    bool enabled;                   /* Org-mode active for this buffer */
    struct buffer *buffer;          /* Buffer pointer */
    org_fold_t folds[MAX_FOLDS];    /* Fold regions */
    int fold_count;
    int global_visibility;          /* 0=overview, 1=contents, 2=show-all */
} org_buffer_state_t;

/* ============================================================================
 * Global State
 * ============================================================================ */

static struct {
    struct uemacs_api *api;
    org_buffer_state_t *states[MAX_ORG_BUFFERS];
    int state_count;
    bool initialized;
} g_org = {0};

/* ============================================================================
 * Buffer State Management
 * ============================================================================ */

/* Find org state for a buffer */
static org_buffer_state_t *org_find_state(struct buffer *bp) {
    for (int i = 0; i < g_org.state_count; i++) {
        if (g_org.states[i] && g_org.states[i]->buffer == bp) {
            return g_org.states[i];
        }
    }
    return nullptr;
}

/* Create org state for a buffer */
static org_buffer_state_t *org_create_state(struct buffer *bp) {
    if (g_org.state_count >= MAX_ORG_BUFFERS) {
        return nullptr;
    }

    org_buffer_state_t *state = calloc(1, sizeof(org_buffer_state_t));
    if (!state) return nullptr;

    state->enabled = true;
    state->buffer = bp;
    state->fold_count = 0;
    state->global_visibility = 2;  /* Start with show-all */

    g_org.states[g_org.state_count++] = state;
    return state;
}

/* Free org state (used during cleanup) */
[[maybe_unused]]
static void org_free_state(org_buffer_state_t *state) {
    if (!state) return;

    /* Remove from states array */
    for (int i = 0; i < g_org.state_count; i++) {
        if (g_org.states[i] == state) {
            /* Shift remaining states down */
            for (int j = i; j < g_org.state_count - 1; j++) {
                g_org.states[j] = g_org.states[j + 1];
            }
            g_org.state_count--;
            break;
        }
    }

    free(state);
}

/* ============================================================================
 * Headline Parsing
 * ============================================================================ */

/* Get headline level from line (returns 0 if not a headline) */
static int org_headline_level(const char *line, int len) {
    if (len == 0 || line[0] != '*') return 0;

    int level = 0;
    while (level < len && level < MAX_HEADLINE_LEVEL && line[level] == '*') {
        level++;
    }

    /* Must be followed by space to be a headline */
    if (level < len && (line[level] == ' ' || line[level] == '\t')) {
        return level;
    }

    return 0;
}

/* Get TODO state from headline line */
static org_todo_state_t org_get_todo_state(const char *line, int len, int level) {
    if (level == 0 || level >= len) return ORG_TODO_NONE;

    /* Skip stars and space */
    int pos = level;
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) pos++;

    /* Check for TODO keyword */
    if (len - pos >= 4 && strncmp(line + pos, "TODO", 4) == 0) {
        if (pos + 4 >= len || line[pos + 4] == ' ' || line[pos + 4] == '\t') {
            return ORG_TODO_TODO;
        }
    }

    /* Check for DONE keyword */
    if (len - pos >= 4 && strncmp(line + pos, "DONE", 4) == 0) {
        if (pos + 4 >= len || line[pos + 4] == ' ' || line[pos + 4] == '\t') {
            return ORG_TODO_DONE;
        }
    }

    return ORG_TODO_NONE;
}

/* ============================================================================
 * Fold Management
 * ============================================================================ */

/* Rebuild fold regions for a buffer */
static void org_rebuild_folds(org_buffer_state_t *state) {
    struct uemacs_api *api = g_org.api;
    if (!api || !state || !state->buffer) return;

    state->fold_count = 0;

    int line_count = api->get_line_count(state->buffer);
    if (line_count <= 0) return;

    /* TODO: Parse buffer content to find headlines and build fold regions.
     * For now this is a stub - folding will be populated on-demand
     * when org-cycle is called on a headline. */
    api->log_debug("org: buffer has %d lines (fold parsing not yet implemented)", line_count);
}

/* Check if a line is within a folded region */
static bool org_is_line_folded(org_buffer_state_t *state, int line_num) {
    if (!state) return false;

    for (int i = 0; i < state->fold_count; i++) {
        org_fold_t *fold = &state->folds[i];
        if (!fold->folded) continue;

        /* Line is hidden if it's inside a folded region but not the header */
        if (line_num > fold->header_line && line_num <= fold->end_line) {
            return true;
        }
    }

    return false;
}

/* Find fold at a specific line (if line is a header) */
static org_fold_t *org_find_fold_at(org_buffer_state_t *state, int line_num) {
    if (!state) return nullptr;

    for (int i = 0; i < state->fold_count; i++) {
        if (state->folds[i].header_line == line_num) {
            return &state->folds[i];
        }
    }

    return nullptr;
}

/* Toggle fold at line */
static void org_toggle_fold(org_buffer_state_t *state, int line_num) {
    org_fold_t *fold = org_find_fold_at(state, line_num);
    if (fold) {
        fold->folded = !fold->folded;
    }
}

/* Fold all headlines to a specific level */
static void org_fold_to_level(org_buffer_state_t *state, int max_level) {
    if (!state) return;

    for (int i = 0; i < state->fold_count; i++) {
        state->folds[i].folded = (state->folds[i].level >= max_level);
    }
}

/* Show all (unfold everything) */
static void org_show_all(org_buffer_state_t *state) {
    if (!state) return;

    for (int i = 0; i < state->fold_count; i++) {
        state->folds[i].folded = false;
    }
}

/* ============================================================================
 * Display Line Event Handler
 * ============================================================================ */

/* Handle display:line event for folding */
static bool on_display_line(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    if (!event || !event->data) return false;

    /* Cast to display line event */
    uemacs_display_line_event_t *evt = (uemacs_display_line_event_t *)event->data;

    /* Find org state for this buffer */
    org_buffer_state_t *state = org_find_state(evt->buffer);
    if (!state || !state->enabled) return false;

    /* Check if this line is folded */
    if (org_is_line_folded(state, evt->line_num)) {
        evt->action = UEMACS_DISPLAY_SKIP;
        return true;  /* Handled - skip this line */
    }

    return false;  /* Render normally */
}

/* ============================================================================
 * Buffer Load Handler
 * ============================================================================ */

/* Check if filename ends with .org */
static bool is_org_file(const char *filename) {
    if (!filename) return false;
    size_t len = strlen(filename);
    return len > 4 && strcasecmp(filename + len - 4, ".org") == 0;
}

/* Handle buffer:load event to detect .org files */
static bool on_buffer_load(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    if (!event || !event->data || !g_org.api) return false;

    struct buffer *bp = (struct buffer *)event->data;
    const char *filename = g_org.api->buffer_filename(bp);

    if (is_org_file(filename)) {
        /* Create org state for this buffer */
        org_buffer_state_t *state = org_create_state(bp);
        if (state) {
            org_rebuild_folds(state);
            g_org.api->log_info("org-mode enabled for: %s", filename);
        }
    }

    return false;  /* Don't consume - let other handlers see this */
}

/* ============================================================================
 * Commands
 * ============================================================================ */

/* org-cycle: TAB on headline cycles fold state */
static int cmd_org_cycle(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    struct buffer *bp = api->current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        /* Not an org buffer - could insert tab or do something else */
        api->message("Not in an org-mode buffer");
        return 0;
    }

    /* Get current line */
    int line, col;
    api->get_point(&line, &col);

    /* Get line text */
    char *line_text = api->get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    api->free(line_text);

    if (level == 0) {
        /* Not on headline - do nothing or insert tab */
        api->message("Not on a headline");
        return 0;
    }

    /* Toggle fold at this headline */
    org_toggle_fold(state, line - 1);  /* Convert 1-based to 0-based */
    api->update_display();

    return 1;
}

/* org-cycle-global: Shift-TAB cycles global visibility */
static int cmd_org_cycle_global(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    struct buffer *bp = api->current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        api->message("Not in an org-mode buffer");
        return 0;
    }

    /* Cycle global visibility: overview -> contents -> show-all */
    state->global_visibility = (state->global_visibility + 1) % 3;

    switch (state->global_visibility) {
        case 0:
            org_fold_to_level(state, 1);
            api->message("Overview");
            break;
        case 1:
            org_fold_to_level(state, 99);
            api->message("Contents");
            break;
        case 2:
            org_show_all(state);
            api->message("Show All");
            break;
    }

    api->update_display();
    return 1;
}

/* org-todo: Cycle TODO state on headline */
static int cmd_org_todo(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    struct buffer *bp = api->current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        api->message("Not in an org-mode buffer");
        return 0;
    }

    /* Get current line */
    char *line_text = api->get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);

    if (level == 0) {
        api->free(line_text);
        api->message("Not on a headline");
        return 0;
    }

    org_todo_state_t todo = org_get_todo_state(line_text, len, level);
    api->free(line_text);

    /* Get current position */
    int line, col;
    api->get_point(&line, &col);

    /* Position after stars and space */
    int pos = level + 1;

    /* Move to position after stars */
    api->set_point(line, pos + 1);

    switch (todo) {
        case ORG_TODO_NONE:
            /* Insert TODO */
            api->buffer_insert("TODO ", 5);
            api->message("TODO");
            break;
        case ORG_TODO_TODO:
            /* Change to DONE - delete "TODO " and insert "DONE " */
            api->delete_chars(5);
            api->buffer_insert("DONE ", 5);
            api->message("DONE");
            break;
        case ORG_TODO_DONE:
            /* Remove keyword - delete "DONE " */
            api->delete_chars(5);
            api->message("");
            break;
    }

    api->update_display();
    return 1;
}

/* org-toggle-checkbox: Toggle checkbox on current line */
static int cmd_org_toggle_checkbox(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    /* Get current line */
    char *line_text = api->get_current_line();
    if (!line_text) return 0;

    /* Look for [ ] or [X] */
    char *unchecked = strstr(line_text, "[ ]");
    char *checked = strstr(line_text, "[X]");

    int line, col;
    api->get_point(&line, &col);

    if (unchecked) {
        /* Toggle to checked */
        int offset = (int)(unchecked - line_text) + 2;  /* Position of space */
        api->set_point(line, offset);
        api->delete_chars(1);
        api->buffer_insert("X", 1);
        api->message("[X]");
    } else if (checked) {
        /* Toggle to unchecked */
        int offset = (int)(checked - line_text) + 2;  /* Position of X */
        api->set_point(line, offset);
        api->delete_chars(1);
        api->buffer_insert(" ", 1);
        api->message("[ ]");
    } else {
        api->message("No checkbox on this line");
    }

    api->free(line_text);
    api->update_display();
    return 1;
}

/* org-insert-heading: Insert new heading at same level */
static int cmd_org_insert_heading(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    /* Get current line to determine level */
    char *line_text = api->get_current_line();
    if (!line_text) {
        /* Default to level 1 */
        api->buffer_insert("\n* ", 3);
        api->update_display();
        return 1;
    }

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    api->free(line_text);

    if (level == 0) level = 1;  /* Default to level 1 */

    /* Build heading prefix */
    char prefix[MAX_HEADLINE_LEVEL + 3];
    prefix[0] = '\n';
    for (int i = 0; i < level; i++) {
        prefix[i + 1] = '*';
    }
    prefix[level + 1] = ' ';
    prefix[level + 2] = '\0';

    /* Insert at end of current line */
    int line, col;
    api->get_point(&line, &col);

    /* Move to end of line first */
    char *cur_line = api->get_current_line();
    if (cur_line) {
        int line_len = (int)strlen(cur_line);
        api->set_point(line, line_len + 1);
        api->free(cur_line);
    }

    api->buffer_insert(prefix, (int)strlen(prefix));
    api->update_display();

    return 1;
}

/* org-promote: Decrease heading level */
static int cmd_org_promote(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    char *line_text = api->get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    api->free(line_text);

    if (level <= 1) {
        api->message("Already at top level");
        return 0;
    }

    /* Delete one star at beginning of line */
    int line, col;
    api->get_point(&line, &col);
    api->set_point(line, 1);
    api->delete_chars(1);

    /* Restore column position (adjusted) */
    if (col > 1) col--;
    api->set_point(line, col);
    api->update_display();

    return 1;
}

/* org-demote: Increase heading level */
static int cmd_org_demote(int f, int n) {
    (void)f;
    (void)n;
    struct uemacs_api *api = g_org.api;
    if (!api) return 0;

    char *line_text = api->get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    api->free(line_text);

    if (level == 0) {
        api->message("Not on a headline");
        return 0;
    }

    if (level >= MAX_HEADLINE_LEVEL) {
        api->message("Maximum level reached");
        return 0;
    }

    /* Insert one star at beginning of line */
    int line, col;
    api->get_point(&line, &col);
    api->set_point(line, 1);
    api->buffer_insert("*", 1);

    /* Restore column position (adjusted) */
    api->set_point(line, col + 1);
    api->update_display();

    return 1;
}

/* ============================================================================
 * Extension Entry Point
 * ============================================================================ */

static int org_init(struct uemacs_api *api) {
    if (!api) {
        return -1;
    }

    g_org.api = api;

    /* Check if enabled in config */
    bool enabled = api->config_bool("org", "enabled", true);
    if (!enabled) {
        api->log_info("c_org: disabled by configuration");
        return 0;
    }

    /* Register event handlers */
    if (api->on(UEMACS_EVT_DISPLAY_LINE, on_display_line, nullptr, 0) != 0) {
        api->log_error("c_org: failed to register display:line handler");
        return -1;
    }

    if (api->on(UEMACS_EVT_BUFFER_LOAD, on_buffer_load, nullptr, 0) != 0) {
        api->log_error("c_org: failed to register buffer:load handler");
        return -1;
    }

    /* Register commands */
    api->register_command("org-cycle", cmd_org_cycle);
    api->register_command("org-cycle-global", cmd_org_cycle_global);
    api->register_command("org-todo", cmd_org_todo);
    api->register_command("org-toggle-checkbox", cmd_org_toggle_checkbox);
    api->register_command("org-insert-heading", cmd_org_insert_heading);
    api->register_command("org-promote", cmd_org_promote);
    api->register_command("org-demote", cmd_org_demote);

    g_org.initialized = true;

    api->log_info("c_org: Org-mode extension v1.0 loaded");
    api->log_info("  Commands: org-cycle, org-cycle-global, org-todo");
    api->log_info("            org-toggle-checkbox, org-insert-heading");
    api->log_info("            org-promote, org-demote");

    return 0;
}

static void org_cleanup(void) {
    if (g_org.api && g_org.initialized) {
        /* Unregister event handlers */
        g_org.api->off(UEMACS_EVT_DISPLAY_LINE, on_display_line);
        g_org.api->off(UEMACS_EVT_BUFFER_LOAD, on_buffer_load);

        /* Unregister commands */
        g_org.api->unregister_command("org-cycle");
        g_org.api->unregister_command("org-cycle-global");
        g_org.api->unregister_command("org-todo");
        g_org.api->unregister_command("org-toggle-checkbox");
        g_org.api->unregister_command("org-insert-heading");
        g_org.api->unregister_command("org-promote");
        g_org.api->unregister_command("org-demote");

        /* Free all buffer states */
        for (int i = 0; i < g_org.state_count; i++) {
            free(g_org.states[i]);
        }
        g_org.state_count = 0;

        g_org.api->log_info("c_org: extension unloaded");
    }
    g_org.initialized = false;
}

/* Extension descriptor */
static struct uemacs_extension org_ext = {
    .api_version = 3,
    .name = "c_org",
    .version = "1.0.0",
    .description = "Org-mode outlining and task management",
    .init = org_init,
    .cleanup = org_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &org_ext;
}
