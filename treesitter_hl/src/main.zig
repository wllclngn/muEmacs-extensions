//! Tree-sitter Syntax Highlighting Extension for μEmacs
//!
//! Written in Zig to demonstrate polyglot extension support.
//! Uses libtree-sitter for incremental parsing.
//!
//! Commands:
//!   ts-highlight    - Highlight current buffer with tree-sitter
//!   ts-clear        - Clear tree-sitter highlights

const std = @import("std");
const builtin = @import("builtin");
const cc = std.builtin.CallingConvention.c;

const c = @cImport({
    @cInclude("tree_sitter/api.h");
});

// =============================================================================
// μEmacs Extension API Bindings (must match extension_api.h exactly)
// =============================================================================

pub const CmdFn = *const fn (c_int, c_int) callconv(cc) c_int;
pub const BufferCb = *const fn (*anyopaque) callconv(cc) void;
pub const KeyCb = *const fn (c_int) callconv(cc) c_int;
pub const IdleCb = *const fn () callconv(cc) void;
pub const CharTransformCb = *const fn (c_int, *c_int) callconv(cc) c_int;

pub const UemacsApi = extern struct {
    api_version: c_int,

    // Command Registration
    register_command: ?*const fn ([*:0]const u8, CmdFn) callconv(cc) c_int,
    unregister_command: ?*const fn ([*:0]const u8) callconv(cc) c_int,

    // Event Hooks
    on_buffer_save: ?*const fn (BufferCb) callconv(cc) c_int,
    on_buffer_load: ?*const fn (BufferCb) callconv(cc) c_int,
    on_key: ?*const fn (KeyCb) callconv(cc) c_int,
    on_idle: ?*const fn (IdleCb) callconv(cc) c_int,
    on_char_transform: ?*const fn (CharTransformCb) callconv(cc) c_int,

    // Remove event hooks
    off_buffer_save: ?*const fn (BufferCb) callconv(cc) c_int,
    off_buffer_load: ?*const fn (BufferCb) callconv(cc) c_int,
    off_key: ?*const fn (KeyCb) callconv(cc) c_int,
    off_idle: ?*const fn (IdleCb) callconv(cc) c_int,
    off_char_transform: ?*const fn (CharTransformCb) callconv(cc) c_int,

    // Buffer Operations
    current_buffer: ?*const fn () callconv(cc) *anyopaque,
    find_buffer: ?*const fn ([*:0]const u8) callconv(cc) ?*anyopaque,
    buffer_contents: ?*const fn (*anyopaque, *usize) callconv(cc) [*:0]u8,
    buffer_filename: ?*const fn (*anyopaque) callconv(cc) [*:0]const u8,
    buffer_name: ?*const fn (*anyopaque) callconv(cc) [*:0]const u8,
    buffer_modified: ?*const fn (*anyopaque) callconv(cc) c_int,
    buffer_insert: ?*const fn ([*:0]const u8, usize) callconv(cc) c_int,
    buffer_insert_at: ?*const fn (*anyopaque, c_int, c_int, [*:0]const u8, usize) callconv(cc) c_int,
    buffer_create: ?*const fn ([*:0]const u8) callconv(cc) ?*anyopaque,
    buffer_switch: ?*const fn (*anyopaque) callconv(cc) c_int,
    buffer_clear: ?*const fn (*anyopaque) callconv(cc) c_int,

    // Cursor/Point Operations
    get_point: ?*const fn (*c_int, *c_int) callconv(cc) void,
    set_point: ?*const fn (c_int, c_int) callconv(cc) void,
    get_line_count: ?*const fn (*anyopaque) callconv(cc) c_int,
    get_word_at_point: ?*const fn () callconv(cc) [*:0]u8,
    get_current_line: ?*const fn () callconv(cc) [*:0]u8,

    // Window Operations
    current_window: ?*const fn () callconv(cc) *anyopaque,
    window_count: ?*const fn () callconv(cc) c_int,
    window_set_wrap_col: ?*const fn (*anyopaque, c_int) callconv(cc) c_int,

    // User Interface
    message: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    vmessage: ?*const fn ([*:0]const u8, *anyopaque) callconv(cc) void,
    prompt: ?*const fn ([*:0]const u8, [*]u8, usize) callconv(cc) c_int,
    prompt_yn: ?*const fn ([*:0]const u8) callconv(cc) c_int,
    update_display: ?*const fn () callconv(cc) void,

    // File Operations
    find_file_line: ?*const fn ([*:0]const u8, c_int) callconv(cc) c_int,

    // Shell Integration
    shell_command: ?*const fn ([*:0]const u8, *[*]u8, *usize) callconv(cc) c_int,

    // Memory Helpers
    alloc: ?*const fn (usize) callconv(cc) *anyopaque,
    free: ?*const fn (*anyopaque) callconv(cc) void,
    strdup: ?*const fn ([*:0]const u8) callconv(cc) [*:0]u8,

    // Logging
    log_info: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_warn: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_error: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
    log_debug: ?*const fn ([*:0]const u8, ...) callconv(cc) void,
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

// External language functions (from tree-sitter-* libraries)
extern fn tree_sitter_c() *c.TSLanguage;
extern fn tree_sitter_python() *c.TSLanguage;
extern fn tree_sitter_rust() *c.TSLanguage;
extern fn tree_sitter_bash() *c.TSLanguage;
extern fn tree_sitter_javascript() *c.TSLanguage;

// =============================================================================
// Global State
// =============================================================================

var api: ?*UemacsApi = null;
var parser: ?*c.TSParser = null;
var current_tree: ?*c.TSTree = null;

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

fn detect_language(filename: [*:0]const u8) Language {
    const name = std.mem.span(filename);

    if (std.mem.endsWith(u8, name, ".c") or std.mem.endsWith(u8, name, ".h")) {
        return .c_lang;
    } else if (std.mem.endsWith(u8, name, ".py")) {
        return .python;
    } else if (std.mem.endsWith(u8, name, ".rs")) {
        return .rust;
    } else if (std.mem.endsWith(u8, name, ".sh") or std.mem.endsWith(u8, name, ".bash")) {
        return .bash;
    } else if (std.mem.endsWith(u8, name, ".js") or std.mem.endsWith(u8, name, ".mjs")) {
        return .javascript;
    }
    return .unknown;
}

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

fn get_face_for_node(node_type: [*:0]const u8) ?[*:0]const u8 {
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
        std.mem.eql(u8, typ, "typedef"))
    {
        return "keyword";
    }

    // Strings
    if (std.mem.eql(u8, typ, "string") or
        std.mem.eql(u8, typ, "string_literal") or
        std.mem.eql(u8, typ, "string_content") or
        std.mem.eql(u8, typ, "raw_string_literal") or
        std.mem.eql(u8, typ, "char_literal"))
    {
        return "string";
    }

    // Comments
    if (std.mem.eql(u8, typ, "comment") or
        std.mem.eql(u8, typ, "line_comment") or
        std.mem.eql(u8, typ, "block_comment"))
    {
        return "comment";
    }

    // Numbers
    if (std.mem.eql(u8, typ, "number") or
        std.mem.eql(u8, typ, "integer") or
        std.mem.eql(u8, typ, "float") or
        std.mem.eql(u8, typ, "integer_literal") or
        std.mem.eql(u8, typ, "float_literal"))
    {
        return "constant";
    }

    // Types
    if (std.mem.eql(u8, typ, "type_identifier") or
        std.mem.eql(u8, typ, "primitive_type") or
        std.mem.eql(u8, typ, "type"))
    {
        return "type";
    }

    // Functions
    if (std.mem.eql(u8, typ, "function_definition") or
        std.mem.eql(u8, typ, "function_declarator") or
        std.mem.eql(u8, typ, "call_expression"))
    {
        return "function";
    }

    return null;
}

// =============================================================================
// Highlight Walker
// =============================================================================

const Highlight = struct {
    line: u32,
    col: u32,
    len: u32,
    face: [*:0]const u8,
};

// Fixed-size buffer for highlights (simpler than dynamic allocation)
const MAX_HIGHLIGHTS = 8192;
var highlight_buffer: [MAX_HIGHLIGHTS]Highlight = undefined;
var highlight_count: usize = 0;

fn walk_tree(node: c.TSNode) void {
    const node_type = c.ts_node_type(node);
    const start = c.ts_node_start_point(node);

    if (get_face_for_node(node_type)) |face| {
        const start_byte = c.ts_node_start_byte(node);
        const end_byte = c.ts_node_end_byte(node);
        const len: u32 = @intCast(end_byte - start_byte);

        if (len > 0 and len < 1000 and highlight_count < MAX_HIGHLIGHTS) {
            highlight_buffer[highlight_count] = .{
                .line = start.row + 1, // 1-indexed
                .col = start.column,
                .len = len,
                .face = face,
            };
            highlight_count += 1;
        }
    }

    // Walk children
    const child_count = c.ts_node_child_count(node);
    for (0..child_count) |i| {
        const child = c.ts_node_child(node, @intCast(i));
        walk_tree(child);
    }
}

// =============================================================================
// Commands
// =============================================================================

fn cmd_ts_highlight(_: c_int, _: c_int) callconv(cc) c_int {
    const a = api orelse return 0;

    // Get current buffer
    const buf = if (a.current_buffer) |f| f() else return 0;

    // Get filename for language detection
    const filename = if (a.buffer_filename) |f| f(buf) else return 0;
    const lang = detect_language(filename);

    if (lang == .unknown) {
        if (a.message) |msg| msg("ts-highlight: Unknown file type");
        return 0;
    }

    // Get language parser
    const ts_lang = get_ts_language(lang) orelse {
        if (a.message) |msg| msg("ts-highlight: No parser for this language");
        return 0;
    };

    // Get buffer contents
    var len: usize = 0;
    const contents = if (a.buffer_contents) |f| f(buf, &len) else return 0;
    defer if (a.free) |f| f(@ptrCast(contents));

    // Create/configure parser
    if (parser == null) {
        parser = c.ts_parser_new();
    }
    _ = c.ts_parser_set_language(parser, ts_lang);

    // Free old tree
    if (current_tree) |tree| {
        c.ts_tree_delete(tree);
    }

    // Parse
    current_tree = c.ts_parser_parse_string(parser, null, contents, @intCast(len));
    const tree = current_tree orelse {
        if (a.message) |msg| msg("ts-highlight: Parse failed");
        return 0;
    };

    // Walk tree and collect highlights
    highlight_count = 0; // Reset counter
    const root = c.ts_tree_root_node(tree);
    walk_tree(root);

    // Report results
    var msg_buf: [128]u8 = undefined;
    const msg_str = std.fmt.bufPrintZ(&msg_buf, "ts-highlight: {d} regions highlighted", .{highlight_count}) catch "ts-highlight: done";

    if (a.message) |msg| msg(msg_str.ptr);

    // TODO: Apply highlights to buffer display
    // This would require μEmacs to have a highlight API
    // For now, we just log what we found

    if (a.log_info) |log| {
        for (highlight_buffer[0..highlight_count]) |hl| {
            var log_buf: [256]u8 = undefined;
            const log_str = std.fmt.bufPrintZ(&log_buf, "  L{d}:{d} len={d} face={s}", .{ hl.line, hl.col, hl.len, std.mem.span(hl.face) }) catch continue;
            log(log_str.ptr);
        }
    }

    return 1;
}

fn cmd_ts_clear(_: c_int, _: c_int) callconv(cc) c_int {
    const a = api orelse return 0;

    if (current_tree) |tree| {
        c.ts_tree_delete(tree);
        current_tree = null;
    }

    if (a.message) |msg| msg("ts-highlight: Cleared");
    return 1;
}

// =============================================================================
// Extension Entry Point
// =============================================================================

fn ts_init(a: *UemacsApi) callconv(cc) c_int {
    api = a;

    // Register commands
    if (a.register_command) |reg| {
        _ = reg("ts-highlight", cmd_ts_highlight);
        _ = reg("ts-clear", cmd_ts_clear);
    }

    if (a.log_info) |log| {
        log("treesitter_hl: Zig extension loaded (C, Python, Rust, Bash, JS)");
    }

    return 0;
}

fn ts_cleanup() callconv(cc) void {
    const a = api orelse return;

    // Unregister commands
    if (a.unregister_command) |unreg| {
        _ = unreg("ts-highlight");
        _ = unreg("ts-clear");
    }

    // Cleanup parser
    if (parser) |p| {
        c.ts_parser_delete(p);
        parser = null;
    }

    if (current_tree) |tree| {
        c.ts_tree_delete(tree);
        current_tree = null;
    }
}

// =============================================================================
// Extension Descriptor
// =============================================================================

export const extension = UemacsExtension{
    .api_version = 2,
    .name = "treesitter_hl",
    .version = "1.0.0",
    .description = "Tree-sitter syntax highlighting (Zig)",
    .init = ts_init,
    .cleanup = ts_cleanup,
};

export fn uemacs_extension_entry() *UemacsExtension {
    return @constCast(&extension);
}
