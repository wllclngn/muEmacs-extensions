# rust_search

Fast ripgrep-powered search extension for uEmacs.

## Language

Rust

## Commands

| Command | Description |
|---------|-------------|
| `rg-search` | Prompt for pattern and search |
| `rg-search-word` | Search for word under cursor |

## Features

- Uses ripgrep's `grep` crate for in-process searching (no fork/exec overhead)
- Results displayed in `*rg-results*` buffer
- Press Enter on result line to jump to file:line
- Respects `.gitignore` patterns

## Dependencies

- Rust toolchain (`cargo`)

## Build

```sh
cargo build --release
cp target/release/librust_search.so ./rust_search.so
```

Or use `uep_build.py` for automatic detection.

## Usage

```
M-x rg-search        # Enter pattern
M-x rg-search-word   # Search word at cursor

# In results buffer:
# - Navigate to a result line
# - Press Enter to jump to file
```
