# go_lsp

Language Server Protocol client for uEmacs.

## Language

Go (cgo)

## Features

- Concurrent LSP client with goroutine-based response handling
- Semantic token highlighting (when server supports it)
- Definition/references navigation
- Hover documentation

## Commands

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

## Supported Languages

| Extension | Server |
|-----------|--------|
| `.py` | pyright-langserver |
| `.rs` | rust-analyzer |
| `.go` | gopls |
| `.c`, `.h`, `.cpp` | clangd |
| `.js`, `.ts` | typescript-language-server |
| `.zig` | zls |

## Dependencies

- Go compiler
- Respective language servers installed

## Build

```sh
go build -buildmode=c-shared -o go_lsp.so
```

## Usage

```
M-x lsp-start      # Auto-detects server from file extension
M-x lsp-hover      # Show type/docs at cursor
M-x lsp-definition # Jump to definition
```
