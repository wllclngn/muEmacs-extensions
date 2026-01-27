# c_git

Git integration extension for μEmacs.

## Language

C

## Commands

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

## Dependencies

- `git` command in PATH

## Build

```sh
make
```

## Usage

```
M-x git-status    # Quick status
M-x git-commit    # Commit workflow
M-x git-log       # View history
```
