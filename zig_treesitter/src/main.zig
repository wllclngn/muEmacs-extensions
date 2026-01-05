//! Tree-sitter Syntax Highlighting Extension for μEmacs
//!
//! Written in Zig to demonstrate polyglot extension support.
//! Uses libtree-sitter for incremental parsing.
//!
//! API Version 3: Uses generic event bus (on/off/emit).
//!
//! Registers as a custom lexer via syntax_register_lexer API.
//! Tree-sitter parses the full buffer and caches the AST.
//! On each lex callback, queries nodes on the requested line.

const std = @import("std");
const builtin = @import("builtin");
const cc = std.builtin.CallingConvention.c;

const c = @cImport({
    @cInclude("tree_sitter/api.h");
});

// =============================================================================
// Face IDs (must match UEMACS_FACE_* in extension_api.h)
// =============================================================================

const Face = enum(c_int) {
    default = 0,
    keyword = 1,
    string = 2,
    comment = 3,
    number = 4,
    type_ = 5,
    function = 6,
    operator = 7,
    preprocessor = 8,
    constant = 9,
    variable = 10,
    attribute = 11,
    escape = 12,
    regex = 13,
    special = 14,
};

// =============================================================================
// μEmacs Extension API Bindings - API Version 3 (Event Bus)
// Must match extension_api.h struct layout EXACTLY
// =============================================================================

pub const LexerState = extern struct {
    mode: c_int,
    nest_depth: c_int,
    string_delim: u8,
    state_hash: u32,
};

pub const LineTokens = opaque {};
pub const Buffer = opaque {};
pub const Window = opaque {};
pub const SyntaxLanguage = opaque {};

// Event structure (matches uemacs_event_t)
pub const UemacsEvent = extern struct {
    name: [*:0]const u8,
    data: ?*anyopaque,
    data_size: usize,
    consumed: bool,
};

// Event handler callback type (matches uemacs_event_fn)
pub const EventFn = *const fn (*UemacsEvent, ?*anyopaque) callconv(cc) bool;

// Lexer callback - note: first param is *SyntaxLanguage, not user_data
pub const SyntaxLexFn = *const fn (
    ?*const SyntaxLanguage,
    ?*Buffer,
    c_int,
    [*]const u8,
    c_int,
    LexerState,
    ?*LineTokens,
) callconv(cc) LexerState;

pub const CmdFn = *const fn (c_int, c_int) callconv(cc) c_int;

// API v3 struct - field order matches extension_api.h exactly
pub const UemacsApi = extern struct {
    api_version: c_int,

    // ─── Event Bus (API v3) ─────────────────────────────────────────────
    on: ?*const fn ([*:0]const u8, EventFn, ?*anyopaque, c_int) callconv(cc) c_int,
    off: ?*const fn ([*:0]const u8, EventFn) callconv(cc) c_int,
    emit: ?*const fn ([*:0]const u8, ?*anyopaque) callconv(cc) bool,

    // ─── Configuration Access ───────────────────────────────────────────
    config_int: ?*const fn ([*:0]const u8, [*:0]const u8, c_int) callconv(cc) c_int,
    config_bool: ?*const fn ([*:0]const u8, [*:0]const u8, bool) callconv(cc) bool,
    config_string: ?*const fn ([*:0]const u8, [*:0]const u8, [*:0]const u8) callconv(cc) ?[*:0]const u8,

    // ─── Command Registration ───────────────────────────────────────────
    register_command: ?*const fn ([*:0]const u8, CmdFn) callconv(cc) c_int,
    unregister_command: ?*const fn ([*:0]const u8) callconv(cc) c_int,

    // ─── Buffer Operations ──────────────────────────────────────────────
    current_buffer: ?*const fn () callconv(cc) ?*Buffer,
    find_buffer: ?*const fn ([*:0]const u8) callconv(cc) ?*Buffer,
    buffer_contents: ?*const fn (?*Buffer, *usize) callconv(cc) ?[*:0]u8,
    buffer_filename: ?*const fn (?*Buffer) callconv(cc) ?[*:0]const u8,
    buffer_name: ?*const fn (?*Buffer) callconv(cc) ?[*:0]const u8,
    buffer_modified: ?*const fn (?*Buffer) callconv(cc) bool,
    buffer_insert: ?*const fn ([*:0]const u8, usize) callconv(cc) c_int,
    buffer_insert_at: ?*const fn (?*Buffer, c_int, c_int, [*:0]const u8, usize) callconv(cc) c_int,
    buffer_create: ?*const fn ([*:0]const u8) callconv(cc) ?*Buffer,
    buffer_switch: ?*const fn (?*Buffer) callconv(cc) c_int,
    buffer_clear: ?*const fn (?*Buffer) callconv(cc) c_int,

    // ─── Cursor/Point Operations ────────────────────────────────────────
    get_point: ?*const fn (*c_int, *c_int) callconv(cc) void,
    set_point: ?*const fn (c_int, c_int) callconv(cc) void,
    get_line_count: ?*const fn (?*Buffer) callconv(cc) c_int,
    get_word_at_point: ?*const fn () callconv(cc) ?[*:0]u8,
    get_current_line: ?*const fn () callconv(cc) ?[*:0]u8,

    // ─── Window Operations ──────────────────────────────────────────────
    current_window: ?*const fn () callconv(cc) ?*Window,
    window_count: ?*const fn () callconv(cc) c_int,
    window_set_wrap_col: ?*const fn (?*Window, c_int) callconv(cc) c_int,
    window_at_row: ?*const fn (c_int) callconv(cc) ?*Window,
    window_switch: ?*const fn (?*Window) callconv(cc) c_int,

    // ─── Mouse/Cursor Helpers ───────────────────────────────────────────
    screen_to_buffer_pos: ?*const fn (?*Window, c_int, c_int, *c_int, *c_int) callconv(cc) c_int,
    set_mark: ?*const fn () callconv(cc) c_int,
    scroll_up: ?*const fn (c_int) callconv(cc) c_int,
    scroll_down: ?*const fn (c_int) callconv(cc) c_int,

    // ─── User Interface ─────────────────────────────────────────────────
    message: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    vmessage: ?*const fn ([*:0]const u8, ?*anyopaque) callconv(cc) void,
    prompt: ?*const fn ([*:0]const u8, [*]u8, usize) callconv(cc) c_int,
    prompt_yn: ?*const fn ([*:0]const u8) callconv(cc) c_int,
    update_display: ?*const fn () callconv(cc) void,

    // ─── File Operations ────────────────────────────────────────────────
    find_file_line: ?*const fn ([*:0]const u8, c_int) callconv(cc) c_int,

    // ─── Shell Integration ──────────────────────────────────────────────
    shell_command: ?*const fn ([*:0]const u8, *?[*]u8, *usize) callconv(cc) c_int,

    // ─── Memory Helpers ─────────────────────────────────────────────────
    alloc: ?*const fn (usize) callconv(cc) ?*anyopaque,
    free: ?*const fn (?*anyopaque) callconv(cc) void,
    strdup: ?*const fn ([*:0]const u8) callconv(cc) ?[*:0]u8,

    // ─── Logging ────────────────────────────────────────────────────────
    log_info: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_warn: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_error: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_debug: ?*const fn ([*:0]const u8, ...) callconv(cc) void,

    // ─── Syntax Highlighting ────────────────────────────────────────────
    syntax_register_lexer: ?*const fn (
        [*:0]const u8,
        [*:null]const ?[*:0]const u8,
        SyntaxLexFn,
        ?*anyopaque,
    ) callconv(cc) c_int,
    syntax_unregister_lexer: ?*const fn ([*:0]const u8) callconv(cc) c_int,
    syntax_add_token: ?*const fn (?*LineTokens, c_int, c_int) callconv(cc) c_int,
    syntax_invalidate_buffer: ?*const fn (?*Buffer) callconv(cc) void,
};

pub const UemacsExtension = extern struct {
    api_version: c_int,
    name: [*:0]const u8,
    version: [*:0]const u8,
    description: [*:0]const u8,
    init: ?*const fn (*UemacsApi) callconv(cc) c_int,
    cleanup: ?*const fn () callconv(cc) void,
};

// =============================================================================
// Tree-sitter Language Bindings
// =============================================================================

extern fn tree_sitter_c() *c.TSLanguage;
extern fn tree_sitter_python() *c.TSLanguage;
extern fn tree_sitter_rust() *c.TSLanguage;
extern fn tree_sitter_bash() *c.TSLanguage;
extern fn tree_sitter_javascript() *c.TSLanguage;

// =============================================================================
// Lint Diagnostic Types (for treesitter:lint events)
// =============================================================================

pub const LintSeverity = enum(u8) {
    err = 1,
    warn = 2,
    info = 3,
    hint = 4,
};

pub const TsLintDiag = extern struct {
    line: u32,
    col: u32,
    end_col: u32,
    severity: u8,
    rule: [*:0]const u8,
    message: [*:0]const u8,
};

pub const TsLintEvent = extern struct {
    buffer: ?*Buffer,
    diags: [*]TsLintDiag,
    count: u32,
};

const MAX_LINT_DIAGS = 256;

// Static storage for lint diagnostics (avoid allocation)
var lint_diags: [MAX_LINT_DIAGS]TsLintDiag = undefined;
var lint_count: u32 = 0;

// Static string storage for rule names and messages
const RULE_EMPTY_BLOCK: [*:0]const u8 = "empty-block";
const RULE_UNREACHABLE: [*:0]const u8 = "unreachable-code";
const RULE_UNUSED_VAR: [*:0]const u8 = "unused-variable";
const RULE_HIGH_COMPLEXITY: [*:0]const u8 = "high-complexity";
const RULE_DEEP_NESTING: [*:0]const u8 = "deep-nesting";
const RULE_LONG_FUNCTION: [*:0]const u8 = "long-function";
const RULE_NESTED_LOOPS: [*:0]const u8 = "nested-loops-On2";
const RULE_TRIPLE_NESTED: [*:0]const u8 = "triple-nested-On3";

const MSG_EMPTY_BLOCK: [*:0]const u8 = "Empty block - consider removing or adding TODO";
const MSG_UNREACHABLE: [*:0]const u8 = "Code after return/break is unreachable";
const MSG_UNUSED_VAR: [*:0]const u8 = "Variable declared but never used";
const MSG_HIGH_COMPLEXITY: [*:0]const u8 = "Cyclomatic complexity > 10 - consider refactoring";
const MSG_DEEP_NESTING: [*:0]const u8 = "Nesting depth > 4 - consider extracting function";
const MSG_LONG_FUNCTION: [*:0]const u8 = "Function exceeds 50 lines - consider splitting";
const MSG_NESTED_LOOPS: [*:0]const u8 = "CAUTION: Approximate Big O is O(n^2)";
const MSG_TRIPLE_NESTED: [*:0]const u8 = "CAUTION: Approximate Big O is O(n^3)";

// =============================================================================
// Global State
// =============================================================================

var api: ?*UemacsApi = null;
var parser: ?*c.TSParser = null;

// Per-buffer tree cache
const BufferCache = struct {
    buffer: ?*Buffer,
    tree: ?*c.TSTree,
    contents_hash: u64,
};

const MAX_CACHED_BUFFERS = 16;
var buffer_caches: [MAX_CACHED_BUFFERS]BufferCache = [_]BufferCache{.{
    .buffer = null,
    .tree = null,
    .contents_hash = 0,
}} ** MAX_CACHED_BUFFERS;

// =============================================================================
// Language Detection
// =============================================================================

const Language = enum {
    c_lang,
    python,
    rust,
    bash,
    javascript,
    unknown,
};

fn get_ts_language(lang: Language) ?*c.TSLanguage {
    return switch (lang) {
        .c_lang => tree_sitter_c(),
        .python => tree_sitter_python(),
        .rust => tree_sitter_rust(),
        .bash => tree_sitter_bash(),
        .javascript => tree_sitter_javascript(),
        .unknown => null,
    };
}

// =============================================================================
// Node Type to Face Mapping
// =============================================================================

fn get_face_for_node(node_type: [*:0]const u8) ?Face {
    const typ = std.mem.span(node_type);

    // Keywords
    if (std.mem.eql(u8, typ, "if") or
        std.mem.eql(u8, typ, "else") or
        std.mem.eql(u8, typ, "while") or
        std.mem.eql(u8, typ, "for") or
        std.mem.eql(u8, typ, "return") or
        std.mem.eql(u8, typ, "break") or
        std.mem.eql(u8, typ, "continue") or
        std.mem.eql(u8, typ, "switch") or
        std.mem.eql(u8, typ, "case") or
        std.mem.eql(u8, typ, "default") or
        std.mem.eql(u8, typ, "def") or
        std.mem.eql(u8, typ, "class") or
        std.mem.eql(u8, typ, "import") or
        std.mem.eql(u8, typ, "from") or
        std.mem.eql(u8, typ, "fn") or
        std.mem.eql(u8, typ, "let") or
        std.mem.eql(u8, typ, "mut") or
        std.mem.eql(u8, typ, "pub") or
        std.mem.eql(u8, typ, "const") or
        std.mem.eql(u8, typ, "static") or
        std.mem.eql(u8, typ, "struct") or
        std.mem.eql(u8, typ, "enum") or
        std.mem.eql(u8, typ, "union") or
        std.mem.eql(u8, typ, "typedef") or
        std.mem.eql(u8, typ, "extern") or
        std.mem.eql(u8, typ, "volatile") or
        std.mem.eql(u8, typ, "register") or
        std.mem.eql(u8, typ, "sizeof") or
        std.mem.eql(u8, typ, "goto") or
        std.mem.eql(u8, typ, "do") or
        std.mem.eql(u8, typ, "try") or
        std.mem.eql(u8, typ, "except") or
        std.mem.eql(u8, typ, "finally") or
        std.mem.eql(u8, typ, "with") or
        std.mem.eql(u8, typ, "as") or
        std.mem.eql(u8, typ, "yield") or
        std.mem.eql(u8, typ, "lambda") or
        std.mem.eql(u8, typ, "async") or
        std.mem.eql(u8, typ, "await") or
        std.mem.eql(u8, typ, "match") or
        std.mem.eql(u8, typ, "impl") or
        std.mem.eql(u8, typ, "trait") or
        std.mem.eql(u8, typ, "use") or
        std.mem.eql(u8, typ, "mod") or
        std.mem.eql(u8, typ, "crate") or
        std.mem.eql(u8, typ, "self") or
        std.mem.eql(u8, typ, "super") or
        std.mem.eql(u8, typ, "where") or
        std.mem.eql(u8, typ, "type") or
        std.mem.eql(u8, typ, "storage_class_specifier") or
        std.mem.eql(u8, typ, "type_qualifier"))
    {
        return .keyword;
    }

    // Strings
    if (std.mem.eql(u8, typ, "string") or
        std.mem.eql(u8, typ, "string_literal") or
        std.mem.eql(u8, typ, "string_content") or
        std.mem.eql(u8, typ, "raw_string_literal") or
        std.mem.eql(u8, typ, "char_literal") or
        std.mem.eql(u8, typ, "character") or
        std.mem.eql(u8, typ, "interpreted_string_literal") or
        std.mem.eql(u8, typ, "template_string"))
    {
        return .string;
    }

    // Comments
    if (std.mem.eql(u8, typ, "comment") or
        std.mem.eql(u8, typ, "line_comment") or
        std.mem.eql(u8, typ, "block_comment"))
    {
        return .comment;
    }

    // Numbers
    if (std.mem.eql(u8, typ, "number") or
        std.mem.eql(u8, typ, "integer") or
        std.mem.eql(u8, typ, "float") or
        std.mem.eql(u8, typ, "integer_literal") or
        std.mem.eql(u8, typ, "float_literal") or
        std.mem.eql(u8, typ, "number_literal"))
    {
        return .number;
    }

    // Types
    if (std.mem.eql(u8, typ, "type_identifier") or
        std.mem.eql(u8, typ, "primitive_type") or
        std.mem.eql(u8, typ, "type_specifier") or
        std.mem.eql(u8, typ, "sized_type_specifier"))
    {
        return .type_;
    }

    // Function identifiers
    if (std.mem.eql(u8, typ, "function_declarator") or
        std.mem.eql(u8, typ, "call_expression"))
    {
        return .function;
    }

    // Preprocessor
    if (std.mem.eql(u8, typ, "preproc_include") or
        std.mem.eql(u8, typ, "preproc_def") or
        std.mem.eql(u8, typ, "preproc_function_def") or
        std.mem.eql(u8, typ, "preproc_call") or
        std.mem.eql(u8, typ, "preproc_ifdef") or
        std.mem.eql(u8, typ, "preproc_else") or
        std.mem.eql(u8, typ, "preproc_elif") or
        std.mem.eql(u8, typ, "preproc_if") or
        std.mem.eql(u8, typ, "preproc_directive") or
        std.mem.eql(u8, typ, "#include") or
        std.mem.eql(u8, typ, "#define"))
    {
        return .preprocessor;
    }

    // Constants
    if (std.mem.eql(u8, typ, "true") or
        std.mem.eql(u8, typ, "false") or
        std.mem.eql(u8, typ, "null") or
        std.mem.eql(u8, typ, "nil") or
        std.mem.eql(u8, typ, "none") or
        std.mem.eql(u8, typ, "None") or
        std.mem.eql(u8, typ, "True") or
        std.mem.eql(u8, typ, "False") or
        std.mem.eql(u8, typ, "NULL"))
    {
        return .constant;
    }

    // Attributes/decorators
    if (std.mem.eql(u8, typ, "decorator") or
        std.mem.eql(u8, typ, "attribute_item") or
        std.mem.eql(u8, typ, "inner_attribute_item"))
    {
        return .attribute;
    }

    // Escape sequences
    if (std.mem.eql(u8, typ, "escape_sequence")) {
        return .escape;
    }

    return null;
}

// =============================================================================
// Tree-Sitter Lexer Callback (API v3 signature)
// =============================================================================

// Token collected for a line
const Token = struct {
    col: u32,
    end_col: u32,
    face: Face,
};

const MAX_LINE_TOKENS = 256;

// Lexer callback - called by syntax system for each line
// Note: API v3 lexer signature: (lang, buffer, line_num, line, len, prev_state, out)
// We need to use the global api pointer since it's not passed to lexer in v3
fn ts_lex_callback(
    _: ?*const SyntaxLanguage,
    buffer: ?*Buffer,
    line_num: c_int,
    line: [*]const u8,
    len: c_int,
    prev_state: LexerState,
    out: ?*LineTokens,
) callconv(cc) LexerState {
    _ = prev_state;
    _ = line;

    const a = api orelse return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };

    if (buffer == null or out == null) {
        return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };
    }

    // Find cached tree for this buffer
    var cache_idx: ?usize = null;
    for (buffer_caches, 0..) |cache, i| {
        if (cache.buffer == buffer) {
            cache_idx = i;
            break;
        }
    }

    const tree = if (cache_idx) |idx| buffer_caches[idx].tree else null;
    if (tree == null) {
        // No cached tree - fall back to default highlighting
        return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };
    }

    // Collect tokens on this line
    var tokens: [MAX_LINE_TOKENS]Token = undefined;
    var token_count: usize = 0;

    // Walk tree and find nodes on this line
    const root = c.ts_tree_root_node(tree.?);
    collect_tokens_on_line(root, @intCast(line_num), &tokens, &token_count);

    // Sort tokens by start column
    std.mem.sort(Token, tokens[0..token_count], {}, struct {
        fn lessThan(_: void, lhs: Token, rhs: Token) bool {
            return lhs.col < rhs.col;
        }
    }.lessThan);

    // Emit tokens via API
    var prev_end: u32 = 0;
    for (tokens[0..token_count]) |tok| {
        // Skip if overlapping with previous
        if (tok.col < prev_end) continue;

        // Emit token
        if (a.syntax_add_token) |add_fn| {
            _ = add_fn(out, @intCast(tok.end_col), @intFromEnum(tok.face));
        }
        prev_end = tok.end_col;
    }

    // Emit default token to end of line if needed
    if (prev_end < @as(u32, @intCast(len))) {
        if (a.syntax_add_token) |add_fn| {
            _ = add_fn(out, len, @intFromEnum(Face.default));
        }
    }

    return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };
}

fn collect_tokens_on_line(node: c.TSNode, line_num: u32, tokens: *[MAX_LINE_TOKENS]Token, count: *usize) void {
    const start = c.ts_node_start_point(node);
    const end = c.ts_node_end_point(node);

    // Check if node overlaps with target line
    if (start.row > line_num or end.row < line_num) {
        return; // Node doesn't touch this line
    }

    // Check if this is a leaf-ish node we want to highlight
    const node_type = c.ts_node_type(node);
    if (get_face_for_node(node_type)) |face| {
        // Calculate column range on this line
        var col_start: u32 = 0;
        var col_end: u32 = 0;

        if (start.row == line_num) {
            col_start = start.column;
        }

        if (end.row == line_num) {
            col_end = end.column;
        } else {
            // Node extends past this line - use large value
            col_end = 10000;
        }

        if (col_end > col_start and count.* < MAX_LINE_TOKENS) {
            tokens[count.*] = .{
                .col = col_start,
                .end_col = col_end,
                .face = face,
            };
            count.* += 1;
        }
    }

    // Recurse into children
    const child_count = c.ts_node_child_count(node);
    for (0..child_count) |i| {
        const child = c.ts_node_child(node, @intCast(i));
        collect_tokens_on_line(child, line_num, tokens, count);
    }
}

// =============================================================================
// Lint Analysis Functions
// =============================================================================

fn add_lint_diag(line: u32, col: u32, end_col: u32, severity: LintSeverity, rule: [*:0]const u8, message: [*:0]const u8) void {
    if (lint_count >= MAX_LINT_DIAGS) return;
    lint_diags[lint_count] = .{
        .line = line,
        .col = col,
        .end_col = end_col,
        .severity = @intFromEnum(severity),
        .rule = rule,
        .message = message,
    };
    lint_count += 1;
}

// Check if node type matches any in list
fn node_type_is(node: c.TSNode, types: []const []const u8) bool {
    const node_type = std.mem.span(c.ts_node_type(node));
    for (types) |t| {
        if (std.mem.eql(u8, node_type, t)) return true;
    }
    return false;
}

// Detect empty blocks: compound_statement with only braces
fn check_empty_block(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

    // Check for compound_statement (C), block (Python/Rust), statement_block (JS)
    const is_block = std.mem.eql(u8, node_type, "compound_statement") or
        std.mem.eql(u8, node_type, "block") or
        std.mem.eql(u8, node_type, "statement_block");

    if (is_block) {
        const child_count = c.ts_node_named_child_count(node);
        if (child_count == 0) {
            const start = c.ts_node_start_point(node);
            add_lint_diag(start.row + 1, start.column, start.column + 2, .warn, RULE_EMPTY_BLOCK, MSG_EMPTY_BLOCK);
        }
    }

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        check_empty_block(c.ts_node_child(node, @intCast(i)));
    }
}

// Detect unreachable code after return/break/continue
fn check_unreachable_code(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

    // Check if this is a block with statements
    const is_block = std.mem.eql(u8, node_type, "compound_statement") or
        std.mem.eql(u8, node_type, "block") or
        std.mem.eql(u8, node_type, "statement_block");

    if (is_block) {
        const child_count = c.ts_node_named_child_count(node);
        var found_terminator = false;

        for (0..child_count) |i| {
            const child = c.ts_node_named_child(node, @intCast(i));
            const child_type = std.mem.span(c.ts_node_type(child));

            if (found_terminator) {
                // This code is unreachable
                const start = c.ts_node_start_point(child);
                add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_UNREACHABLE, MSG_UNREACHABLE);
                break; // Only report first unreachable
            }

            // Check for terminators
            if (std.mem.eql(u8, child_type, "return_statement") or
                std.mem.eql(u8, child_type, "break_statement") or
                std.mem.eql(u8, child_type, "continue_statement") or
                std.mem.eql(u8, child_type, "throw_statement") or
                std.mem.eql(u8, child_type, "raise_statement"))
            {
                found_terminator = true;
            }
        }
    }

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        check_unreachable_code(c.ts_node_child(node, @intCast(i)));
    }
}

// Count complexity of a function
fn count_complexity(node: c.TSNode) u32 {
    var complexity: u32 = 0;
    const node_type = std.mem.span(c.ts_node_type(node));

    // Each branch adds 1 to complexity
    if (std.mem.eql(u8, node_type, "if_statement") or
        std.mem.eql(u8, node_type, "if_expression") or
        std.mem.eql(u8, node_type, "while_statement") or
        std.mem.eql(u8, node_type, "for_statement") or
        std.mem.eql(u8, node_type, "for_in_statement") or
        std.mem.eql(u8, node_type, "case_statement") or
        std.mem.eql(u8, node_type, "switch_case") or
        std.mem.eql(u8, node_type, "catch_clause") or
        std.mem.eql(u8, node_type, "except_clause") or
        std.mem.eql(u8, node_type, "conditional_expression") or
        std.mem.eql(u8, node_type, "ternary_expression") or
        std.mem.eql(u8, node_type, "&&") or
        std.mem.eql(u8, node_type, "||") or
        std.mem.eql(u8, node_type, "and") or
        std.mem.eql(u8, node_type, "or"))
    {
        complexity += 1;
    }

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        complexity += count_complexity(c.ts_node_child(node, @intCast(i)));
    }

    return complexity;
}

// Count max nesting depth
fn count_nesting_depth(node: c.TSNode, current_depth: u32) u32 {
    var max_depth = current_depth;
    const node_type = std.mem.span(c.ts_node_type(node));

    // Nesting constructs
    const increases_nesting = std.mem.eql(u8, node_type, "if_statement") or
        std.mem.eql(u8, node_type, "while_statement") or
        std.mem.eql(u8, node_type, "for_statement") or
        std.mem.eql(u8, node_type, "for_in_statement") or
        std.mem.eql(u8, node_type, "switch_statement") or
        std.mem.eql(u8, node_type, "try_statement") or
        std.mem.eql(u8, node_type, "with_statement");

    const new_depth = if (increases_nesting) current_depth + 1 else current_depth;
    if (new_depth > max_depth) max_depth = new_depth;

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        const child_depth = count_nesting_depth(c.ts_node_child(node, @intCast(i)), new_depth);
        if (child_depth > max_depth) max_depth = child_depth;
    }

    return max_depth;
}

// Count loop nesting (for Big O detection)
fn count_loop_nesting(node: c.TSNode, current_depth: u32) u32 {
    var max_depth = current_depth;
    const node_type = std.mem.span(c.ts_node_type(node));

    // Loop constructs only
    const is_loop = std.mem.eql(u8, node_type, "while_statement") or
        std.mem.eql(u8, node_type, "for_statement") or
        std.mem.eql(u8, node_type, "for_in_statement") or
        std.mem.eql(u8, node_type, "do_statement") or
        std.mem.eql(u8, node_type, "for_expression") or
        std.mem.eql(u8, node_type, "while_expression");

    const new_depth = if (is_loop) current_depth + 1 else current_depth;
    if (new_depth > max_depth) max_depth = new_depth;

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        const child_depth = count_loop_nesting(c.ts_node_child(node, @intCast(i)), new_depth);
        if (child_depth > max_depth) max_depth = child_depth;
    }

    return max_depth;
}

// Analyze function for complexity metrics
fn analyze_function(node: c.TSNode) void {
    const start = c.ts_node_start_point(node);
    const end = c.ts_node_end_point(node);

    // Function length (lines)
    const line_count = end.row - start.row + 1;
    if (line_count > 50) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .info, RULE_LONG_FUNCTION, MSG_LONG_FUNCTION);
    }

    // Cyclomatic complexity
    const complexity = count_complexity(node) + 1; // +1 for function itself
    if (complexity > 10) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_HIGH_COMPLEXITY, MSG_HIGH_COMPLEXITY);
    }

    // Nesting depth
    const nesting = count_nesting_depth(node, 0);
    if (nesting > 4) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_DEEP_NESTING, MSG_DEEP_NESTING);
    }

    // Loop nesting (Big O detection)
    const loop_depth = count_loop_nesting(node, 0);
    if (loop_depth >= 3) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_TRIPLE_NESTED, MSG_TRIPLE_NESTED);
    } else if (loop_depth >= 2) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_NESTED_LOOPS, MSG_NESTED_LOOPS);
    }
}

// Find and analyze all functions in AST
fn find_and_analyze_functions(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

    // Function definitions in various languages
    const is_function = std.mem.eql(u8, node_type, "function_definition") or
        std.mem.eql(u8, node_type, "function_declaration") or
        std.mem.eql(u8, node_type, "method_definition") or
        std.mem.eql(u8, node_type, "function_item") or // Rust
        std.mem.eql(u8, node_type, "function") or // JS
        std.mem.eql(u8, node_type, "arrow_function");

    if (is_function) {
        analyze_function(node);
    }

    // Recurse
    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        find_and_analyze_functions(c.ts_node_child(node, @intCast(i)));
    }
}

// Main lint analysis entry point
fn run_lint_analysis(tree: *c.TSTree, buffer: ?*Buffer) void {
    const a = api orelse return;

    // Reset diagnostics
    lint_count = 0;

    const root = c.ts_tree_root_node(tree);

    // Run all checks
    check_empty_block(root);
    check_unreachable_code(root);
    find_and_analyze_functions(root);

    // Emit treesitter:lint event if we found anything
    if (lint_count > 0) {
        var event = TsLintEvent{
            .buffer = buffer,
            .diags = &lint_diags,
            .count = lint_count,
        };

        if (a.emit) |emit_fn| {
            _ = emit_fn("treesitter:lint", @ptrCast(&event));
        }

        if (a.log_info) |log| {
            log("treesitter_hl: Emitted %d lint diagnostics", lint_count);
        }
    }
}

// =============================================================================
// Buffer Load Event Handler (API v3 Event Bus)
// =============================================================================

// Event name constant
const BUFFER_LOAD_EVENT: [*:0]const u8 = "buffer:load";

// Event handler for buffer:load - event.data is the buffer pointer
fn on_buffer_load_event(event: *UemacsEvent, _: ?*anyopaque) callconv(cc) bool {
    const a = api orelse return false;
    const buffer: ?*Buffer = @ptrCast(event.data);
    if (buffer == null) return false;

    // Get filename for language detection
    const filename = if (a.buffer_filename) |f| f(buffer) else return false;
    if (filename == null) return false;

    const name = std.mem.span(filename.?);

    // Detect language
    const lang: Language = blk: {
        if (std.mem.endsWith(u8, name, ".c") or std.mem.endsWith(u8, name, ".h")) {
            break :blk .c_lang;
        } else if (std.mem.endsWith(u8, name, ".py")) {
            break :blk .python;
        } else if (std.mem.endsWith(u8, name, ".rs")) {
            break :blk .rust;
        } else if (std.mem.endsWith(u8, name, ".sh") or std.mem.endsWith(u8, name, ".bash")) {
            break :blk .bash;
        } else if (std.mem.endsWith(u8, name, ".js") or std.mem.endsWith(u8, name, ".mjs")) {
            break :blk .javascript;
        }
        break :blk .unknown;
    };

    if (lang == .unknown) return false;

    // Get tree-sitter language
    const ts_lang = get_ts_language(lang) orelse return false;

    // Get buffer contents
    var contents_len: usize = 0;
    const contents = if (a.buffer_contents) |f| f(buffer, &contents_len) else return false;
    if (contents == null) return false;
    defer if (a.free) |f| f(@ptrCast(contents));

    // Create/configure parser
    if (parser == null) {
        parser = c.ts_parser_new();
    }
    _ = c.ts_parser_set_language(parser, ts_lang);

    // Parse
    const tree = c.ts_parser_parse_string(parser, null, contents.?, @intCast(contents_len));
    if (tree == null) return false;

    // Find or allocate cache slot
    var slot: ?usize = null;
    for (buffer_caches, 0..) |cache, i| {
        if (cache.buffer == buffer) {
            // Reuse existing slot
            if (cache.tree) |old_tree| {
                c.ts_tree_delete(old_tree);
            }
            slot = i;
            break;
        }
    }

    if (slot == null) {
        // Find empty slot
        for (buffer_caches, 0..) |cache, i| {
            if (cache.buffer == null) {
                slot = i;
                break;
            }
        }
    }

    if (slot == null) {
        // Evict oldest (slot 0)
        if (buffer_caches[0].tree) |old_tree| {
            c.ts_tree_delete(old_tree);
        }
        slot = 0;
    }

    buffer_caches[slot.?] = .{
        .buffer = buffer,
        .tree = tree,
        .contents_hash = std.hash.Wyhash.hash(0, contents.?[0..contents_len]),
    };

    // Run lint analysis on the parsed tree
    run_lint_analysis(tree.?, buffer);

    // Emit treesitter:parsed event for linter integration
    if (a.emit) |emit_fn| {
        _ = emit_fn("treesitter:parsed", @ptrCast(buffer));
    }

    if (a.log_info) |log| {
        log("treesitter_hl: Parsed %s", filename.?);
    }

    return false; // Don't consume event - other handlers may want it
}

// =============================================================================
// Extension Entry Point (API v3)
// =============================================================================

fn ts_init(a: *UemacsApi) callconv(cc) c_int {
    api = a;

    // Register buffer:load event handler (API v3 event bus)
    if (a.on) |on_fn| {
        _ = on_fn(BUFFER_LOAD_EVENT, on_buffer_load_event, null, 0);
    }

    // Register as lexer for C files (overrides built-in)
    if (a.syntax_register_lexer) |reg| {
        const c_patterns = [_:null]?[*:0]const u8{ "*.c", "*.h", null };
        const lang_id = reg("treesitter-c", &c_patterns, ts_lex_callback, null);
        if (lang_id >= 0) {
            if (a.log_info) |log| log("treesitter_hl: Registered C lexer (id=%d)", lang_id);
        }
    }

    if (a.log_info) |log| {
        log("treesitter_hl: Zig extension loaded (API v3)");
    }

    return 0;
}

fn ts_cleanup() callconv(cc) void {
    const a = api orelse return;

    // Unregister event handler (API v3 event bus)
    if (a.off) |off_fn| {
        _ = off_fn(BUFFER_LOAD_EVENT, on_buffer_load_event);
    }

    // Unregister lexers
    if (a.syntax_unregister_lexer) |unreg| {
        _ = unreg("treesitter-c");
    }

    // Cleanup cached trees
    for (&buffer_caches) |*cache| {
        if (cache.tree) |tree| {
            c.ts_tree_delete(tree);
        }
        cache.* = .{ .buffer = null, .tree = null, .contents_hash = 0 };
    }

    // Cleanup parser
    if (parser) |p| {
        c.ts_parser_delete(p);
        parser = null;
    }
}

// =============================================================================
// Extension Descriptor
// =============================================================================

export const extension = UemacsExtension{
    .api_version = 3,  // API v3 - Event Bus
    .name = "zig_treesitter",
    .version = "3.0.0",
    .description = "Tree-sitter syntax highlighting (Zig, API v3)",
    .init = ts_init,
    .cleanup = ts_cleanup,
};

export fn uemacs_extension_entry() *UemacsExtension {
    return @constCast(&extension);
}
