/*
 * lint.c - Unified Linter Extension for muEmacs
 *
 * API Version: 4 (ABI-Stable Named Lookup)
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

#include <uep/extension.h>
#include <uep/extension_api.h>

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
 * ABI-Stable API Access
 *
 * Instead of using struct offsets (which break when the API struct changes),
 * we look up functions by name at init time.
 * ============================================================================ */

/* Function pointer types for the API functions we use */
typedef int (*on_fn)(const char*, uemacs_event_fn, void*, int);
typedef int (*off_fn)(const char*, uemacs_event_fn);
typedef int (*register_command_fn)(const char*, uemacs_cmd_fn);
typedef int (*unregister_command_fn)(const char*);
typedef struct buffer *(*current_buffer_fn)(void);
typedef char *(*buffer_contents_fn)(struct buffer*, size_t*);
typedef const char *(*buffer_filename_fn)(struct buffer*);
typedef const char *(*buffer_name_fn)(struct buffer*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef struct buffer *(*buffer_create_fn)(const char*);
typedef int (*buffer_switch_fn)(struct buffer*);
typedef int (*buffer_clear_fn)(struct buffer*);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef char *(*get_current_line_fn)(void);
typedef int (*find_file_line_fn)(const char*, int);
typedef void (*message_fn)(const char*, ...);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);

/* Our local API - only the functions we actually use */
static struct {
    on_fn on;
    off_fn off;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_contents_fn buffer_contents;
    buffer_filename_fn buffer_filename;
    buffer_name_fn buffer_name;
    buffer_insert_fn buffer_insert;
    buffer_create_fn buffer_create;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    get_point_fn get_point;
    set_point_fn set_point;
    get_current_line_fn get_current_line;
    find_file_line_fn find_file_line;
    message_fn message;
    free_fn free;
    log_fn log_info;
    log_fn log_warn;
    log_fn log_error;
} api;

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
            if (api.log_warn) {
                api.log_warn("lint: Failed to compile pattern '%s'", r->name);
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
    if (!api.current_buffer) return;

    /* Clear previous pattern diagnostics */
    clear_source_diags(bp, "pattern");

    /* Get buffer contents */
    size_t len;
    char *contents = api.buffer_contents(bp, &len);
    if (!contents) return;

    const char *filename = api.buffer_filename(bp);

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

    api.free(contents);
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
    if (!event || !event->data ) return false;

    lsp_diag_event_t *lsp_event = event->data;

    /* Find buffer by filename/URI */
    /* TODO: Convert URI to path properly */
    const char *path = lsp_event->uri;
    if (strncmp(path, "file://", 7) == 0) path += 7;

    /* Find buffer with matching filename */
    struct buffer *bp = api.current_buffer();
    if (!bp) return false;

    const char *buf_file = api.buffer_filename(bp);
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
    if (!event || !event->data ) return false;

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

    if (api.log_info) {
        api.log_info("lint: Received %d diagnostics from tree-sitter", ts_event->count);
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
    if (!api.current_buffer || !api.buffer_name) return false;

    struct buffer *bp = api.current_buffer();
    if (!bp) return false;

    const char *name = api.buffer_name(bp);
    return name && strcmp(name, LINT_BUFFER_NAME) == 0;
}

/* Parse current line and jump to source location */
static bool do_lint_goto(void) {
    if (!api.get_current_line) return false;

    char *line = api.get_current_line();
    if (!line) {
        api.message("No line content");
        return false;
    }

    /* Skip to first non-space, check if it's a digit (result line) */
    const char *p = line;
    while (*p == ' ') p++;

    if (!isdigit(*p)) {
        api.message("Not on a result line");
        api.free(line);
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
        api.message("Invalid format - expected line:col");
        api.free(line);
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

    api.free(line);

    if (line_num <= 0) {
        api.message("Invalid line number");
        return false;
    }

    /* Jump to the source file */
    if (g_lint_source_file[0] == '\0') {
        api.message("No source file recorded");
        return false;
    }

    if (api.find_file_line && api.find_file_line(g_lint_source_file, line_num)) {
        /* Move to column if we have set_point */
        if (api.set_point && col > 0) {
            api.set_point(line_num, col);
        }
        api.message("%s:%d:%d", g_lint_source_file, line_num, col);
        return true;
    } else {
        api.message("Failed to open: %s", g_lint_source_file);
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
    if (!api.current_buffer) return 0;

    struct buffer *bp = api.current_buffer();
    if (!bp) return 0;

    /* Get source filename before we switch buffers */
    const char *filename = api.buffer_filename(bp);
    if (!filename) filename = api.buffer_name(bp);
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
        api.message("lint: No issues found");
        return 1;
    }

    /* Create *lint* buffer */
    struct buffer *lint_buf = api.buffer_create(LINT_BUFFER_NAME);
    if (!lint_buf) {
        api.message("lint: Failed to create buffer");
        return 0;
    }

    api.buffer_clear(lint_buf);
    api.buffer_switch(lint_buf);

    /* Write header */
    char header[512];
    snprintf(header, sizeof(header), "Lint: %s (%d issue%s)\n",
             filename, count, count == 1 ? "" : "s");
    api.buffer_insert(header, strlen(header));
    api.buffer_insert("Press Enter on a line to jump to source\n\n", 42);

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
        api.buffer_insert(line, strlen(line));
    }

    /* Position cursor on first result */
    api.set_point(4, 0);

    api.message("lint: %d issue%s - Enter to jump", count, count == 1 ? "" : "s");
    return 1;
}

static int cmd_lint_clear(int f, int n) {
    (void)f; (void)n;
    if (!api.current_buffer) return 0;

    struct buffer *bp = api.current_buffer();
    if (!bp) return 0;

    clear_buffer_diags(bp);
    api.message("lint: Diagnostics cleared");

    return 1;
}

/* ============================================================================
 * Extension Entry Points
 * ============================================================================ */

static int lint_init(struct uemacs_api *editor_api) {
    /*
     * Use get_function() for ABI stability.
     * This extension will work even if the API struct layout changes.
     */
    if (!editor_api->get_function) {
        fprintf(stderr, "c_lint: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up all API functions by name */
    #define LOOKUP(name) editor_api->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_contents = (buffer_contents_fn)LOOKUP(buffer_contents);
    api.buffer_filename = (buffer_filename_fn)LOOKUP(buffer_filename);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.find_file_line = (find_file_line_fn)LOOKUP(find_file_line);
    api.message = (message_fn)LOOKUP(message);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_warn = (log_fn)LOOKUP(log_warn);
    api.log_error = (log_fn)LOOKUP(log_error);

    #undef LOOKUP

    /* Verify critical functions were found */
    if (!api.on || !api.register_command || !api.log_info) {
        fprintf(stderr, "c_lint: Missing critical API functions\n");
        return -1;
    }

    /* Compile pattern rules */
    compile_pattern_rules();

    /* Register commands */
    api.register_command("lint", cmd_lint);
    api.register_command("lint-clear", cmd_lint_clear);

    /* Subscribe to events */
    api.on("lsp:diagnostics", on_lsp_diagnostics, NULL, 0);
    api.on("treesitter:parsed", on_treesitter_parsed, NULL, 0);
    api.on("treesitter:lint", on_treesitter_lint, NULL, 0);
    api.on("input:key", lint_key_event_handler, NULL, 0);

    api.log_info("c_lint v4.0.0 loaded (ABI-stable, buffer navigation)");

    return 0;
}

static void lint_cleanup(void) {
    /* Unregister commands */
    if (api.unregister_command) {
        api.unregister_command("lint");
        api.unregister_command("lint-clear");
    }

    /* Unsubscribe from events */
    if (api.off) {
        api.off("lsp:diagnostics", on_lsp_diagnostics);
        api.off("treesitter:parsed", on_treesitter_parsed);
        api.off("treesitter:lint", on_treesitter_lint);
        api.off("input:key", lint_key_event_handler);
    }

    /* Free compiled patterns */
    free_pattern_rules();

    /* Clear diagnostic storage */
    g_buffer_count = 0;
    g_lint_source_file[0] = '\0';
}

static struct uemacs_extension ext = {
    .api_version = 4  /* ABI-stable API */,
    .name = "c_lint",
    .version = "4.0.0",
    .description = "Unified linter with buffer navigation (Enter to jump)",
    .init = lint_init,
    .cleanup = lint_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &ext;
}
