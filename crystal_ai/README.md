# crystal_ai

AI-powered code assistance extension for μEmacs, using Claude CLI agents.

## Language

Crystal (with C bridge)

## Commands

| Command | Description |
|---------|-------------|
| `ai-spawn` | Spawn a Claude agent with a prompt, output to `*claude-N*` buffer |
| `ai-status` | List all agents and their status |
| `ai-output` | Switch to an agent's output buffer |
| `ai-kill` | Kill a running agent |
| `ai-poll` | Poll agents and update output buffers |
| `ai-complete` | Complete code at cursor (spawns agent) |
| `ai-explain` | Explain code at cursor |
| `ai-fix` | Suggest fix for code at cursor |

## Dependencies

- Crystal compiler (`crystal`)
- Claude CLI (`claude`) in PATH

## Build

```sh
make
```

## Usage

```
M-x ai-spawn     # Enter your prompt
M-x ai-poll      # Check for output
M-x ai-status    # See all agents
```
