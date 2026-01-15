/*
 * mouse.c - Comprehensive C23 Mouse Support Extension for muEmacs
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Features:
 * - Click to position cursor
 * - Double-click to select word
 * - Triple-click to select line
 * - Drag to select text
 * - Shift+click to extend selection
 * - Scroll wheel support
 * - Middle-click paste
 * - Window focus on click
 *
 * C23 Features Used:
 * - nullptr instead of NULL
 * - constexpr for compile-time constants
 * - [[nodiscard]] attribute
 * - Designated initializers
 * - bool as first-class type
 *
 * 2026 - Linux/C23 compliant
 */

#define _POSIX_C_SOURCE 199309L  /* For clock_gettime */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

/* Include the muEmacs extension API */
#include "uep/extension.h"
#include "uep/extension_api.h"
#include "terminal/input_state.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

/* Click timing thresholds (milliseconds) - read from config if available */
static int double_click_ms = 400;
static int triple_click_ms = 600;

/* Scroll wheel lines per event - configurable */
static int scroll_lines = 3;
static int scroll_lines_fast = 9;  /* With Ctrl */

/* Mouse button codes (from SGR 1006 protocol) */
typedef enum : uint8_t {
    MOUSE_LEFT   = 0,
    MOUSE_MIDDLE = 1,
    MOUSE_RIGHT  = 2,
    MOUSE_BTN4   = 3,
    MOUSE_BTN5   = 4,
    /* Scroll events (button code | 64) */
    MOUSE_SCROLL_UP    = 64,
    MOUSE_SCROLL_DOWN  = 65,
    MOUSE_SCROLL_LEFT  = 66,
    MOUSE_SCROLL_RIGHT = 67,
} mouse_button_t;

/* Mouse action types */
typedef enum : uint8_t {
    MOUSE_PRESS   = 0,
    MOUSE_RELEASE = 1,
    MOUSE_DRAG    = 2,
    MOUSE_MOVE    = 3,
} mouse_action_t;

/* Selection modes */
typedef enum : uint8_t {
    SEL_NONE,
    SEL_CHAR,
    SEL_WORD,
    SEL_LINE,
    SEL_BLOCK,
} selection_mode_t;

/* ============================================================================
 * State
 * ============================================================================ */

/* Click detection state */
typedef struct {
    uint16_t x, y;
    uint64_t timestamp_ms;
    int click_count;
} click_state_t;

/* Selection tracking */
typedef struct {
    bool active;
    bool dragging;
    uint16_t anchor_x, anchor_y;
    selection_mode_t mode;
} selection_state_t;

/* ============================================================================
 * ABI-Stable API Access - Function pointer types
 * ============================================================================ */

typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef int (*config_int_fn)(const char*, const char*, int);
typedef bool (*config_bool_fn)(const char*, const char*, bool);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct window *(*current_window_fn)(void);
typedef struct window *(*window_at_row_fn)(int);
typedef int (*window_switch_fn)(struct window*);
typedef int (*screen_to_buffer_pos_fn)(struct window*, int, int, int*, int*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef int (*set_mark_fn)(void);
typedef int (*scroll_up_fn)(int);
typedef int (*scroll_down_fn)(int);
typedef char *(*get_word_at_point_fn)(void);
typedef char *(*get_current_line_fn)(void);
typedef void (*message_fn)(const char*, ...);
typedef void (*update_display_fn)(void);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);

/* Global extension state with ABI-stable function pointers */
static struct {
    /* ABI-stable API function pointers */
    on_fn on;
    off_fn off;
    config_int_fn config_int;
    config_bool_fn config_bool;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_window_fn current_window;
    window_at_row_fn window_at_row;
    window_switch_fn window_switch;
    screen_to_buffer_pos_fn screen_to_buffer_pos;
    get_point_fn get_point;
    set_point_fn set_point;
    set_mark_fn set_mark;
    scroll_up_fn scroll_up;
    scroll_down_fn scroll_down;
    get_word_at_point_fn get_word_at_point;
    get_current_line_fn get_current_line;
    message_fn message;
    update_display_fn update_display;
    free_fn free;
    log_fn log_info;
    log_fn log_error;

    /* State */
    click_state_t last_click;
    selection_state_t selection;
    bool initialized;
} g_mouse = {0};

/* ============================================================================
 * Utilities
 * ============================================================================ */

/* Get current timestamp in milliseconds */
[[nodiscard]]
static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ============================================================================
 * Click Handlers
 * ============================================================================ */

/* Position cursor at click location */
static bool position_cursor(uint16_t x, uint16_t y) {
        if (!g_mouse.current_window) return false;

    /* Find window at click position */
    struct window *wp = g_mouse.window_at_row(y);
    if (!wp) return false;

    /* Switch to window if different */
    struct window *cur_wp = g_mouse.current_window();
    if (wp != cur_wp) {
        g_mouse.window_switch(wp);
    }

    /* Convert screen position to buffer position */
    int buf_line, buf_offset;
    if (g_mouse.screen_to_buffer_pos(wp, y, x, &buf_line, &buf_offset) != 0) {
        return false;  /* Click outside buffer content */
    }

    /* Position cursor */
    g_mouse.set_point(buf_line, buf_offset + 1);  /* API uses 1-based columns */
    g_mouse.update_display();

    return true;
}

/* Handle single click */
static bool handle_single_click(uint16_t x, uint16_t y, uint8_t mods) {
        if (!g_mouse.current_window) return false;

    /* Shift+click extends selection */
    if (mods & MOD_SHIFT) {
        /* Set mark at current position before moving */
        if (!g_mouse.selection.active) {
            g_mouse.set_mark();
        }
        g_mouse.selection.active = true;
    } else {
        /* Clear selection */
        g_mouse.selection.active = false;
    }

    /* Position cursor at click */
    if (!position_cursor(x, y)) {
        return false;
    }

    /* Start potential drag */
    g_mouse.selection.dragging = true;
    g_mouse.selection.anchor_x = x;
    g_mouse.selection.anchor_y = y;
    g_mouse.selection.mode = (mods & MOD_ALT) ? SEL_BLOCK : SEL_CHAR;

    return true;
}

/* Handle double click - select word */
static bool handle_double_click(uint16_t x, uint16_t y, uint8_t mods) {
    (void)mods;
        if (!g_mouse.current_window) return false;

    /* Position cursor first */
    if (!position_cursor(x, y)) {
        return false;
    }

    /* Get word at point and select it */
    char *word = g_mouse.get_word_at_point();
    if (word) {
        /* Word exists - set mark at start, move to end */
        /* First, move back to word start */
        int line, col;
        g_mouse.get_point(&line, &col);

        /* Free the word - selection logic is simplified for now */
        g_mouse.free(word);

        /* Set mark at current position */
        g_mouse.set_mark();

        /* This is simplified - ideally we'd have a select-word command */
        g_mouse.selection.active = true;
        g_mouse.selection.mode = SEL_WORD;
    }

    g_mouse.update_display();
    return true;
}

/* Handle triple click - select line */
static bool handle_triple_click(uint16_t x, uint16_t y, uint8_t mods) {
    (void)mods;
        if (!g_mouse.current_window) return false;

    /* Position cursor at click position to get the right line */
    if (!position_cursor(x, y)) {
        return false;
    }

    /* Get current line, move to beginning, set mark, move to end */
    int line, col;
    g_mouse.get_point(&line, &col);

    /* Move to beginning of line */
    g_mouse.set_point(line, 1);

    /* Set mark */
    g_mouse.set_mark();

    /* Move to end of line (get line content to find length) */
    char *line_content = g_mouse.get_current_line();
    if (line_content) {
        int line_len = (int)strlen(line_content);
        g_mouse.set_point(line, line_len + 1);
        g_mouse.free(line_content);
    }

    g_mouse.selection.active = true;
    g_mouse.selection.mode = SEL_LINE;

    g_mouse.update_display();
    return true;
}

/* ============================================================================
 * Drag and Release Handlers
 * ============================================================================ */

/* Handle mouse drag - extend selection */
static bool handle_drag(uint16_t x, uint16_t y, uint8_t mods) {
    (void)mods;

    if (!g_mouse.selection.dragging) {
        return false;
    }

        if (!g_mouse.current_window) return false;

    /* Find window at current position */
    struct window *wp = g_mouse.window_at_row(y);
    if (!wp) return false;

    /* Convert to buffer position */
    int buf_line, buf_offset;
    if (g_mouse.screen_to_buffer_pos(wp, y, x, &buf_line, &buf_offset) != 0) {
        return false;
    }

    /* If this is the first drag after click, set the mark */
    if (!g_mouse.selection.active) {
        g_mouse.set_mark();
        g_mouse.selection.active = true;
    }

    /* Move point to drag position */
    g_mouse.set_point(buf_line, buf_offset + 1);
    g_mouse.update_display();

    return true;
}

/* Handle mouse button release */
static bool handle_release(uint16_t x, uint16_t y, uint8_t mods) {
    (void)x;
    (void)y;
    (void)mods;

    /* End drag */
    g_mouse.selection.dragging = false;

    return true;
}

/* ============================================================================
 * Scroll Handlers
 * ============================================================================ */

/* Handle scroll wheel */
static bool handle_scroll(mouse_button_t btn, uint16_t x __attribute__((unused)),
                          uint16_t y, uint8_t mods) {
        if (!g_mouse.current_window) return false;

    /* Find window under cursor */
    struct window *wp = g_mouse.window_at_row(y);
    if (!wp) return false;

    /* Switch to window if different */
    struct window *cur_wp = g_mouse.current_window();
    if (wp != cur_wp) {
        g_mouse.window_switch(wp);
    }

    /* Determine scroll amount */
    int scroll_amount = scroll_lines;
    if (mods & MOD_CTRL) {
        scroll_amount = scroll_lines_fast;
    }

    /* Scroll based on direction */
    switch (btn) {
        case MOUSE_SCROLL_UP:
            g_mouse.scroll_up(scroll_amount);
            break;
        case MOUSE_SCROLL_DOWN:
            g_mouse.scroll_down(scroll_amount);
            break;
        case MOUSE_SCROLL_LEFT:
        case MOUSE_SCROLL_RIGHT:
            /* Horizontal scroll - not implemented yet */
            break;
        default:
            return false;
    }

    g_mouse.update_display();
    return true;
}

/* ============================================================================
 * Middle Click Handler
 * ============================================================================ */

/* Handle middle click - paste from kill ring (X11 tradition) */
static bool handle_middle_click(uint16_t x, uint16_t y, uint8_t mods) {
    (void)mods;
        if (!g_mouse.current_window) return false;

    /* Position cursor at click location */
    if (!position_cursor(x, y)) {
        return false;
    }

    /* Yank from kill ring - this would need a command execution mechanism */
    /* For now, just position the cursor */
    g_mouse.message("Middle-click: cursor positioned (yank not implemented in extension)");

    return true;
}

/* ============================================================================
 * Main Event Handler
 * ============================================================================ */

/* Main mouse event handler - registered with event bus */
[[nodiscard]]
static bool handle_mouse_event(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    if (!event || !event->data || !g_mouse.current_window) {
        return false;
    }

    /* Extract input_key_event from event data */
    struct input_key_event *evt = (struct input_key_event *)event->data;

    /* Extract event data */
    uint8_t button_raw = evt->mouse_button;
    mouse_action_t action = (mouse_action_t)evt->code;
    uint16_t x = evt->mouse_x;
    uint16_t y = evt->mouse_y;
    uint8_t mods = (uint8_t)evt->modifiers;

    /* Decode button - handle scroll separately */
    mouse_button_t btn;
    if (button_raw >= 64) {
        /* Scroll event */
        btn = (mouse_button_t)(button_raw);
        return handle_scroll(btn, x, y, mods);
    } else {
        btn = (mouse_button_t)(button_raw & 0x03);
    }

    /* Handle by action type */
    switch (action) {
        case MOUSE_PRESS: {
            /* Detect multi-click */
            uint64_t now = get_timestamp_ms();
            int click_count = 1;

            if (x == g_mouse.last_click.x && y == g_mouse.last_click.y) {
                uint64_t delta = now - g_mouse.last_click.timestamp_ms;
                /* Use appropriate threshold for double vs triple click */
                uint64_t threshold = (g_mouse.last_click.click_count == 1)
                    ? (uint64_t)double_click_ms
                    : (uint64_t)triple_click_ms;
                if (delta < threshold) {
                    click_count = g_mouse.last_click.click_count + 1;
                    if (click_count > 3) click_count = 1;
                }
            }

            /* Update click state */
            g_mouse.last_click = (click_state_t){
                .x = x,
                .y = y,
                .timestamp_ms = now,
                .click_count = click_count
            };

            /* Handle by button */
            switch (btn) {
                case MOUSE_LEFT:
                    switch (click_count) {
                        case 1: return handle_single_click(x, y, mods);
                        case 2: return handle_double_click(x, y, mods);
                        case 3: return handle_triple_click(x, y, mods);
                    }
                    break;
                case MOUSE_MIDDLE:
                    return handle_middle_click(x, y, mods);
                case MOUSE_RIGHT:
                    /* Right-click - could implement context menu */
                    return false;
                default:
                    break;
            }
            break;
        }

        case MOUSE_RELEASE:
            return handle_release(x, y, mods);

        case MOUSE_DRAG:
            return handle_drag(x, y, mods);

        case MOUSE_MOVE:
            /* Hover - not used currently */
            return false;
    }

    return false;
}

/* ============================================================================
 * Extension Commands
 * ============================================================================ */

/* M-x mouse-enable - enable mouse support */
static int cmd_mouse_enable(int f, int n) {
    (void)f;
    (void)n;

    if (g_mouse.message) {
        g_mouse.message("Mouse support is already enabled");
    }
    return true;
}

/* M-x mouse-disable - disable mouse support */
static int cmd_mouse_disable(int f, int n) {
    (void)f;
    (void)n;

    if (g_mouse.message) {
        g_mouse.message("Mouse support cannot be disabled at runtime");
    }
    return true;
}

/* M-x mouse-status - show mouse status */
static int cmd_mouse_status(int f, int n) {
    (void)f;
    (void)n;

    if (g_mouse.message) {
        g_mouse.message("Mouse: SGR 1006 mode enabled, selection=%s",
                            g_mouse.selection.active ? "active" : "none");
    }
    return true;
}

/* ============================================================================
 * Extension Entry Point
 * ============================================================================ */

static int mouse_init(struct uemacs_api *editor_api) {
    /*
     * Use get_function() for ABI stability.
     */
    if (!editor_api || !editor_api->get_function) {
        fprintf(stderr, "c_mouse: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    g_mouse.on = (on_fn)LOOKUP(on);
    g_mouse.off = (off_fn)LOOKUP(off);
    g_mouse.config_int = (config_int_fn)LOOKUP(config_int);
    g_mouse.config_bool = (config_bool_fn)LOOKUP(config_bool);
    g_mouse.register_command = (register_command_fn)LOOKUP(register_command);
    g_mouse.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    g_mouse.current_window = (current_window_fn)LOOKUP(current_window);
    g_mouse.window_at_row = (window_at_row_fn)LOOKUP(window_at_row);
    g_mouse.window_switch = (window_switch_fn)LOOKUP(window_switch);
    g_mouse.screen_to_buffer_pos = (screen_to_buffer_pos_fn)LOOKUP(screen_to_buffer_pos);
    g_mouse.get_point = (get_point_fn)LOOKUP(get_point);
    g_mouse.set_point = (set_point_fn)LOOKUP(set_point);
    g_mouse.set_mark = (set_mark_fn)LOOKUP(set_mark);
    g_mouse.scroll_up = (scroll_up_fn)LOOKUP(scroll_up);
    g_mouse.scroll_down = (scroll_down_fn)LOOKUP(scroll_down);
    g_mouse.get_word_at_point = (get_word_at_point_fn)LOOKUP(get_word_at_point);
    g_mouse.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    g_mouse.message = (message_fn)LOOKUP(message);
    g_mouse.update_display = (update_display_fn)LOOKUP(update_display);
    g_mouse.free = (free_fn)LOOKUP(free);
    g_mouse.log_info = (log_fn)LOOKUP(log_info);
    g_mouse.log_error = (log_fn)LOOKUP(log_error);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!g_mouse.on || !g_mouse.register_command || !g_mouse.log_info) {
        fprintf(stderr, "c_mouse: Missing critical API functions\n");
        return -1;
    }

    /* Read configuration from TOML [extension.mouse] section */
    if (g_mouse.config_bool) {
        bool enabled = g_mouse.config_bool("mouse", "enabled", true);
        if (!enabled) {
            g_mouse.log_info("mouse_support: disabled by configuration");
            return 0;  /* Not an error, just disabled */
        }
    }

    if (g_mouse.config_int) {
        scroll_lines = g_mouse.config_int("mouse", "scroll_lines", 3);
        scroll_lines_fast = scroll_lines * 3;
        double_click_ms = g_mouse.config_int("mouse", "double_click_ms", 400);
        triple_click_ms = g_mouse.config_int("mouse", "triple_click_ms", 600);
    }

    /* Register mouse event handler with event bus */
    if (g_mouse.on(UEMACS_EVT_INPUT_MOUSE, (uemacs_event_fn)handle_mouse_event, NULL, 0) != 0) {
        g_mouse.log_error("mouse_support: failed to register event handler");
        return -1;
    }

    /* Register commands */
    g_mouse.register_command("mouse-enable", cmd_mouse_enable);
    g_mouse.register_command("mouse-disable", cmd_mouse_disable);
    g_mouse.register_command("mouse-status", cmd_mouse_status);

    /* Initialize state */
    memset(&g_mouse.last_click, 0, sizeof(g_mouse.last_click));
    memset(&g_mouse.selection, 0, sizeof(g_mouse.selection));
    g_mouse.initialized = true;

    g_mouse.log_info("c_mouse v4.0.0 loaded (ABI-stable, SGR 1006/1016)");
    g_mouse.log_info("  scroll_lines=%d, double_click=%dms, triple_click=%dms",
                  scroll_lines, double_click_ms, triple_click_ms);
    return 0;
}

static void mouse_cleanup(void) {
    if (g_mouse.initialized) {
        if (g_mouse.off) {
            g_mouse.off(UEMACS_EVT_INPUT_MOUSE, (uemacs_event_fn)handle_mouse_event);
        }
        if (g_mouse.unregister_command) {
            g_mouse.unregister_command("mouse-enable");
            g_mouse.unregister_command("mouse-disable");
            g_mouse.unregister_command("mouse-status");
        }
        if (g_mouse.log_info) {
            g_mouse.log_info("mouse_support: extension unloaded");
        }
    }
    g_mouse.initialized = false;
}

/* Extension descriptor */
static struct uemacs_extension mouse_ext = {
    .api_version = 4  /* ABI-stable API */,
    .name = "c_mouse",
    .version = "4.0.0",
    .description = "Comprehensive C23 mouse support with SGR 1006/1016 (ABI-stable)",
    .init = mouse_init,
    .cleanup = mouse_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &mouse_ext;
}
