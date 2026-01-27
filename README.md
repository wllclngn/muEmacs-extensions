# μEmacs Extensions

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
| `c_linus` | C | In-Process | Linus Torvalds uEmacs compatibility |
| `c_minibuffer` | C | In-Process | Modern completion framework |
| `c_mouse` | C | In-Process | Mouse support |
| `c_org` | C | In-Process | Org-mode outlining |
| `c_write_edit` | C | In-Process | Prose editing mode |
| `crystal_ai` | Crystal | Out-of-Process | AI code assistance |
| `go_chess` | Go | Out-of-Process | Chess engine with learning |
| `go_dfs` | Go | Out-of-Process | Concurrent DFS file traversal |
| `go_lsp` | Go | Out-of-Process | Language Server Protocol client |
| `go_sam` | Go | Out-of-Process | Structural regular expressions (sam) |
| `go_sudoku` | Go | Out-of-Process | Sudoku game |
| `haskell_calc` | Haskell | Out-of-Process | Scientific calculator |
| `haskell_project` | Haskell | Out-of-Process | Project management |
| `pascal_multicursor` | Pascal | Out-of-Process | Multiple cursors |
| `rust_re2` | Rust | In-Process | RE2-style regex search (ripgrep) |
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
| `fuzzy-find` | Fuzzy match files in current directory |
| `fuzzy-grep` | Search file contents with fuzzy matching |

### c_git
| Command | Description |
|---------|-------------|
| `git-status` | Show repository status (short) |
| `git-status-full` | Show full status in buffer |
| `git-stage` | Stage current file |
| `git-unstage` | Unstage current file |
| `git-commit` | Commit staged changes (prompts for message) |
| `git-diff` | Show diff of current file |
| `git-log` | Show commit history (use C-u N for N commits) |
| `git-pull` | Pull from remote |
| `git-push` | Push to remote |
| `git-branch` | Show current branch |
| `git-stash` | Stash changes |
| `git-stash-pop` | Pop stashed changes |

### c_lint
| Command | Description |
|---------|-------------|
| `lint` | Run linter on buffer |
| `lint-clear` | Clear diagnostics |

### c_linus
| Command | Description |
|---------|-------------|
| `linus-mode` | Toggle Linus Torvalds uEmacs compatibility mode |

Enables classic modeline format, VTIME-based bracket flash, and disables modern visual features.

### c_minibuffer
| Command | Description |
|---------|-------------|
| `switch-buffer` | Buffer picker with live filtering (shadows built-in) |
| `pick-cancel` | Cancel current pick operation |

### c_mouse
| Command | Description |
|---------|-------------|
| `mouse-enable` | Enable mouse support |
| `mouse-disable` | Disable mouse support |
| `mouse-status` | Show mouse state |

### c_org
| Command | Description |
|---------|-------------|
| `org-mode` | Toggle org-mode for current buffer |
| `org-tab` | Fold/unfold headline at cursor (TAB) |
| `org-cycle-global` | Cycle global visibility (Shift-TAB) |
| `org-todo` | Cycle TODO state (TODO -> DONE -> none) |
| `org-checkbox` | Toggle checkbox at cursor |

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

### go_chess
| Command | Description |
|---------|-------------|
| `chess` | Start new game (Human=White, AI=Black) |
| `chess-move` | Make a move (e.g., "e2e4", "e7e8q" for promotion) |
| `chess-undo` | Undo last move pair |
| `chess-depth` | Set search depth (default: 6) |
| `chess-eval` | Show current position evaluation |
| `chess-hint` | Get AI suggestion for human |
| `chess-flip` | Flip board orientation |
| `chess-fen` | Show current FEN position |
| `chess-auto` | AI vs AI mode (runs until game ends) |
| `chess-workers` | Set worker count (default: 2) |
| `chess-stop` | Stop AI vs AI game |

### go_dfs
| Command | Description |
|---------|-------------|
| `dfs-find` | Find files matching pattern (concurrent) |
| `dfs-grep` | Search file contents concurrently |
| `dfs-count` | Count files/directories concurrently |

### go_lsp
| Command | Description |
|---------|-------------|
| `lsp-start` | Start LSP server for current file type |
| `lsp-stop` | Stop the LSP server |
| `lsp-hover` | Show hover info at cursor |
| `lsp-definition` | Jump to definition |
| `lsp-references` | Find all references |
| `lsp-refresh-tokens` | Refresh semantic token highlighting |
| `lsp-completion` | Trigger code completion |
| `lsp-diagnostics` | Show diagnostics |
| `lsp-code-action` | Show code actions |
| `lsp-document-symbols` | List document symbols |
| `lsp-workspace-symbols` | Search workspace symbols |

### go_sam
| Command | Description |
|---------|-------------|
| `sam` | Execute sam structural regex command |

Supports Rob Pike's sam commands: `x/pattern/cmd`, `y/pattern/cmd`, `g/pattern/cmd`, `v/pattern/cmd`.

### go_sudoku
| Command | Description |
|---------|-------------|
| `sudoku-new` | Start a new puzzle |
| `sudoku-check` | Check for errors |
| `sudoku-hint` | Reveal one cell |
| `sudoku-solve` | Show the solution |
| `sudoku-reset` | Reset to original puzzle |

### haskell_calc
| Command | Description |
|---------|-------------|
| `calc` | Open calculator REPL in *calc* buffer |
| `calc-hex` | Format last result as hexadecimal |
| `calc-bin` | Format last result as binary |
| `calc-oct` | Format last result as octal |

SpeedCrunch-style calculator with variables, history, and scientific functions.

### haskell_project
| Command | Description |
|---------|-------------|
| `project-root` | Show project root |
| `project-files` | List source files |
| `project-find` | Navigate to project file |

### pascal_multicursor
| Command | Description |
|---------|-------------|
| `mc-add` | Add cursor at current position |
| `mc-clear` | Clear all cursors |
| `mc-next` | Jump to next cursor position |
| `mc-insert` | Insert marker at all cursor positions |

### rust_re2
| Command | Description |
|---------|-------------|
| `re2` | RE2-style regex search |
| `re2-word` | Search word at cursor |
| `re2-case` | Toggle case insensitive mode |
| `re2-smart` | Toggle smart case mode |
| `re2-word-boundary` | Toggle whole word matching |
| `re2-hidden` | Toggle hidden file inclusion |
| `re2-gitignore` | Toggle .gitignore respect |

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

[extension.rust_re2]
case_insensitive = false
smart_case = true
word_boundary = false
hidden = false
git_ignore = true
threads = 0              # 0 = auto-detect CPU cores

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

Current API version: **4** (ABI-Stable Named Lookup)

Extensions declare their API version in the `uemacs_extension` struct:
```c
static struct uemacs_extension ext = {
    .name = "language_tool",
    .version = "1.0.0",
    .api_version = 4,
    ...
};
```
