/*
 * bridge.c - C Bridge for Haskell Scientific Calculator Extension
 *
 * API Version: 4 (ABI-Stable Named Lookup)
 *
 * SpeedCrunch-style calculator with:
 * - Dedicated *calc* buffer with REPL interface
 * - Syntax highlighting for expressions
 * - Variables and expression history
 * - Scientific functions (sin, cos, log, etc.)
 *
 * IMPORTANT: Must call hs_init() before any Haskell code and hs_exit() on cleanup.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <uep/extension_api.h>
#include <uep/extension.h>

/* Forward declarations for syntax system */
struct syntax_language;

/* Haskell runtime - conditionally compiled when USE_HASKELL is defined */
#ifdef USE_HASKELL
extern void hs_init(int *argc, char ***argv);
extern void hs_exit(void);
extern char *hs_calc_eval(const char *expr);
extern char *hs_calc_format_hex(double val);
extern char *hs_calc_format_bin(double val);
extern char *hs_calc_format_oct(double val);
extern void hs_calc_free(char *ptr);
#endif

/* Function pointer types for the API functions we use */
typedef int (*cmd_fn_t)(int, int);
typedef bool (*event_fn_t)(uemacs_event_t*, void*);
typedef int (*on_fn)(const char*, event_fn_t, void*, int);
typedef int (*off_fn)(const char*, event_fn_t);
typedef int (*register_command_fn)(const char*, cmd_fn_t);
typedef int (*unregister_command_fn)(const char*);
typedef void *(*current_buffer_fn)(void);
typedef void *(*buffer_create_fn)(const char*);
typedef void *(*find_buffer_fn)(const char*);
typedef int (*buffer_switch_fn)(void*);
typedef int (*buffer_clear_fn)(void*);
typedef int (*buffer_insert_fn)(const char*, size_t);
typedef const char *(*buffer_name_fn)(void*);
typedef char *(*get_current_line_fn)(void);
typedef void (*get_point_fn)(int*, int*);
typedef void (*set_point_fn)(int, int);
typedef void (*message_fn)(const char*, ...);
typedef int (*prompt_fn)(const char*, char*, size_t);
typedef void (*free_fn)(void*);
typedef void (*log_fn)(const char*, ...);
typedef int (*syntax_add_token_fn)(uemacs_line_tokens_t*, int, int);
typedef int (*syntax_register_lexer_fn)(const char*, const char**, uemacs_syntax_lex_fn, void*);
typedef int (*syntax_unregister_lexer_fn)(const char*);

/* Our local API */
static struct {
    on_fn on;
    off_fn off;
    register_command_fn register_command;
    unregister_command_fn unregister_command;
    current_buffer_fn current_buffer;
    buffer_create_fn buffer_create;
    find_buffer_fn find_buffer;
    buffer_switch_fn buffer_switch;
    buffer_clear_fn buffer_clear;
    buffer_insert_fn buffer_insert;
    buffer_name_fn buffer_name;
    get_current_line_fn get_current_line;
    get_point_fn get_point;
    set_point_fn set_point;
    message_fn message;
    prompt_fn prompt;
    free_fn free;
    log_fn log_info;
    log_fn log_error;
    log_fn log_debug;
    syntax_add_token_fn syntax_add_token;
    syntax_register_lexer_fn syntax_register_lexer;
    syntax_unregister_lexer_fn syntax_unregister_lexer;
} api;

/* Calculator state */
static double g_last_result = 0.0;
static bool g_has_result = false;

#define CALC_BUFFER "*calc*"

/* Built-in expression evaluator (fallback if Haskell not available) */
static double eval_simple(const char *expr, bool *ok);

/* Scientific functions table */
static struct {
    const char *name;
    double (*fn)(double);
} g_functions[] = {
    {"sin", sin}, {"cos", cos}, {"tan", tan},
    {"asin", asin}, {"acos", acos}, {"atan", atan},
    {"sinh", sinh}, {"cosh", cosh}, {"tanh", tanh},
    {"log", log10}, {"ln", log}, {"log2", log2},
    {"exp", exp}, {"sqrt", sqrt}, {"cbrt", cbrt},
    {"abs", fabs}, {"floor", floor}, {"ceil", ceil},
    {"round", round},
    {NULL, NULL}
};

/* Constants */
static struct {
    const char *name;
    double value;
} g_constants[] = {
    {"pi", 3.14159265358979323846},
    {"e", 2.71828182845904523536},
    {"phi", 1.61803398874989484820},  /* Golden ratio */
    {"tau", 6.28318530717958647692},  /* 2*pi */
    {NULL, 0}
};

/* Simple recursive descent parser for expressions */
static const char *g_expr_ptr;

static double parse_expr(bool *ok);
static double parse_term(bool *ok);
static double parse_power(bool *ok);
static double parse_unary(bool *ok);
static double parse_primary(bool *ok);

static void skip_ws(void) {
    while (*g_expr_ptr && isspace(*g_expr_ptr)) g_expr_ptr++;
}

static double parse_number(bool *ok) {
    skip_ws();
    char *end;
    double val;

    /* Hex: 0x... */
    if (g_expr_ptr[0] == '0' && (g_expr_ptr[1] == 'x' || g_expr_ptr[1] == 'X')) {
        long lval = strtol(g_expr_ptr, &end, 16);
        if (end == g_expr_ptr) { *ok = false; return 0; }
        g_expr_ptr = end;
        return (double)lval;
    }

    /* Binary: 0b... */
    if (g_expr_ptr[0] == '0' && (g_expr_ptr[1] == 'b' || g_expr_ptr[1] == 'B')) {
        g_expr_ptr += 2;
        long lval = strtol(g_expr_ptr, &end, 2);
        if (end == g_expr_ptr) { *ok = false; return 0; }
        g_expr_ptr = end;
        return (double)lval;
    }

    val = strtod(g_expr_ptr, &end);
    if (end == g_expr_ptr) { *ok = false; return 0; }
    g_expr_ptr = end;
    return val;
}

static double parse_primary(bool *ok) {
    skip_ws();

    /* Parentheses */
    if (*g_expr_ptr == '(') {
        g_expr_ptr++;
        double val = parse_expr(ok);
        skip_ws();
        if (*g_expr_ptr == ')') g_expr_ptr++;
        return val;
    }

    /* Identifier: constant, variable, or function */
    if (isalpha(*g_expr_ptr) || *g_expr_ptr == '_') {
        char name[64];
        int i = 0;
        while ((isalnum(*g_expr_ptr) || *g_expr_ptr == '_') && i < 63) {
            name[i++] = *g_expr_ptr++;
        }
        name[i] = '\0';

        skip_ws();

        /* Check for function call */
        if (*g_expr_ptr == '(') {
            g_expr_ptr++;
            double arg = parse_expr(ok);
            skip_ws();
            if (*g_expr_ptr == ')') g_expr_ptr++;

            for (int j = 0; g_functions[j].name; j++) {
                if (strcmp(name, g_functions[j].name) == 0) {
                    return g_functions[j].fn(arg);
                }
            }
            *ok = false;
            return 0;
        }

        /* Check constants */
        for (int j = 0; g_constants[j].name; j++) {
            if (strcmp(name, g_constants[j].name) == 0) {
                return g_constants[j].value;
            }
        }

        /* Check 'ans' for last result */
        if (strcmp(name, "ans") == 0) {
            return g_last_result;
        }

        *ok = false;
        return 0;
    }

    /* Number */
    return parse_number(ok);
}

static double parse_unary(bool *ok) {
    skip_ws();
    if (*g_expr_ptr == '-') {
        g_expr_ptr++;
        return -parse_unary(ok);
    }
    if (*g_expr_ptr == '+') {
        g_expr_ptr++;
        return parse_unary(ok);
    }
    return parse_primary(ok);
}

static double parse_power(bool *ok) {
    double base = parse_unary(ok);
    skip_ws();
    if (*g_expr_ptr == '^') {
        g_expr_ptr++;
        double exp = parse_power(ok);  /* Right associative */
        return pow(base, exp);
    }
    return base;
}

static double parse_term(bool *ok) {
    double left = parse_power(ok);
    while (1) {
        skip_ws();
        if (*g_expr_ptr == '*') {
            g_expr_ptr++;
            left *= parse_power(ok);
        } else if (*g_expr_ptr == '/') {
            g_expr_ptr++;
            double right = parse_power(ok);
            if (right == 0) { *ok = false; return 0; }
            left /= right;
        } else if (*g_expr_ptr == '%') {
            g_expr_ptr++;
            left = fmod(left, parse_power(ok));
        } else {
            break;
        }
    }
    return left;
}

static double parse_expr(bool *ok) {
    double left = parse_term(ok);
    while (1) {
        skip_ws();
        if (*g_expr_ptr == '+') {
            g_expr_ptr++;
            left += parse_term(ok);
        } else if (*g_expr_ptr == '-') {
            g_expr_ptr++;
            left -= parse_term(ok);
        } else {
            break;
        }
    }
    return left;
}

static double eval_simple(const char *expr, bool *ok) {
    *ok = true;
    g_expr_ptr = expr;
    double result = parse_expr(ok);
    skip_ws();
    if (*g_expr_ptr != '\0') *ok = false;
    return result;
}

/* Format result */
static void format_result(double val, char *buf, size_t buflen) {
    if (val == floor(val) && fabs(val) < 1e15) {
        snprintf(buf, buflen, "= %.0f", val);
    } else {
        snprintf(buf, buflen, "= %.10g", val);
    }
}

/* Evaluate current line and insert result */
static void calc_eval_line(void) {
    if (!api.get_current_line) return;

    char *line = api.get_current_line();
    if (!line || line[0] == '\0') {
        if (api.log_debug) api.log_debug("haskell_calc: eval_line - empty or NULL line");
        if (line && api.free) api.free(line);
        return;
    }

    /* Trim */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }

    if (api.log_debug) api.log_debug("haskell_calc: eval_line - line='%s' len=%zu", line, len);

    /* Skip empty lines or result lines */
    if (len == 0 || line[0] == '=' || line[0] == '#') {
        if (api.log_debug) api.log_debug("haskell_calc: eval_line - skipping (empty/result/comment)");
        if (api.free) api.free(line);
        return;
    }

    /* Evaluate */
    bool ok;
    double result = eval_simple(line, &ok);

    if (api.log_debug) api.log_debug("haskell_calc: eval_line - eval ok=%d result=%g", ok, result);

    if (api.free) api.free(line);

    if (!ok) {
        if (api.message) api.message("Parse error");
        return;
    }

    g_last_result = result;
    g_has_result = true;

    /* Insert result on next line */
    char result_str[128];
    format_result(result, result_str, sizeof(result_str));
    strcat(result_str, "\n");

    /* Move to end of line and insert newline + result */
    if (api.buffer_insert) {
        api.buffer_insert("\n", 1);
        api.buffer_insert(result_str, strlen(result_str));
    }
}

/* Key handler - intercept Enter in *calc* buffer */
static bool calc_key_handler(uemacs_event_t *event, void *user_data) {
    (void)user_data;

    if (!event || !event->data) return false;

    int key = *(int *)event->data;

    /* Debug: log received key */
    if (api.log_debug) {
        api.log_debug("haskell_calc: key_handler received key=0x%02X ('%c')", key, (key >= 32 && key < 127) ? key : '?');
    }

    /* Only handle Enter */
    if (key != '\r' && key != '\n') return false;

    /* Check if we're in *calc* buffer */
    if (!api.current_buffer || !api.buffer_name) return false;

    void *bp = api.current_buffer();
    if (!bp) return false;

    const char *name = api.buffer_name(bp);
    if (!name || strcmp(name, CALC_BUFFER) != 0) return false;

    /* Evaluate the current line */
    calc_eval_line();
    return true;
}

/* Syntax highlighting lexer for calc buffer */
static uemacs_lexer_state_t calc_lexer(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int line_len,
    uemacs_lexer_state_t prev_state,
    uemacs_line_tokens_t *tokens)
{
    (void)lang;
    (void)buffer;
    (void)line_num;
    (void)prev_state;

    if (!line || !tokens || !api.syntax_add_token) return UEMACS_LEXER_STATE_INIT;

    int i = 0;
    while (i < line_len) {
        /* Skip whitespace */
        if (isspace(line[i])) {
            i++;
            continue;
        }

        int start = i;

        /* Result line (starts with = or #) */
        if (line[i] == '=' || line[i] == '#') {
            api.syntax_add_token(tokens, line_len, UEMACS_FACE_STRING);
            break;
        }

        /* Numbers (including hex 0x, binary 0b) */
        if (isdigit(line[i]) || (line[i] == '.' && i + 1 < line_len && isdigit(line[i+1]))) {
            while (i < line_len && (isxdigit(line[i]) || line[i] == '.' ||
                   line[i] == 'x' || line[i] == 'X' ||
                   line[i] == 'b' || line[i] == 'B' ||
                   line[i] == 'e' || line[i] == 'E' ||
                   line[i] == '+' || line[i] == '-')) {
                i++;
            }
            api.syntax_add_token(tokens, i, UEMACS_FACE_NUMBER);
            continue;
        }

        /* Identifiers (functions, constants, variables) */
        if (isalpha(line[i]) || line[i] == '_') {
            while (i < line_len && (isalnum(line[i]) || line[i] == '_')) {
                i++;
            }
            int len = i - start;
            char word[65];
            if (len < 64) {
                memcpy(word, line + start, len);
                word[len] = '\0';

                /* Check if it's a function */
                bool is_func = false;
                for (int j = 0; g_functions[j].name; j++) {
                    if (strcmp(word, g_functions[j].name) == 0) {
                        is_func = true;
                        break;
                    }
                }

                if (is_func) {
                    api.syntax_add_token(tokens, i, UEMACS_FACE_FUNCTION);
                } else {
                    /* Check if it's a constant */
                    bool is_const = false;
                    for (int j = 0; g_constants[j].name; j++) {
                        if (strcmp(word, g_constants[j].name) == 0) {
                            is_const = true;
                            break;
                        }
                    }
                    if (is_const || strcmp(word, "ans") == 0) {
                        api.syntax_add_token(tokens, i, UEMACS_FACE_CONSTANT);
                    } else {
                        api.syntax_add_token(tokens, i, UEMACS_FACE_VARIABLE);
                    }
                }
            } else {
                api.syntax_add_token(tokens, i, UEMACS_FACE_VARIABLE);
            }
            continue;
        }

        /* Operators */
        if (strchr("+-*/^%()=", line[i])) {
            i++;
            api.syntax_add_token(tokens, i, UEMACS_FACE_OPERATOR);
            continue;
        }

        /* Unknown - skip */
        i++;
    }

    return UEMACS_LEXER_STATE_INIT;  /* No multi-line state */
}

/* Command: calc - open calculator buffer */
static int cmd_calc(int f, int n) {
    (void)f; (void)n;

    void *bp = api.find_buffer ? api.find_buffer(CALC_BUFFER) : NULL;
    if (!bp && api.buffer_create) {
        bp = api.buffer_create(CALC_BUFFER);

        /* Insert welcome message */
        if (bp && api.buffer_switch && api.buffer_insert) {
            api.buffer_switch(bp);
            const char *welcome =
                "# Calculator - Type expressions, press Enter to evaluate\n"
                "# Functions: sin cos tan asin acos atan sinh cosh tanh\n"
                "#            log ln log2 exp sqrt cbrt abs floor ceil round\n"
                "# Constants: pi e phi tau ans\n"
                "# Operators: + - * / ^ % ()\n"
                "# Formats: 0x (hex), 0b (binary)\n"
                "#\n";
            api.buffer_insert(welcome, strlen(welcome));
        }
    }

    if (!bp) {
        if (api.message) api.message("Failed to create calculator buffer");
        return 0;
    }

    if (api.buffer_switch) {
        api.buffer_switch(bp);
    }

    if (api.message) {
        api.message("Calculator ready. Type expression, press Enter.");
    }

    return 1;
}

/* Command: calc-eval - quick eval via minibuffer */
static int cmd_calc_eval(int f, int n) {
    (void)f; (void)n;

    if (!api.prompt) {
        if (api.message) api.message("Prompt not available");
        return 0;
    }

    char expr[256] = {0};
    int result = api.prompt("Calc: ", expr, sizeof(expr));
    if (result != 1) return 0;

    bool ok;
    double val = eval_simple(expr, &ok);

    if (!ok) {
        if (api.message) api.message("Parse error");
        return 0;
    }

    g_last_result = val;
    g_has_result = true;

    char result_str[128];
    format_result(val, result_str, sizeof(result_str));

    if (api.message) api.message("%s %s", expr, result_str);

    return 1;
}

/* Command: calc-hex */
static int cmd_calc_hex(int f, int n) {
    (void)f; (void)n;

    if (!g_has_result) {
        if (api.message) api.message("No result to convert");
        return 0;
    }

    long long ival = (long long)g_last_result;
    if (api.message) api.message("0x%llX", ival);

    return 1;
}

/* Command: calc-bin */
static int cmd_calc_bin(int f, int n) {
    (void)f; (void)n;

    if (!g_has_result) {
        if (api.message) api.message("No result to convert");
        return 0;
    }

    unsigned long long uval = (unsigned long long)g_last_result;
    char buf[128] = "0b";
    int pos = 2;

    if (uval == 0) {
        buf[pos++] = '0';
    } else {
        /* Find highest bit */
        int bits[64];
        int count = 0;
        while (uval > 0 && count < 64) {
            bits[count++] = uval & 1;
            uval >>= 1;
        }
        for (int i = count - 1; i >= 0; i--) {
            buf[pos++] = '0' + bits[i];
        }
    }
    buf[pos] = '\0';

    if (api.message) api.message("%s", buf);

    return 1;
}

/* Command: calc-oct */
static int cmd_calc_oct(int f, int n) {
    (void)f; (void)n;

    if (!g_has_result) {
        if (api.message) api.message("No result to convert");
        return 0;
    }

    long long ival = (long long)g_last_result;
    if (api.message) api.message("0o%llo", ival);

    return 1;
}

/* Extension init */
static int calc_init(struct uemacs_api *uapi) {
    if (!uapi || !uapi->get_function) {
        fprintf(stderr, "haskell_calc: Requires μEmacs with get_function() support\n");
        return -1;
    }

    /* Look up API functions */
    #define LOOKUP(name) uapi->get_function(#name)

    api.on = (on_fn)LOOKUP(on);
    api.off = (off_fn)LOOKUP(off);
    api.register_command = (register_command_fn)LOOKUP(register_command);
    api.unregister_command = (unregister_command_fn)LOOKUP(unregister_command);
    api.current_buffer = (current_buffer_fn)LOOKUP(current_buffer);
    api.buffer_create = (buffer_create_fn)LOOKUP(buffer_create);
    api.find_buffer = (find_buffer_fn)LOOKUP(find_buffer);
    api.buffer_switch = (buffer_switch_fn)LOOKUP(buffer_switch);
    api.buffer_clear = (buffer_clear_fn)LOOKUP(buffer_clear);
    api.buffer_insert = (buffer_insert_fn)LOOKUP(buffer_insert);
    api.buffer_name = (buffer_name_fn)LOOKUP(buffer_name);
    api.get_current_line = (get_current_line_fn)LOOKUP(get_current_line);
    api.get_point = (get_point_fn)LOOKUP(get_point);
    api.set_point = (set_point_fn)LOOKUP(set_point);
    api.message = (message_fn)LOOKUP(message);
    api.prompt = (prompt_fn)LOOKUP(prompt);
    api.free = (free_fn)LOOKUP(free);
    api.log_info = (log_fn)LOOKUP(log_info);
    api.log_error = (log_fn)LOOKUP(log_error);
    api.log_debug = (log_fn)LOOKUP(log_debug);
    api.syntax_add_token = (syntax_add_token_fn)LOOKUP(syntax_add_token);
    api.syntax_register_lexer = (syntax_register_lexer_fn)LOOKUP(syntax_register_lexer);
    api.syntax_unregister_lexer = (syntax_unregister_lexer_fn)LOOKUP(syntax_unregister_lexer);

    #undef LOOKUP

    if (!api.register_command) {
        fprintf(stderr, "haskell_calc: Missing register_command\n");
        return -1;
    }

    /* Initialize Haskell runtime (for future Parsec integration) */
#ifdef USE_HASKELL
    static int argc = 1;
    static char *argv[] = { "haskell_calc", NULL };
    static char **pargv = argv;
    hs_init(&argc, &pargv);
#endif

    /* Register syntax lexer for *calc* buffer */
    if (api.syntax_register_lexer) {
        static const char *calc_patterns[] = { "*calc*", NULL };
        api.syntax_register_lexer("calc", calc_patterns, calc_lexer, NULL);
    }

    /* Register commands */
    api.register_command("calc", cmd_calc);
    api.register_command("calc-eval", cmd_calc_eval);
    api.register_command("calc-hex", cmd_calc_hex);
    api.register_command("calc-bin", cmd_calc_bin);
    api.register_command("calc-oct", cmd_calc_oct);

    /* Register key handler for Enter in calc buffer */
    if (api.on) {
        api.on("input:key", calc_key_handler, NULL, 10);
    }

    if (api.log_info) {
        api.log_info("haskell_calc: Loaded (v4.0, SpeedCrunch-style)");
    }

    return 0;
}

static void calc_cleanup(void) {
    if (api.off) {
        api.off("input:key", calc_key_handler);
    }

    if (api.syntax_unregister_lexer) {
        api.syntax_unregister_lexer("calc");
    }

    if (api.unregister_command) {
        api.unregister_command("calc");
        api.unregister_command("calc-eval");
        api.unregister_command("calc-hex");
        api.unregister_command("calc-bin");
        api.unregister_command("calc-oct");
    }

#ifdef USE_HASKELL
    hs_exit();
#endif
}

/* Extension descriptor */
static struct uemacs_extension ext = {
    .api_version = 4,
    .name = "haskell_calc",
    .version = "4.0.0",
    .description = "SpeedCrunch-style scientific calculator (Haskell)",
    .init = calc_init,
    .cleanup = calc_cleanup,
};

struct uemacs_extension *uemacs_extension_entry(void) {
    return &ext;
}
