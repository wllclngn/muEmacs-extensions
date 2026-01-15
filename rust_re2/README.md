# rust_re2

RE2-style regex search extension for μEmacs, powered by ripgrep.

Named after RE2 (Google's C++ regex library) and Go's regexp package, both designed by Russ Cox using Thompson NFA construction for guaranteed linear-time matching.

## Language

Rust

## Commands

| Command | Description |
|---------|-------------|
| `re2` | Prompt for pattern and search |
| `re2-word` | Search for word under cursor |
| `re2-case` | Toggle case insensitive mode |
| `re2-smart` | Toggle smart case mode |
| `re2-word-boundary` | Toggle whole word matching |
| `re2-hidden` | Toggle hidden file inclusion |
| `re2-gitignore` | Toggle .gitignore respect |

## Features

- **Parallel multi-threaded search** - Uses all CPU cores for maximum speed
- **Thompson NFA regex engine** - Guaranteed linear-time matching (no catastrophic backtracking)
- **Full ripgrep integration** - Built on `grep-regex`, `grep-searcher`, and `ignore` crates
- **Memory-mapped I/O** - Efficient handling of large files
- **Smart filtering** - Respects `.gitignore`, `.ignore`, and git exclude patterns
- **Binary detection** - Automatically skips binary files
- **Configurable** - All options exposed via μEmacs config system
- **Interactive toggles** - Change search behavior on the fly

## Configuration

Add to your `settings.toml` under `[extension.rust_re2]`:

```toml
[extension.rust_re2]
# Case sensitivity
case_insensitive = false      # Force case insensitive (-i)
smart_case = true             # Case insensitive if pattern all lowercase

# Matching behavior
word_boundary = false         # Match whole words only (-w)
fixed_strings = false         # Literal strings, not regex (-F)
multiline = false             # Allow patterns to span lines

# Context lines (grep -A/-B/-C style)
context_before = 0            # Lines before match (-B)
context_after = 0             # Lines after match (-A)

# File filtering
hidden = false                # Include hidden files
follow_symlinks = false       # Follow symbolic links
git_ignore = true             # Respect .gitignore files
max_depth = 0                 # Max directory depth (0 = unlimited)
max_filesize = 0              # Max file size in bytes (0 = unlimited)
max_count = 0                 # Max matches per file (0 = unlimited)

# File types (comma-separated, e.g., "rust,c,py")
# See 'rg --type-list' for available types
file_types = ""

# Glob patterns (comma-separated)
glob_include = ""             # Files to include (e.g., "*.rs,*.c")
glob_exclude = ""             # Files to exclude (e.g., "*.log,*.tmp")

# Performance
threads = 0                   # Number of threads (0 = auto-detect)
mmap = true                   # Use memory-mapped files for large files
```

## Dependencies

- Rust toolchain (cargo)
- ripgrep crates (automatically fetched by cargo):
  - `grep-regex` - RE2-style regex matcher
  - `grep-searcher` - File searching with binary detection
  - `grep-matcher` - Matcher trait abstraction
  - `ignore` - Parallel directory walking with gitignore support
  - `crossbeam-channel` - Lock-free inter-thread communication
  - `num_cpus` - CPU core detection
  - `memmap2` - Memory-mapped file I/O

## Build

```sh
cd extensions/rust_re2
cargo build --release
cp target/release/librust_re2.so ~/.config/uemacs/extensions/
```

Or use `uep_build.py` for automatic detection.

## Usage

```
M-x re2              # Enter regex pattern, search from buffer's directory
M-x re2-word         # Search for word under cursor

# Toggle options before searching:
M-x re2-case         # Toggle case insensitive (shows ON/OFF)
M-x re2-smart        # Toggle smart case (shows ON/OFF)
M-x re2-word-boundary # Toggle whole word matching (shows ON/OFF)
M-x re2-hidden       # Toggle hidden files (shows INCLUDED/EXCLUDED)
M-x re2-gitignore    # Toggle .gitignore (shows RESPECTED/IGNORED)

# In *re2-results* buffer:
# - Navigate to a result line
# - Press Enter to jump to file:line
```

## Results Format

```
42 RESULTS FOUND (1234 files searched). Search completed in 23 ms.

/path/to/file.rs:123:0: matched line content
/path/to/other.c:456:12: another match
...
```

## Architecture

```
lib.rs          Entry point, command handlers, config loading
├── ffi.rs      C FFI bindings to μEmacs extension API
└── search.rs   Parallel ripgrep search implementation
    ├── SearchOptions    All configurable search parameters
    ├── search_parallel  Multi-threaded directory search
    ├── build_matcher    RE2 regex compilation
    ├── build_searcher   Searcher with binary detection, mmap
    └── build_walker     Parallel directory walker with filters
```

## Algorithm

The regex engine uses Thompson NFA (Nondeterministic Finite Automaton) construction, which guarantees:

- **O(n) time complexity** - Linear in input size, regardless of pattern
- **No catastrophic backtracking** - Unlike PCRE/Perl regexes
- **Predictable performance** - Same pattern always takes similar time

This is the same algorithm used by:
- RE2 (Google's C++ regex library)
- Go's `regexp` package
- Rust's `regex` crate
- ripgrep

## License

Same as μEmacs (public domain / MIT)
