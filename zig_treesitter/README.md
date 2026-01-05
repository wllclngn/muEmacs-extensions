# zig_treesitter

Tree-sitter syntax highlighting extension for uEmacs.

## Language

Zig

## Features

- Incremental parsing via libtree-sitter
- AST-based syntax highlighting (more accurate than regex)
- Per-buffer tree caching
- Registers as custom lexer, overriding built-in highlighting

## Supported Languages

- C (`.c`, `.h`)
- Python (`.py`)
- Rust (`.rs`)
- Bash (`.sh`, `.bash`)
- JavaScript (`.js`, `.mjs`)

## Architecture

1. On buffer load, parses file with tree-sitter
2. Caches AST per-buffer (up to 16 buffers)
3. Lexer callback walks AST nodes on requested line
4. Maps node types to face IDs (keyword, string, comment, etc.)

## Dependencies

- Zig compiler
- libtree-sitter
- Tree-sitter language grammars (tree-sitter-c, tree-sitter-python, etc.)

## Build

```sh
zig build -Doptimize=ReleaseFast
cp zig-out/lib/libzig_treesitter.so ./zig_treesitter.so
```

## Usage

Automatic - highlighting activates when opening supported file types.
