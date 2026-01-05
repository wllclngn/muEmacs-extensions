# haskell_project

Project management extension for uEmacs.

## Language

Haskell (GHC with FFI)

## Commands

| Command | Description |
|---------|-------------|
| `project-root` | Find and display project root directory |
| `project-files` | List all source files in project |
| `project-find` | Show project files for navigation |

## Project Detection

Walks up directory tree looking for:
- VCS: `.git`, `.hg`, `.svn`
- Build: `Makefile`, `CMakeLists.txt`
- Package: `package.json`, `Cargo.toml`, `go.mod`, `build.zig`, `stack.yaml`, `dune-project`, `setup.py`, `pyproject.toml`

## Source File Types

Recognizes: `.c`, `.h`, `.hs`, `.py`, `.rs`, `.go`, `.zig`, `.js`, `.ts`, `.cpp`, `.hpp`, `.java`, `.rb`, `.ml`, `.f90`, `.adb`, `.ads`

## Ignored Directories

`node_modules`, `target`, `build`, `dist`, `__pycache__`, `.git`

## Dependencies

- GHC (Glasgow Haskell Compiler)

## Build

```sh
make
```

## Usage

```
M-x project-root   # Shows: "Project: /path/to/root"
M-x project-files  # Lists files in *project-files* buffer
```
