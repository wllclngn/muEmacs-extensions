/*
 * bridge.c - C Bridge for COBOL CSV Reader Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides C wrappers for COBOL FFI interop.
 * Uses get_function() for ABI stability - immune to API struct layout changes.
 *
 * IMPORTANT: Must call cob_init() before any COBOL code and cob_tidy() on cleanup.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <libcob.h>
#include <uep/extension_api.h>
#include <uep/extension.h>

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*buffer_create_fn)(const char*);
typedef void *(*find_buffer_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef void (*message_fn)(const char*, ...);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef void (*log_fn)(const char*, ...);

/* Our local API - only the functions we actually use */
static struct {
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    buffer_create_fn buffer_create;
    find_buffer_fn find_buffer;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    message_fn message;
    prompt_fn prompt;
    log_fn log_info;
    log_fn log_error;
} api;

/* CSV Buffer - shared with COBOL */
#define MAX_ROWS 1000
#define MAX_COLS 50
#define MAX_CELL_LEN 256
#define MAX_LINE_LEN 8192

static char g_cells[MAX_ROWS][MAX_COLS][MAX_CELL_LEN];
static int g_row_count = 0;
static int g_col_count = 0;
static int g_col_widths[MAX_COLS];

/* COBOL callable functions */
extern void cobol_parse_csv_line(const char *line, int line_len, int row_num);
extern void cobol_get_cell(int row, int col, char *buffer, int buffer_len);

/* Parse a CSV file into g_cells */
static int parse_csv_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        if (api.message) api.message("Cannot open file: %s", filepath);
        return -1;
    }

    /* Reset state */
    g_row_count = 0;
    g_col_count = 0;
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_col_widths, 0, sizeof(g_col_widths));

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp) && g_row_count < MAX_ROWS) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) continue;

        /* Parse line into cells - simple CSV parsing */
        int col = 0;
        const char *p = line;
        char cell[MAX_CELL_LEN];
        int cell_idx = 0;
        bool in_quotes = false;

        while (*p && col < MAX_COLS) {
            if (*p == '"') {
                in_quotes = !in_quotes;
                p++;
            } else if (*p == ',' && !in_quotes) {
                cell[cell_idx] = '\0';
                strncpy(g_cells[g_row_count][col], cell, MAX_CELL_LEN - 1);

                /* Track column width */
                int width = (int)strlen(cell);
                if (width > g_col_widths[col]) g_col_widths[col] = width;

                col++;
                cell_idx = 0;
                p++;
            } else {
                if (cell_idx < MAX_CELL_LEN - 1) {
                    cell[cell_idx++] = *p;
                }
                p++;
            }
        }

        /* Last cell in row */
        if (cell_idx > 0 || col > 0) {
            cell[cell_idx] = '\0';
            strncpy(g_cells[g_row_count][col], cell, MAX_CELL_LEN - 1);
            int width = (int)strlen(cell);
            if (width > g_col_widths[col]) g_col_widths[col] = width;
            col++;
        }

        if (col > g_col_count) g_col_count = col;
        g_row_count++;
    }

    fclose(fp);
    return 0;
}

/* Format and display CSV in *csv* buffer */
static void display_csv_buffer(const char *filepath) {
    void *bp = api.find_buffer ? api.find_buffer("*csv*") : NULL;
    if (!bp && api.buffer_create) {
        bp = api.buffer_create("*csv*");
    }

    if (!bp) {
        if (api.message) api.message("Failed to create *csv* buffer");
        return;
    }

    if (api.buffer_switch) api.buffer_switch(bp);
    if (api.buffer_clear) api.buffer_clear(bp);

    /* Build formatted output */
    char output[65536];
    int pos = 0;

    /* Header with filename */
    pos += snprintf(output + pos, sizeof(output) - pos,
                    "CSV: %s (%d rows x %d cols)\n",
                    filepath, g_row_count, g_col_count);

    /* Separator line */
    for (int c = 0; c < g_col_count && pos < (int)sizeof(output) - 100; c++) {
        int w = g_col_widths[c] < 4 ? 4 : (g_col_widths[c] > 30 ? 30 : g_col_widths[c]);
        for (int i = 0; i < w + 2; i++) output[pos++] = '-';
        output[pos++] = '+';
    }
    output[pos++] = '\n';

    /* Data rows */
    for (int r = 0; r < g_row_count && pos < (int)sizeof(output) - 1000; r++) {
        for (int c = 0; c < g_col_count; c++) {
            int w = g_col_widths[c] < 4 ? 4 : (g_col_widths[c] > 30 ? 30 : g_col_widths[c]);
            pos += snprintf(output + pos, sizeof(output) - pos,
                           " %-*.*s |", w, w, g_cells[r][c]);
        }
        output[pos++] = '\n';

        /* Separator after header row */
        if (r == 0) {
            for (int c = 0; c < g_col_count && pos < (int)sizeof(output) - 100; c++) {
                int w = g_col_widths[c] < 4 ? 4 : (g_col_widths[c] > 30 ? 30 : g_col_widths[c]);
                for (int i = 0; i < w + 2; i++) output[pos++] = '-';
                output[pos++] = '+';
            }
            output[pos++] = '\n';
        }
    }

    if (api.buffer_insert) {
        api.buffer_insert(output, pos);
    }

    if (api.message) {
        api.message("Loaded %d rows x %d cols", g_row_count, g_col_count);
    }
}

/* Command: csv-open */
static int cmd_csv_open(int f, int n) {
    (void)f; (void)n;

    char filepath[1024] = {0};

    if (!api.prompt) {
        if (api.message) api.message("Prompt not available");
        return 0;
    }

    int result = api.prompt("CSV file: ", filepath, sizeof(filepath));
    if (result != 1 || filepath[0] == '\0') {
        if (api.message) api.message("Cancelled");
        return 0;
    }

    if (parse_csv_file(filepath) != 0) {
        return 0;
    }

    display_csv_buffer(filepath);
    return 1;
}

/* Command: csv-column-sum */
static int cmd_csv_column_sum(int f, int n) {
    (void)f; (void)n;

    if (g_row_count == 0) {
        if (api.message) api.message("No CSV loaded");
        return 0;
    }

    char col_str[32] = {0};
    if (!api.prompt) return 0;

    int result = api.prompt("Column number (1-based): ", col_str, sizeof(col_str));
    if (result != 1) return 0;

    int col = atoi(col_str) - 1;
    if (col < 0 || col >= g_col_count) {
        if (api.message) api.message("Invalid column: %s", col_str);
        return 0;
    }

    double sum = 0.0;
    int count = 0;

    /* Skip header row (row 0), sum numeric values */
    for (int r = 1; r < g_row_count; r++) {
        char *end;
        double val = strtod(g_cells[r][col], &end);
        if (end != g_cells[r][col] && *end == '\0') {
            sum += val;
            count++;
        }
    }

    if (api.message) {
        api.message("Column %d sum: %.2f (%d values)", col + 1, sum, count);
    }

    return 1;
}

/* Extension init */
static int csv_init(struct uemacs_api *uapi) {
    if (!uapi || !uapi->get_function) {
        fprintf(stderr, "cobol_csv: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up API functions */
    #define LOOKUP(name) uapi->get_function(#name)

    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.find_buffer = (find_buffer_fn)LOOKUP(find_buffer);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.message = (message_fn)LOOKUP(message);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);

    #undef LOOKUP

    if (!api.register_command) {
        fprintf(stderr, "cobol_csv: Missing register_command\n");
        return -1;
    }

    /* Initialize COBOL runtime */
    cob_init(0, NULL);

    /* Register commands */
    api.register_command("csv-open", cmd_csv_open);
    api.register_command("csv-column-sum", cmd_csv_column_sum);

    if (api.log_info) {
        api.log_info("cobol_csv: Loaded (v4.0, ABI-stable)");
    }

    return 0;
}

static void csv_cleanup(void) {
    if (api.unregister_command) {
        api.unregister_command("csv-open");
        api.unregister_command("csv-column-sum");
    }

    /* Cleanup COBOL runtime */
    cob_tidy();
}

/* Extension descriptor */
static struct uemacs_extension ext = {
    .api_version = 4,
    .name = "cobol_csv",
    .version = "4.0.0",
    .description = "CSV spreadsheet viewer (COBOL)",
    .init = csv_init,
    .cleanup = csv_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &ext;
}
