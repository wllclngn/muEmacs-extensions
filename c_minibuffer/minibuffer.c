/*
 * minibuffer.c - Modern Completion Framework for μEmacs
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Replaces the default minibuffer with a buffer-based completion UI.
 * When loaded, intercepts switch-buffer, find-file, execute-extended-command
 * to provide live filtering, fuzzy matching, and visual candidate selection.
 *
 * Commands:
 *   M-x switch-buffer    - Buffer picker (shadows built-in)
 *   M-x pick-cancel      - Cancel current pick operation
 *
 * Configuration (settings.toml):
 *   [extension.c_minibuffer]
 *   max_candidates = 15
 *   modified_indicator = "Δ"    # Symbol shown for modified buffers
 *
 * Compile: gcc -std=c23 -shared -fPIC -o c_minibuffer.so minibuffer.c
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include <uep/extension.h>
#include <uep/extension_api.h>

/* Function pointer types for the API functions we use */
typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef int (*config_int_fn)(const char*, const char*, int);
typedef bool (*config_bool_fn)(const char*, const char*, bool);
typedef const char *(*config_string_fn)(const char*, const char*, const char*);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct buffer *(*current_buffer_fn)(void);
typedef struct buffer *(*find_buffer_fn)(const char*);
typedef struct buffer *(*buffer_first_fn)(void);
typedef struct buffer *(*buffer_next_fn)(struct buffer*);
typedef const char *(*buffer_filename_fn)(struct buffer*);
typedef const char *(*buffer_name_fn)(struct buffer*);
typedef bool (*buffer_modified_fn)(struct buffer*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef struct buffer *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(struct buffer*);
typedef int (*buffer_clear_fn)(struct buffer*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef void (*message_fn)(const char*, ...);
typedef void (*update_display_fn)(void);
typedef void (*log_fn)(const char*, ...);

/* Our local API - only the functions we actually use */
static struct {
    on_fn on;
    off_fn off;
    config_int_fn config_int;
    config_bool_fn config_bool;
    config_string_fn config_string;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    find_buffer_fn find_buffer;
    buffer_first_fn buffer_first;
    buffer_next_fn buffer_next;
    buffer_filename_fn buffer_filename;
    buffer_name_fn buffer_name;
    buffer_modified_fn buffer_modified;
    buffer_insert_fn buffer_insert;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    get_point_fn get_point;
    set_point_fn set_point;
    message_fn message;
    update_display_fn update_display;
    log_fn log_info;
    log_fn log_error;
    log_fn log_debug;
} api;

/* Picker buffer name */
#define PICK_BUFFER "*pick*"

/* Maximum candidates to show */
#define MAX_CANDIDATES 64
#define MAX_FILTER_LEN 256

/* Picker state */
typedef enum {
    PICK_INACTIVE,
    PICK_BUFFER_SWITCH,
    PICK_FILE_FIND,
    PICK_COMMAND
} pick_mode_t;

typedef struct {
    char name[256];
    char display[512];
    bool modified;
    void *data;  /* Pointer to actual buffer/file/command */
} candidate_t;

static struct {
    pick_mode_t mode;
    struct buffer *prev_buffer;     /* Buffer to return to on cancel */
    struct buffer *pick_buffer;     /* The *pick* buffer */

    candidate_t candidates[MAX_CANDIDATES];
    int candidate_count;
    int selected_index;

    char filter[MAX_FILTER_LEN];
    int filter_len;

    int max_visible;
    char modified_indicator[16];    /* Symbol for modified buffers (default: Δ) */
} picker;

/* Forward declarations */
static int cmd_switch_buffer(int f, int n);
static int cmd_pick_cancel(int f, int n);
static bool key_handler(uemacs_event_t *event, void *user_data);
static void picker_refresh(void);
static void picker_populate_buffers(void);
static void picker_select(void);
static void picker_cancel(void);

/* ============================================================================
 * Extension Entry Points
 * ============================================================================ */

static int minibuffer_init(struct uemacs_api *uapi) {
    if (!uapi) {
        fprintf(stderr, "c_minibuffer: NULL API pointer\n");
        return -1;
    }

    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    typedef void (*generic_fn)(void);
    generic_fn (*get_fn)(const char*) = (generic_fn(*)(const char*))uapi->get_function;

    if (!get_fn) {
        fprintf(stderr, "c_minibuffer: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) get_fn(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.config_int = (config_int_fn)LOOKUP(config_int);
    api.config_bool = (config_bool_fn)LOOKUP(config_bool);
    api.config_string = (config_string_fn)LOOKUP(config_string);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.find_buffer = (find_buffer_fn)LOOKUP(find_buffer);
    api.buffer_first = (buffer_first_fn)LOOKUP(buffer_first);
    api.buffer_next = (buffer_next_fn)LOOKUP(buffer_next);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_modified = (buffer_modified_fn)LOOKUP(buffer_modified);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.message = (message_fn)LOOKUP(message);
    api.update_display = (update_display_fn)LOOKUP(update_display);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.log_debug = (log_fn)LOOKUP(log_debug);

    #undef LOOKUP

    /* Verify required functions are available */
    if (!api.register_command || !api.buffer_first || !api.buffer_next) {
        if (api.log_error) {
            api.log_error("c_minibuffer: Missing required API functions");
        }
        return -1;
    }

    /* Initialize picker state */
    memset(&picker, 0, sizeof(picker));
    picker.mode = PICK_INACTIVE;
    picker.max_visible = api.config_int ?
        api.config_int("c_minibuffer", "max_candidates", 15) : 15;

    /* Read modified indicator from config (default: Δ to match modeline) */
    const char *mod_ind = api.config_string ?
        api.config_string("c_minibuffer", "modified_indicator", "Δ") : "Δ";
    strncpy(picker.modified_indicator, mod_ind, sizeof(picker.modified_indicator) - 1);
    picker.modified_indicator[sizeof(picker.modified_indicator) - 1] = '\0';

    /* Register commands - shadows built-in switch-buffer */
    api.register_command("switch-buffer", cmd_switch_buffer);
    api.register_command("pick-cancel", cmd_pick_cancel);

    /* Register key handler */
    if (api.on) {
        api.on("input:key", key_handler, NULL, 10);  /* High priority */
    }

    if (api.log_info) {
        api.log_info("c_minibuffer: Loaded (v4.0, ABI-stable)");
    }

    return 0;
}

static void minibuffer_cleanup(void) {
    /* Cancel any active pick */
    if (picker.mode != PICK_INACTIVE) {
        picker_cancel();
    }

    /* Unregister key handler */
    if (api.off) {
        api.off("input:key", key_handler);
    }

    /* Unregister commands */
    if (api.unregister_command) {
        api.unregister_command("switch-buffer");
        api.unregister_command("pick-cancel");
    }

    if (api.log_info) {
        api.log_info("c_minibuffer: Unloaded");
    }
}

/* Extension descriptor */
static struct uemacs_extension ext = {
    .api_version = 4,  /* ABI-stable API */
    .name = "c_minibuffer",
    .version = "4.0.0",  /* v4: ABI-stable migration */
    .description = "Modern completion framework with buffer-based picker",
    .init = minibuffer_init,
    .cleanup = minibuffer_cleanup
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &ext;
}

/* ============================================================================
 * Picker Logic
 * ============================================================================ */

/* Simple substring match - returns true if filter is found in str */
static bool filter_match(const char *str, const char *filter) {
    if (!filter || !*filter) return true;
    if (!str) return false;

    /* Case-insensitive substring search */
    size_t flen = strlen(filter);
    size_t slen = strlen(str);

    if (flen > slen) return false;

    for (size_t i = 0; i <= slen - flen; i++) {
        bool match = true;
        for (size_t j = 0; j < flen; j++) {
            if (tolower((unsigned char)str[i + j]) !=
                tolower((unsigned char)filter[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/* Populate candidates with buffer list */
static void picker_populate_buffers(void) {
    picker.candidate_count = 0;

    if (!api.buffer_first || !api.buffer_next) return;

    struct buffer *bp = api.buffer_first();
    while (bp && picker.candidate_count < MAX_CANDIDATES) {
        const char *name = api.buffer_name(bp);
        if (!name) {
            bp = api.buffer_next(bp);
            continue;
        }

        /* Skip the pick buffer itself */
        if (strcmp(name, PICK_BUFFER) == 0) {
            bp = api.buffer_next(bp);
            continue;
        }

        /* Apply filter */
        if (!filter_match(name, picker.filter)) {
            bp = api.buffer_next(bp);
            continue;
        }

        candidate_t *c = &picker.candidates[picker.candidate_count];
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        c->modified = api.buffer_modified ? api.buffer_modified(bp) : false;
        c->data = bp;

        /* Format display string */
        const char *fname = api.buffer_filename ? api.buffer_filename(bp) : NULL;
        if (fname && *fname) {
            snprintf(c->display, sizeof(c->display), "  %s%s%s  %s",
                     c->name,
                     c->modified ? " " : "",
                     c->modified ? picker.modified_indicator : "",
                     fname);
        } else {
            snprintf(c->display, sizeof(c->display), "  %s%s%s",
                     c->name,
                     c->modified ? " " : "",
                     c->modified ? picker.modified_indicator : "");
        }

        picker.candidate_count++;
        bp = api.buffer_next(bp);
    }

    /* Clamp selected index */
    if (picker.selected_index >= picker.candidate_count) {
        picker.selected_index = picker.candidate_count > 0 ?
            picker.candidate_count - 1 : 0;
    }
}

/* Refresh the picker buffer display */
static void picker_refresh(void) {
    if (!picker.pick_buffer || !api.buffer_switch || !api.buffer_clear) {
        return;
    }

    /* Repopulate based on current filter */
    if (picker.mode == PICK_BUFFER_SWITCH) {
        picker_populate_buffers();
    }

    /* Switch to pick buffer and clear it */
    api.buffer_switch(picker.pick_buffer);
    api.buffer_clear(picker.pick_buffer);

    /* Build display content */
    char content[8192];
    int pos = 0;

    /* Header line with prompt and filter */
    const char *prompt = "Switch buffer";
    if (picker.mode == PICK_FILE_FIND) prompt = "Find file";
    else if (picker.mode == PICK_COMMAND) prompt = "M-x";

    pos += snprintf(content + pos, sizeof(content) - pos,
                    "%s: %s\n", prompt, picker.filter);

    /* Separator */
    pos += snprintf(content + pos, sizeof(content) - pos,
                    "────────────────────────────────────────\n");

    /* Candidates */
    int visible = picker.candidate_count < picker.max_visible ?
                  picker.candidate_count : picker.max_visible;

    for (int i = 0; i < visible && i < picker.candidate_count; i++) {
        candidate_t *c = &picker.candidates[i];

        /* Mark selected candidate */
        if (i == picker.selected_index) {
            content[pos++] = '>';
            /* Replace first space with nothing to maintain alignment */
            pos += snprintf(content + pos, sizeof(content) - pos,
                           "%s\n", c->display + 1);
        } else {
            pos += snprintf(content + pos, sizeof(content) - pos,
                           "%s\n", c->display);
        }
    }

    /* Footer with count */
    pos += snprintf(content + pos, sizeof(content) - pos,
                    "[%d/%d]\n",
                    picker.candidate_count > 0 ? picker.selected_index + 1 : 0,
                    picker.candidate_count);

    /* Insert content */
    if (api.buffer_insert) {
        api.buffer_insert(content, pos);
    }

    /* Move cursor to end of filter (line 1) */
    if (api.set_point) {
        api.set_point(1, (int)strlen(prompt) + 3 + picker.filter_len);
    }

    if (api.update_display) {
        api.update_display();
    }
}

/* Start the picker for buffer switching */
static void picker_start_buffer_switch(void) {
    /* Save current buffer */
    picker.prev_buffer = api.current_buffer ? api.current_buffer() : NULL;

    /* Create or get the pick buffer */
    picker.pick_buffer = api.find_buffer ? api.find_buffer(PICK_BUFFER) : NULL;
    if (!picker.pick_buffer && api.buffer_create) {
        picker.pick_buffer = api.buffer_create(PICK_BUFFER);
    }

    if (!picker.pick_buffer) {
        if (api.message) {
            api.message("Failed to create picker buffer");
        }
        return;
    }

    /* Initialize picker state */
    picker.mode = PICK_BUFFER_SWITCH;
    picker.filter[0] = '\0';
    picker.filter_len = 0;
    picker.selected_index = 0;

    /* Refresh display */
    picker_refresh();

    if (api.message) {
        api.message("Type to filter, Enter to select, ESC to cancel");
    }
}

/* Execute selection */
static void picker_select(void) {
    if (picker.mode == PICK_INACTIVE) return;
    if (picker.candidate_count == 0) return;

    candidate_t *c = &picker.candidates[picker.selected_index];

    if (picker.mode == PICK_BUFFER_SWITCH && c->data) {
        struct buffer *target = (struct buffer *)c->data;
        picker.mode = PICK_INACTIVE;

        if (api.buffer_switch) {
            api.buffer_switch(target);
        }

        if (api.message) {
            api.message("Switched to %s", c->name);
        }
    }
}

/* Cancel and return to previous buffer */
static void picker_cancel(void) {
    if (picker.mode == PICK_INACTIVE) return;

    picker.mode = PICK_INACTIVE;

    if (picker.prev_buffer && api.buffer_switch) {
        api.buffer_switch(picker.prev_buffer);
    }

    if (api.message) {
        api.message("Cancelled");
    }
}

/* ============================================================================
 * Key Handler
 * ============================================================================ */

static bool key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    if (!event || picker.mode == PICK_INACTIVE) {
        return false;  /* Not our event */
    }

    /* Check if we're in the pick buffer */
    struct buffer *cur = api.current_buffer ? api.current_buffer() : NULL;
    if (cur != picker.pick_buffer) {
        return false;
    }

    /* Get the key from event data */
    int *key_ptr = (int *)event->data;
    if (!key_ptr) return false;
    int key = *key_ptr;

    /* Handle special keys */
    switch (key) {
        case 27:  /* ESC */
        case 7:   /* C-g */
            picker_cancel();
            return true;

        case 13:  /* Enter */
        case 10:  /* LF */
            picker_select();
            return true;

        case 14:  /* C-n */
        case 'j':
            /* Next candidate */
            if (picker.selected_index < picker.candidate_count - 1) {
                picker.selected_index++;
                picker_refresh();
            }
            return true;

        case 16:  /* C-p */
        case 'k':
            /* Previous candidate */
            if (picker.selected_index > 0) {
                picker.selected_index--;
                picker_refresh();
            }
            return true;

        case 127:  /* DEL */
        case 8:    /* Backspace */
            /* Remove last filter char */
            if (picker.filter_len > 0) {
                picker.filter[--picker.filter_len] = '\0';
                picker.selected_index = 0;
                picker_refresh();
            }
            return true;

        case 21:  /* C-u - clear filter */
            picker.filter[0] = '\0';
            picker.filter_len = 0;
            picker.selected_index = 0;
            picker_refresh();
            return true;

        default:
            /* Add printable chars to filter */
            if (key >= 32 && key < 127 && picker.filter_len < MAX_FILTER_LEN - 1) {
                picker.filter[picker.filter_len++] = (char)key;
                picker.filter[picker.filter_len] = '\0';
                picker.selected_index = 0;
                picker_refresh();
                return true;
            }
            break;
    }

    return false;
}

/* ============================================================================
 * Commands
 * ============================================================================ */

static int cmd_switch_buffer(int f, int n) {
    (void)f;
    (void)n;

    picker_start_buffer_switch();
    return 1;
}

static int cmd_pick_cancel(int f, int n) {
    (void)f;
    (void)n;

    picker_cancel();
    return 1;
}
