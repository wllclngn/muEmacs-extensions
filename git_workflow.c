/*
 * git_workflow.c - Git Integration Extension for μEmacs
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
 * Compile: gcc -shared -fPIC -o git_workflow.so git_workflow.c
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

/* Buffer name for git output */
#define GIT_BUFFER "*git*"

/* Maximum command output size */
#define MAX_OUTPUT (64 * 1024)

/* Check if current buffer is in a git repository */
static bool in_git_repo(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git rev-parse --is-inside-work-tree 2>/dev/null", &output, &len);
    bool result = (ret == 0 && output && strncmp(output, "true", 4) == 0);
    if (output) api->free(output);
    return result;
}

/* Get the git root directory */
static char *get_git_root(void) {
    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git rev-parse --show-toplevel 2>/dev/null", &output, &len);
    if (ret == 0 && output && len > 0) {
        /* Trim newline */
        if (output[len-1] == '\n') output[len-1] = '\0';
        return output;
    }
    if (output) api->free(output);
    return NULL;
}

/* Run a git command and display output in message line or buffer */
static int run_git_cmd(const char *cmd, bool show_in_buffer) {
    char *output = NULL;
    size_t len = 0;

    api->log_debug("Git: Running: %s", cmd);

    int ret = api->shell_command(cmd, &output, &len);

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
            api->message("Git error: %s", errmsg);
        } else {
            api->message("Git command failed (exit %d)", ret);
        }
        if (output) api->free(output);
        return 0;
    }

    if (show_in_buffer && output && len > 0) {
        /* Insert output into current buffer */
        api->buffer_insert(output, len);
    } else if (output && len > 0) {
        /* Show in message line (first line only) */
        char msg[256];
        strncpy(msg, output, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        char *nl = strchr(msg, '\n');
        if (nl) *nl = '\0';
        api->message("%s", msg);
    }

    if (output) api->free(output);
    return 1;
}

/* === Git Commands === */

/* git-status: Show repository status */
static int cmd_git_status(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git status --short --branch", &output, &len);

    if (ret == 0 && output) {
        /* Format nicely for message */
        api->message("Git: %.*s", (int)(len > 200 ? 200 : len), output);
        api->log_info("Git status:\n%s", output);
    } else {
        api->message("Failed to get git status");
    }

    if (output) api->free(output);
    return 1;
}

/* git-status-buffer: Show full status in a buffer */
static int cmd_git_status_full(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git status", &output, &len);

    if (ret == 0 && output && len > 0) {
        api->buffer_insert("=== Git Status ===\n\n", 21);
        api->buffer_insert(output, len);
        api->message("Git status displayed");
    } else {
        api->message("Failed to get git status");
    }

    if (output) api->free(output);
    return 1;
}

/* git-stage: Stage the current file */
static int cmd_git_stage(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    const char *filename = api->buffer_filename(api->current_buffer());
    if (!filename || !*filename) {
        api->message("Buffer has no file");
        return 0;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git add -- '%s' 2>&1", filename);

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret == 0) {
        api->message("Staged: %s", filename);
        api->log_info("Git: Staged %s", filename);
    } else {
        api->message("Failed to stage: %s", output ? output : "unknown error");
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-unstage: Unstage the current file */
static int cmd_git_unstage(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    const char *filename = api->buffer_filename(api->current_buffer());
    if (!filename || !*filename) {
        api->message("Buffer has no file");
        return 0;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git reset HEAD -- '%s' 2>&1", filename);

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret == 0) {
        api->message("Unstaged: %s", filename);
        api->log_info("Git: Unstaged %s", filename);
    } else {
        api->message("Failed to unstage: %s", output ? output : "unknown error");
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-commit: Commit staged changes */
static int cmd_git_commit(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    /* Prompt for commit message */
    char message[512];
    if (api->prompt("Commit message: ", message, sizeof(message)) != 0) {
        api->message("Commit aborted");
        return 0;
    }

    if (!message[0]) {
        api->message("Empty commit message, aborted");
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
    int ret = api->shell_command(cmd, &output, &len);

    if (ret == 0) {
        api->message("Committed successfully");
        api->log_info("Git: Committed with message: %s", message);
    } else {
        /* Show first line of error */
        char errmsg[128] = "commit failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            errmsg[sizeof(errmsg) - 1] = '\0';
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api->message("Git: %s", errmsg);
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-diff: Show diff of current file */
static int cmd_git_diff(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    const char *filename = api->buffer_filename(api->current_buffer());
    char cmd[1024];

    if (filename && *filename) {
        snprintf(cmd, sizeof(cmd), "git diff --color=never -- '%s' 2>&1", filename);
    } else {
        snprintf(cmd, sizeof(cmd), "git diff --color=never 2>&1");
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret == 0) {
        if (output && len > 0) {
            api->buffer_insert("\n=== Git Diff ===\n\n", 19);
            api->buffer_insert(output, len);
            api->message("Diff displayed (%zu bytes)", len);
        } else {
            api->message("No changes");
        }
    } else {
        api->message("Failed to get diff");
    }

    if (output) api->free(output);
    return 1;
}

/* git-log: Show commit history */
static int cmd_git_log(int f, int n) {
    (void)f;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    /* Use count argument for number of commits, default 10 */
    int count = (n > 0 && n < 100) ? n : 10;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "git log --oneline --graph --decorate -n %d 2>&1", count);

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command(cmd, &output, &len);

    if (ret == 0 && output && len > 0) {
        api->buffer_insert("\n=== Git Log ===\n\n", 18);
        api->buffer_insert(output, len);
        api->message("Showing %d commits", count);
    } else {
        api->message("Failed to get log");
    }

    if (output) api->free(output);
    return 1;
}

/* git-pull: Pull from remote */
static int cmd_git_pull(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    api->message("Pulling from remote...");
    api->update_display();

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git pull 2>&1", &output, &len);

    if (ret == 0) {
        api->message("Pull successful");
        api->log_info("Git pull:\n%s", output ? output : "(no output)");
    } else {
        char errmsg[128] = "pull failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api->message("Git: %s", errmsg);
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-push: Push to remote */
static int cmd_git_push(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    api->message("Pushing to remote...");
    api->update_display();

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git push 2>&1", &output, &len);

    if (ret == 0) {
        api->message("Push successful");
        api->log_info("Git push:\n%s", output ? output : "(no output)");
    } else {
        char errmsg[128] = "push failed";
        if (output) {
            strncpy(errmsg, output, sizeof(errmsg) - 1);
            char *nl = strchr(errmsg, '\n');
            if (nl) *nl = '\0';
        }
        api->message("Git: %s", errmsg);
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-branch: Show current branch */
static int cmd_git_branch(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git branch --show-current 2>&1", &output, &len);

    if (ret == 0 && output) {
        /* Trim newline */
        if (len > 0 && output[len-1] == '\n') output[len-1] = '\0';
        api->message("Branch: %s", output);
    } else {
        api->message("Failed to get branch");
    }

    if (output) api->free(output);
    return 1;
}

/* git-stash: Stash current changes */
static int cmd_git_stash(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git stash 2>&1", &output, &len);

    if (ret == 0) {
        api->message("Changes stashed");
        api->log_info("Git stash:\n%s", output ? output : "(no output)");
    } else {
        api->message("Stash failed: %s", output ? output : "unknown error");
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* git-stash-pop: Pop stashed changes */
static int cmd_git_stash_pop(int f, int n) {
    (void)f; (void)n;

    if (!in_git_repo()) {
        api->message("Not in a git repository");
        return 0;
    }

    char *output = NULL;
    size_t len = 0;
    int ret = api->shell_command("git stash pop 2>&1", &output, &len);

    if (ret == 0) {
        api->message("Stash popped");
        api->log_info("Git stash pop:\n%s", output ? output : "(no output)");
    } else {
        api->message("Stash pop failed: %s", output ? output : "unknown error");
    }

    if (output) api->free(output);
    return ret == 0 ? 1 : 0;
}

/* === Hooks === */

static int file_save_count = 0;

/* Called when a buffer is saved */
static void on_buffer_save(struct buffer *bp) {
    file_save_count++;

    /* Only check git status periodically to avoid slowdown */
    if (file_save_count % 5 == 0 && in_git_repo()) {
        api->log_debug("Git: File saved, checking status...");
    }
}

/* === Extension Lifecycle === */

static int git_init(struct uemacs_api *editor_api) {
    api = editor_api;

    if (api->api_version < 1) {
        api->log_error("Git: API version too old");
        return -1;
    }

    /* Register commands */
    api->register_command("git-status", cmd_git_status);
    api->register_command("git-status-full", cmd_git_status_full);
    api->register_command("git-stage", cmd_git_stage);
    api->register_command("git-unstage", cmd_git_unstage);
    api->register_command("git-commit", cmd_git_commit);
    api->register_command("git-diff", cmd_git_diff);
    api->register_command("git-log", cmd_git_log);
    api->register_command("git-pull", cmd_git_pull);
    api->register_command("git-push", cmd_git_push);
    api->register_command("git-branch", cmd_git_branch);
    api->register_command("git-stash", cmd_git_stash);
    api->register_command("git-stash-pop", cmd_git_stash_pop);

    /* Register hooks */
    api->on_buffer_save(on_buffer_save);

    api->log_info("Git workflow extension loaded (12 commands)");

    /* Show welcome only if in a git repo */
    if (in_git_repo()) {
        char *output = NULL;
        size_t len = 0;
        api->shell_command("git branch --show-current 2>/dev/null", &output, &len);
        if (output && len > 0) {
            if (output[len-1] == '\n') output[len-1] = '\0';
            api->message("Git: On branch '%s'", output);
            api->free(output);
        }
    }

    return 0;
}

static void git_cleanup(void) {
    /* Unregister hooks */
    api->off_buffer_save(on_buffer_save);

    /* Unregister commands */
    api->unregister_command("git-status");
    api->unregister_command("git-status-full");
    api->unregister_command("git-stage");
    api->unregister_command("git-unstage");
    api->unregister_command("git-commit");
    api->unregister_command("git-diff");
    api->unregister_command("git-log");
    api->unregister_command("git-pull");
    api->unregister_command("git-push");
    api->unregister_command("git-branch");
    api->unregister_command("git-stash");
    api->unregister_command("git-stash-pop");

    api->log_info("Git workflow extension unloaded");
}

/* Extension descriptor */
static struct uemacs_extension git_ext = {
    .api_version = UEMACS_API_VERSION,
    .name = "git-workflow",
    .version = "1.0.0",
    .description = "Git integration for μEmacs",
    .init = git_init,
    .cleanup = git_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &git_ext;
}
