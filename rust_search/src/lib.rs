//! rust_search - Rust ripgrep extension for μEmacs
//!
//! This extension proves that Rust can compile to a .so that μEmacs
//! loads via dlopen(). It uses the grep crate (ripgrep's library)
//! for in-process searching without fork/exec overhead.
//!
//! Commands provided:
//! - rg-search: Search for pattern in current directory
//! - rg-search-word: Search for word under cursor
//!
//! Press Enter in results buffer to jump to file:line.

mod ffi;
mod search;

use ffi::{UemacsApi, UemacsEvent, UemacsExtension};
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::sync::atomic::{AtomicPtr, Ordering};

/// Results buffer name
const RG_RESULTS_BUFFER: &str = "*rg-results-rs*";

/// Event name for key input (must match UEMACS_EVT_INPUT_KEY)
static INPUT_KEY_EVENT: &[u8; 10] = b"input:key\0";

/// Global API pointer - set during init
static API: AtomicPtr<UemacsApi> = AtomicPtr::new(std::ptr::null_mut());

/// Last search pattern (for repeat searches)
use std::sync::Mutex;
static LAST_PATTERN: Mutex<Option<String>> = Mutex::new(None);

// Static strings with explicit lifetime for C FFI
static NAME: &[u8; 12] = b"rust_search\0";
static VERSION: &[u8; 6] = b"2.0.0\0";
static DESC: &[u8; 49] = b"Rust ripgrep integration (in-process, event bus)\0";

/// Extension descriptor - static lifetime, C-compatible strings
static EXTENSION: UemacsExtension = UemacsExtension {
    api_version: 3,  // UEMACS_API_VERSION v3 (event bus)
    name: NAME.as_ptr() as *const c_char,
    version: VERSION.as_ptr() as *const c_char,
    description: DESC.as_ptr() as *const c_char,
    init: Some(rg_init),
    cleanup: Some(rg_cleanup),
};

/// Entry point - called by μEmacs dlopen() loader
#[no_mangle]
pub extern "C" fn uemacs_extension_entry() -> *mut UemacsExtension {
    &EXTENSION as *const _ as *mut _
}

/// Initialize the extension
extern "C" fn rg_init(api: *mut UemacsApi) -> c_int {
    // Store API pointer
    API.store(api, Ordering::SeqCst);

    // Register commands
    unsafe {
        if let Some(register) = (*api).register_command {
            let cmd_search = CString::new("rg-search").unwrap();
            let cmd_word = CString::new("rg-search-word").unwrap();

            register(cmd_search.as_ptr(), cmd_rg_search);
            register(cmd_word.as_ptr(), cmd_rg_search_word);
        }

        // Register key event handler (API v3 event bus)
        if let Some(on) = (*api).on {
            on(
                INPUT_KEY_EVENT.as_ptr() as *const c_char,
                rg_key_event_handler,
                std::ptr::null_mut(),
                0,  // priority: normal
            );
        }

        // Log that we loaded
        if let Some(log_info) = (*api).log_info {
            let msg = CString::new("rust_search: Rust ripgrep extension loaded").unwrap();
            log_info(msg.as_ptr());
        }
    }

    0 // Success
}

/// Cleanup the extension
extern "C" fn rg_cleanup() {
    let api = API.load(Ordering::SeqCst);
    if !api.is_null() {
        unsafe {
            // Unregister key event handler (API v3)
            if let Some(off) = (*api).off {
                off(
                    INPUT_KEY_EVENT.as_ptr() as *const c_char,
                    rg_key_event_handler,
                );
            }

            if let Some(unregister) = (*api).unregister_command {
                let cmd_search = CString::new("rg-search").unwrap();
                let cmd_word = CString::new("rg-search-word").unwrap();

                unregister(cmd_search.as_ptr());
                unregister(cmd_word.as_ptr());
            }
        }
    }
}

/// Get the API pointer safely
fn get_api() -> Option<*mut UemacsApi> {
    let api = API.load(Ordering::SeqCst);
    if api.is_null() {
        None
    } else {
        Some(api)
    }
}

/// Show a message to the user
fn message(msg: &str) {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(message_fn) = (*api).message {
                if let Ok(cmsg) = CString::new(msg) {
                    message_fn(cmsg.as_ptr());
                }
            }
        }
    }
}

/// Prompt user for input
fn prompt(prompt_text: &str) -> Option<String> {
    let api = get_api()?;
    unsafe {
        let prompt_fn = (*api).prompt?;
        let cprompt = CString::new(prompt_text).ok()?;
        let mut buf = [0u8; 256];

        if prompt_fn(cprompt.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len()) == 0 {
            // Success - user entered something (C API returns 0 on success)
            let cstr = CStr::from_ptr(buf.as_ptr() as *const c_char);
            Some(cstr.to_string_lossy().to_string())
        } else {
            // Cancelled (ESC) or error (C API returns -1)
            None
        }
    }
}

/// Get word at cursor
fn get_word_at_point() -> Option<String> {
    let api = get_api()?;
    unsafe {
        let get_word_fn = (*api).get_word_at_point?;
        let ptr = get_word_fn();
        if ptr.is_null() {
            return None;
        }
        let cstr = CStr::from_ptr(ptr);
        let result = cstr.to_string_lossy().to_string();

        // Free the string using μEmacs allocator
        if let Some(free_fn) = (*api).free {
            free_fn(ptr as *mut _);
        }

        Some(result)
    }
}

/// Get current line text
fn get_current_line() -> Option<String> {
    let api = get_api()?;
    unsafe {
        let get_line_fn = (*api).get_current_line?;
        let ptr = get_line_fn();
        if ptr.is_null() {
            return None;
        }
        let cstr = CStr::from_ptr(ptr);
        let result = cstr.to_string_lossy().to_string();

        // Free the string
        if let Some(free_fn) = (*api).free {
            free_fn(ptr as *mut _);
        }

        Some(result)
    }
}

/// Create or get a buffer by name
fn get_or_create_buffer(name: &str) -> Option<*mut std::ffi::c_void> {
    let api = get_api()?;
    unsafe {
        let create_fn = (*api).buffer_create?;
        let cname = CString::new(name).ok()?;
        let bp = create_fn(cname.as_ptr());
        if bp.is_null() {
            None
        } else {
            Some(bp)
        }
    }
}

/// Switch to a buffer
fn switch_to_buffer(bp: *mut std::ffi::c_void) -> bool {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(switch_fn) = (*api).buffer_switch {
                return switch_fn(bp) != 0;
            }
        }
    }
    false
}

/// Clear a buffer
fn clear_buffer(bp: *mut std::ffi::c_void) -> bool {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(clear_fn) = (*api).buffer_clear {
                return clear_fn(bp) != 0;
            }
        }
    }
    false
}

/// Insert text into current buffer
fn buffer_insert(text: &str) -> bool {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(insert_fn) = (*api).buffer_insert {
                if let Ok(ctext) = CString::new(text) {
                    return insert_fn(ctext.as_ptr(), text.len()) != 0;
                }
            }
        }
    }
    false
}

/// Open a file at a specific line
fn find_file_line(path: &str, line: i32) -> bool {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(find_fn) = (*api).find_file_line {
                if let Ok(cpath) = CString::new(path) {
                    return find_fn(cpath.as_ptr(), line) != 0;
                }
            }
        }
    }
    false
}

/// Update the display
fn update_display() {
    if let Some(api) = get_api() {
        unsafe {
            if let Some(update_fn) = (*api).update_display {
                update_fn();
            }
        }
    }
}

/// Get the directory of the current buffer's file
fn get_buffer_directory() -> Option<String> {
    let api = get_api()?;
    unsafe {
        let current_buf_fn = (*api).current_buffer?;
        let current_buf = current_buf_fn();
        if current_buf.is_null() {
            return None;
        }
        let filename_fn = (*api).buffer_filename?;
        let filename_ptr = filename_fn(current_buf);
        if filename_ptr.is_null() {
            return None;
        }
        let filename = CStr::from_ptr(filename_ptr).to_string_lossy().to_string();
        if filename.is_empty() {
            return None;
        }
        // Extract directory from path
        if let Some(pos) = filename.rfind('/') {
            Some(filename[..pos].to_string())
        } else {
            None
        }
    }
}

/// Get the current buffer's name
fn get_buffer_name() -> Option<String> {
    let api = get_api()?;
    unsafe {
        let current_buf_fn = (*api).current_buffer?;
        let current_buf = current_buf_fn();
        if current_buf.is_null() {
            return None;
        }
        let name_fn = (*api).buffer_name?;
        let name_ptr = name_fn(current_buf);
        if name_ptr.is_null() {
            return None;
        }
        Some(CStr::from_ptr(name_ptr).to_string_lossy().to_string())
    }
}

/// Check if we're in the results buffer
fn in_results_buffer() -> bool {
    get_buffer_name()
        .map(|name| name == RG_RESULTS_BUFFER)
        .unwrap_or(false)
}

/// Perform the search and display results
fn do_search(pattern: &str) -> bool {
    // Store pattern for repeat searches
    {
        let mut guard = LAST_PATTERN.lock().unwrap();
        *guard = Some(pattern.to_string());
    }

    // Search from buffer's directory, fall back to cwd
    let search_dir = get_buffer_directory().unwrap_or_else(|| ".".to_string());

    message(&format!("Searching for: {} in {}...", pattern, search_dir));
    update_display();

    // Search the directory
    let matches = match search::search_directory(pattern, &search_dir) {
        Ok(m) => m,
        Err(e) => {
            message(&format!("Search error: {}", e));
            return false;
        }
    };

    if matches.is_empty() {
        message("No matches found");
        return true;
    }

    // Create results buffer
    let bp = match get_or_create_buffer(RG_RESULTS_BUFFER) {
        Some(b) => b,
        None => {
            message("Failed to create results buffer");
            return false;
        }
    };

    // Switch to it and clear
    switch_to_buffer(bp);
    clear_buffer(bp);

    // Format and insert results
    let results = search::format_results(&matches);
    buffer_insert(&results);

    message(&format!("{} matches - Enter to jump to file", matches.len()));
    true
}

/// Command: rg-search-rs
/// Prompt for pattern and search
extern "C" fn cmd_rg_search(_f: c_int, _n: c_int) -> c_int {
    let pattern = match prompt("Pattern (Rust): ") {
        Some(p) if !p.is_empty() => p,
        _ => {
            message("Cancelled");
            return 0;
        }
    };

    if do_search(&pattern) {
        1
    } else {
        0
    }
}

/// Command: rg-search-word-rs
/// Search for word under cursor
extern "C" fn cmd_rg_search_word(_f: c_int, _n: c_int) -> c_int {
    let word = match get_word_at_point() {
        Some(w) if !w.is_empty() => w,
        _ => {
            message("No word at point");
            return 0;
        }
    };

    if do_search(&word) {
        1
    } else {
        0
    }
}

/// Core goto logic - jump to file:line from current line
fn do_goto() -> bool {
    let line = match get_current_line() {
        Some(l) => l,
        None => {
            message("No line content");
            return false;
        }
    };

    // Skip header lines (start with '=')
    if line.starts_with('=') || line.is_empty() {
        message("Not on a result line");
        return false;
    }

    // Parse file:line:col: format
    let parts: Vec<&str> = line.splitn(4, ':').collect();
    if parts.len() < 2 {
        message("Not a valid result line");
        return false;
    }

    let file = parts[0];
    let line_num: i32 = match parts[1].parse() {
        Ok(n) => n,
        Err(_) => {
            message("Invalid line number");
            return false;
        }
    };

    if find_file_line(file, line_num) {
        message(&format!("{}:{}", file, line_num));
        true
    } else {
        message(&format!("Failed to open: {}", file));
        false
    }
}

/// Event handler for key input (API v3 event bus)
/// Returns true if event was consumed (Enter pressed in results buffer)
extern "C" fn rg_key_event_handler(event: *mut UemacsEvent, _user_data: *mut c_void) -> bool {
    if event.is_null() {
        return false;
    }

    unsafe {
        // Event data is the key code (passed as pointer-sized int)
        let key = (*event).data as c_int;

        // Only handle Enter (CR = 13, LF = 10)
        if key != '\r' as c_int && key != '\n' as c_int {
            return false;  // Not our key
        }

        // Check if we're in the results buffer
        if !in_results_buffer() {
            return false;  // Not in our buffer
        }

        // We're in the results buffer and Enter was pressed - handle it
        do_goto();
        true  // Event consumed
    }
}
