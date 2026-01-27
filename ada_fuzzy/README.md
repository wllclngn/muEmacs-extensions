# ada_fuzzy

Fuzzy file finder and grep extension for μEmacs.

## Language

Ada (GNAT)

## Commands

| Command | Description |
|---------|-------------|
| `fuzzy-find` | Fuzzy match files in current directory |
| `fuzzy-grep` | Search file contents with fuzzy matching |

## Algorithm

Uses a custom fuzzy scoring algorithm:
- Base points for character matches
- Bonus for consecutive matches
- Bonus for matches at word boundaries (after `/`, `_`, `.`)

## Dependencies

- GNAT Ada compiler

## Build

```sh
make
```

## Usage

```
M-x fuzzy-find   # Enter pattern, jumps to best match
M-x fuzzy-grep   # Search file contents
```
