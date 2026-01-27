# c_lint

Unified linter extension for μEmacs aggregating diagnostics from multiple sources.

## Language

C

## Features

- **Thompson NFA Engine**: Pattern-based linting with regex matching
- **Event Bus Integration**: Receives diagnostics from tree-sitter and LSP
- **Unified Display**: Aggregates all diagnostics in one interface

## Commands

| Command | Description |
|---------|-------------|
| `lint` | Run linter on current buffer |
| `lint-clear` | Clear all diagnostics |

## Diagnostic Sources

1. **Pattern Rules**: Built-in Thompson NFA regex engine
2. **Tree-sitter**: AST-based diagnostics via event bus (requires `zig_treesitter`)
3. **LSP**: Language server diagnostics via event bus (requires `go_lsp`)

## Build

```sh
make
```

## Usage

```
M-x lint         # Run linter
M-x lint-clear   # Clear diagnostics
```
