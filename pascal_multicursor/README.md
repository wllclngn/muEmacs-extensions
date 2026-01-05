# pascal_multicursor

Multiple cursor editing extension for uEmacs.

## Language

Free Pascal

## Commands

| Command | Description |
|---------|-------------|
| `mc-add` | Add cursor at current position |
| `mc-clear` | Clear all cursors |
| `mc-next` | Jump to next cursor position |
| `mc-insert` | Insert marker at all cursor positions |

## Limits

- Maximum 64 cursors

## Dependencies

- Free Pascal Compiler (`fpc`)

## Build

```sh
make
```

## Usage

```
# Add cursors at multiple locations
M-x mc-add    # at first location
M-x mc-add    # at second location
M-x mc-add    # at third location

# Navigate between cursors
M-x mc-next   # cycles through positions

# Clear when done
M-x mc-clear
```
