/*
 * write_edit.c - WriteEdit Prose Mode Extension for μEmacs
 *
 * Provides Word-like document editing:
 * - Soft-wrap: Visual line wrapping without hard newlines
 * - Smart typography: Real-time transforms (-- → —, smart quotes)
 *
 * Commands:
 *   write-edit  - Toggle write-edit mode for current buffer
 *
 * Compile: gcc -shared -fPIC -I../include -o write_edit.so write_edit.c
 * Install: cp write_edit.so ~/.config/muemacs/extensions/
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <uep/extension.h>
#include <uep/extension_api.h>

static struct uemacs_api *api;

/* Settings */
static int soft_wrap_col = 80;
static bool smart_typography = true;
static bool em_dash_enabled = true;
static bool smart_quotes_enabled = true;
static bool curly_apostrophe_enabled = true;

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
 * Character transform callback for smart typography
 *
 * Returns:
 *   0 - No transform, use original character
 *   1 - Use *out instead of c
 *  -1 - Delete previous char, then insert *out (em-dash case)
 */
static int writeedit_transform(int c, int *out) {
    if (!is_writeedit_enabled()) {
        prev_char = c;
        return 0;
    }

    if (!smart_typography) {
        prev_char = c;
        return 0;
    }

    /* Em dash: -- → — */
    if (em_dash_enabled && c == '-' && prev_char == '-') {
        *out = EMDASH;
        prev_char = 0;  /* Reset - em dash is complete */
        return -1;      /* Signal: delete prev char, insert em-dash */
    }

    /* Smart double quotes */
    if (smart_quotes_enabled && c == '"') {
        if (is_word_boundary(prev_char)) {
            *out = LEFT_DQUOTE;
        } else {
            *out = RIGHT_DQUOTE;
        }
        prev_char = c;
        return 1;
    }

    /* Curly apostrophe: ' → ' */
    if (curly_apostrophe_enabled && c == '\'') {
        *out = RIGHT_SQUOTE;
        prev_char = c;
        return 1;
    }

    /* No transformation */
    prev_char = c;
    return 0;
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
        api->message("[WRITEEDIT ENABLED - SOFT WRAP AT %d]", soft_wrap_col);
    }

    api->update_display();
    return 1;
}

/*
 * Extension init
 */
static int writeedit_init(struct uemacs_api *editor_api) {
    api = editor_api;

    if (api->api_version < 1) {
        api->log_error("write_edit: API version too old");
        return -1;
    }

    /* Initialize buffer tracking */
    memset(enabled_buffers, 0, sizeof(enabled_buffers));
    enabled_buffer_count = 0;
    prev_char = 0;

    /* Register command */
    if (api->register_command("write-edit", cmd_write_edit) != 0) {
        api->log_error("write_edit: Failed to register write-edit command");
        return -1;
    }

    /* Register character transform hook */
    if (api->on_char_transform(writeedit_transform) != 0) {
        api->log_error("write_edit: Failed to register char transform hook");
        api->unregister_command("write-edit");
        return -1;
    }

    api->log_info("write_edit v1.0.0 loaded - M-x write-edit to toggle");
    return 0;
}

/*
 * Extension cleanup
 */
static void writeedit_cleanup(void) {
    /* Remove hooks */
    api->off_char_transform(writeedit_transform);
    api->unregister_command("write-edit");

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
    .api_version = UEMACS_API_VERSION,
    .name = "write-edit",
    .version = "1.0.0",
    .description = "Prose editing mode with soft-wrap and smart typography",
    .init = writeedit_init,
    .cleanup = writeedit_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &writeedit_ext;
}
