# c_write_edit

Prose editing mode for μEmacs with Word-like features.

## Language

C

## Features

- **Soft wrap**: Visual line wrapping without inserting hard newlines
- **Smart typography**: Real-time character transforms
  - `--` becomes em-dash (`-`)
  - `"` becomes curly quotes (`"` / `"`)
  - `'` becomes curly apostrophe (`'`)

## Commands

| Command | Description |
|---------|-------------|
| `write-edit` | Toggle write-edit mode for current buffer |

## Settings

Configured via `[extension.c_write_edit]` in settings.toml:
- `soft_wrap_col`: Wrap column (default: 80)
- `smart_typography`: Enable transforms (default: true)
- `em_dash`: `--` to em-dash (default: true)
- `smart_quotes`: Smart double quotes (default: true)
- `curly_apostrophe`: Smart apostrophes (default: true)

## Build

```sh
make
```

## Usage

```
M-x write-edit   # Toggle mode

# When enabled:
# - Lines wrap visually at column 80
# - Type -- for em-dash
# - Quotes become curly automatically
```
