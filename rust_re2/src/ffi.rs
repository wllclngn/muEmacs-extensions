//! FFI bindings for μEmacs extension API v4
//!
//! API Version 4: ABI-Stable Named Lookup
//!
//! This extension uses get_function() for ABI stability - immune to
//! API struct layout changes between field additions.
//!
//! The UemacsApi struct ONLY needs to be accurate enough to access get_function.
//! All actual API calls go through get_function() lookup.

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

/// Generic function pointer type returned by get_function
pub type GenericFn = unsafe extern "C" fn();

/// Get function signature: takes name, returns generic function pointer
pub type GetFunctionFn = unsafe extern "C" fn(*const c_char) -> Option<GenericFn>;

/// The Editor API struct - layout must match C struct for get_function access
///
/// IMPORTANT: We only use api_version and get_function from this struct directly.
/// All other functions are looked up via get_function() for ABI stability.
///
/// If the C struct changes, only the _padding size needs adjustment.
#[repr(C)]
pub struct UemacsApi {
    /// API version for compatibility checking (always at offset 0)
    pub api_version: c_int,

    /// Padding to account for alignment
    _pad: c_int,

    /// 59 function pointers (on/off/emit through modeline_refresh)
    /// We don't use these directly - they're just padding to reach get_function
    _ptrs: [*const c_void; 59],

    /// Struct size for version detection (second-to-last field)
    pub struct_size: usize,

    /// ABI-stable function lookup (LAST field - guaranteed stable position)
    pub get_function: Option<GetFunctionFn>,
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
