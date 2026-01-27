# go_chess

Chess engine extension for μEmacs featuring parallel search, unified self-play learning, and an embedded opening book seeded from 100+ years of master games.

## Language

Go (CGO)

## Architecture

### Parallel Alpha-Beta Search (YBWC)

The engine implements Young Brothers Wait Concept (YBWC) with work-stealing:

```
Root Position
    ├─ First child: searched sequentially (establishes bounds)
    └─ Remaining children: pushed to work-stealing deques
        ├─ Workers pop from their own deque (LIFO)
        ├─ Idle workers steal from others (FIFO)
        └─ Results flow back via channels
```

Key components:
- **Work-stealing deques**: Each worker has a local deque; steal from bottom, pop from top
- **Help-while-waiting**: Workers process other tasks while waiting for children (prevents deadlock)
- **Sibling alpha sharing**: Atomic bounds updates across parallel siblings
- **Cutoff cancellation**: When beta cutoff found, cancel remaining sibling tasks

Based on concurrent-dfs infrastructure adapted for game tree search.

### Unified Learning System

Both White and Black are the **same AI** learning from both perspectives:

```
Game Result: White wins

White's moves → recorded as WINS   → "this worked"
Black's moves → recorded as LOSSES → "avoid this"

Both feed the SAME unified model
```

When a human plays against this AI, it knows:
- What moves lead to wins (prefer)
- What moves lead to losses (avoid)
- Double learning per game (2x data efficiency)

Research basis: AlphaZero, Leela Chess Zero, Crafty book learning

### Softmax Temperature Exploration

Self-play uses temperature-based move selection to prevent identical games:

```
P(move_i) = exp(score_i / τ) / Σexp(score_j / τ)

τ = 1.0  for ply 1-20   (exploration)
τ → 0.1  for ply 21-30  (exploitation)
τ = 0    after ply 30   (greedy)
```

Uses Linux `getrandom()` syscall via crypto/rand for true randomness.

Research basis: AlphaZero (Dirichlet noise + temperature), Leela Chess Zero

## Commands

| Command | Description |
|---------|-------------|
| `chess` | Start new game (Human=White, AI=Black) |
| `chess-move` | Make a move (e.g., "e2e4", "e7e8q" for promotion) |
| `chess-undo` | Undo last move pair |
| `chess-depth` | Set search depth (default: 6) |
| `chess-eval` | Show current position evaluation |
| `chess-hint` | Get AI suggestion for human |
| `chess-flip` | Flip board orientation |
| `chess-fen` | Show current FEN position |
| `chess-auto` | AI vs AI mode (runs until game ends) |
| `chess-workers` | Set worker count (default: 2) |
| `chess-stop` | Stop AI vs AI game |

## Opening Book

Embedded opening book seeded from the Lichess Masters Database:

- **1,233 positions** from grandmaster games
- **1.2+ million master games** (e4: 1.2M, d4: 977K, etc.)
- **Auto-seeds on first run** - no manual setup required
- **Accumulative learning** - training builds on this foundation

Book location: `~/.config/muemacs/chess_book.json`

## Dependencies

- Go 1.21+

## Build

```sh
python3 build.py
```

## Training

`run_games.py` runs self-play games to train the unified model.

```sh
# Basic usage
python3 run_games.py [num_games] [options]

# Options
  --reset                  Reset learning data (fresh start)
  -d, --debug              Show per-move output
  -wN, --workers=N         Worker threads (default: all CPUs)
  --depth=N                Search depth for both sides (default: 6)
  --depth-white=N          White's search depth
  --depth-black=N          Black's search depth
  --draw-value=N           Draw value 0-1 (default: 0.5)
  -h, --help               Show help

# Examples
python3 run_games.py                    # 5 games at depth 6
python3 run_games.py --reset 20         # Reset and run 20 games
python3 run_games.py 100 --depth=5      # 100 games at depth 5
python3 run_games.py 50 --draw-value=0.3  # Encourage decisive games
```

### Training Output

```
[INFO] Book loaded: 1233 positions
[INFO] Running 10 games... (training mode: exploration ON)
[INFO] Game 1: White wins by checkmate (35 moves)
[INFO] Game 2: Draw (200 moves)
...
[INFO] === Learning Statistics ===
[INFO] Positions with learning: 142 (+142)
[INFO] Win/Loss/Draw records: 30/30/90
```

The Win/Loss/Draw records show both perspectives being learned:
- **30 wins**: Moves that led to victory for the side that played them
- **30 losses**: Moves that led to defeat for the side that played them
- **90 draws**: Moves from drawn games

### Learning Data Structure

```
Position (FEN) → Moves
    └─ Move "e2e4"
        ├─ master_games: 1231250  (from Lichess)
        ├─ our_games: 15          (self-play count)
        ├─ our_wins: 10           (led to win for this side)
        ├─ our_losses: 3          (led to loss for this side)
        └─ our_draws: 2           (led to draw)
```

Win rate = our_wins / our_games → used to weight move selection

### Draw Value and Contempt

| draw_value | Contempt | Effect |
|------------|----------|--------|
| 0.5 | 0cp | Neutral - accepts draws |
| 0.4 | 10cp | Slight aggression |
| 0.3 | 20cp | Moderate - avoids repetitions |
| 0.0 | 50cp | Maximum aggression |

## Performance

Typical search times (12 workers, Ryzen):

| Depth | Time | Notes |
|-------|------|-------|
| 3 | ~6ms | Quick evaluation |
| 4 | ~80ms | Fast play |
| 5 | ~700ms | Good balance |
| 6 | ~3-5s | Strong play |
| 7+ | varies | Tournament strength |

## Configuration

In `~/.config/muemacs/settings.toml`:

```toml
[extension.go_chess]
search_depth = 6
workers = 2
auto_delay_ms = 500
```

## Research References

- [AlphaZero Paper](https://arxiv.org/pdf/1712.01815) - Self-play, temperature, Dirichlet noise
- [Leela Chess Zero](https://lczero.org/) - Open-source AlphaZero implementation
- [Chess Programming Wiki - Book Learning](https://www.chessprogramming.org/Book_Learning) - Result-driven learning
- [Chess Programming Wiki - YBWC](https://www.chessprogramming.org/Young_Brothers_Wait_Concept) - Parallel search

## Data Attribution

Opening book data from [Lichess Masters Database](https://lichess.org).
License: CC0 Public Domain.
