# μEmacs-extensions v1.0.0

A polyglot showcase for the μEmacs Extension Platform (UEP). Each extension is written in a different language, compiled to a shared library, and loaded via `dlopen()`.

## Architecture

Extensions communicate with μEmacs through the C-based `uemacs_api` struct, which provides:
- Command registration/unregistration
- Buffer operations (create, switch, insert, clear)
- Cursor/point manipulation
- User prompts and messages
- Event hooks (key press, buffer save/load, idle)
- Shell command execution
- Logging

Non-C languages use a thin C bridge layer that wraps the API and exposes it via FFI.

## Extensions

| Extension | Language | Description |
|-----------|----------|-------------|
| `ai_crystal/` | Crystal | AI agents via Claude CLI |
| `fuzzy_ada/` | Ada 2012 | Fuzzy file finder |
| `git_fortran/` | Fortran | Git integration |
| `lsp_client/` | Go | Language Server Protocol client |
| `multicursor_pascal/` | Free Pascal | Multiple cursor editing |
| `project_haskell/` | Haskell | Project root detection and file listing |
| `rg_search.c` | C23 | ripgrep search (fork/exec) |
| `rg_search_rs/` | Rust | ripgrep search (in-process via grep crate) |
| `treesitter_hl/` | Zig | Tree-sitter syntax highlighting |

## Commands

### AI (Crystal)
- `ai-spawn` - Spawn Claude agent with prompt, output to `*claude-N*` buffer
- `ai-status` - List all agents and their status
- `ai-output` - Switch to agent's output buffer
- `ai-kill` - Kill a running agent
- `ai-poll` - Poll agents and update buffers
- `ai-complete` - Complete code at point
- `ai-explain` - Explain current line
- `ai-fix` - Suggest fix for current line

### Fuzzy Finder (Ada)
- `fuzzy-find` - Fuzzy file search
- `fuzzy-grep` - Fuzzy content search

### Git (Fortran)
- `git-status` - Show `git status --short`
- `git-diff` - Show `git diff`
- `git-log` - Show 20 recent commits
- `git-blame` - Blame current line
- `git-add` - Stage current file

### LSP (Go)
- `lsp-start` - Start language server for current file
- `lsp-stop` - Stop language server
- `lsp-hover` - Show hover info at point
- `lsp-definition` - Jump to definition
- `lsp-references` - Find all references

Supported: Python (pyright), Rust (rust-analyzer), Go (gopls), C/C++ (clangd), JS/TS, Zig (zls)

### Multiple Cursors (Pascal)
- `mc-add` - Add cursor at point
- `mc-clear` - Clear all cursors
- `mc-next` - Cycle to next cursor
- `mc-insert` - Insert marker at all cursor positions

### Project (Haskell)
- `project-root` - Find and display project root
- `project-files` - List all source files in project
- `project-find` - Interactive file list

Detects: `.git`, `Makefile`, `CMakeLists.txt`, `package.json`, `Cargo.toml`, `go.mod`, etc.

### Ripgrep Search (C / Rust)
- `rg-search` / `rg-search-rs` - Search for pattern
- `rg-search-word` / `rg-search-word-rs` - Search word under cursor
- `rg-goto` - Jump to file:line from results (also Enter key)

### Tree-sitter (Zig)
- `ts-highlight` - Parse and highlight current buffer
- `ts-clear` - Clear highlighting state

Parsers: C, Python, Rust, Bash, JavaScript

## Building

Each extension has its own `Makefile`. Build individually:

```sh
cd ai_crystal && make
cd fuzzy_ada && make
cd git_fortran && make
cd multicursor_pascal && make
cd project_haskell && make
cd rg_search_rs && cargo build --release
cd treesitter_hl && zig build
```

For Go:
```sh
cd lsp_client && go build -buildmode=c-shared -o lsp_client.so
```

For C:
```sh
gcc -shared -fPIC -O2 -o rg_search.so rg_search.c
```

## Installation

Copy `.so` files to the extension directory:
```sh
cp *.so ~/.config/muemacs/extensions/
```

Or set `extension_dir` in `settings.toml`:
```toml
[extensions]
extension_dir = "~/.config/muemacs/extensions"
```

## Extension API (C)

```c
struct uemacs_extension {
    int api_version;
    const char *name;
    const char *version;
    const char *description;
    int (*init)(struct uemacs_api *api);
    void (*cleanup)(void);
};

struct uemacs_extension *uemacs_extension_entry(void);
```

The `init` function receives a pointer to `uemacs_api` with ~40 function pointers for editor interaction. Return 0 on success.

## License

Same as μEmacs.
