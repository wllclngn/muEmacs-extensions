//! FFI bindings for μEmacs extension API v3
//!
//! These structs MUST match the C definitions exactly.
//! See: include/uep/extension_api.h (API Version 3: Event Bus)

use std::ffi::{c_char, c_int, c_void};

/// Command function signature (matches μEmacs uemacs_cmd_fn)
pub type CmdFn = extern "C" fn(c_int, c_int) -> c_int;

/// Event structure passed to handlers (matches uemacs_event_t)
#[repr(C)]
pub struct UemacsEvent {
    pub name: *const c_char,
    pub data: *mut c_void,
    pub data_size: usize,
    pub consumed: bool,
}

/// Event handler callback (matches uemacs_event_fn)
pub type EventFn = extern "C" fn(*mut UemacsEvent, *mut c_void) -> bool;

/// The Editor API v3 - passed to extensions on init
/// MUST match struct uemacs_api exactly (event bus layout)
#[repr(C)]
pub struct UemacsApi {
    /// API version for compatibility checking
    pub api_version: c_int,

    // ─── Event Bus (API v3) ─────────────────────────────────────────────
    pub on: Option<extern "C" fn(*const c_char, EventFn, *mut c_void, c_int) -> c_int>,
    pub off: Option<extern "C" fn(*const c_char, EventFn) -> c_int>,
    pub emit: Option<extern "C" fn(*const c_char, *mut c_void) -> bool>,

    // ─── Configuration Access ───────────────────────────────────────────
    pub config_int: Option<extern "C" fn(*const c_char, *const c_char, c_int) -> c_int>,
    pub config_bool: Option<extern "C" fn(*const c_char, *const c_char, bool) -> bool>,
    pub config_string: Option<extern "C" fn(*const c_char, *const c_char, *const c_char) -> *const c_char>,

    // ─── Command Registration ───────────────────────────────────────────
    pub register_command: Option<extern "C" fn(*const c_char, CmdFn) -> c_int>,
    pub unregister_command: Option<extern "C" fn(*const c_char) -> c_int>,

    // ─── Buffer Operations ──────────────────────────────────────────────
    pub current_buffer: Option<extern "C" fn() -> *mut c_void>,
    pub find_buffer: Option<extern "C" fn(*const c_char) -> *mut c_void>,
    pub buffer_contents: Option<extern "C" fn(*mut c_void, *mut usize) -> *mut c_char>,
    pub buffer_filename: Option<extern "C" fn(*mut c_void) -> *const c_char>,
    pub buffer_name: Option<extern "C" fn(*mut c_void) -> *const c_char>,
    pub buffer_modified: Option<extern "C" fn(*mut c_void) -> bool>,
    pub buffer_insert: Option<extern "C" fn(*const c_char, usize) -> c_int>,
    pub buffer_insert_at: Option<extern "C" fn(*mut c_void, c_int, c_int, *const c_char, usize) -> c_int>,
    pub buffer_create: Option<extern "C" fn(*const c_char) -> *mut c_void>,
    pub buffer_switch: Option<extern "C" fn(*mut c_void) -> c_int>,
    pub buffer_clear: Option<extern "C" fn(*mut c_void) -> c_int>,

    // ─── Cursor/Point Operations ────────────────────────────────────────
    pub get_point: Option<extern "C" fn(*mut c_int, *mut c_int)>,
    pub set_point: Option<extern "C" fn(c_int, c_int)>,
    pub get_line_count: Option<extern "C" fn(*mut c_void) -> c_int>,
    pub get_word_at_point: Option<extern "C" fn() -> *mut c_char>,
    pub get_current_line: Option<extern "C" fn() -> *mut c_char>,

    // ─── Window Operations ──────────────────────────────────────────────
    pub current_window: Option<extern "C" fn() -> *mut c_void>,
    pub window_count: Option<extern "C" fn() -> c_int>,
    pub window_set_wrap_col: Option<extern "C" fn(*mut c_void, c_int) -> c_int>,
    pub window_at_row: Option<extern "C" fn(c_int) -> *mut c_void>,
    pub window_switch: Option<extern "C" fn(*mut c_void) -> c_int>,

    // ─── Mouse/Cursor Helpers ───────────────────────────────────────────
    pub screen_to_buffer_pos: Option<extern "C" fn(*mut c_void, c_int, c_int, *mut c_int, *mut c_int) -> c_int>,
    pub set_mark: Option<extern "C" fn() -> c_int>,
    pub scroll_up: Option<extern "C" fn(c_int) -> c_int>,
    pub scroll_down: Option<extern "C" fn(c_int) -> c_int>,

    // ─── User Interface ─────────────────────────────────────────────────
    pub message: Option<extern "C" fn(*const c_char, ...) -> c_int>,
    pub vmessage: Option<extern "C" fn(*const c_char, *mut c_void) -> c_int>,
    pub prompt: Option<extern "C" fn(*const c_char, *mut c_char, usize) -> c_int>,
    pub prompt_yn: Option<extern "C" fn(*const c_char) -> c_int>,
    pub update_display: Option<extern "C" fn()>,

    // ─── File Operations ────────────────────────────────────────────────
    pub find_file_line: Option<extern "C" fn(*const c_char, c_int) -> c_int>,

    // ─── Shell Integration ──────────────────────────────────────────────
    pub shell_command: Option<extern "C" fn(*const c_char, *mut *mut c_char, *mut usize) -> c_int>,

    // ─── Memory Helpers ─────────────────────────────────────────────────
    pub alloc: Option<extern "C" fn(usize) -> *mut c_void>,
    pub free: Option<extern "C" fn(*mut c_void)>,
    pub strdup: Option<extern "C" fn(*const c_char) -> *mut c_char>,

    // ─── Logging ────────────────────────────────────────────────────────
    pub log_info: Option<extern "C" fn(*const c_char, ...)>,
    pub log_warn: Option<extern "C" fn(*const c_char, ...)>,
    pub log_error: Option<extern "C" fn(*const c_char, ...)>,
    pub log_debug: Option<extern "C" fn(*const c_char, ...)>,

    // ─── Syntax Highlighting ────────────────────────────────────────────
    // (omitted - search extension doesn't use these)
}

/// Extension descriptor - matches struct uemacs_extension
#[repr(C)]
pub struct UemacsExtension {
    pub api_version: c_int,  // MUST be first!
    pub name: *const c_char,
    pub version: *const c_char,
    pub description: *const c_char,
    pub init: Option<extern "C" fn(*mut UemacsApi) -> c_int>,
    pub cleanup: Option<extern "C" fn()>,
}

// Safety: Extension descriptor is read-only after creation
unsafe impl Sync for UemacsExtension {}
