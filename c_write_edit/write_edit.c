/*
 * write_edit.c - WriteEdit Prose Mode Extension for μEmacs
 *
 * API Version: 3 (Event Bus)
 *
 * Provides Word-like document editing:
 * - Soft-wrap: Visual line wrapping without hard newlines
 * - Smart typography: Real-time transforms (-- → —, smart quotes)
 * - Bullet journaling: Timestamped entries with [EARLIER:] consolidation
 *
 * Commands:
 *   WE  - Toggle write-edit mode (auto-inserts bullet entry)
 *
 * Configuration (settings.toml):
 *   [extension.c_write_edit]
 *   soft_wrap_col = 80            # Column for soft wrapping
 *   smart_typography = true       # Enable smart quotes/dashes
 *   em_dash = true                # -- becomes em-dash
 *   smart_quotes = true           # Curly double quotes
 *   curly_apostrophe = true       # Curly single quotes
 *
 * Compile: gcc -std=c23 -shared -fPIC -o c_write_edit.so write_edit.c
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <uep/extension.h>
#include <uep/extension_api.h>

static struct uemacs_api *api;

/* Settings (read from [extension.write-edit] in settings.toml) */
static int soft_wrap_col = 80;
static bool smart_typography = true;
static bool em_dash_enabled = true;
static bool smart_quotes_enabled = true;
static bool curly_apostrophe_enabled = true;

/*
 * Character insert event data structure.
 * Must match event_char_insert_t in event_bus.h exactly!
 */
typedef struct {
    int character;      /* Input: character about to be inserted (Unicode codepoint) */
    int transformed;    /* Output: transformed character (set by handler) */
    bool cancel;        /* Set true to delete previous char before inserting */
} char_insert_event_t;

/* Track which buffers have write-edit enabled */
#define MAX_WRITEEDIT_BUFFERS 32
static char *enabled_buffers[MAX_WRITEEDIT_BUFFERS];
static int enabled_buffer_count = 0;

/* Track previous character for context-aware transforms */
static int prev_char = 0;

/* Unicode codepoints for smart typography */
#define EMDASH          0x2014  /* — */
#define LEFT_DQUOTE     0x201C  /* " */
#define RIGHT_DQUOTE    0x201D  /* " */
#define RIGHT_SQUOTE    0x2019  /* ' (also apostrophe) */
#define BULLET          "●"     /* UTF-8 bullet */

/* Forward declaration */
static bool is_writeedit_enabled(void);

/*
 * Modeline callback - returns ":WE" when write-edit is active
 */
static char *writeedit_modeline_format(void *user_data) {
    (void)user_data;
    if (!is_writeedit_enabled()) return NULL;
    return api->strdup(":WE");
}

/*
 * Check if write-edit mode is enabled for current buffer
 */
static bool is_writeedit_enabled(void) {
    struct buffer *bp = api->current_buffer();
    if (!bp) return false;

    const char *name = api->buffer_name(bp);
    if (!name) return false;

    for (int i = 0; i < enabled_buffer_count; i++) {
        if (enabled_buffers[i] && strcmp(enabled_buffers[i], name) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Enable write-edit for current buffer
 */
static void enable_writeedit(const char *bufname) {
    /* Check if already enabled */
    for (int i = 0; i < enabled_buffer_count; i++) {
        if (enabled_buffers[i] && strcmp(enabled_buffers[i], bufname) == 0) {
            return;  /* Already enabled */
        }
    }

    /* Find empty slot or add new */
    for (int i = 0; i < MAX_WRITEEDIT_BUFFERS; i++) {
        if (!enabled_buffers[i]) {
            enabled_buffers[i] = api->strdup(bufname);
            if (i >= enabled_buffer_count) {
                enabled_buffer_count = i + 1;
            }
            return;
        }
    }
}

/*
 * Disable write-edit for current buffer
 */
static void disable_writeedit(const char *bufname) {
    for (int i = 0; i < enabled_buffer_count; i++) {
        if (enabled_buffers[i] && strcmp(enabled_buffers[i], bufname) == 0) {
            api->free(enabled_buffers[i]);
            enabled_buffers[i] = NULL;
            return;
        }
    }
}

/*
 * Check if character is a word boundary (for smart quote context)
 */
static bool is_word_boundary(int c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\n' ||
           c == '(' || c == '[' || c == '{' || c == '<';
}

/*
 * Event handler for character insert events (smart typography).
 * Uses the event bus API (v3).
 *
 * The event->data is a char_insert_event_t pointer:
 *   character:   The character about to be inserted
 *   transformed: Set this to the transformed character
 *   cancel:      Set true to delete previous char before inserting
 *
 * Returns true if event was handled (prevents further handlers).
 */
static bool writeedit_char_event(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    char_insert_event_t *data = (char_insert_event_t *)event->data;
    int c = data->character;

    if (!is_writeedit_enabled() || !smart_typography) {
        /* Not in write-edit mode or typography disabled - pass through */
        prev_char = c;
        return false;
    }

    /* Em dash: -- → — */
    if (em_dash_enabled && c == '-' && prev_char == '-') {
        data->transformed = EMDASH;
        data->cancel = true;   /* Delete prev '-', then insert em-dash */
        prev_char = 0;
        return true;
    }

    /* Smart double quotes */
    if (smart_quotes_enabled && c == '"') {
        data->transformed = is_word_boundary(prev_char) ? LEFT_DQUOTE : RIGHT_DQUOTE;
        data->cancel = false;  /* Just replace, don't delete prev */
        prev_char = c;
        return true;
    }

    /* Curly apostrophe: ' → ' */
    if (curly_apostrophe_enabled && c == '\'') {
        data->transformed = RIGHT_SQUOTE;
        data->cancel = false;  /* Just replace, don't delete prev */
        prev_char = c;
        return true;
    }

    /* No transformation */
    prev_char = c;
    return false;
}

/*
 * Parse date/time from bullet line: "● MM/DD/YYYY HH:MM:SS"
 * Returns true if parsed, fills date and time strings
 */
static bool parse_bullet_datetime(const char *line, char *date_out, char *time_out) {
    if (!line) return false;

    /* Check for bullet: ● is 0xE2 0x97 0x8F in UTF-8 */
    const unsigned char *p = (const unsigned char *)line;
    if (p[0] != 0xE2 || p[1] != 0x97 || p[2] != 0x8F) {
        return false;
    }
    p += 3;  /* Skip ● */
    while (*p == ' ') p++;

    /* Parse MM/DD/YYYY HH:MM:SS */
    int mon, day, year, hour, min, sec;
    if (sscanf((const char *)p, "%d/%d/%d %d:%d:%d", &mon, &day, &year, &hour, &min, &sec) == 6) {
        if (date_out) snprintf(date_out, 32, "%02d/%02d/%04d", mon, day, year);
        if (time_out) snprintf(time_out, 16, "%02d:%02d:%02d", hour, min, sec);
        return true;
    }
    return false;
}

/*
 * Insert timestamped bullet entry (internal helper)
 *
 * Format:
 *   ● MM/DD/YYYY HH:MM:SS
 *
 *   [CURSOR]
 *
 *   [EARLIER: TIME] previous content...  (if same day)
 *   OR
 *   ● PREV_DATE PREV_TIME               (if different day)
 */
static void insert_bullet_entry(void) {
    /* Get current time */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) {
        api->message("[ERROR: Could not get current time]");
        return;
    }

    char new_date[32], new_time[16];
    snprintf(new_date, sizeof(new_date), "%02d/%02d/%04d",
             tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    snprintf(new_time, sizeof(new_time), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    /* Get first line - only do bullet stuff if file already has one */
    api->set_point(1, 0);
    char *first_line = api->get_current_line();
    char old_date[32] = {0}, old_time[16] = {0};

    if (!first_line || !parse_bullet_datetime(first_line, old_date, old_time)) {
        /* No bullet on line 1 - not a journal file, skip bullet insertion */
        api->log_debug("write_edit: No bullet found, skipping date insertion");
        if (first_line) api->free(first_line);
        return;
    }

    int old_len = (int)strlen(first_line);
    bool same_day = (strcmp(new_date, old_date) == 0);
    api->log_debug("write_edit: old=%s new=%s same=%d len=%d", old_date, new_date, same_day, old_len);
    api->free(first_line);

    if (same_day) {
        /* Same day: Insert [EARLIER:] then new bullet, then delete old bullet */
        api->set_point(1, 0);
        char marker[48];
        int mlen = snprintf(marker, sizeof(marker), "[EARLIER: %s]\n\n", old_time);
        api->buffer_insert(marker, mlen);

        api->set_point(1, 0);
        char timestamp[64];
        int tlen = snprintf(timestamp, sizeof(timestamp),
                           BULLET " %s %s\n\n\n\n", new_date, new_time);
        api->buffer_insert(timestamp, tlen);

        /* Old bullet now at line 7 - delete it + following blank line */
        api->set_point(7, 0);
        api->delete_chars(old_len + 2);  /* content + newline + blank newline */
    } else {
        /* Different day - just insert new timestamp at top */
        api->set_point(1, 0);
        char timestamp[64];
        int len = snprintf(timestamp, sizeof(timestamp),
                           BULLET " %s %s\n\n\n\n", new_date, new_time);
        api->buffer_insert(timestamp, len);
    }

    /* Position cursor on line 3 */
    api->set_point(3, 0);
    api->update_display();
}

/*
 * Command: Toggle WriteEdit mode for current buffer
 * M-x write-edit
 */
static int cmd_write_edit(int f, int n) {
    (void)f;
    (void)n;

    struct buffer *bp = api->current_buffer();
    if (!bp) return 0;

    const char *bufname = api->buffer_name(bp);
    if (!bufname) return 0;

    struct window *wp = api->current_window();

    if (is_writeedit_enabled()) {
        /* Disable WriteEdit mode */
        disable_writeedit(bufname);

        /* Disable soft wrap */
        if (wp) {
            api->window_set_wrap_col(wp, 0);
        }

        prev_char = 0;
        api->message("[WRITEEDIT DISABLED]");
    } else {
        /* Enable WriteEdit mode */
        enable_writeedit(bufname);

        /* Enable soft wrap */
        if (wp) {
            api->window_set_wrap_col(wp, soft_wrap_col);
        }

        prev_char = 0;

        /* Auto-insert bullet entry */
        insert_bullet_entry();
    }

    /* Force modeline refresh for immediate indicator update */
    if (api->modeline_refresh) {
        api->modeline_refresh();
    }
    api->update_display();
    return 1;
}

/*
 * Extension init
 */
static int writeedit_init(struct uemacs_api *editor_api) {
    api = editor_api;

    if (api->api_version < 3) {
        api->log_error("write_edit: Requires API v3 (event bus)");
        return -1;
    }

    /* Read configuration from settings.toml [extension.write-edit] */
    soft_wrap_col = api->config_int("write-edit", "soft_wrap_col", 80);
    smart_typography = api->config_bool("write-edit", "smart_typography", true);
    em_dash_enabled = api->config_bool("write-edit", "em_dash", true);
    smart_quotes_enabled = api->config_bool("write-edit", "smart_quotes", true);
    curly_apostrophe_enabled = api->config_bool("write-edit", "curly_apostrophe", true);

    /* Initialize buffer tracking */
    memset(enabled_buffers, 0, sizeof(enabled_buffers));
    enabled_buffer_count = 0;
    prev_char = 0;

    /* Register command */
    if (api->register_command("WE", cmd_write_edit) != 0) {
        api->log_error("write_edit: Failed to register WE command");
        return -1;
    }

    /* Register character transform event handler via event bus */
    if (api->on(UEMACS_EVT_CHAR_INSERT, writeedit_char_event, NULL, 0) != 0) {
        api->log_error("write_edit: Failed to register char insert handler");
        api->unregister_command("write-edit");
        return -1;
    }

    /* Register modeline segment (low urgency - informational) */
    if (api->modeline_register) {
        api->modeline_register("write-edit", writeedit_modeline_format, NULL,
                               UEMACS_MODELINE_URGENCY_LOW);
    }

    api->log_info("write_edit v3.0.0 loaded (wrap=%d, typography=%s, modeline)",
                  soft_wrap_col, smart_typography ? "on" : "off");
    return 0;
}

/*
 * Extension cleanup
 */
static void writeedit_cleanup(void) {
    /* Remove event handler and command */
    api->off(UEMACS_EVT_CHAR_INSERT, writeedit_char_event);
    api->unregister_command("WE");

    /* Unregister modeline segment */
    if (api->modeline_unregister) {
        api->modeline_unregister("write-edit");
    }

    /* Free buffer name strings */
    for (int i = 0; i < MAX_WRITEEDIT_BUFFERS; i++) {
        if (enabled_buffers[i]) {
            api->free(enabled_buffers[i]);
            enabled_buffers[i] = NULL;
        }
    }
    enabled_buffer_count = 0;

    api->log_info("write_edit unloaded");
}

/* Extension descriptor */
static struct uemacs_extension writeedit_ext = {
    .api_version = 3,  /* Requires event bus API */
    .name = "c_write_edit",
    .version = "3.1.0",  /* v3.1: Bullet journaling */
    .description = "Prose editing: soft-wrap, smart typography, bullet journaling",
    .init = writeedit_init,
    .cleanup = writeedit_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &writeedit_ext;
}
