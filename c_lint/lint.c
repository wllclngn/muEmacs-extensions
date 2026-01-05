/*
 * lint.c - Unified Linter Extension for muEmacs
 *
 * API Version: 3 (Event Bus)
 *
 * Aggregates diagnostics from three sources:
 * 1. Pattern rules (Thompson NFA, adapted from muEmacs core)
 * 2. Tree-sitter AST queries (via event bus)
 * 3. LSP diagnostics (via event bus)
 *
 * Commands: lint, lint-next, lint-prev, lint-clear, lint-list
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

/* ============================================================================
 * Thompson NFA Engine (adapted from muEmacs nfa.c for string matching)
 * Features: literals, dot (.), anchors (^,$), closure (*), char classes [...]
 * ============================================================================ */

#define NFA_MAX_STATES 512
#define NFA_MAX_LIST   1024

typedef enum { ST_CHAR, ST_ANY, ST_CLASS, ST_SPLIT, ST_MATCH, ST_BOL, ST_EOL } nfa_stype_t;

typedef struct {
    nfa_stype_t type;
    unsigned char c;
    unsigned char cls[32];
    int out;
    int out1;
} nfa_state_t;

typedef struct {
    int start_state;
    int state_count;
    bool case_sensitive;
    nfa_state_t states[NFA_MAX_STATES];
} nfa_program_t;

typedef struct {
    int idx[NFA_MAX_LIST];
    int n;
} nfa_list_t;

/* Match result */
typedef struct {
    int start;
    int end;
    bool found;
} nfa_match_t;

static inline unsigned char nfa_normalize(unsigned char c, bool cs) {
    return cs ? c : (unsigned char)tolower(c);
}

static inline void nfa_cls_set(unsigned char *cls, int b) {
    cls[b >> 3] |= (1u << (b & 7));
}

static inline int nfa_cls_has(const unsigned char *cls, int b) {
    return (cls[b >> 3] & (1u << (b & 7))) != 0;
}

static int nfa_add_state(nfa_program_t *prog, nfa_stype_t t, unsigned char c, int out, int out1) {
    if (prog->state_count >= NFA_MAX_STATES) return -1;
    int idx = prog->state_count++;
    prog->states[idx].type = t;
    prog->states[idx].c = c;
    prog->states[idx].out = out;
    prog->states[idx].out1 = out1;
    memset(prog->states[idx].cls, 0, sizeof(prog->states[idx].cls));
    return idx;
}

static void nfa_patch(nfa_program_t *prog, int s, int target) {
    if (s < 0) return;
    if (prog->states[s].type == ST_SPLIT) {
        if (prog->states[s].out1 < 0) prog->states[s].out1 = target;
        else prog->states[s].out = target;
    } else {
        prog->states[s].out = target;
    }
}

/* Compile pattern into NFA program */
static bool nfa_compile(nfa_program_t *prog, const char *pattern, bool case_sensitive) {
    if (!prog || !pattern || !*pattern) return false;

    prog->state_count = 0;
    prog->case_sensitive = case_sensitive;

    const char *p = pattern;
    bool start_anchor = false, end_anchor = false;

    if (*p == '^') { start_anchor = true; p++; }

    int start = -1, last = -1;

    while (*p && *p != '$') {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            int s = nfa_add_state(prog, ST_CHAR, nfa_normalize(*p, case_sensitive), -1, -1);
            if (s < 0) return false;
            if (start == -1) start = s; else nfa_patch(prog, last, s);
            last = s;
            p++;
            continue;
        }
        if (*p == '.') {
            int s = nfa_add_state(prog, ST_ANY, 0, -1, -1);
            if (s < 0) return false;
            if (start == -1) start = s; else nfa_patch(prog, last, s);
            last = s;
            p++;
            if (*p == '*') {
                int split = nfa_add_state(prog, ST_SPLIT, 0, -1, -1);
                if (split < 0) return false;
                prog->states[s].out = split;
                prog->states[split].out = s;
                if (start == s) start = split;
                else nfa_patch(prog, last, split);
                last = split;
                p++;
            }
            continue;
        }
        if (*p == '[') {
            p++;
            int negate = 0;
            unsigned char cls[32] = {0};
            if (*p == '^') { negate = 1; p++; }
            if (*p == ']' || *p == '\0') return false;
            while (*p && *p != ']') {
                unsigned char a = nfa_normalize(*p, case_sensitive);
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    unsigned char b = nfa_normalize(p[2], case_sensitive);
                    if (a > b) { unsigned char t = a; a = b; b = t; }
                    for (unsigned char x = a; x <= b; x++) nfa_cls_set(cls, x);
                    p += 3;
                } else {
                    nfa_cls_set(cls, a);
                    p++;
                }
            }
            if (*p != ']') return false;
            p++;
            if (negate) {
                for (int i = 0; i < 32; i++) cls[i] = ~cls[i];
                cls['\n' >> 3] &= ~(1u << ('\n' & 7));
            }
            int s = nfa_add_state(prog, ST_CLASS, 0, -1, -1);
            if (s < 0) return false;
            memcpy(prog->states[s].cls, cls, sizeof(cls));
            if (start == -1) start = s; else nfa_patch(prog, last, s);
            last = s;
            if (*p == '*') {
                int split = nfa_add_state(prog, ST_SPLIT, 0, -1, -1);
                if (split < 0) return false;
                prog->states[s].out = split;
                prog->states[split].out = s;
                if (start == s) start = split;
                else nfa_patch(prog, last, split);
                last = split;
                p++;
            }
            continue;
        }
        if (*p == '*') return false; /* Stray * */

        /* Literal char */
        int s = nfa_add_state(prog, ST_CHAR, nfa_normalize(*p, case_sensitive), -1, -1);
        if (s < 0) return false;
        if (start == -1) start = s; else nfa_patch(prog, last, s);
        last = s;
        p++;
        if (*p == '*') {
            int split = nfa_add_state(prog, ST_SPLIT, 0, -1, -1);
            if (split < 0) return false;
            prog->states[s].out = split;
            prog->states[split].out = s;
            if (start == s) start = split;
            else nfa_patch(prog, last, split);
            last = split;
            p++;
        }
    }

    if (*p == '$') { end_anchor = true; p++; }
    if (*p != '\0') return false; /* Unsupported construct */

    /* Add anchors */
    if (start_anchor) {
        int bol = nfa_add_state(prog, ST_BOL, 0, start, -1);
        if (bol < 0) return false;
        start = bol;
    }

    int match = nfa_add_state(prog, ST_MATCH, 0, -1, -1);
    if (match < 0) return false;

    if (end_anchor) {
        int eol = nfa_add_state(prog, ST_EOL, 0, match, -1);
        if (eol < 0) return false;
        if (last >= 0) nfa_patch(prog, last, eol);
        else start = eol;
    } else {
        if (last >= 0) nfa_patch(prog, last, match);
        else start = match;
    }

    prog->start_state = start;
    return true;
}

static void nfa_list_clear(nfa_list_t *l) { l->n = 0; }

static void nfa_list_add(nfa_list_t *l, int s) {
    if (l->n < NFA_MAX_LIST) l->idx[l->n++] = s;
}

/* Add state with epsilon closure */
static void nfa_add_epsilon(const nfa_program_t *prog, nfa_list_t *l, int s,
                            bool at_bol, bool at_eol) {
    while (s >= 0) {
        const nfa_state_t *st = &prog->states[s];
        if (st->type == ST_SPLIT) {
            if (st->out1 >= 0) nfa_add_epsilon(prog, l, st->out1, at_bol, at_eol);
            s = st->out;
        } else if (st->type == ST_BOL) {
            if (!at_bol) return;
            s = st->out;
        } else if (st->type == ST_EOL) {
            if (!at_eol) return;
            s = st->out;
        } else {
            nfa_list_add(l, s);
            return;
        }
    }
}

/* Step through one character */
static void nfa_step(const nfa_program_t *prog, const nfa_list_t *cur,
                     unsigned char byte, nfa_list_t *next) {
    nfa_list_clear(next);
    for (int i = 0; i < cur->n; i++) {
        int s = cur->idx[i];
        const nfa_state_t *st = &prog->states[s];
        switch (st->type) {
            case ST_CHAR:
                if (byte == st->c) nfa_list_add(next, st->out);
                break;
            case ST_ANY:
                nfa_list_add(next, st->out);
                break;
            case ST_CLASS:
                if (nfa_cls_has(st->cls, byte)) nfa_list_add(next, st->out);
                break;
            default:
                break;
        }
    }
}

/* Check if any state is a match state */
static bool nfa_has_match(const nfa_program_t *prog, const nfa_list_t *l) {
    for (int i = 0; i < l->n; i++) {
        if (prog->states[l->idx[i]].type == ST_MATCH) return true;
    }
    return false;
}

/* Search for pattern in string, return first match */
static nfa_match_t nfa_search(const nfa_program_t *prog, const char *str, int len) {
    nfa_match_t result = {0, 0, false};
    if (!prog || !str) return result;

    nfa_list_t cur = {0}, next = {0};

    for (int start_pos = 0; start_pos < len; start_pos++) {
        bool at_bol = (start_pos == 0);
        nfa_list_clear(&cur);
        nfa_add_epsilon(prog, &cur, prog->start_state, at_bol, false);

        /* Check for zero-length match at start */
        if (nfa_has_match(prog, &cur)) {
            result.start = start_pos;
            result.end = start_pos;
            result.found = true;
            return result;
        }

        for (int pos = start_pos; pos < len; pos++) {
            unsigned char byte = nfa_normalize((unsigned char)str[pos], prog->case_sensitive);
            bool at_eol = (pos == len - 1);

            nfa_step(prog, &cur, byte, &next);

            /* Epsilon closure */
            nfa_list_t closure = {0};
            for (int i = 0; i < next.n; i++) {
                nfa_add_epsilon(prog, &closure, next.idx[i], false, at_eol);
            }

            if (nfa_has_match(prog, &closure)) {
                result.start = start_pos;
                result.end = pos + 1;
                result.found = true;
                return result;
            }

            cur = closure;
            if (cur.n == 0) break; /* No active states, try next start position */
        }

        /* Check EOL at end of string */
        if (cur.n > 0) {
            nfa_list_t eol_check = {0};
            for (int i = 0; i < cur.n; i++) {
                nfa_add_epsilon(prog, &eol_check, cur.idx[i], false, true);
            }
            if (nfa_has_match(prog, &eol_check)) {
                result.start = start_pos;
                result.end = len;
                result.found = true;
                return result;
            }
        }
    }

    return result;
}

/* ============================================================================
 * API Types (mirrors extension_api.h)
 * ============================================================================ */

struct buffer;
struct window;
struct uemacs_event;

typedef struct uemacs_event {
    const char *name;
    void *data;
    size_t data_size;
    bool consumed;
} uemacs_event_t;

typedef bool (*uemacs_event_fn)(uemacs_event_t *event, void *user_data);
typedef int (*uemacs_cmd_fn)(int f, int n);

struct uemacs_api {
    int api_version;

    /* Event Bus */
    int (*on)(const char *event, uemacs_event_fn handler, void *user_data, int priority);
    int (*off)(const char *event, uemacs_event_fn handler);
    bool (*emit)(const char *event, void *data);

    /* Configuration */
    int (*config_int)(const char *ext_name, const char *key, int default_val);
    bool (*config_bool)(const char *ext_name, const char *key, bool default_val);
    const char *(*config_string)(const char *ext_name, const char *key, const char *default_val);

    /* Command Registration */
    int (*register_command)(const char *name, uemacs_cmd_fn func);
    int (*unregister_command)(const char *name);

    /* Buffer Operations */
    struct buffer *(*current_buffer)(void);
    struct buffer *(*find_buffer)(const char *name);
    char *(*buffer_contents)(struct buffer *bp, size_t *len);
    const char *(*buffer_filename)(struct buffer *bp);
    const char *(*buffer_name)(struct buffer *bp);
    bool (*buffer_modified)(struct buffer *bp);
    int (*buffer_insert)(const char *text, size_t len);
    int (*buffer_insert_at)(struct buffer *bp, int line, int col, const char *text, size_t len);
    struct buffer *(*buffer_create)(const char *name);
    int (*buffer_switch)(struct buffer *bp);
    int (*buffer_clear)(struct buffer *bp);

    /* Cursor/Point */
    void (*get_point)(int *line, int *col);
    void (*set_point)(int line, int col);
    int (*get_line_count)(struct buffer *bp);
    char *(*get_word_at_point)(void);
    char *(*get_current_line)(void);

    /* Window */
    struct window *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(struct window *wp, int col);
    struct window *(*window_at_row)(int row);
    int (*window_switch)(struct window *wp);

    /* Mouse/Cursor Helpers */
    int (*screen_to_buffer_pos)(struct window *wp, int screen_row, int screen_col,
                                int *buf_line, int *buf_offset);
    int (*set_mark)(void);
    int (*scroll_up)(int lines);
    int (*scroll_down)(int lines);

    /* UI */
    void (*message)(const char *fmt, ...);
    void (*vmessage)(const char *fmt, void *ap);
    int (*prompt)(const char *prompt, char *buf, size_t buflen);
    int (*prompt_yn)(const char *prompt);
    void (*update_display)(void);

    /* File */
    int (*find_file_line)(const char *path, int line);

    /* Shell */
    int (*shell_command)(const char *cmd, char **output, size_t *len);

    /* Memory */
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    char *(*strdup)(const char *s);

    /* Logging */
    void (*log_info)(const char *fmt, ...);
    void (*log_warn)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);

    /* Syntax */
    void *syntax_register_lexer;
    void *syntax_unregister_lexer;
    void *syntax_add_token;
    void *syntax_invalidate_buffer;
};

typedef struct {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(struct uemacs_api *);
    void (*cleanup)(void);
} uemacs_extension;

/* ============================================================================
 * Diagnostic Types
 * ============================================================================ */

typedef enum {
    LINT_ERROR   = 1,
    LINT_WARNING = 2,
    LINT_INFO    = 3,
    LINT_HINT    = 4
} lint_severity_t;

typedef struct {
    int line;           /* 1-based */
    int col;            /* 0-based */
    int end_col;        /* 0-based, -1 if unknown */
    lint_severity_t severity;
    char source[32];    /* "pattern", "treesitter", "lsp" */
    char message[256];
} lint_diagnostic_t;

#define MAX_DIAGNOSTICS 1024
#define MAX_BUFFERS 64

typedef struct {
    struct buffer *bp;
    lint_diagnostic_t diags[MAX_DIAGNOSTICS];
    int count;
    int cursor;         /* Current diagnostic index for navigation */
} buffer_diagnostics_t;

/* ============================================================================
 * Pattern Rules (using Thompson NFA)
 * ============================================================================ */

typedef struct {
    const char *name;
    const char *pattern;        /* NFA pattern */
    lint_severity_t severity;
    const char *message;
    const char *filetypes;      /* Comma-separated, NULL = all */
    nfa_program_t compiled;
    bool valid;
} pattern_rule_t;

/* Default rules - NFA patterns (simpler than POSIX regex)
 * Supported: literals, . (any), * (closure), [...] (char class), ^ (BOL), $ (EOL)
 *
 * 84 total rules across pattern (44) + semantic (40)
 */
static pattern_rule_t pattern_rules[] = {
    /* ═══════════════════════════════════════════════════════════════════════
     * STYLE RULES (10)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"trailing-whitespace", "[ \t][ \t]*$", LINT_WARNING,
     "Trailing whitespace", NULL, {0}, false},
    {"line-too-long-80", "^................................................................................", LINT_INFO,
     "Line exceeds 80 characters", NULL, {0}, false},
    {"tab-after-space", " \t", LINT_WARNING,
     "Tab after space (mixed indentation)", NULL, {0}, false},
    {"space-after-tab", "\t ", LINT_WARNING,
     "Space after tab (mixed indentation)", NULL, {0}, false},
    {"multiple-spaces", "   ", LINT_HINT,
     "Three or more consecutive spaces", NULL, {0}, false},
    {"trailing-comma", ",$", LINT_HINT,
     "Trailing comma at end of line", NULL, {0}, false},
    {"space-before-paren", " (", LINT_HINT,
     "Space before opening parenthesis", "c,h,cpp,hpp", {0}, false},
    {"no-space-after-comma", ",[^ \t\n]", LINT_HINT,
     "Missing space after comma", NULL, {0}, false},
    {"double-blank", "^$", LINT_HINT,
     "Blank line (check for multiple)", NULL, {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * DOCUMENTATION MARKERS (8)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"todo-marker", "TODO", LINT_INFO,
     "TODO marker found", NULL, {0}, false},
    {"fixme-marker", "FIXME", LINT_INFO,
     "FIXME marker found", NULL, {0}, false},
    {"xxx-marker", "XXX", LINT_WARNING,
     "XXX marker found (needs attention)", NULL, {0}, false},
    {"hack-marker", "HACK", LINT_WARNING,
     "HACK marker found", NULL, {0}, false},
    {"bug-marker", "BUG", LINT_WARNING,
     "BUG marker found", NULL, {0}, false},
    {"warn-marker", "WARNING", LINT_INFO,
     "WARNING marker in comment", NULL, {0}, false},
    {"deprecated-marker", "DEPRECATED", LINT_WARNING,
     "DEPRECATED marker found", NULL, {0}, false},
    {"noqa-marker", "noqa", LINT_HINT,
     "Lint suppression marker", NULL, {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * C SECURITY - DANGEROUS FUNCTIONS (11)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"dangerous-gets", "gets[ \t]*(", LINT_ERROR,
     "gets() is unsafe - use fgets() instead", "c,h,cpp,hpp", {0}, false},
    {"dangerous-strcpy", "strcpy[ \t]*(", LINT_WARNING,
     "strcpy() can overflow - use strncpy() or strlcpy()", "c,h,cpp,hpp", {0}, false},
    {"dangerous-strcat", "strcat[ \t]*(", LINT_WARNING,
     "strcat() can overflow - use strncat() or strlcat()", "c,h,cpp,hpp", {0}, false},
    {"dangerous-sprintf", "sprintf[ \t]*(", LINT_WARNING,
     "sprintf() can overflow - use snprintf()", "c,h,cpp,hpp", {0}, false},
    {"dangerous-vsprintf", "vsprintf[ \t]*(", LINT_WARNING,
     "vsprintf() can overflow - use vsnprintf()", "c,h,cpp,hpp", {0}, false},
    {"dangerous-scanf", "scanf[ \t]*(", LINT_WARNING,
     "scanf() without width limit can overflow", "c,h,cpp,hpp", {0}, false},
    {"dangerous-sscanf", "sscanf[ \t]*(", LINT_HINT,
     "sscanf() - ensure format specifiers have width limits", "c,h,cpp,hpp", {0}, false},
    {"dangerous-system", "system[ \t]*(", LINT_WARNING,
     "system() can be exploited - validate input carefully", "c,h,cpp,hpp", {0}, false},
    {"dangerous-popen", "popen[ \t]*(", LINT_WARNING,
     "popen() can be exploited - validate input carefully", "c,h,cpp,hpp", {0}, false},
    {"dangerous-mktemp", "mktemp[ \t]*(", LINT_WARNING,
     "mktemp() is insecure - use mkstemp()", "c,h,cpp,hpp", {0}, false},
    {"dangerous-tmpnam", "tmpnam[ \t]*(", LINT_WARNING,
     "tmpnam() is insecure - use mkstemp()", "c,h,cpp,hpp", {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * GENERAL SECURITY - HARDCODED SECRETS (7)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"hardcoded-password", "password[ \t]*=[ \t]*\"", LINT_WARNING,
     "Possible hardcoded password", NULL, {0}, false},
    {"hardcoded-passwd", "passwd[ \t]*=[ \t]*\"", LINT_WARNING,
     "Possible hardcoded password", NULL, {0}, false},
    {"hardcoded-secret", "secret[ \t]*=[ \t]*\"", LINT_WARNING,
     "Possible hardcoded secret", NULL, {0}, false},
    {"hardcoded-apikey", "api_key[ \t]*=[ \t]*\"", LINT_WARNING,
     "Possible hardcoded API key", NULL, {0}, false},
    {"hardcoded-token", "token[ \t]*=[ \t]*\"", LINT_WARNING,
     "Possible hardcoded token", NULL, {0}, false},
    {"private-key-begin", "-----BEGIN", LINT_ERROR,
     "Private key material detected", NULL, {0}, false},
    {"aws-key-pattern", "AKIA", LINT_WARNING,
     "Possible AWS access key (starts with AKIA)", NULL, {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * BUG PATTERNS (6)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"double-semicolon", ";;", LINT_WARNING,
     "Double semicolon - possible typo", NULL, {0}, false},
    {"empty-if-body", ");[ \t]*$", LINT_HINT,
     "Statement ends with ); - check for empty if body", "c,h,cpp,hpp", {0}, false},
    {"self-assign-pattern", "= *[a-z_][a-z_0-9]* *;", LINT_HINT,
     "Simple assignment - verify not self-assignment", NULL, {0}, false},
    {"null-literal-cmp", "== NULL", LINT_HINT,
     "Consider using !ptr instead of ptr == NULL", "c,h,cpp,hpp", {0}, false},
    {"null-literal-cmp2", "!= NULL", LINT_HINT,
     "Consider using ptr instead of ptr != NULL", "c,h,cpp,hpp", {0}, false},
    {"zero-division-risk", "/ 0", LINT_ERROR,
     "Division by zero", NULL, {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * DEBUG/DEV LEFTOVERS (6)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"console-log", "console.log", LINT_INFO,
     "console.log() left in code", "js,ts,jsx,tsx", {0}, false},
    {"console-debug", "console.debug", LINT_INFO,
     "console.debug() left in code", "js,ts,jsx,tsx", {0}, false},
    {"debugger-stmt", "debugger", LINT_WARNING,
     "debugger statement left in code", "js,ts,jsx,tsx", {0}, false},
    {"python-breakpoint", "breakpoint()", LINT_WARNING,
     "breakpoint() left in code", "py", {0}, false},
    {"python-pdb", "pdb.set_trace", LINT_WARNING,
     "pdb.set_trace() left in code", "py", {0}, false},
    {"debug-printf", "DEBUG", LINT_HINT,
     "DEBUG marker - verify intended for production", "c,h,cpp,hpp", {0}, false},

    /* ═══════════════════════════════════════════════════════════════════════
     * ADDITIONAL PATTERNS (4)
     * ═══════════════════════════════════════════════════════════════════════ */
    {"magic-number-large", "= [0-9][0-9][0-9][0-9]", LINT_HINT,
     "Magic number (4+ digits) - consider named constant", NULL, {0}, false},
    {"goto-statement", "goto ", LINT_INFO,
     "goto statement found", "c,h,cpp,hpp", {0}, false},
    {"infinite-loop", "while[ \t]*(1)", LINT_INFO,
     "Infinite loop - ensure exit condition exists", "c,h,cpp,hpp", {0}, false},
    {"c-cast", "([a-z_][a-z_0-9]*[ ]*\\**)[ ]*[a-z_]", LINT_HINT,
     "C-style cast - consider static_cast in C++", "cpp,hpp", {0}, false},

    /* Sentinel */
    {NULL, NULL, 0, NULL, NULL, {0}, false}
};

/* ============================================================================
 * Global State
 * ============================================================================ */

static struct uemacs_api *g_api = NULL;
static buffer_diagnostics_t g_buffer_diags[MAX_BUFFERS];
static int g_buffer_count = 0;

/* ============================================================================
 * Diagnostic Storage
 * ============================================================================ */

static buffer_diagnostics_t *get_buffer_diags(struct buffer *bp) {
    for (int i = 0; i < g_buffer_count; i++) {
        if (g_buffer_diags[i].bp == bp) {
            return &g_buffer_diags[i];
        }
    }
    /* Create new entry */
    if (g_buffer_count >= MAX_BUFFERS) {
        return NULL;
    }
    buffer_diagnostics_t *bd = &g_buffer_diags[g_buffer_count++];
    bd->bp = bp;
    bd->count = 0;
    bd->cursor = 0;
    return bd;
}

static void add_diagnostic(struct buffer *bp, int line, int col, int end_col,
                          lint_severity_t severity, const char *source,
                          const char *message) {
    buffer_diagnostics_t *bd = get_buffer_diags(bp);
    if (!bd || bd->count >= MAX_DIAGNOSTICS) return;

    lint_diagnostic_t *d = &bd->diags[bd->count++];
    d->line = line;
    d->col = col;
    d->end_col = end_col;
    d->severity = severity;
    strncpy(d->source, source, sizeof(d->source) - 1);
    d->source[sizeof(d->source) - 1] = '\0';
    strncpy(d->message, message, sizeof(d->message) - 1);
    d->message[sizeof(d->message) - 1] = '\0';
}

static void clear_buffer_diags(struct buffer *bp) {
    buffer_diagnostics_t *bd = get_buffer_diags(bp);
    if (bd) {
        bd->count = 0;
        bd->cursor = 0;
    }
}

static void clear_source_diags(struct buffer *bp, const char *source) {
    buffer_diagnostics_t *bd = get_buffer_diags(bp);
    if (!bd) return;

    int write_idx = 0;
    for (int i = 0; i < bd->count; i++) {
        if (strcmp(bd->diags[i].source, source) != 0) {
            if (write_idx != i) {
                bd->diags[write_idx] = bd->diags[i];
            }
            write_idx++;
        }
    }
    bd->count = write_idx;
    if (bd->cursor >= bd->count) {
        bd->cursor = bd->count > 0 ? bd->count - 1 : 0;
    }
}

/* Compare for sorting: by line, then column */
static int diag_compare(const void *a, const void *b) {
    const lint_diagnostic_t *da = a;
    const lint_diagnostic_t *db = b;
    if (da->line != db->line) return da->line - db->line;
    return da->col - db->col;
}

static void sort_diagnostics(struct buffer *bp) {
    buffer_diagnostics_t *bd = get_buffer_diags(bp);
    if (bd && bd->count > 1) {
        qsort(bd->diags, bd->count, sizeof(lint_diagnostic_t), diag_compare);
    }
}

/* ============================================================================
 * Pattern Matching (NFA-based)
 * ============================================================================ */

static void compile_pattern_rules(void) {
    for (int i = 0; pattern_rules[i].name != NULL; i++) {
        pattern_rule_t *r = &pattern_rules[i];
        if (nfa_compile(&r->compiled, r->pattern, true)) {
            r->valid = true;
        } else {
            r->valid = false;
            if (g_api && g_api->log_warn) {
                g_api->log_warn("lint: Failed to compile pattern '%s'", r->name);
            }
        }
    }
}

static void free_pattern_rules(void) {
    /* NFA programs don't need explicit cleanup - they're static structs */
    for (int i = 0; pattern_rules[i].name != NULL; i++) {
        pattern_rules[i].valid = false;
    }
}

static bool matches_filetype(const char *filename, const char *filetypes) {
    if (!filetypes || !filename) return true;

    /* Extract extension */
    const char *dot = strrchr(filename, '.');
    if (!dot) return false;
    const char *ext = dot + 1;

    /* Check against comma-separated list */
    const char *p = filetypes;
    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (strncmp(p, ext, len) == 0 && strlen(ext) == len) {
            return true;
        }
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static void run_pattern_rules(struct buffer *bp) {
    if (!g_api) return;

    /* Clear previous pattern diagnostics */
    clear_source_diags(bp, "pattern");

    /* Get buffer contents */
    size_t len;
    char *contents = g_api->buffer_contents(bp, &len);
    if (!contents) return;

    const char *filename = g_api->buffer_filename(bp);

    /* Process line by line */
    int line_num = 1;
    const char *line_start = contents;
    const char *end = contents + len;

    while (line_start < end) {
        /* Find end of line */
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n') line_end++;

        int line_len = (int)(line_end - line_start);

        /* Check each pattern */
        for (int i = 0; pattern_rules[i].name != NULL; i++) {
            pattern_rule_t *r = &pattern_rules[i];
            if (!r->valid) continue;
            if (!matches_filetype(filename, r->filetypes)) continue;

            /* Search for all matches on this line */
            int search_offset = 0;
            while (search_offset < line_len) {
                nfa_match_t match = nfa_search(&r->compiled,
                                               line_start + search_offset,
                                               line_len - search_offset);
                if (!match.found) break;

                int col = search_offset + match.start;
                int end_col = search_offset + match.end;

                char msg[256];
                snprintf(msg, sizeof(msg), "%s: %s", r->name, r->message);

                add_diagnostic(bp, line_num, col, end_col, r->severity, "pattern", msg);

                /* Move past this match (at least 1 char to avoid infinite loop) */
                search_offset += match.end > match.start ? match.end : match.start + 1;
            }
        }

        /* Next line */
        line_start = (line_end < end) ? line_end + 1 : end;
        line_num++;
    }

    g_api->free(contents);
}

/* ============================================================================
 * Event Handlers (for tree-sitter and LSP integration)
 * ============================================================================ */

/* LSP diagnostic event data (must match LSP extension) */
typedef struct {
    const char *uri;
    int line;
    int col;
    int end_col;
    int severity;
    const char *message;
} lsp_diag_t;

typedef struct {
    const char *uri;
    lsp_diag_t *diags;
    int count;
} lsp_diag_event_t;

static bool on_lsp_diagnostics(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data || !g_api) return false;

    lsp_diag_event_t *lsp_event = event->data;

    /* Find buffer by filename/URI */
    /* TODO: Convert URI to path properly */
    const char *path = lsp_event->uri;
    if (strncmp(path, "file://", 7) == 0) path += 7;

    /* Find buffer with matching filename */
    struct buffer *bp = g_api->current_buffer();
    if (!bp) return false;

    const char *buf_file = g_api->buffer_filename(bp);
    if (!buf_file || strcmp(buf_file, path) != 0) {
        /* TODO: Search all buffers */
        return false;
    }

    /* Clear previous LSP diagnostics for this buffer */
    clear_source_diags(bp, "lsp");

    /* Add new diagnostics */
    for (int i = 0; i < lsp_event->count; i++) {
        lsp_diag_t *d = &lsp_event->diags[i];
        lint_severity_t sev = LINT_INFO;
        switch (d->severity) {
            case 1: sev = LINT_ERROR; break;
            case 2: sev = LINT_WARNING; break;
            case 3: sev = LINT_INFO; break;
            case 4: sev = LINT_HINT; break;
        }
        add_diagnostic(bp, d->line, d->col, d->end_col, sev, "lsp", d->message);
    }

    sort_diagnostics(bp);
    return false; /* Don't consume */
}

static bool on_treesitter_parsed(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    /* Buffer was parsed by tree-sitter - lint events come separately */
    return false; /* Don't consume */
}

/* Tree-sitter lint event data (must match tree-sitter extension) */
typedef struct {
    uint32_t line;
    uint32_t col;
    uint32_t end_col;
    uint8_t severity;
    const char *rule;
    const char *message;
} ts_lint_diag_t;

typedef struct {
    struct buffer *buffer;
    ts_lint_diag_t *diags;
    uint32_t count;
} ts_lint_event_t;

static bool on_treesitter_lint(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data || !g_api) return false;

    ts_lint_event_t *ts_event = event->data;
    struct buffer *bp = ts_event->buffer;
    if (!bp) return false;

    /* Clear previous tree-sitter diagnostics for this buffer */
    clear_source_diags(bp, "treesitter");

    /* Add new diagnostics */
    for (uint32_t i = 0; i < ts_event->count; i++) {
        ts_lint_diag_t *d = &ts_event->diags[i];
        lint_severity_t sev = LINT_INFO;
        switch (d->severity) {
            case 1: sev = LINT_ERROR; break;
            case 2: sev = LINT_WARNING; break;
            case 3: sev = LINT_INFO; break;
            case 4: sev = LINT_HINT; break;
        }

        /* Build message with rule name */
        char msg[300];
        snprintf(msg, sizeof(msg), "[%s] %s", d->rule, d->message);

        add_diagnostic(bp, d->line, d->col, d->end_col, sev, "treesitter", msg);
    }

    sort_diagnostics(bp);

    if (g_api->log_info) {
        g_api->log_info("lint: Received %d diagnostics from tree-sitter", ts_event->count);
    }

    return false; /* Don't consume */
}

/* ============================================================================
 * Lint Buffer Navigation (Enter to jump to source)
 * ============================================================================ */

#define LINT_BUFFER_NAME "*lint*"

/* Store the source filename for the current lint results */
static char g_lint_source_file[512] = {0};

/* Check if we're in the *lint* buffer */
static bool in_lint_buffer(void) {
    if (!g_api || !g_api->current_buffer || !g_api->buffer_name) return false;

    struct buffer *bp = g_api->current_buffer();
    if (!bp) return false;

    const char *name = g_api->buffer_name(bp);
    return name && strcmp(name, LINT_BUFFER_NAME) == 0;
}

/* Parse current line and jump to source location */
static bool do_lint_goto(void) {
    if (!g_api || !g_api->get_current_line) return false;

    char *line = g_api->get_current_line();
    if (!line) {
        g_api->message("No line content");
        return false;
    }

    /* Skip to first non-space, check if it's a digit (result line) */
    const char *p = line;
    while (*p == ' ') p++;

    if (!isdigit(*p)) {
        g_api->message("Not on a result line");
        g_api->free(line);
        return false;
    }

    /* Parse line:col from format "  123:  5 [WARN] ..." */

    int line_num = 0, col = 0;

    /* Parse line number */
    while (isdigit(*p)) {
        line_num = line_num * 10 + (*p - '0');
        p++;
    }

    if (*p != ':') {
        g_api->message("Invalid format - expected line:col");
        g_api->free(line);
        return false;
    }
    p++;  /* Skip ':' */

    /* Skip spaces after colon */
    while (*p == ' ') p++;

    /* Parse column */
    while (isdigit(*p)) {
        col = col * 10 + (*p - '0');
        p++;
    }

    g_api->free(line);

    if (line_num <= 0) {
        g_api->message("Invalid line number");
        return false;
    }

    /* Jump to the source file */
    if (g_lint_source_file[0] == '\0') {
        g_api->message("No source file recorded");
        return false;
    }

    if (g_api->find_file_line && g_api->find_file_line(g_lint_source_file, line_num)) {
        /* Move to column if we have set_point */
        if (g_api->set_point && col > 0) {
            g_api->set_point(line_num, col);
        }
        g_api->message("%s:%d:%d", g_lint_source_file, line_num, col);
        return true;
    } else {
        g_api->message("Failed to open: %s", g_lint_source_file);
        return false;
    }
}

/* Event handler for key input - intercept Enter in *lint* buffer */
static bool lint_key_event_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;
    if (!event || !event->data) return false;

    /* Event data is a pointer to the key code */
    int key = *(int *)event->data;

    /* Only handle Enter (CR = 13, LF = 10) */
    if (key != '\r' && key != '\n') {
        return false;  /* Not our key */
    }

    /* Check if we're in the lint buffer */
    if (!in_lint_buffer()) {
        return false;  /* Not in our buffer */
    }

    /* We're in the lint buffer and Enter was pressed - handle it */
    do_lint_goto();
    return true;  /* Event consumed */
}

/* ============================================================================
 * Commands
 * ============================================================================ */

static int cmd_lint(int f, int n) {
    (void)f; (void)n;
    if (!g_api) return 0;

    struct buffer *bp = g_api->current_buffer();
    if (!bp) return 0;

    /* Get source filename before we switch buffers */
    const char *filename = g_api->buffer_filename(bp);
    if (!filename) filename = g_api->buffer_name(bp);
    if (!filename) filename = "(unknown)";

    /* Store for navigation */
    strncpy(g_lint_source_file, filename, sizeof(g_lint_source_file) - 1);
    g_lint_source_file[sizeof(g_lint_source_file) - 1] = '\0';

    /* Clear all diagnostics for this buffer */
    clear_buffer_diags(bp);

    /* Run pattern rules */
    run_pattern_rules(bp);

    /* Sort all diagnostics */
    sort_diagnostics(bp);

    buffer_diagnostics_t *bd = get_buffer_diags(bp);
    int count = bd ? bd->count : 0;

    if (count == 0) {
        g_api->message("lint: No issues found");
        return 1;
    }

    /* Create *lint* buffer */
    struct buffer *lint_buf = g_api->buffer_create(LINT_BUFFER_NAME);
    if (!lint_buf) {
        g_api->message("lint: Failed to create buffer");
        return 0;
    }

    g_api->buffer_clear(lint_buf);
    g_api->buffer_switch(lint_buf);

    /* Write header */
    char header[512];
    snprintf(header, sizeof(header), "Lint: %s (%d issue%s)\n",
             filename, count, count == 1 ? "" : "s");
    g_api->buffer_insert(header, strlen(header));
    g_api->buffer_insert("Press Enter on a line to jump to source\n\n", 42);

    /* Write each diagnostic */
    for (int i = 0; i < bd->count; i++) {
        lint_diagnostic_t *d = &bd->diags[i];
        const char *sev_str = "?";
        switch (d->severity) {
            case LINT_ERROR:   sev_str = "ERROR"; break;
            case LINT_WARNING: sev_str = "WARN "; break;
            case LINT_INFO:    sev_str = "INFO "; break;
            case LINT_HINT:    sev_str = "HINT "; break;
        }

        char line[512];
        snprintf(line, sizeof(line), "%4d:%3d [%s] %s\n",
                 d->line, d->col, sev_str, d->message);
        g_api->buffer_insert(line, strlen(line));
    }

    /* Position cursor on first result */
    g_api->set_point(4, 0);

    g_api->message("lint: %d issue%s - Enter to jump", count, count == 1 ? "" : "s");
    return 1;
}

static int cmd_lint_clear(int f, int n) {
    (void)f; (void)n;
    if (!g_api) return 0;

    struct buffer *bp = g_api->current_buffer();
    if (!bp) return 0;

    clear_buffer_diags(bp);
    g_api->message("lint: Diagnostics cleared");

    return 1;
}

/* ============================================================================
 * Extension Entry Points
 * ============================================================================ */

static int lint_init(struct uemacs_api *api) {
    g_api = api;

    /* Compile pattern rules */
    compile_pattern_rules();

    /* Register commands */
    if (api->register_command) {
        api->register_command("lint", cmd_lint);
        api->register_command("lint-clear", cmd_lint_clear);
    }

    /* Subscribe to events */
    if (api->on) {
        /* External diagnostic sources */
        api->on("lsp:diagnostics", on_lsp_diagnostics, NULL, 0);
        api->on("treesitter:parsed", on_treesitter_parsed, NULL, 0);
        api->on("treesitter:lint", on_treesitter_lint, NULL, 0);
        /* Key handler for Enter in *lint* buffer */
        api->on("input:key", lint_key_event_handler, NULL, 0);
    }

    if (api->log_info) {
        api->log_info("lint: Extension loaded (v3.0, buffer navigation)");
    }

    return 0;
}

static void lint_cleanup(void) {
    if (g_api) {
        /* Unregister commands */
        if (g_api->unregister_command) {
            g_api->unregister_command("lint");
            g_api->unregister_command("lint-clear");
        }

        /* Unsubscribe from events */
        if (g_api->off) {
            g_api->off("lsp:diagnostics", on_lsp_diagnostics);
            g_api->off("treesitter:parsed", on_treesitter_parsed);
            g_api->off("treesitter:lint", on_treesitter_lint);
            g_api->off("input:key", lint_key_event_handler);
        }
    }

    /* Free compiled patterns */
    free_pattern_rules();

    /* Clear diagnostic storage */
    g_buffer_count = 0;
    g_lint_source_file[0] = '\0';
}

static uemacs_extension ext = {
    .api_version = 3,
    .name = "c_lint",
    .version = "3.0.0",
    .description = "Unified linter with buffer navigation (Enter to jump)",
    .init = lint_init,
    .cleanup = lint_cleanup,
};

uemacs_extension *uemacs_extension_entry(void) {
    return &ext;
}
