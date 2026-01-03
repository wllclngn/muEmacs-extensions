/*
 * rg_search.c - ripgrep Integration for μEmacs
 *
 * Provides fast recursive grep using ripgrep.
 *
 * Commands:
 *   rg-search      - Search for pattern in current directory
 *   rg-search-word - Search for word under cursor
 *   rg-goto        - Jump to file:line from results buffer
 *
 * Compile: gcc -shared -fPIC -I../include -o rg_search.so rg_search.c
 * Install: cp rg_search.so ~/.config/muemacs/extensions/
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <uep/extension.h>
#include <uep/extension_api.h>

static struct uemacs_api *api;
static char last_pattern[256] = {0};

#define RG_RESULTS_BUFFER "*rg-results*"

/* Parse a ripgrep --vimgrep output line: file:line:col:text */
static int parse_rg_line(const char *line, char *file, size_t file_size,
                         int *line_num, int *col_num) {
    if (!line || !file) return -1;

    /* Find first colon (after filename) */
    const char *p = line;
    const char *colon1 = strchr(p, ':');
    if (!colon1) return -1;

    /* Copy filename */
    size_t flen = colon1 - p;
    if (flen >= file_size) flen = file_size - 1;
    strncpy(file, p, flen);
    file[flen] = '\0';

    /* Parse line number */
    p = colon1 + 1;
    const char *colon2 = strchr(p, ':');
    if (!colon2) return -1;

    if (line_num) *line_num = atoi(p);

    /* Parse column number */
    p = colon2 + 1;
    const char *colon3 = strchr(p, ':');
    if (!colon3) {
        /* No column, just line:text format */
        if (col_num) *col_num = 1;
    } else {
        if (col_num) *col_num = atoi(p);
    }

    return 0;
}

/* Count matches in output */
static int count_matches(const char *output) {
    if (!output) return 0;

    int count = 0;
    const char *p = output;
    while ((p = strchr(p, '\n')) != NULL) {
        count++;
        p++;
    }
    /* Count last line if no trailing newline */
    if (output[0] && output[strlen(output) - 1] != '\n') {
        count++;
    }
    return count;
}

/* Command: Search for pattern */
static int cmd_rg_search(int f, int n) {
    (void)f;
    (void)n;

    char pattern[256];

    /* Prompt for pattern */
    if (api->prompt("rg pattern: ", pattern, sizeof(pattern)) != 0) {
        api->message("Cancelled");
        return 0;
    }

    if (pattern[0] == '\0') {
        api->message("No pattern specified");
        return 0;
    }

    /* Save pattern for potential re-use */
    strncpy(last_pattern, pattern, sizeof(last_pattern) - 1);
    last_pattern[sizeof(last_pattern) - 1] = '\0';

    /* Build rg command */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rg --vimgrep --color=never --no-heading '%s' 2>/dev/null",
             pattern);

    api->message("Searching...");
    api->update_display();

    /* Run ripgrep */
    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret != 0 && (!output || len == 0)) {
        api->message("No matches found for '%s'", pattern);
        if (output) api->free(output);
        return 1;
    }

    /* Count matches */
    int match_count = count_matches(output);

    /* Get or create results buffer */
    struct buffer *bp = api->find_buffer(RG_RESULTS_BUFFER);
    if (!bp) {
        bp = api->buffer_create(RG_RESULTS_BUFFER);
        if (!bp) {
            api->message("Failed to create results buffer");
            api->free(output);
            return 0;
        }
    }

    /* Switch to results buffer and clear it */
    api->buffer_switch(bp);
    api->buffer_clear(bp);

    /* Build header */
    char header[256];
    snprintf(header, sizeof(header), "=== %d matches for '%s' ===\n\n",
             match_count, pattern);

    /* Insert header and results */
    api->buffer_insert(header, strlen(header));
    if (output && len > 0) {
        api->buffer_insert(output, len);
    }

    /* Position at start */
    api->set_point(3, 1);  /* Skip header lines */

    api->message("%d matches - Enter to jump to file", match_count);

    if (output) api->free(output);
    return 1;
}

/* Command: Search for word under cursor */
static int cmd_rg_search_word(int f, int n) {
    (void)f;
    (void)n;

    char *word = api->get_word_at_point();
    if (!word) {
        api->message("No word at point");
        return 0;
    }

    /* Save pattern */
    strncpy(last_pattern, word, sizeof(last_pattern) - 1);
    last_pattern[sizeof(last_pattern) - 1] = '\0';

    /* Build rg command with word boundaries */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rg --vimgrep --color=never --no-heading -w '%s' 2>/dev/null",
             word);

    api->message("Searching for '%s'...", word);
    api->update_display();

    /* Run ripgrep */
    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret != 0 && (!output || len == 0)) {
        api->message("No matches found for '%s'", word);
        api->free(word);
        if (output) api->free(output);
        return 1;
    }

    int match_count = count_matches(output);

    /* Get or create results buffer */
    struct buffer *bp = api->find_buffer(RG_RESULTS_BUFFER);
    if (!bp) {
        bp = api->buffer_create(RG_RESULTS_BUFFER);
        if (!bp) {
            api->message("Failed to create results buffer");
            api->free(word);
            api->free(output);
            return 0;
        }
    }

    /* Switch to results buffer and clear it */
    api->buffer_switch(bp);
    api->buffer_clear(bp);

    /* Build header */
    char header[256];
    snprintf(header, sizeof(header), "=== %d matches for word '%s' ===\n\n",
             match_count, word);

    /* Insert header and results */
    api->buffer_insert(header, strlen(header));
    if (output && len > 0) {
        api->buffer_insert(output, len);
    }

    api->set_point(3, 1);

    api->message("%d matches for '%s'", match_count, word);

    api->free(word);
    if (output) api->free(output);
    return 1;
}

/* Core goto logic - used by command and key hook */
static int do_rg_goto(void) {
    /* Get current line */
    char *line = api->get_current_line();
    if (!line) {
        api->message("No line at point");
        return 0;
    }

    /* Skip header lines */
    if (line[0] == '=' || line[0] == '\0') {
        api->message("Not on a result line");
        api->free(line);
        return 0;
    }

    /* Parse the line */
    char file[512];
    int line_num = 1;
    int col_num = 1;

    if (parse_rg_line(line, file, sizeof(file), &line_num, &col_num) != 0) {
        api->message("Cannot parse result line");
        api->free(line);
        return 0;
    }

    api->free(line);

    /* Open file at line */
    if (api->find_file_line(file, line_num) != 0) {
        api->message("Cannot open '%s'", file);
        return 0;
    }

    /* Try to position at column */
    if (col_num > 1) {
        int cur_line, cur_col;
        api->get_point(&cur_line, &cur_col);
        api->set_point(cur_line, col_num);
    }

    api->message("%s:%d", file, line_num);
    return 1;
}

/* Command: Jump to file:line from results buffer */
static int cmd_rg_goto(int f, int n) {
    (void)f;
    (void)n;
    return do_rg_goto();
}

/* Key hook: Handle Enter in results buffer */
static int rg_key_hook(int key) {
    /* Only handle Enter (CR = 13) */
    if (key != '\r' && key != '\n') {
        return 0;  /* Not our key */
    }

    /* Check if we're in the results buffer */
    struct buffer *bp = api->current_buffer();
    if (!bp) return 0;

    const char *name = api->buffer_name(bp);
    if (!name || strcmp(name, RG_RESULTS_BUFFER) != 0) {
        return 0;  /* Not in our buffer */
    }

    /* We're in the results buffer and Enter was pressed - handle it */
    do_rg_goto();
    return 1;  /* Key consumed */
}

/* Extension init */
static int rg_init(struct uemacs_api *editor_api) {
    api = editor_api;

    if (api->api_version < 1) {
        api->log_error("rg_search: API version too old");
        return -1;
    }

    /* Register commands */
    if (api->register_command("rg-search", cmd_rg_search) != 0) {
        api->log_error("rg_search: Failed to register rg-search");
        return -1;
    }
    if (api->register_command("rg-search-word", cmd_rg_search_word) != 0) {
        api->log_error("rg_search: Failed to register rg-search-word");
        return -1;
    }
    if (api->register_command("rg-goto", cmd_rg_goto) != 0) {
        api->log_error("rg_search: Failed to register rg-goto");
        return -1;
    }

    /* Register key hook for Enter in results buffer */
    if (api->on_key(rg_key_hook) != 0) {
        api->log_warn("rg_search: Failed to register key hook (Enter won't auto-jump)");
    }

    api->log_info("rg_search v1.0.0 loaded - M-x rg-search to search");
    return 0;
}

/* Extension cleanup */
static void rg_cleanup(void) {
    api->off_key(rg_key_hook);
    api->unregister_command("rg-search");
    api->unregister_command("rg-search-word");
    api->unregister_command("rg-goto");
    api->log_info("rg_search unloaded");
}

/* Extension descriptor */
static struct uemacs_extension rg_ext = {
    .api_version = UEMACS_API_VERSION,
    .name = "rg-search",
    .version = "1.0.0",
    .description = "ripgrep integration for fast recursive search",
    .init = rg_init,
    .cleanup = rg_cleanup,
};

/* Entry point */
struct uemacs_extension *uemacs_extension_entry(void) {
    return &rg_ext;
}
