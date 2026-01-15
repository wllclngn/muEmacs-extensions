# UEP Extensions

μEmacs Extension Platform (UEP) native extensions directory.

## Architecture

### In-Process (dlopen)
Direct `.so` loading via `dlopen()`. Lower overhead, full memory access.
- **Languages**: C, Rust, Zig
- **ASan compatible**: Yes

### Out-of-Process (Atomic IPC)
Separate process with lock-free IPC via atomic ring buffers and shared memory.
- **Languages**: Go, Ada, Haskell, Crystal, Pascal
- **IPC**: 16-slot ring buffers (64KB/slot), memfd shared memory
- **Benefits**: Process isolation, runtime independence, no deadlocks

## Naming Convention

Extensions follow the `language_tool` naming pattern:
- **Directory**: `language_tool/` (snake_case, language prefix)
- **Shared Object**: `language_tool.so` (matches directory)
- **Commands**: `prefix-action` (kebab-case)

## Installed Extensions

| Extension | Language | Type | Commands | Description |
|-----------|----------|------|----------|-------------|
| `ada_fuzzy` | Ada | Out-of-Process | 2 | Fuzzy file finder |
| `c_git` | C/Fortran | In-Process | 12 | Git integration |
| `c_lint` | C | In-Process | 2 | Unified linter |
| `c_minibuffer` | C | In-Process | - | Enhanced minibuffer |
| `c_mouse` | C | In-Process | 3 | Mouse support |
| `c_org` | C | In-Process | - | Org-mode support |
| `c_write_edit` | C | In-Process | 1 | Prose editing mode |
| `crystal_ai` | Crystal | Out-of-Process | 8 | AI code assistance |
| `go_dfs` | Go | Out-of-Process | 3 | Directory tree |
| `go_lsp` | Go | Out-of-Process | 11 | LSP client |
| `go_sam` | Go | Out-of-Process | 7 | Structural editing |
| `go_sudoku` | Go | Out-of-Process | 5 | Sudoku game |
| `haskell_calc` | Haskell | Out-of-Process | - | Calculator |
| `haskell_project` | Haskell | Out-of-Process | 3 | Project management |
| `pascal_multicursor` | Pascal | Out-of-Process | 4 | Multiple cursors |
| `rust_re2` | Rust | In-Process | - | RE2 regex engine |
| `zig_treesitter` | Zig | In-Process | - | Tree-sitter syntax |

## Command Reference

### ada_fuzzy
| Command | Description |
|---------|-------------|
| `fuzzy-find` | Fuzzy file finder |
| `fuzzy-buffer` | Fuzzy buffer switcher |

### c_git (Fortran core)
| Command | Description |
|---------|-------------|
| `git-status` | Show git status |
| `git-status-full` | Full status with diff |
| `git-stage` | Stage current file |
| `git-unstage` | Unstage current file |
| `git-commit` | Commit staged changes |
| `git-diff` | Show unstaged changes |
| `git-log` | Show commit history |
| `git-pull` | Pull from remote |
| `git-push` | Push to remote |
| `git-branch` | Show/switch branches |
| `git-stash` | Stash changes |
| `git-stash-pop` | Pop stashed changes |

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

### go_dfs
| Command | Description |
|---------|-------------|
| `dfs-tree` | Show directory tree |
| `dfs-find` | Find files by pattern |
| `dfs-goto` | Navigate to file |

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
| `lsp-diagnostics` | Show diagnostics |
| `lsp-symbols` | Show document symbols |

### go_sam
| Command | Description |
|---------|-------------|
| `sam-x` | Execute sam command |
| `sam-edit` | Edit with sam expression |
| `sam-print` | Print matching text |
| `sam-substitute` | Substitute pattern |
| `sam-delete` | Delete matching text |
| `sam-insert` | Insert text |
| `sam-append` | Append text |

### go_sudoku
| Command | Description |
|---------|-------------|
| `sudoku-new` | Start new puzzle |
| `sudoku-check` | Check solution |
| `sudoku-hint` | Get hint |
| `sudoku-solve` | Auto-solve puzzle |
| `sudoku-reset` | Reset puzzle |

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

## Out-of-Process IPC

Extensions in Go, Ada, Haskell, Crystal, and Pascal run in separate processes
communicating via atomic ring buffers in shared memory.

### Architecture
```
┌─────────────────────────────────────────────────────────────────┐
│                    SHARED MEMORY (memfd)                        │
├─────────────────────────────────────────────────────────────────┤
│  Header: magic, version, ext_ready (atomic), shutdown (atomic)  │
│                                                                 │
│  [Editor → Extension Ring]  16 slots × 64KB                     │
│    _Atomic state: EMPTY(0) / PENDING(1) / COMPLETE(2)           │
│    msg_type, payload_len, result, payload[65504]                │
│                                                                 │
│  [Extension → Editor Ring]  16 slots × 64KB                     │
│    Same structure                                               │
└─────────────────────────────────────────────────────────────────┘
```

### Message Flow
- **Command invocation**: Editor writes to `to_ext` ring, extension spins for PENDING
- **API calls**: Extension writes to `to_editor` ring, editor polls in main loop
- **No blocking**: Editor polls non-blocking; extensions spin-wait (acceptable for dedicated process)

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

[extension.c_minibuffer]
max_candidates = 15
modified_indicator = "Δ"

[extension.go_lsp]
enabled = true
```

## Building Extensions

Extensions are auto-built by `uep_build.py` with API version injection:

```sh
# Manual build (if needed)
cd ~/.config/muemacs/extensions/language_tool
python3 /path/to/uemacs/scripts/uep_build.py .

# Or use extension-specific build
make
```

### API Version Injection
The build system automatically injects the current API version:
- **C/C++**: `-DUEMACS_API_VERSION_BUILD=4`
- **Rust**: `UEMACS_API_VERSION` env var
- **Zig**: `-Dapi_version=4`
- **Go**: `CGO_CFLAGS` with version define

## Adding Extensions

1. Create directory: `~/.config/muemacs/extensions/language_tool/`
2. Implement extension with `uemacs_extension` struct
3. Use `UEMACS_API_VERSION_BUILD` macro for API version
4. Build shared object: `language_tool.so`
5. Restart μEmacs (auto-loads from extensions directory)

### Bridge Pattern (Out-of-Process)
For non-C languages, use a C bridge that calls into the native runtime:
```
bridge.c (C) ←→ language_tool.so (native)
    ↓
uemacs-ext-runner (loads .so, proxies API via IPC)
    ↓
Editor (polls for messages)
```

## API Version

Current API version: **4** (ABI-Stable Named Lookup)

Extensions use build-time version injection:
```c
static struct uemacs_extension ext = {
    .api_version = UEMACS_API_VERSION_BUILD,
    .name = "language_tool",
    .version = "1.0.0",
    .init = my_init,
    .cleanup = my_cleanup,
};
```

### API v4 Features
- `get_function()` for ABI-stable function lookup by name
- Atomic ring buffer IPC (no eventfd blocking)
- Auto-build with version injection
- Full buffer/prompt API for out-of-process extensions
