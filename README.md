# UEP Extensions

μEmacs Extension Platform (UEP) native extensions directory.

## Naming Convention

Extensions follow the `language_tool` naming pattern:
- **Directory**: `language_tool/` (snake_case, language prefix)
- **Shared Object**: `language_tool.so` (matches directory)
- **Commands**: `prefix-action` (kebab-case)

## Installed Extensions

| Extension | Language | Type | Description |
|-----------|----------|------|-------------|
| `ada_fuzzy` | Ada | Out-of-Process | Fuzzy file finder |
| `c_git` | C | In-Process | Git integration |
| `c_lint` | C | In-Process | Unified linter |
| `c_mouse` | C | In-Process | Mouse support |
| `c_write_edit` | C | In-Process | Prose editing mode |
| `crystal_ai` | Crystal | Out-of-Process | AI code assistance |
| `go_lsp` | Go | Out-of-Process | Language Server Protocol client |
| `haskell_project` | Haskell | Out-of-Process | Project management |
| `pascal_multicursor` | Pascal | Out-of-Process | Multiple cursors |
| `rust_search` | Rust | In-Process | Ripgrep-powered search |
| `zig_treesitter` | Zig | In-Process | Tree-sitter syntax highlighting |

## Extension Types

### In-Process (dlopen)
Direct `.so` loading via `dlopen()`. Lower overhead, full memory access.
- **Languages**: C, Rust, Zig

### Out-of-Process (IPC)
Separate process with bidirectional IPC via memfd/eventfd.
- **Languages**: Go, Ada, Haskell, Crystal, Pascal
- **Benefits**: Process isolation, crash recovery, language flexibility

## Command Reference

### ada_fuzzy
| Command | Description |
|---------|-------------|
| `fuzzy-find` | Fuzzy file finder |

### c_git
| Command | Description |
|---------|-------------|
| `git-status` | Show git status |
| `git-diff` | Show unstaged changes |
| `git-log` | Show commit history |
| `git-blame` | Show line-by-line blame |
| `git-stage` | Stage current file |
| `git-commit` | Commit staged changes |
| `git-branch` | Show/switch branches |

### c_lint
| Command | Description |
|---------|-------------|
| `lint` | Run linter on buffer |
| `lint-clear` | Clear diagnostics |

### c_mouse
| Command | Description |
|---------|-------------|
| `mouse-enable` | Enable mouse support |
| `mouse-disable` | Disable mouse support |
| `mouse-status` | Show mouse state |

### c_write_edit
| Command | Description |
|---------|-------------|
| `write-edit` | Toggle prose editing mode |

### crystal_ai
| Command | Description |
|---------|-------------|
| `ai-spawn` | Spawn Claude agent with prompt |
| `ai-status` | List agents and status |
| `ai-output` | Switch to agent buffer |
| `ai-kill` | Kill running agent |
| `ai-poll` | Poll agents for output |
| `ai-complete` | Complete code at cursor |
| `ai-explain` | Explain code at cursor |
| `ai-fix` | Suggest fix for code |

### go_lsp
| Command | Description |
|---------|-------------|
| `lsp-start` | Start language server |
| `lsp-stop` | Stop language server |
| `lsp-restart` | Restart language server |
| `lsp-goto-def` | Go to definition |
| `lsp-find-refs` | Find references |
| `lsp-hover` | Show hover info |
| `lsp-rename` | Rename symbol |
| `lsp-format` | Format buffer |
| `lsp-actions` | Show code actions |

### haskell_project
| Command | Description |
|---------|-------------|
| `project-root` | Show project root |
| `project-files` | List source files |
| `project-find` | Navigate to project file |

### pascal_multicursor
| Command | Description |
|---------|-------------|
| `mc-add` | Add cursor at point |
| `mc-add-next` | Add cursor at next match |
| `mc-add-all` | Add cursors at all matches |
| `mc-clear` | Clear all cursors |

### rust_search
| Command | Description |
|---------|-------------|
| `rg-search` | Search with pattern |
| `rg-search-word` | Search word at cursor |

### zig_treesitter
Automatic - activates on supported file types (.c, .h, .py, .rs, .sh, .js).

## Configuration

Extensions are configured in `~/.config/muemacs/settings.toml`:

```toml
[extension.c_mouse]
enabled = true
scroll_lines = 3

[extension.c_git]
auto_status = true

[extension.c_write_edit]
soft_wrap_col = 80
smart_typography = true

[extension.go_lsp]
enabled = true
```

## Building Extensions

Each extension has its own build system:

```sh
# C extensions
cd extension_name && make

# Rust extensions
cargo build --release && cp target/release/lib*.so ./extension_name.so

# Zig extensions
zig build -Doptimize=ReleaseFast && cp zig-out/lib/*.so ./extension_name.so

# Out-of-process extensions (Go, Ada, Haskell, Crystal, Pascal)
make  # Builds bridge.c and language-specific components
```

## Adding Extensions

1. Create directory: `~/.config/muemacs/extensions/language_tool/`
2. Implement extension with `uemacs_extension` struct
3. Build shared object: `language_tool.so`
4. Add config section: `[extension.language_tool]` in settings.toml
5. Restart μEmacs or use `M-x extension-load`

## API Version

Current API version: **3** (Event Bus)

Extensions declare their API version in the `uemacs_extension` struct:
```c
static struct uemacs_extension ext = {
    .name = "language_tool",
    .version = "1.0.0",
    .api_version = 3,
    ...
};
```
