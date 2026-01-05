# c_mouse

Comprehensive mouse support extension for uEmacs.

## Language

C

## Features

- **Click to Position**: Click anywhere to move cursor
- **Double-Click**: Select word under cursor
- **Triple-Click**: Select entire line
- **Drag Selection**: Click and drag to select text
- **Shift+Click**: Extend selection from cursor
- **Scroll Wheel**: Scroll buffer up/down
- **Middle-Click Paste**: Paste from selection buffer
- **Window Focus**: Click to focus split windows

## Commands

| Command | Description |
|---------|-------------|
| `mouse-enable` | Enable mouse support |
| `mouse-disable` | Disable mouse support |
| `mouse-status` | Show current mouse state |

## Settings

Configured via `[extension.c_mouse]` in settings.toml:
- `scroll_lines`: Lines per scroll wheel event (default: 3)

## Protocol

Uses SGR 1006 extended mouse protocol for accurate coordinate tracking.

## Build

```sh
make
```

## Usage

```
M-x mouse-enable   # Enable mouse
M-x mouse-disable  # Disable mouse
M-x mouse-status   # Check status
```
