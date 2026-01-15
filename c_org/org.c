/*
 * org.c - Org-mode Extension for muEmacs
 *
 * API Version: 4 (ABI-Stable Named Lookup)
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

#define _GNU_SOURCE  /* For strcasestr */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>    /* For snprintf */
#include <string.h>
#include <strings.h>  /* For strcasecmp, strcasestr */
#include <stdlib.h>
#include <ctype.h>
#include <time.h>     /* For timestamp functions */

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
 * ABI-Stable API Access - Function pointer types
 * ============================================================================ */

typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef bool (*config_bool_fn)(const char*, const char*, bool);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct buffer *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(struct buffer*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef int (*get_line_count_fn)(struct buffer*);
typedef char *(*get_line_at_fn)(struct buffer*, int);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef char *(*get_current_line_fn)(void);
typedef int (*delete_chars_fn)(int);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef int (*prompt_yn_fn)(const char*);
typedef void (*message_fn)(const char*, ...);
typedef void (*update_display_fn)(void);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);

/* ============================================================================
 * Global State
 * ============================================================================ */

static struct {
    /* ABI-stable API function pointers */
    on_fn on;
    off_fn off;
    config_bool_fn config_bool;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_filename_fn buffer_filename;
    buffer_insert_fn buffer_insert;
    get_line_count_fn get_line_count;
    get_line_at_fn get_line_at;
    get_point_fn get_point;
    set_point_fn set_point;
    get_current_line_fn get_current_line;
    delete_chars_fn delete_chars;
    prompt_fn prompt;
    prompt_yn_fn prompt_yn;
    message_fn message;
    update_display_fn update_display;
    free_fn free;
    log_fn log_info;
    log_fn log_error;
    log_fn log_debug;

    /* State */
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
        if (!state || !state->buffer) return;

    state->fold_count = 0;

    int line_count = g_org.get_line_count(state->buffer);
    if (line_count <= 0) return;

    /* Stack to track nested headlines (for computing end_line) */
    int headline_stack[MAX_HEADLINE_LEVEL + 1];  /* Stack of fold indices */
    int level_stack[MAX_HEADLINE_LEVEL + 1];     /* Stack of headline levels */
    int stack_depth = 0;

    /* Scan all lines looking for headlines */
    for (int line = 0; line < line_count; line++) {
        /* get_line_at uses 1-based line numbers */
        char *line_text = g_org.get_line_at(state->buffer, line + 1);
        if (!line_text) continue;

        int len = (int)strlen(line_text);
        int level = org_headline_level(line_text, len);
        g_org.free(line_text);

        if (level > 0) {
            /* Found a headline - close any open folds at same or deeper level */
            while (stack_depth > 0 && level_stack[stack_depth - 1] >= level) {
                stack_depth--;
                int fold_idx = headline_stack[stack_depth];
                /* End line is the line before this headline */
                state->folds[fold_idx].end_line = line - 1;
            }

            /* Start a new fold region for this headline */
            if (state->fold_count < MAX_FOLDS) {
                int idx = state->fold_count++;
                state->folds[idx].header_line = line;  /* 0-based */
                state->folds[idx].level = level;
                state->folds[idx].folded = false;
                state->folds[idx].end_line = line_count - 1;  /* Default to buffer end */

                /* Push onto stack */
                headline_stack[stack_depth] = idx;
                level_stack[stack_depth] = level;
                stack_depth++;
            }
        }
    }

    /* Close any remaining open folds (they extend to end of buffer) */
    while (stack_depth > 0) {
        stack_depth--;
        int fold_idx = headline_stack[stack_depth];
        state->folds[fold_idx].end_line = line_count - 1;
    }

    g_org.log_debug("org: found %d fold regions in %d lines", state->fold_count, line_count);
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

/* Unfold a headline and all its ancestors */
static void org_unfold_to_line(org_buffer_state_t *state, int line_num) {
    if (!state) return;

    /* Find the fold at this line and unfold it */
    for (int i = 0; i < state->fold_count; i++) {
        org_fold_t *fold = &state->folds[i];
        if (fold->header_line == line_num) {
            fold->folded = false;
        }
        /* Also unfold any fold that contains this line */
        if (fold->header_line < line_num && fold->end_line >= line_num) {
            fold->folded = false;
        }
    }
}

/* Fold all headlines */
static void org_fold_all(org_buffer_state_t *state) {
    if (!state) return;

    for (int i = 0; i < state->fold_count; i++) {
        state->folds[i].folded = true;
    }
}

/* ============================================================================
 * Tag Parsing
 * ============================================================================ */

#define MAX_TAGS 8
#define MAX_TAG_LEN 32

/* Parse tags from a headline line.
 * Tags appear at the end of the line as :tag1:tag2:
 * Returns the character position where tags start (0 if no tags)
 */
static int org_parse_tags(const char *line, int len, char tags[MAX_TAGS][MAX_TAG_LEN], int *tag_count) {
    *tag_count = 0;

    if (len < 3) return 0;

    /* Find trailing :tag1:tag2: pattern */
    const char *end = line + len - 1;

    /* Skip trailing whitespace */
    while (end > line && (*end == ' ' || *end == '\t')) end--;

    if (*end != ':') return 0;

    /* Walk backwards to find start of tag sequence */
    const char *start = end - 1;
    while (start > line && *start != ' ' && *start != '\t') start--;

    if (start == line && *start != ' ' && *start != '\t') {
        /* Tags can't be the entire line */
        return 0;
    }
    start++;

    if (*start != ':') return 0;

    /* Now parse :tag1:tag2: */
    const char *p = start + 1;
    while (p < end && *tag_count < MAX_TAGS) {
        const char *colon = p;
        while (colon < end && *colon != ':') colon++;

        if (colon > end) break;

        int taglen = (int)(colon - p);
        if (taglen > 0 && taglen < MAX_TAG_LEN) {
            memcpy(tags[*tag_count], p, taglen);
            tags[*tag_count][taglen] = '\0';
            (*tag_count)++;
        }
        p = colon + 1;
    }

    return (int)(start - line);  /* Position where tags start */
}

/* ============================================================================
 * Timestamp Support
 * ============================================================================ */

/* Timestamp structure */
typedef struct {
    int year, month, day;
    int hour, minute;  /* -1 if not specified */
    bool active;       /* < > vs [ ] */
    bool has_time;
} org_timestamp_t;

/* Parse a timestamp string.
 * Formats: <2025-01-08 Wed> or [2025-01-08 Wed] or <2025-01-08 Wed 14:30>
 * Returns true if successfully parsed.
 */
static bool org_parse_timestamp(const char *str, org_timestamp_t *ts) {
    if (!str || !ts) return false;

    char delim = str[0];
    if (delim != '<' && delim != '[') return false;

    ts->active = (delim == '<');
    ts->has_time = false;
    ts->hour = -1;
    ts->minute = -1;

    /* Parse YYYY-MM-DD */
    if (sscanf(str + 1, "%d-%d-%d", &ts->year, &ts->month, &ts->day) != 3)
        return false;

    /* Look for time HH:MM after day name */
    const char *p = str + 12;  /* Skip <YYYY-MM-DD  */
    char end_delim = (delim == '<') ? '>' : ']';

    while (*p && *p != end_delim) {
        if (isdigit((unsigned char)*p) && p[1] && p[2] == ':') {
            if (sscanf(p, "%d:%d", &ts->hour, &ts->minute) == 2) {
                ts->has_time = true;
                break;
            }
        }
        p++;
    }

    return true;
}

/* Get day of week name (0=Sun, 1=Mon, ..., 6=Sat) */
static const char *org_day_name(int year, int month, int day) {
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    /* Zeller's congruence for Gregorian calendar */
    if (month < 3) {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    h = ((h % 7) + 7) % 7;  /* Ensure positive */
    /* Convert from Zeller (0=Sat) to standard (0=Sun) */
    int dow = (h + 6) % 7;
    return days[dow];
}

/* Format a timestamp string.
 * Buffer should be at least 32 chars.
 */
static void org_format_timestamp(org_timestamp_t *ts, char *buf, size_t buflen) {
    const char *day = org_day_name(ts->year, ts->month, ts->day);
    char open = ts->active ? '<' : '[';
    char close = ts->active ? '>' : ']';

    if (ts->has_time && ts->hour >= 0) {
        snprintf(buf, buflen, "%c%04d-%02d-%02d %s %02d:%02d%c",
                 open, ts->year, ts->month, ts->day, day,
                 ts->hour, ts->minute, close);
    } else {
        snprintf(buf, buflen, "%c%04d-%02d-%02d %s%c",
                 open, ts->year, ts->month, ts->day, day, close);
    }
}

/* Get current date/time as timestamp */
static void org_current_timestamp(org_timestamp_t *ts, bool with_time) {
    time_t now = time(nullptr);
    struct tm *tm = localtime(&now);

    ts->year = tm->tm_year + 1900;
    ts->month = tm->tm_mon + 1;
    ts->day = tm->tm_mday;
    ts->active = true;
    ts->has_time = with_time;
    if (with_time) {
        ts->hour = tm->tm_hour;
        ts->minute = tm->tm_min;
    } else {
        ts->hour = -1;
        ts->minute = -1;
    }
}

/* ============================================================================
 * Priority Support
 * ============================================================================ */

/* Get priority from headline (returns 0 if no priority, 'A'-'C' otherwise) */
static char org_get_priority(const char *line, int len, int level) {
    if (level == 0 || level >= len) return 0;

    /* Skip stars and space */
    int pos = level;
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) pos++;

    /* Skip TODO keyword if present */
    if (len - pos >= 4 && strncmp(line + pos, "TODO", 4) == 0) {
        pos += 4;
        while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    } else if (len - pos >= 4 && strncmp(line + pos, "DONE", 4) == 0) {
        pos += 4;
        while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    }

    /* Check for [#X] pattern */
    if (pos + 4 <= len &&
        line[pos] == '[' && line[pos + 1] == '#' &&
        line[pos + 3] == ']') {
        char p = line[pos + 2];
        if (p >= 'A' && p <= 'C') {
            return p;
        }
    }

    return 0;
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

    if (!event || !event->data || !g_org.current_buffer) return false;

    struct buffer *bp = (struct buffer *)event->data;
    const char *filename = g_org.buffer_filename(bp);

    if (is_org_file(filename)) {
        /* Create org state for this buffer */
        org_buffer_state_t *state = org_create_state(bp);
        if (state) {
            org_rebuild_folds(state);
            g_org.log_info("org-mode enabled for: %s", filename);
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
        if (!g_org.current_buffer) return 0;

    struct buffer *bp = g_org.current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        /* Not an org buffer - could insert tab or do something else */
        g_org.message("Not in an org-mode buffer");
        return 0;
    }

    /* Get current line */
    int line, col;
    g_org.get_point(&line, &col);

    /* Get line text */
    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    g_org.free(line_text);

    if (level == 0) {
        /* Not on headline - do nothing or insert tab */
        g_org.message("Not on a headline");
        return 0;
    }

    /* Toggle fold at this headline */
    org_toggle_fold(state, line - 1);  /* Convert 1-based to 0-based */
    g_org.update_display();

    return 1;
}

/* org-cycle-global: Shift-TAB cycles global visibility */
static int cmd_org_cycle_global(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    struct buffer *bp = g_org.current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        g_org.message("Not in an org-mode buffer");
        return 0;
    }

    /* Cycle global visibility: overview -> contents -> show-all */
    state->global_visibility = (state->global_visibility + 1) % 3;

    switch (state->global_visibility) {
        case 0:
            org_fold_to_level(state, 1);
            g_org.message("Overview");
            break;
        case 1:
            org_fold_to_level(state, 99);
            g_org.message("Contents");
            break;
        case 2:
            org_show_all(state);
            g_org.message("Show All");
            break;
    }

    g_org.update_display();
    return 1;
}

/* org-todo: Cycle TODO state on headline */
static int cmd_org_todo(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    struct buffer *bp = g_org.current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        g_org.message("Not in an org-mode buffer");
        return 0;
    }

    /* Get current line */
    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);

    if (level == 0) {
        g_org.free(line_text);
        g_org.message("Not on a headline");
        return 0;
    }

    org_todo_state_t todo = org_get_todo_state(line_text, len, level);
    g_org.free(line_text);

    /* Get current position */
    int line, col;
    g_org.get_point(&line, &col);

    /* Position after stars and space */
    int pos = level + 1;

    /* Move to position after stars */
    g_org.set_point(line, pos + 1);

    switch (todo) {
        case ORG_TODO_NONE:
            /* Insert TODO */
            g_org.buffer_insert("TODO ", 5);
            g_org.message("TODO");
            break;
        case ORG_TODO_TODO:
            /* Change to DONE - delete "TODO " and insert "DONE " */
            g_org.delete_chars(5);
            g_org.buffer_insert("DONE ", 5);
            g_org.message("DONE");
            break;
        case ORG_TODO_DONE:
            /* Remove keyword - delete "DONE " */
            g_org.delete_chars(5);
            g_org.message("");
            break;
    }

    g_org.update_display();
    return 1;
}

/* org-toggle-checkbox: Toggle checkbox on current line */
static int cmd_org_toggle_checkbox(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current line */
    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    /* Look for [ ] or [X] */
    char *unchecked = strstr(line_text, "[ ]");
    char *checked = strstr(line_text, "[X]");

    int line, col;
    g_org.get_point(&line, &col);

    if (unchecked) {
        /* Toggle to checked */
        int offset = (int)(unchecked - line_text) + 2;  /* Position of space */
        g_org.set_point(line, offset);
        g_org.delete_chars(1);
        g_org.buffer_insert("X", 1);
        g_org.message("[X]");
    } else if (checked) {
        /* Toggle to unchecked */
        int offset = (int)(checked - line_text) + 2;  /* Position of X */
        g_org.set_point(line, offset);
        g_org.delete_chars(1);
        g_org.buffer_insert(" ", 1);
        g_org.message("[ ]");
    } else {
        g_org.message("No checkbox on this line");
    }

    g_org.free(line_text);
    g_org.update_display();
    return 1;
}

/* org-insert-heading: Insert new heading at same level */
static int cmd_org_insert_heading(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current line to determine level */
    char *line_text = g_org.get_current_line();
    if (!line_text) {
        /* Default to level 1 */
        g_org.buffer_insert("\n* ", 3);
        g_org.update_display();
        return 1;
    }

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    g_org.free(line_text);

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
    g_org.get_point(&line, &col);

    /* Move to end of line first */
    char *cur_line = g_org.get_current_line();
    if (cur_line) {
        int line_len = (int)strlen(cur_line);
        g_org.set_point(line, line_len + 1);
        g_org.free(cur_line);
    }

    g_org.buffer_insert(prefix, (int)strlen(prefix));
    g_org.update_display();

    return 1;
}

/* org-promote: Decrease heading level */
static int cmd_org_promote(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    g_org.free(line_text);

    if (level <= 1) {
        g_org.message("Already at top level");
        return 0;
    }

    /* Delete one star at beginning of line */
    int line, col;
    g_org.get_point(&line, &col);
    g_org.set_point(line, 1);
    g_org.delete_chars(1);

    /* Restore column position (adjusted) */
    if (col > 1) col--;
    g_org.set_point(line, col);
    g_org.update_display();

    return 1;
}

/* org-demote: Increase heading level */
static int cmd_org_demote(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);
    g_org.free(line_text);

    if (level == 0) {
        g_org.message("Not on a headline");
        return 0;
    }

    if (level >= MAX_HEADLINE_LEVEL) {
        g_org.message("Maximum level reached");
        return 0;
    }

    /* Insert one star at beginning of line */
    int line, col;
    g_org.get_point(&line, &col);
    g_org.set_point(line, 1);
    g_org.buffer_insert("*", 1);

    /* Restore column position (adjusted) */
    g_org.set_point(line, col + 1);
    g_org.update_display();

    return 1;
}

/* org-sparse-tree: Filter view to matching headlines */
static int cmd_org_sparse_tree(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    struct buffer *bp = g_org.current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        g_org.message("Not in an org-mode buffer");
        return 0;
    }

    /* Prompt for search pattern */
    char pattern[256];
    if (g_org.prompt("Sparse tree for: ", pattern, sizeof(pattern)) != 1) {
        return 0;
    }

    if (strlen(pattern) == 0) {
        g_org.message("Empty pattern");
        return 0;
    }

    /* Fold everything first */
    org_fold_all(state);

    /* Track how many matches */
    int matches = 0;

    /* Unfold headlines matching pattern and their ancestors */
    for (int i = 0; i < state->fold_count; i++) {
        int line_num = state->folds[i].header_line;
        char *line_text = g_org.get_line_at(bp, line_num + 1);  /* 1-based */
        if (!line_text) continue;

        /* Case-insensitive search */
        if (strcasestr(line_text, pattern)) {
            org_unfold_to_line(state, line_num);
            matches++;
        }

        g_org.free(line_text);
    }

    g_org.update_display();
    g_org.message("Sparse tree: %d match%s for '%s'",
                 matches, matches == 1 ? "" : "es", pattern);

    return 1;
}

/* org-tags-sparse-tree: Filter view to headlines with specific tag */
static int cmd_org_tags_sparse_tree(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    struct buffer *bp = g_org.current_buffer();
    org_buffer_state_t *state = org_find_state(bp);

    if (!state) {
        g_org.message("Not in an org-mode buffer");
        return 0;
    }

    /* Prompt for tag */
    char tag[64];
    if (g_org.prompt("Tag: ", tag, sizeof(tag)) != 1) {
        return 0;
    }

    if (strlen(tag) == 0) {
        g_org.message("Empty tag");
        return 0;
    }

    /* Fold everything first */
    org_fold_all(state);

    /* Track matches */
    int matches = 0;

    /* Check each headline for the tag */
    for (int i = 0; i < state->fold_count; i++) {
        int line_num = state->folds[i].header_line;
        char *line_text = g_org.get_line_at(bp, line_num + 1);
        if (!line_text) continue;

        int len = (int)strlen(line_text);
        char tags[MAX_TAGS][MAX_TAG_LEN];
        int tag_count;

        org_parse_tags(line_text, len, tags, &tag_count);

        for (int t = 0; t < tag_count; t++) {
            if (strcasecmp(tags[t], tag) == 0) {
                org_unfold_to_line(state, line_num);
                matches++;
                break;
            }
        }

        g_org.free(line_text);
    }

    g_org.update_display();
    g_org.message("Tag tree: %d match%s for ':%s:'",
                 matches, matches == 1 ? "" : "es", tag);

    return 1;
}

/* org-set-tags: Set tags on current headline */
static int cmd_org_set_tags(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current line */
    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);

    if (level == 0) {
        g_org.free(line_text);
        g_org.message("Not on a headline");
        return 0;
    }

    /* Check for existing tags */
    char existing_tags[MAX_TAGS][MAX_TAG_LEN];
    int existing_count;
    int tags_pos = org_parse_tags(line_text, len, existing_tags, &existing_count);

    /* Build default value from existing tags */
    char default_val[256] = "";
    if (existing_count > 0) {
        char *p = default_val;
        for (int i = 0; i < existing_count; i++) {
            int written = snprintf(p, sizeof(default_val) - (p - default_val),
                                   "%s%s", i > 0 ? ":" : "", existing_tags[i]);
            if (written > 0) p += written;
        }
    }

    g_org.free(line_text);

    /* Prompt for new tags */
    char input[256];
    strncpy(input, default_val, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';

    g_org.message("Tags (colon-separated, e.g., work:urgent):");
    if (g_org.prompt("Tags: ", input, sizeof(input)) != 1) {
        return 0;
    }

    /* Get current position */
    int line, col;
    g_org.get_point(&line, &col);

    /* Re-read line for modification */
    line_text = g_org.get_current_line();
    if (!line_text) return 0;

    len = (int)strlen(line_text);
    tags_pos = org_parse_tags(line_text, len, existing_tags, &existing_count);

    /* Build new tags string */
    char new_tags[256];
    if (strlen(input) > 0) {
        /* Ensure format is :tag1:tag2: */
        char *p = new_tags;
        *p++ = ' ';
        *p++ = ':';

        char *tok = input;
        char *next;
        while (tok && *tok) {
            /* Skip leading colons */
            while (*tok == ':') tok++;
            if (!*tok) break;

            /* Find end of tag */
            next = strchr(tok, ':');
            int taglen = next ? (int)(next - tok) : (int)strlen(tok);

            if (taglen > 0) {
                memcpy(p, tok, taglen);
                p += taglen;
                *p++ = ':';
            }

            tok = next ? next + 1 : nullptr;
        }
        *p = '\0';
    } else {
        new_tags[0] = '\0';
    }

    /* Delete existing tags if present */
    if (tags_pos > 0) {
        /* Move to tag position and delete to end of line */
        g_org.set_point(line, tags_pos + 1);
        g_org.delete_chars(len - tags_pos);
    } else {
        /* Move to end of line */
        g_org.set_point(line, len + 1);
    }

    /* Insert new tags */
    if (strlen(new_tags) > 0) {
        g_org.buffer_insert(new_tags, (int)strlen(new_tags));
    }

    g_org.free(line_text);
    g_org.update_display();
    g_org.message("Tags set");

    return 1;
}

/* org-timestamp: Insert timestamp at point */
static int cmd_org_timestamp(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Ask if user wants time included */
    int include_time = g_org.prompt_yn("Include time?");

    org_timestamp_t ts;
    org_current_timestamp(&ts, include_time == 1);

    char buf[32];
    org_format_timestamp(&ts, buf, sizeof(buf));

    g_org.buffer_insert(buf, (int)strlen(buf));
    g_org.update_display();

    return 1;
}

/* org-schedule: Add SCHEDULED timestamp to current heading */
static int cmd_org_schedule(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current line to verify we're on/near a headline */
    int line, col;
    g_org.get_point(&line, &col);

    /* Generate today's timestamp */
    org_timestamp_t ts;
    org_current_timestamp(&ts, false);

    char ts_buf[32];
    org_format_timestamp(&ts, ts_buf, sizeof(ts_buf));

    /* Build SCHEDULED line */
    char scheduled[64];
    snprintf(scheduled, sizeof(scheduled), "\nSCHEDULED: %s", ts_buf);

    /* Move to end of current line and insert */
    char *cur_line = g_org.get_current_line();
    if (cur_line) {
        int line_len = (int)strlen(cur_line);
        g_org.set_point(line, line_len + 1);
        g_org.free(cur_line);
    }

    g_org.buffer_insert(scheduled, (int)strlen(scheduled));
    g_org.update_display();
    g_org.message("Scheduled for %s", ts_buf);

    return 1;
}

/* org-deadline: Add DEADLINE timestamp to current heading */
static int cmd_org_deadline(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current position */
    int line, col;
    g_org.get_point(&line, &col);

    /* Generate today's timestamp */
    org_timestamp_t ts;
    org_current_timestamp(&ts, false);

    char ts_buf[32];
    org_format_timestamp(&ts, ts_buf, sizeof(ts_buf));

    /* Build DEADLINE line */
    char deadline[64];
    snprintf(deadline, sizeof(deadline), "\nDEADLINE: %s", ts_buf);

    /* Move to end of current line and insert */
    char *cur_line = g_org.get_current_line();
    if (cur_line) {
        int line_len = (int)strlen(cur_line);
        g_org.set_point(line, line_len + 1);
        g_org.free(cur_line);
    }

    g_org.buffer_insert(deadline, (int)strlen(deadline));
    g_org.update_display();
    g_org.message("Deadline set for %s", ts_buf);

    return 1;
}

/* org-priority: Cycle priority on current headline */
static int cmd_org_priority(int f, int n) {
    (void)f;
    (void)n;
        if (!g_org.current_buffer) return 0;

    /* Get current line */
    char *line_text = g_org.get_current_line();
    if (!line_text) return 0;

    int len = (int)strlen(line_text);
    int level = org_headline_level(line_text, len);

    if (level == 0) {
        g_org.free(line_text);
        g_org.message("Not on a headline");
        return 0;
    }

    /* Get current priority */
    char current = org_get_priority(line_text, len, level);
    g_org.free(line_text);

    /* Find position to insert/modify priority */
    int line, col;
    g_org.get_point(&line, &col);

    /* Re-read line to find exact position */
    line_text = g_org.get_current_line();
    if (!line_text) return 0;

    len = (int)strlen(line_text);

    /* Find position after stars and space, after TODO if present */
    int pos = level;
    while (pos < len && (line_text[pos] == ' ' || line_text[pos] == '\t')) pos++;

    /* Skip TODO/DONE keyword if present */
    if (len - pos >= 4 && strncmp(line_text + pos, "TODO", 4) == 0) {
        pos += 4;
        while (pos < len && (line_text[pos] == ' ' || line_text[pos] == '\t')) pos++;
    } else if (len - pos >= 4 && strncmp(line_text + pos, "DONE", 4) == 0) {
        pos += 4;
        while (pos < len && (line_text[pos] == ' ' || line_text[pos] == '\t')) pos++;
    }

    g_org.free(line_text);

    /* Move to position */
    g_org.set_point(line, pos + 1);

    /* Cycle: none -> A -> B -> C -> none */
    if (current == 0) {
        /* Insert [#A] */
        g_org.buffer_insert("[#A] ", 5);
        g_org.message("Priority A");
    } else if (current == 'A') {
        /* Change to B */
        g_org.delete_chars(5);
        g_org.buffer_insert("[#B] ", 5);
        g_org.message("Priority B");
    } else if (current == 'B') {
        /* Change to C */
        g_org.delete_chars(5);
        g_org.buffer_insert("[#C] ", 5);
        g_org.message("Priority C");
    } else {
        /* Remove priority */
        g_org.delete_chars(5);
        g_org.message("Priority removed");
    }

    g_org.update_display();
    return 1;
}

/* ============================================================================
 * Extension Entry Point
 * ============================================================================ */

static int org_init(struct uemacs_api *editor_api) {
    /*
     * Use get_function() for ABI stability.
     */
    if (!editor_api || !editor_api->get_function) {
        fprintf(stderr, "c_org: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    g_org.on = (on_fn)LOOKUP(on);
    g_org.off = (off_fn)LOOKUP(off);
    g_org.config_bool = (config_bool_fn)LOOKUP(config_bool);
    g_org.register_command = (register_command_fn)LOOKUP(register_command);
    g_org.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    g_org.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    g_org.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    g_org.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    g_org.get_line_count = (get_line_count_fn)LOOKUP(get_line_count);
    g_org.get_line_at = (get_line_at_fn)LOOKUP(get_line_at);
    g_org.get_point = (get_point_fn)LOOKUP(get_point);
    g_org.set_point = (set_point_fn)LOOKUP(set_point);
    g_org.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    g_org.delete_chars = (delete_chars_fn)LOOKUP(delete_chars);
    g_org.prompt = (prompt_fn)LOOKUP(prompt);
    g_org.prompt_yn = (prompt_yn_fn)LOOKUP(prompt_yn);
    g_org.message = (message_fn)LOOKUP(message);
    g_org.update_display = (update_display_fn)LOOKUP(update_display);
    g_org.free = (free_fn)LOOKUP(free);
    g_org.log_info = (log_fn)LOOKUP(log_info);
    g_org.log_error = (log_fn)LOOKUP(log_error);
    g_org.log_debug = (log_fn)LOOKUP(log_debug);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!g_org.on || !g_org.register_command || !g_org.log_info) {
        fprintf(stderr, "c_org: Missing critical API functions\n");
        return -1;
    }

    /* Check if enabled in config */
    if (g_org.config_bool) {
        bool enabled = g_org.config_bool("org", "enabled", true);
        if (!enabled) {
            g_org.log_info("c_org: disabled by configuration");
            return 0;
        }
    }

    /* Register event handlers */
    if (g_org.on(UEMACS_EVT_DISPLAY_LINE, on_display_line, NULL, 0) != 0) {
        g_org.log_error("c_org: failed to register display:line handler");
        return -1;
    }

    if (g_org.on(UEMACS_EVT_BUFFER_LOAD, on_buffer_load, NULL, 0) != 0) {
        g_org.log_error("c_org: failed to register buffer:load handler");
        return -1;
    }

    /* Register commands */
    g_org.register_command("org-cycle", cmd_org_cycle);
    g_org.register_command("org-cycle-global", cmd_org_cycle_global);
    g_org.register_command("org-todo", cmd_org_todo);
    g_org.register_command("org-toggle-checkbox", cmd_org_toggle_checkbox);
    g_org.register_command("org-insert-heading", cmd_org_insert_heading);
    g_org.register_command("org-promote", cmd_org_promote);
    g_org.register_command("org-demote", cmd_org_demote);
    g_org.register_command("org-sparse-tree", cmd_org_sparse_tree);
    g_org.register_command("org-tags-sparse-tree", cmd_org_tags_sparse_tree);
    g_org.register_command("org-set-tags", cmd_org_set_tags);
    g_org.register_command("org-timestamp", cmd_org_timestamp);
    g_org.register_command("org-schedule", cmd_org_schedule);
    g_org.register_command("org-deadline", cmd_org_deadline);
    g_org.register_command("org-priority", cmd_org_priority);

    g_org.initialized = true;

    g_org.log_info("c_org v4.0.0 loaded (ABI-stable, Org-mode outlining)");

    return 0;
}

static void org_cleanup(void) {
    if (g_org.initialized) {
        /* Unregister event handlers */
        if (g_org.off) {
            g_org.off(UEMACS_EVT_DISPLAY_LINE, on_display_line);
            g_org.off(UEMACS_EVT_BUFFER_LOAD, on_buffer_load);
        }

        /* Unregister commands */
        if (g_org.unregister_command) {
            g_org.unregister_command("org-cycle");
            g_org.unregister_command("org-cycle-global");
            g_org.unregister_command("org-todo");
            g_org.unregister_command("org-toggle-checkbox");
            g_org.unregister_command("org-insert-heading");
            g_org.unregister_command("org-promote");
            g_org.unregister_command("org-demote");
            g_org.unregister_command("org-sparse-tree");
            g_org.unregister_command("org-tags-sparse-tree");
            g_org.unregister_command("org-set-tags");
            g_org.unregister_command("org-timestamp");
            g_org.unregister_command("org-schedule");
            g_org.unregister_command("org-deadline");
            g_org.unregister_command("org-priority");
        }

        /* Free all buffer states */
        for (int i = 0; i < g_org.state_count; i++) {
            free(g_org.states[i]);
        }
        g_org.state_count = 0;

        if (g_org.log_info) {
            g_org.log_info("c_org: extension unloaded");
        }
    }
    g_org.initialized = false;
}

/* Extension descriptor */
static struct uemacs_extension org_ext = {
    .api_version = 4  /* ABI-stable API */,
    .name = "c_org",
    .version = "4.0.0",
    .description = "Org-mode outlining and task management (ABI-stable)",
    .init = org_init,
    .cleanup = org_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &org_ext;
}
