/*
 * git_workflow.c - Git Integration Extension for μEmacs
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * Provides git commands directly within the editor.
 *
 * Commands:
 *   M-x git-status    - Show repository status
 *   M-x git-stage     - Stage current file
 *   M-x git-unstage   - Unstage current file
 *   M-x git-commit    - Commit staged changes
 *   M-x git-diff      - Show diff of current file
 *   M-x git-log       - Show commit history
 *   M-x git-pull      - Pull from remote
 *   M-x git-push      - Push to remote
 *
 * Configuration (settings.toml):
 *   [extension.git]
 *   auto_status = true      # Show status hints on save
 *   status_interval = 5     # Check status every N saves
 *
 * Compile: gcc -std=c23 -shared -fPIC -o git_workflow.so git_workflow.c
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <uep/extension.h>
#include <uep/extension_api.h>

/*
 * ABI-Stable API Access
 *
 * Instead of using struct offsets (which break when the API struct changes),
 * we look up functions by name at init time. This makes the extension immune
 * to additions/removals/reordering in the μEmacs API.
 */

/* Function pointer types for the API functions we use */
typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef int (*config_int_fn)(const char*, const char*, int);
typedef bool (*config_bool_fn)(const char*, const char*, bool);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct buffer *(*current_buffer_fn)(void);
typedef const char *(*buffer_filename_fn)(struct buffer*);
typedef const char *(*buffer_name_fn)(struct buffer*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef struct buffer *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(struct buffer*);
typedef int (*buffer_clear_fn)(struct buffer*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef char *(*get_current_line_fn)(void);
typedef void (*message_fn)(const char*, ...);
typedef void (*update_display_fn)(void);
typedef int (*shell_command_fn)(const char*, char**, size_t*);
typedef int (*find_file_line_fn)(const char*, int);
typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);
typedef int (*prompt_fn)(const char*, char*, size_t);

/* Our local API - only the functions we actually use */
static struct {
    on_fn on;
    off_fn off;
    config_int_fn config_int;
    config_bool_fn config_bool;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_filename_fn buffer_filename;
    buffer_name_fn buffer_name;
    buffer_insert_fn buffer_insert;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    get_point_fn get_point;
    set_point_fn set_point;
    get_current_line_fn get_current_line;
    message_fn message;
    update_display_fn update_display;
    shell_command_fn shell_command;
    find_file_line_fn find_file_line;
    prompt_fn prompt;
    alloc_fn alloc;
    free_fn free;
    log_fn log_info;
    log_fn log_error;
    log_fn log_debug;
} api;

/* Buffer names for git output */
#define GIT_STATUS_BUFFER "*git-status*"
#define GIT_LOG_BUFFER    "*git-log*"
#define GIT_DIFF_BUFFER   "*git-diff*"

/* Maximum command output size */
#define MAX_OUTPUT (64 * 1024)

/* Store git root for file path resolution */
static char g_git_root[512] = {0};

/* Check if current buffer is in a git repository */
static bool in_git_repo(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git rev-parse --is-inside-work-tree 2>/dev/null", &output, &len);
    bool result = (ret == 0 && output && strncmp(output, "true", 4) == 0);
    if (output) api.free(output);
    return result;
}

/* Get the git root directory */
static char *get_git_root(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git rev-parse --show-toplevel 2>/dev/null", &output, &len);
    if (ret == 0 && output && len > 0) {
        /* Trim newline */
        if (output[len-1] == '\n') output[len-1] = '\0';
        return output;
    }
    if (output) api.free(output);
    return NULL;
}

/* Run a git command and display output in message line or buffer */
static int run_git_cmd(const char *cmd, bool show_in_buffer) {
    char *output = NULL;
    size_t len = 0;

    api.log_debug("Git: Running: %s", cmd);

    int ret = api.shell_command(cmd, &output, &len);

    if (ret != 0) {
        if (output && len > 0) {
            /* Show error - truncate for message line */
            char errmsg[256];
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            errmsg[sizeof(errmsg) - 1] = '\0';
            /* Remove newlines */
            for (char *p = errmsg; *p; p++) {
                if (*p == '\n') *p = ' ';
            }
            api.message("Git error: %s", errmsg);
        } else {
            api.message("Git command failed (exit %d)", ret);
        }
        if (output) api.free(output);
        return 0;
    }

    if (show_in_buffer && output && len > 0) {
        /* Insert output into current buffer */
        api.buffer_insert(output, len);
    } else if (output && len > 0) {
        /* Show in message line (first line only) */
        char msg[256];
        strncpy(msg, output, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        char *nl = strchr(msg, '\n');
        if (nl) *nl = '\0';
        api.message("%s", msg);
    }

    if (output) api.free(output);
    return 1;
}

/* ============================================================================
 * Buffer Navigation (Enter to jump)
 * ============================================================================ */

/* Check if we're in a git results buffer */
static bool in_git_buffer(const char **out_name) {
    if (!api.current_buffer || !api.buffer_name) return false;

    void *bp = api.current_buffer();
    if (!bp) return false;

    const char *name = api.buffer_name(bp);
    if (!name) return false;

    if (out_name) *out_name = name;

    return (strcmp(name, GIT_STATUS_BUFFER) == 0 ||
            strcmp(name, GIT_LOG_BUFFER) == 0 ||
            strcmp(name, GIT_DIFF_BUFFER) == 0);
}

/* Jump to file from git-status line (format: "XY filename") */
static bool do_git_status_goto(const char *line) {
    /* Skip status codes (first 2 chars) and space */
    if (strlen(line) < 4) return false;

    const char *filename = line + 3;

    /* Build full path */
    char fullpath[1024];
    if (g_git_root[0]) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_git_root, filename);
    } else {
        strncpy(fullpath, filename, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
    }

    if (api.find_file_line && api.find_file_line(fullpath, 1)) {
        api.message("%s", fullpath);
        return true;
    }
    return false;
}

/* Jump to commit from git-log line (format: "hash message") */
static bool do_git_log_goto(const char *line) {
    /* Extract commit hash (first 7+ chars until space) */
    char hash[64] = {0};
    const char *p = line;

    /* Skip graph chars if present */
    while (*p && (*p == '*' || *p == '|' || *p == '/' || *p == '\\' || *p == ' ')) p++;

    int i = 0;
    while (*p && *p != ' ' && i < 63) {
        hash[i++] = *p++;
    }
    hash[i] = '\0';

    if (strlen(hash) < 7) {
        api.message("Not a valid commit line");
        return false;
    }

    /* Show commit diff in a new buffer */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "git show --color=never %s 2>&1", hash);

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0 && output && len > 0) {
        /* Create/switch to diff buffer */
        void *bp = api.buffer_create(GIT_DIFF_BUFFER);
        if (bp) {
            api.buffer_clear(bp);
            api.buffer_switch(bp);

            char header[128];
            snprintf(header, sizeof(header), "Commit: %s\nPress Enter on a file path to open it\n\n", hash);
            api.buffer_insert(header, strlen(header));
            api.buffer_insert(output, len);
            api.set_point(1, 0);
            api.message("Showing commit %s", hash);
        }
    } else {
        api.message("Failed to show commit %s", hash);
    }

    if (output) api.free(output);
    return true;
}

/* Jump to file from git-diff line (format: "+++ b/filename" or "diff --git a/... b/...") */
static bool do_git_diff_goto(const char *line) {
    const char *filename = NULL;

    if (strncmp(line, "+++ b/", 6) == 0) {
        filename = line + 6;
    } else if (strncmp(line, "--- a/", 6) == 0) {
        filename = line + 6;
    } else if (strncmp(line, "diff --git", 10) == 0) {
        /* Format: diff --git a/file b/file */
        const char *b = strstr(line, " b/");
        if (b) filename = b + 3;
    }

    if (!filename) {
        api.message("Not a file path line");
        return false;
    }

    /* Build full path */
    char fullpath[1024];
    if (g_git_root[0]) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_git_root, filename);
    } else {
        strncpy(fullpath, filename, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
    }

    /* Trim trailing whitespace */
    size_t len = strlen(fullpath);
    while (len > 0 && (fullpath[len-1] == '\n' || fullpath[len-1] == '\r' || fullpath[len-1] == ' ')) {
        fullpath[--len] = '\0';
    }

    if (api.find_file_line && api.find_file_line(fullpath, 1)) {
        api.message("%s", fullpath);
        return true;
    }
    return false;
}

/* Main goto handler */
static bool do_git_goto(void) {
    const char *bufname = NULL;
    if (!in_git_buffer(&bufname)) return false;

    char *line = api.get_current_line();
    if (!line || line[0] == '\0') {
        if (line) api.free(line);
        api.message("Empty line");
        return false;
    }

    /* Trim trailing whitespace */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }

    bool result = false;

    if (strcmp(bufname, GIT_STATUS_BUFFER) == 0) {
        result = do_git_status_goto(line);
    } else if (strcmp(bufname, GIT_LOG_BUFFER) == 0) {
        result = do_git_log_goto(line);
    } else if (strcmp(bufname, GIT_DIFF_BUFFER) == 0) {
        result = do_git_diff_goto(line);
    }

    api.free(line);
    return result;
}

/* Key event handler */
static bool git_key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    int key = (int)(intptr_t)event->data;

    if (key != '\r' && key != '\n') return false;
    if (!in_git_buffer(NULL)) return false;

    do_git_goto();
    return true;
}

/* === Git Commands === */

/* git-status: Show repository status in buffer */
static int cmd_git_status(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    /* Store git root for path resolution */
    char *root = get_git_root();
    if (root) {
        strncpy(g_git_root, root, sizeof(g_git_root) - 1);
        g_git_root[sizeof(g_git_root) - 1] = '\0';
        api.free(root);
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git status --short", &output, &len);

    if (ret == 0 && output && len > 0) {
        /* Create/switch to status buffer */
        void *bp = api.buffer_create(GIT_STATUS_BUFFER);
        if (bp) {
            api.buffer_clear(bp);
            api.buffer_switch(bp);

            /* Header */
            char header[256];
            snprintf(header, sizeof(header), "Git Status: %s\nPress Enter on a file to open it\n\n", g_git_root);
            api.buffer_insert(header, strlen(header));

            /* Status output */
            api.buffer_insert(output, len);
            api.set_point(4, 0);  /* Position on first file */

            /* Count files */
            int count = 0;
            for (size_t i = 0; i < len; i++) if (output[i] == '\n') count++;
            api.message("git-status: %d file%s - Enter to open", count, count == 1 ? "" : "s");
        }
    } else if (ret == 0) {
        api.message("Working tree clean");
    } else {
        api.message("Failed to get git status");
    }

    if (output) api.free(output);
    return 1;
}

/* git-status-buffer: Show full status in a buffer */
static int cmd_git_status_full(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git status", &output, &len);

    if (ret == 0 && output && len > 0) {
        api.buffer_insert("=== Git Status ===\n\n", 21);
        api.buffer_insert(output, len);
        api.message("Git status displayed");
    } else {
        api.message("Failed to get git status");
    }

    if (output) api.free(output);
    return 1;
}

/* git-stage: Stage the current file */
static int cmd_git_stage(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    const char *filename = api.buffer_filename(api.current_buffer());
    if (!filename || !*filename) {
        api.message("Buffer has no file");
        return 0;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git add -- '%s' 2>&1", filename);

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0) {
        api.message("Staged: %s", filename);
        api.log_info("Git: Staged %s", filename);
    } else {
        api.message("Failed to stage: %s", output ? output : "unknown error");
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-unstage: Unstage the current file */
static int cmd_git_unstage(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    const char *filename = api.buffer_filename(api.current_buffer());
    if (!filename || !*filename) {
        api.message("Buffer has no file");
        return 0;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git reset HEAD -- '%s' 2>&1", filename);

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0) {
        api.message("Unstaged: %s", filename);
        api.log_info("Git: Unstaged %s", filename);
    } else {
        api.message("Failed to unstage: %s", output ? output : "unknown error");
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-commit: Commit staged changes */
static int cmd_git_commit(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    /* Prompt for commit message */
    char message[512];
    if (api.prompt("Commit message: ", message, sizeof(message)) != 0) {
        api.message("Commit aborted");
        return 0;
    }

    if (!message[0]) {
        api.message("Empty commit message, aborted");
        return 0;
    }

    /* Escape single quotes in message */
    char escaped[1024];
    char *dst = escaped;
    for (const char *src = message; *src && dst < escaped + sizeof(escaped) - 5; src++) {
        if (*src == '\'') {
            *dst++ = '\'';
            *dst++ = '\\';
            *dst++ = '\'';
            *dst++ = '\'';
        } else {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git commit -m '%s' 2>&1", escaped);

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0) {
        api.message("Committed successfully");
        api.log_info("Git: Committed with message: %s", message);
    } else {
        /* Show first line of error */
        char errmsg[128] = "commit failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            errmsg[sizeof(errmsg) - 1] = '\0';
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api.message("Git: %s", errmsg);
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-diff: Show diff in dedicated buffer */
static int cmd_git_diff(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    /* Store git root */
    char *root = get_git_root();
    if (root) {
        strncpy(g_git_root, root, sizeof(g_git_root) - 1);
        g_git_root[sizeof(g_git_root) - 1] = '\0';
        api.free(root);
    }

    const char *filename = api.buffer_filename(api.current_buffer());
    char cmd[1024];

    if (filename && *filename) {
        snprintf(cmd, sizeof(cmd), "git diff --color=never -- '%s' 2>&1", filename);
    } else {
        snprintf(cmd, sizeof(cmd), "git diff --color=never 2>&1");
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0) {
        if (output && len > 0) {
            /* Create/switch to diff buffer */
            void *bp = api.buffer_create(GIT_DIFF_BUFFER);
            if (bp) {
                api.buffer_clear(bp);
                api.buffer_switch(bp);

                char header[256];
                snprintf(header, sizeof(header), "Git Diff: %s\nPress Enter on +++ or --- line to open file\n\n",
                         filename ? filename : "(all files)");
                api.buffer_insert(header, strlen(header));
                api.buffer_insert(output, len);
                api.set_point(4, 0);
                api.message("git-diff: %zu bytes - Enter to open file", len);
            }
        } else {
            api.message("No changes");
        }
    } else {
        api.message("Failed to get diff");
    }

    if (output) api.free(output);
    return 1;
}

/* git-log: Show commit history in dedicated buffer */
static int cmd_git_log(int f, int n) {
    (void)f;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    /* Store git root */
    char *root = get_git_root();
    if (root) {
        strncpy(g_git_root, root, sizeof(g_git_root) - 1);
        g_git_root[sizeof(g_git_root) - 1] = '\0';
        api.free(root);
    }

    /* Use count argument for number of commits, default 20 */
    int count = (n > 0 && n < 100) ? n : 20;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "git log --oneline --graph --decorate -n %d 2>&1", count);

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command(cmd, &output, &len);

    if (ret == 0 && output && len > 0) {
        /* Create/switch to log buffer */
        void *bp = api.buffer_create(GIT_LOG_BUFFER);
        if (bp) {
            api.buffer_clear(bp);
            api.buffer_switch(bp);

            char header[128];
            snprintf(header, sizeof(header), "Git Log: %s\nPress Enter on a commit to show diff\n\n", g_git_root);
            api.buffer_insert(header, strlen(header));
            api.buffer_insert(output, len);
            api.set_point(4, 0);
            api.message("git-log: %d commits - Enter to show diff", count);
        }
    } else {
        api.message("Failed to get log");
    }

    if (output) api.free(output);
    return 1;
}

/* git-pull: Pull from remote */
static int cmd_git_pull(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    api.message("Pulling from remote...");
    api.update_display();

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git pull 2>&1", &output, &len);

    if (ret == 0) {
        api.message("Pull successful");
        api.log_info("Git pull:\n%s", output ? output : "(no output)");
    } else {
        char errmsg[128] = "pull failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api.message("Git: %s", errmsg);
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-push: Push to remote */
static int cmd_git_push(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    api.message("Pushing to remote...");
    api.update_display();

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git push 2>&1", &output, &len);

    if (ret == 0) {
        api.message("Push successful");
        api.log_info("Git push:\n%s", output ? output : "(no output)");
    } else {
        char errmsg[128] = "push failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api.message("Git: %s", errmsg);
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-branch: Show current branch */
static int cmd_git_branch(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git branch --show-current 2>&1", &output, &len);

    if (ret == 0 && output) {
        /* Trim newline */
        if (len > 0 && output[len-1] == '\n') output[len-1] = '\0';
        api.message("Branch: %s", output);
    } else {
        api.message("Failed to get branch");
    }

    if (output) api.free(output);
    return 1;
}

/* git-stash: Stash current changes */
static int cmd_git_stash(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git stash 2>&1", &output, &len);

    if (ret == 0) {
        api.message("Changes stashed");
        api.log_info("Git stash:\n%s", output ? output : "(no output)");
    } else {
        api.message("Stash failed: %s", output ? output : "unknown error");
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* git-stash-pop: Pop stashed changes */
static int cmd_git_stash_pop(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api.message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api.shell_command("git stash pop 2>&1", &output, &len);

    if (ret == 0) {
        api.message("Stash popped");
        api.log_info("Git stash pop:\n%s", output ? output : "(no output)");
    } else {
        api.message("Stash pop failed: %s", output ? output : "unknown error");
    }

    if (output) api.free(output);
    return ret == 0 ? 1 : 0;
}

/* === Event Handlers === */

static int file_save_count = 0;
static int status_interval = 5;  /* Check every N saves */
static bool auto_status = true;

/*
 * Event handler for buffer save events.
 * Uses the new event bus API (v3).
 */
static bool on_buffer_save_event(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    (void)event;  /* event->data contains struct buffer* if needed */

    file_save_count++;

    /* Only check git status periodically to avoid slowdown */
    if (auto_status && file_save_count % status_interval == 0 && in_git_repo()) {
        api.log_debug("Git: File saved (%d), checking status...", file_save_count);
    }

    return false;  /* Don't consume - allow other handlers */
}

/* === Extension Lifecycle === */

static int git_init(struct uemacs_api *editor_api) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "c_git: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.config_int = (config_int_fn)LOOKUP(config_int);
    api.config_bool = (config_bool_fn)LOOKUP(config_bool);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.message = (message_fn)LOOKUP(message);
    api.update_display = (update_display_fn)LOOKUP(update_display);
    api.shell_command = (shell_command_fn)LOOKUP(shell_command);
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.alloc = (alloc_fn)LOOKUP(alloc);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.log_debug = (log_fn)LOOKUP(log_debug);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.on || !api.off || !api.register_command || !api.log_error) {
        fprintf(stderr, "c_git: Missing critical API functions\n");
        return -1;
    }

    /* Read configuration from settings.toml [extension.git] */
    if (api.config_bool) {
        auto_status = api.config_bool("git", "auto_status", true);
    }
    if (api.config_int) {
        status_interval = api.config_int("git", "status_interval", 5);
    }

    /* Register commands */
    api.register_command("git-status", cmd_git_status);
    api.register_command("git-status-full", cmd_git_status_full);
    api.register_command("git-stage", cmd_git_stage);
    api.register_command("git-unstage", cmd_git_unstage);
    api.register_command("git-commit", cmd_git_commit);
    api.register_command("git-diff", cmd_git_diff);
    api.register_command("git-log", cmd_git_log);
    api.register_command("git-pull", cmd_git_pull);
    api.register_command("git-push", cmd_git_push);
    api.register_command("git-branch", cmd_git_branch);
    api.register_command("git-stash", cmd_git_stash);
    api.register_command("git-stash-pop", cmd_git_stash_pop);

    /* Register event handlers via event bus */
    api.on(UEMACS_EVT_BUFFER_SAVE, on_buffer_save_event, NULL, 0);
    api.on("input:key", git_key_handler, NULL, 0);

    api.log_info("c_git v4.0.0 loaded (ABI-stable, buffer navigation, 12 commands)");

    /* Show welcome only if in a git repo */
    if (in_git_repo()) {
        char *output = NULL;
        size_t len = 0;
        api.shell_command("git branch --show-current 2>/dev/null", &output, &len);
        if (output && len > 0) {
            if (output[len-1] == '\n') output[len-1] = '\0';
            api.message("Git: On branch '%s'", output);
            api.free(output);
        }
    }

    return 0;
}

static void git_cleanup(void) {
    /* Unregister event handlers */
    api.off(UEMACS_EVT_BUFFER_SAVE, on_buffer_save_event);
    api.off("input:key", git_key_handler);

    /* Unregister commands */
    api.unregister_command("git-status");
    api.unregister_command("git-status-full");
    api.unregister_command("git-stage");
    api.unregister_command("git-unstage");
    api.unregister_command("git-commit");
    api.unregister_command("git-diff");
    api.unregister_command("git-log");
    api.unregister_command("git-pull");
    api.unregister_command("git-push");
    api.unregister_command("git-branch");
    api.unregister_command("git-stash");
    api.unregister_command("git-stash-pop");

    g_git_root[0] = '\0';
    api.log_info("Git workflow extension unloaded");
}

/* Extension descriptor */
static struct uemacs_extension git_ext = {
    .api_version = 4  /* ABI-stable API */,  /* Requires event bus API */
    .name = "c_git",
    .version = "4.0.0",  /* v4: ABI-stable named lookup */
    .description = "Git integration with buffer navigation",
    .init = git_init,
    .cleanup = git_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &git_ext;
}
