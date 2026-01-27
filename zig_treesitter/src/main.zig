//! Tree-sitter Syntax Highlighting Extension for μEmacs
//!
//! Written in Zig to demonstrate polyglot extension support.
//! Uses libtree-sitter for incremental parsing.
//!
//! API Version 4: ABI-Stable Named Lookup
//!
//! Uses get_function() for ABI stability - immune to API struct layout changes.
//!
//! Registers as a custom lexer via syntax_register_lexer API.
//! Tree-sitter parses the full buffer and caches the AST.
//! On each lex callback, queries nodes on the requested line.

const std = @import("std");
const builtin = @import("builtin");
const config = @import("config");
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
// μEmacs Extension API Types - API Version 4 (ABI-Stable)
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

// Lexer callback
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

// Generic function pointer type returned by get_function
pub const GenericFn = *const fn () callconv(cc) void;

// get_function signature
pub const GetFunctionFn = *const fn ([*:0]const u8) callconv(cc) ?GenericFn;

// Minimal API struct - only what's needed for get_function access
// Padding calculated: api_version(4) + pad(4) + 59 pointers(472) = 480 bytes before struct_size
pub const UemacsApi = extern struct {
    api_version: c_int,
    _pad: c_int,
    _ptrs: [59]*const anyopaque,  // Padding for 59 function pointers
    struct_size: usize,
    get_function: ?GetFunctionFn,
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
// Function Pointer Types (for functions we use)
// =============================================================================

const OnFn = *const fn ([*:0]const u8, EventFn, ?*anyopaque, c_int) callconv(cc) c_int;
const OffFn = *const fn ([*:0]const u8, EventFn) callconv(cc) c_int;
const EmitFn = *const fn ([*:0]const u8, ?*anyopaque) callconv(cc) bool;
const BufferContentsFn = *const fn (?*Buffer, *usize) callconv(cc) ?[*:0]u8;
const BufferFilenameFn = *const fn (?*Buffer) callconv(cc) ?[*:0]const u8;
const FreeFn = *const fn (?*anyopaque) callconv(cc) void;
const LogFn = *const fn ([*:0]const u8, ...) callconv(cc) void;
const SyntaxRegisterLexerFn = *const fn (
    [*:0]const u8,
    [*:null]const ?[*:0]const u8,
    SyntaxLexFn,
    ?*anyopaque,
) callconv(cc) c_int;
const SyntaxUnregisterLexerFn = *const fn ([*:0]const u8) callconv(cc) c_int;
const SyntaxAddTokenFn = *const fn (?*LineTokens, c_int, c_int) callconv(cc) c_int;

// =============================================================================
// Stored Function Pointers (looked up via get_function)
// =============================================================================

const Api = struct {
    on: ?OnFn = null,
    off: ?OffFn = null,
    emit: ?EmitFn = null,
    buffer_contents: ?BufferContentsFn = null,
    buffer_filename: ?BufferFilenameFn = null,
    free: ?FreeFn = null,
    log_info: ?LogFn = null,
    syntax_register_lexer: ?SyntaxRegisterLexerFn = null,
    syntax_unregister_lexer: ?SyntaxUnregisterLexerFn = null,
    syntax_add_token: ?SyntaxAddTokenFn = null,
};

var api: Api = .{};
var get_function: ?GetFunctionFn = null;

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
// Helper function to look up API functions
// =============================================================================

fn lookup(comptime T: type, name: [*:0]const u8) ?T {
    const gf = get_function orelse return null;
    const fn_ptr = gf(name) orelse return null;
    return @ptrCast(fn_ptr);
}

// =============================================================================
// Tree-Sitter Lexer Callback (API v4 signature)
// =============================================================================

// Token collected for a line
const Token = struct {
    col: u32,
    end_col: u32,
    face: Face,
};

const MAX_LINE_TOKENS = 256;

// Lexer callback - called by syntax system for each line
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
        return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };
    }

    // Collect tokens on this line
    var tokens: [MAX_LINE_TOKENS]Token = undefined;
    var token_count: usize = 0;

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
        if (tok.col < prev_end) continue;

        if (api.syntax_add_token) |add_fn| {
            _ = add_fn(out, @intCast(tok.end_col), @intFromEnum(tok.face));
        }
        prev_end = tok.end_col;
    }

    // Emit default token to end of line if needed
    if (prev_end < @as(u32, @intCast(len))) {
        if (api.syntax_add_token) |add_fn| {
            _ = add_fn(out, len, @intFromEnum(Face.default));
        }
    }

    return .{ .mode = 0, .nest_depth = 0, .string_delim = 0, .state_hash = 0 };
}

fn collect_tokens_on_line(node: c.TSNode, line_num: u32, tokens: *[MAX_LINE_TOKENS]Token, count: *usize) void {
    const start = c.ts_node_start_point(node);
    const end = c.ts_node_end_point(node);

    if (start.row > line_num or end.row < line_num) {
        return;
    }

    const node_type = c.ts_node_type(node);
    if (get_face_for_node(node_type)) |face| {
        var col_start: u32 = 0;
        var col_end: u32 = 0;

        if (start.row == line_num) {
            col_start = start.column;
        }

        if (end.row == line_num) {
            col_end = end.column;
        } else {
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

fn check_empty_block(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

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

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        check_empty_block(c.ts_node_child(node, @intCast(i)));
    }
}

fn check_unreachable_code(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

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
                const start = c.ts_node_start_point(child);
                add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_UNREACHABLE, MSG_UNREACHABLE);
                break;
            }

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

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        check_unreachable_code(c.ts_node_child(node, @intCast(i)));
    }
}

fn count_complexity(node: c.TSNode) u32 {
    var complexity: u32 = 0;
    const node_type = std.mem.span(c.ts_node_type(node));

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

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        complexity += count_complexity(c.ts_node_child(node, @intCast(i)));
    }

    return complexity;
}

fn count_nesting_depth(node: c.TSNode, current_depth: u32) u32 {
    var max_depth = current_depth;
    const node_type = std.mem.span(c.ts_node_type(node));

    const increases_nesting = std.mem.eql(u8, node_type, "if_statement") or
        std.mem.eql(u8, node_type, "while_statement") or
        std.mem.eql(u8, node_type, "for_statement") or
        std.mem.eql(u8, node_type, "for_in_statement") or
        std.mem.eql(u8, node_type, "switch_statement") or
        std.mem.eql(u8, node_type, "try_statement") or
        std.mem.eql(u8, node_type, "with_statement");

    const new_depth = if (increases_nesting) current_depth + 1 else current_depth;
    if (new_depth > max_depth) max_depth = new_depth;

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        const child_depth = count_nesting_depth(c.ts_node_child(node, @intCast(i)), new_depth);
        if (child_depth > max_depth) max_depth = child_depth;
    }

    return max_depth;
}

fn count_loop_nesting(node: c.TSNode, current_depth: u32) u32 {
    var max_depth = current_depth;
    const node_type = std.mem.span(c.ts_node_type(node));

    const is_loop = std.mem.eql(u8, node_type, "while_statement") or
        std.mem.eql(u8, node_type, "for_statement") or
        std.mem.eql(u8, node_type, "for_in_statement") or
        std.mem.eql(u8, node_type, "do_statement") or
        std.mem.eql(u8, node_type, "for_expression") or
        std.mem.eql(u8, node_type, "while_expression");

    const new_depth = if (is_loop) current_depth + 1 else current_depth;
    if (new_depth > max_depth) max_depth = new_depth;

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        const child_depth = count_loop_nesting(c.ts_node_child(node, @intCast(i)), new_depth);
        if (child_depth > max_depth) max_depth = child_depth;
    }

    return max_depth;
}

fn analyze_function(node: c.TSNode) void {
    const start = c.ts_node_start_point(node);
    const end = c.ts_node_end_point(node);

    const line_count = end.row - start.row + 1;
    if (line_count > 50) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .info, RULE_LONG_FUNCTION, MSG_LONG_FUNCTION);
    }

    const complexity = count_complexity(node) + 1;
    if (complexity > 10) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_HIGH_COMPLEXITY, MSG_HIGH_COMPLEXITY);
    }

    const nesting = count_nesting_depth(node, 0);
    if (nesting > 4) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_DEEP_NESTING, MSG_DEEP_NESTING);
    }

    const loop_depth = count_loop_nesting(node, 0);
    if (loop_depth >= 3) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_TRIPLE_NESTED, MSG_TRIPLE_NESTED);
    } else if (loop_depth >= 2) {
        add_lint_diag(start.row + 1, start.column, start.column + 10, .warn, RULE_NESTED_LOOPS, MSG_NESTED_LOOPS);
    }
}

fn find_and_analyze_functions(node: c.TSNode) void {
    const node_type = std.mem.span(c.ts_node_type(node));

    const is_function = std.mem.eql(u8, node_type, "function_definition") or
        std.mem.eql(u8, node_type, "function_declaration") or
        std.mem.eql(u8, node_type, "method_definition") or
        std.mem.eql(u8, node_type, "function_item") or
        std.mem.eql(u8, node_type, "function") or
        std.mem.eql(u8, node_type, "arrow_function");

    if (is_function) {
        analyze_function(node);
    }

    const count = c.ts_node_child_count(node);
    for (0..count) |i| {
        find_and_analyze_functions(c.ts_node_child(node, @intCast(i)));
    }
}

fn run_lint_analysis(tree: *c.TSTree, buffer: ?*Buffer) void {
    lint_count = 0;

    const root = c.ts_tree_root_node(tree);

    check_empty_block(root);
    check_unreachable_code(root);
    find_and_analyze_functions(root);

    if (lint_count > 0) {
        var event = TsLintEvent{
            .buffer = buffer,
            .diags = &lint_diags,
            .count = lint_count,
        };

        if (api.emit) |emit_fn| {
            _ = emit_fn("treesitter:lint", @ptrCast(&event));
        }

        if (api.log_info) |log| {
            log("treesitter_hl: Emitted %d lint diagnostics", lint_count);
        }
    }
}

// =============================================================================
// Buffer Load Event Handler
// =============================================================================

const BUFFER_LOAD_EVENT: [*:0]const u8 = "buffer:load";

fn on_buffer_load_event(event: *UemacsEvent, _: ?*anyopaque) callconv(cc) bool {
    const buffer: ?*Buffer = @ptrCast(event.data);
    if (buffer == null) return false;

    const filename = if (api.buffer_filename) |f| f(buffer) else return false;
    if (filename == null) return false;

    const name = std.mem.span(filename.?);

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

    const ts_lang = get_ts_language(lang) orelse return false;

    var contents_len: usize = 0;
    const contents = if (api.buffer_contents) |f| f(buffer, &contents_len) else return false;
    if (contents == null) return false;
    defer if (api.free) |f| f(@ptrCast(contents));

    if (parser == null) {
        parser = c.ts_parser_new();
    }
    _ = c.ts_parser_set_language(parser, ts_lang);

    const tree = c.ts_parser_parse_string(parser, null, contents.?, @intCast(contents_len));
    if (tree == null) return false;

    var slot: ?usize = null;
    for (buffer_caches, 0..) |cache, i| {
        if (cache.buffer == buffer) {
            if (cache.tree) |old_tree| {
                c.ts_tree_delete(old_tree);
            }
            slot = i;
            break;
        }
    }

    if (slot == null) {
        for (buffer_caches, 0..) |cache, i| {
            if (cache.buffer == null) {
                slot = i;
                break;
            }
        }
    }

    if (slot == null) {
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

    run_lint_analysis(tree.?, buffer);

    if (api.emit) |emit_fn| {
        _ = emit_fn("treesitter:parsed", @ptrCast(buffer));
    }

    if (api.log_info) |log| {
        log("treesitter_hl: Parsed %s", filename.?);
    }

    return false;
}

// =============================================================================
// Extension Entry Point (API v4 - ABI-Stable)
// =============================================================================

fn ts_init(a: *UemacsApi) callconv(cc) c_int {
    // Get get_function for ABI-stable lookups
    get_function = a.get_function;
    if (get_function == null) {
        std.debug.print("zig_treesitter: Requires μEmacs with get_function() support\n", .{});
        return -1;
    }

    // Look up all API functions by name
    api.on = lookup(OnFn, "on");
    api.off = lookup(OffFn, "off");
    api.emit = lookup(EmitFn, "emit");
    api.buffer_contents = lookup(BufferContentsFn, "buffer_contents");
    api.buffer_filename = lookup(BufferFilenameFn, "buffer_filename");
    api.free = lookup(FreeFn, "free");
    api.log_info = lookup(LogFn, "log_info");
    api.syntax_register_lexer = lookup(SyntaxRegisterLexerFn, "syntax_register_lexer");
    api.syntax_unregister_lexer = lookup(SyntaxUnregisterLexerFn, "syntax_unregister_lexer");
    api.syntax_add_token = lookup(SyntaxAddTokenFn, "syntax_add_token");

    // Register buffer:load event handler
    if (api.on) |on_fn| {
        _ = on_fn(BUFFER_LOAD_EVENT, on_buffer_load_event, null, 0);
    }

    // Register as lexer for C files
    if (api.syntax_register_lexer) |reg| {
        const c_patterns = [_:null]?[*:0]const u8{ "*.c", "*.h", null };
        const lang_id = reg("treesitter-c", &c_patterns, ts_lex_callback, null);
        if (lang_id >= 0) {
            if (api.log_info) |log| log("treesitter_hl: Registered C lexer (id=%d)", lang_id);
        }
    }

    if (api.log_info) |log| {
        log("treesitter_hl: Loaded (v4.0, ABI-stable)");
    }

    return 0;
}

fn ts_cleanup() callconv(cc) void {
    // Unregister event handler
    if (api.off) |off_fn| {
        _ = off_fn(BUFFER_LOAD_EVENT, on_buffer_load_event);
    }

    // Unregister lexers
    if (api.syntax_unregister_lexer) |unreg| {
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
    .api_version = config.api_version,  // From build.zig via -Dapi_version
    .name = "zig_treesitter",
    .version = "4.0.0",
    .description = "Tree-sitter syntax highlighting (Zig, ABI-stable)",
    .init = ts_init,
    .cleanup = ts_cleanup,
};

export fn uemacs_extension_entry() *UemacsExtension {
    return @constCast(&extension);
}
