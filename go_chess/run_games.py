#!/usr/bin/env python3
"""
Chess AI Training Script
Runs AI vs AI games and tracks learning progress.

Both White and Black train the SAME system - every game teaches both sides.

Usage: python3 run_games.py [num_games] [options]

Options:
  -d, --debug              Show per-move output
  -wN, --workers=N         Set worker threads (default: all CPUs)
  --depth=N                Set search depth for both sides (default: 6)
  --depth-white=N          Set White's search depth (overrides --depth)
  --depth-black=N          Set Black's search depth (overrides --depth)
  --draw-value=N           Symmetric contempt (0=loss, 0.5=neutral, 1=win)
                           Default: 0.5. Use 0.3-0.4 to encourage decisive games.
  --contempt-white=N       White's draw value (0.0=aggressive, 0.5=neutral)
  --contempt-black=N       Black's draw value (0.0=aggressive, 0.5=neutral)
                           Use these to make one side more aggressive.
                           Example: --contempt-white=0.3 --contempt-black=0.5
  --reset                  Reset learning data (clear book, fresh start)
  -h, --help               Show this help

Default: 5 games, depth 6, all CPU cores

Logging matches muEmacs style: [INFO], [DEBUG], [ERROR]
"""

import subprocess
import json
import sys
import os
from pathlib import Path
from datetime import datetime

CHESS_DIR = Path(__file__).parent.resolve()
BOOK_PATH = Path.home() / ".config/muemacs/chess_book.json"
TEST_FILE = CHESS_DIR / "auto_games_test.go"

# Persistent log file for training runs
# =====================================
# This log captures ALL training output to a file for post-mortem analysis.
# Critical for overnight runs where terminal output is lost.
#
# Log location: /tmp/go_chess_training.log
# Format: [TIMESTAMP] [LEVEL] message
#
# Overwritten each run - check after training completes or if errors occur.
# Lost on reboot, but that's fine - it's just for debugging the current session.
LOG_PATH = Path("/tmp/go_chess_training.log")
_log_file = None

def _init_log():
    """Initialize log file handle."""
    global _log_file
    if _log_file is None:
        try:
            _log_file = open(LOG_PATH, "w", buffering=1)  # Overwrite, line buffered
            _log_file.write(f"[{datetime.now().isoformat()}] Training session started\n")
            _log_file.write(f"{'='*60}\n")
        except Exception as e:
            print(f"[WARN] Could not open log file: {e}", file=sys.stderr)

def _write_log(level, msg):
    """Write to log file with timestamp."""
    global _log_file
    if _log_file is None:
        _init_log()
    if _log_file:
        try:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            _log_file.write(f"[{timestamp}] [{level}] {msg}\n")
        except:
            pass  # Don't crash on log failures

DEBUG = "--debug" in sys.argv or "-d" in sys.argv

# Get worker count from args or use all CPUs
WORKERS = os.cpu_count() or 8
for arg in sys.argv:
    if arg.startswith("-w") and arg[2:].isdigit():
        WORKERS = int(arg[2:])
    elif arg.startswith("--workers="):
        WORKERS = int(arg.split("=")[1])

# Get search depth from args (default: 6)
DEPTH_DEFAULT = 6
DEPTH_WHITE = DEPTH_DEFAULT
DEPTH_BLACK = DEPTH_DEFAULT
for arg in sys.argv:
    if arg.startswith("--depth="):
        DEPTH_DEFAULT = int(arg.split("=")[1])
        DEPTH_WHITE = DEPTH_DEFAULT
        DEPTH_BLACK = DEPTH_DEFAULT
    elif arg.startswith("--depth-white="):
        DEPTH_WHITE = int(arg.split("=")[1])
    elif arg.startswith("--depth-black="):
        DEPTH_BLACK = int(arg.split("=")[1])

# Warning for deep search
if DEPTH_WHITE > 7 or DEPTH_BLACK > 7:
    print("[WARN] Running at higher than Depth 7 will extend run times.")

# Get unified draw value (symmetric for self-play training)
# 0.5 = standard (draw worth half a win)
# Lower values encourage decisive games (faster learning)
DRAW_VALUE = 0.5
CONTEMPT_WHITE = None  # None = use DRAW_VALUE
CONTEMPT_BLACK = None  # None = use DRAW_VALUE
for arg in sys.argv:
    if arg.startswith("--draw-value="):
        DRAW_VALUE = float(arg.split("=")[1])
    elif arg.startswith("--contempt-white="):
        CONTEMPT_WHITE = float(arg.split("=")[1])
    elif arg.startswith("--contempt-black="):
        CONTEMPT_BLACK = float(arg.split("=")[1])

# If only one side's contempt is set, use DRAW_VALUE for the other
if CONTEMPT_WHITE is not None and CONTEMPT_BLACK is None:
    CONTEMPT_BLACK = DRAW_VALUE
if CONTEMPT_BLACK is not None and CONTEMPT_WHITE is None:
    CONTEMPT_WHITE = DRAW_VALUE

if "--help" in sys.argv or "-h" in sys.argv:
    print(__doc__)
    sys.exit(0)


def log_info(msg):
    """Log info to stdout and file."""
    print(f"[INFO] {msg}")
    _write_log("INFO", msg)


def log_debug(msg):
    """Log debug to stdout (if enabled) and always to file."""
    if DEBUG:
        print(f"[DEBUG] {msg}")
    _write_log("DEBUG", msg)


def log_error(msg):
    """Log error to stderr and file."""
    print(f"[ERROR] {msg}", file=sys.stderr)
    _write_log("ERROR", msg)


def get_learning_stats():
    """Read chess_book.json and return learning statistics."""
    stats = {
        "total_positions": 0,
        "positions_with_learning": 0,
        "total_our_games": 0,
        "total_our_wins": 0,
        "total_our_losses": 0,
        "total_our_draws": 0,
    }

    if not BOOK_PATH.exists():
        return stats

    try:
        with open(BOOK_PATH) as f:
            book = json.load(f)

        positions = book.get("positions", {})
        stats["total_positions"] = len(positions)

        for fen, pos in positions.items():
            has_learning = False
            for move in pos.get("moves", []):
                our_games = move.get("our_games", 0)
                if our_games > 0:
                    has_learning = True
                    stats["total_our_games"] += our_games
                    stats["total_our_wins"] += move.get("our_wins", 0)
                    stats["total_our_losses"] += move.get("our_losses", 0)
                    stats["total_our_draws"] += move.get("our_draws", 0)

            if has_learning:
                stats["positions_with_learning"] += 1

    except (json.JSONDecodeError, IOError) as e:
        log_error(f"Failed to read book: {e}")

    return stats


def generate_go_test(num_games, depth_white=6, depth_black=6, contempt_white=None, contempt_black=None):
    """Generate Go test file that plays N complete AI vs AI games.

    depth_white: search depth for White
    depth_black: search depth for Black
    contempt_white: White's draw value (0=aggressive, 0.5=neutral), or None for symmetric
    contempt_black: Black's draw value (0=aggressive, 0.5=neutral), or None for symmetric
    """

    # Build contempt setup code
    if contempt_white is not None and contempt_black is not None:
        contempt_setup = f"SetAsymmetricContempt({contempt_white}, {contempt_black})"
        contempt_white_cp = int((0.5 - contempt_white) * 100)
        contempt_black_cp = int((0.5 - contempt_black) * 100)
        contempt_log = f'fmt.Printf("[INFO] Depth: W={depth_white} B={depth_black}, Contempt: W=%dcp B=%dcp\\n", {contempt_white_cp}, {contempt_black_cp})'
    else:
        contempt_setup = f"SetContempt({DRAW_VALUE})"
        contempt_cp = int((0.5 - DRAW_VALUE) * 100)
        contempt_log = f'fmt.Printf("[INFO] Depth: W={depth_white} B={depth_black}, Contempt: %dcp\\n", {contempt_cp})'

    test_code = f'''package main

import (
	"fmt"
	"testing"
	"time"
)

const maxMoves = 200  // Prevent infinite games

func TestAutoGames(t *testing.T) {{
	InitOpeningBook()
	SetTrainingMode(true)  // Enable exploration via softmax temperature
	{contempt_setup}  // Set contempt
	ResetLearningStats()  // Clear per-session counters

	if globalBook == nil {{
		fmt.Println("[ERROR] Failed to load opening book")
		t.Fatal("No book")
	}}

	fmt.Printf("[INFO] Book loaded: %d positions\\n", len(globalBook.Positions))
	fmt.Println("[INFO] Running {num_games} games... (training mode: exploration ON)")
	{contempt_log}

	// Ensure book is saved at the end
	defer func() {{
		time.Sleep(100 * time.Millisecond)
		globalBook.Save()
		fmt.Println("[DEBUG] Book saved")
	}}()

	whiteWins := 0
	blackWins := 0
	draws := 0

	for gameNum := 1; gameNum <= {num_games}; gameNum++ {{
		result, moves := playOneGame(gameNum)

		switch result {{
		case ResultWhiteWins:
			whiteWins++
			fmt.Printf("[INFO] Game %d: White wins by checkmate (%d moves)\\n", gameNum, moves)
		case ResultBlackWins:
			blackWins++
			fmt.Printf("[INFO] Game %d: Black wins by checkmate (%d moves)\\n", gameNum, moves)
		case ResultDraw:
			draws++
			fmt.Printf("[INFO] Game %d: Draw (%d moves)\\n", gameNum, moves)
		default:
			fmt.Printf("[INFO] Game %d: Unknown result (%d moves)\\n", gameNum, moves)
		}}
	}}

	fmt.Println("[INFO] === Summary ===")
	fmt.Printf("[INFO] Games played: %d\\n", {num_games})
	fmt.Printf("[INFO] Results: White %d, Black %d, Draw %d\\n", whiteWins, blackWins, draws)

	// Report learning threshold stats
	recorded, skipped := GetLearningStats()
	if recorded+skipped > 0 {{
		skipPct := float64(skipped) / float64(recorded+skipped) * 100
		fmt.Printf("[INFO] Learning: %d recorded, %d skipped (%.1f%% redundant)\\n", recorded, skipped, skipPct)
	}}
}}

func playOneGame(gameNum int) (GameResult, int) {{
	board := NewBoard()
	var history []Move
	var fenHistory []string

	positionCounts := make(map[string]int)
	halfMoveClock := 0

	fmt.Printf("[DEBUG] Game %d starting...\\n", gameNum)

	for moveNum := 1; moveNum <= maxMoves; moveNum++ {{
		fen := board.ToFEN()
		fenHistory = append(fenHistory, fen)

		// Check for threefold repetition
		posKey := fenPositionKey(fen)
		positionCounts[posKey]++
		if positionCounts[posKey] >= 3 {{
			fmt.Printf("[DEBUG] Game %d: Threefold repetition\\n", gameNum)
			globalBook.LearnFromGame(history, fenHistory, ResultDraw)
			return ResultDraw, len(history)
		}}

		// Check for 50-move rule
		if halfMoveClock >= 100 {{
			fmt.Printf("[DEBUG] Game %d: 50-move rule\\n", gameNum)
			globalBook.LearnFromGame(history, fenHistory, ResultDraw)
			return ResultDraw, len(history)
		}}

		// Check for checkmate/stalemate
		legalMoves := board.GenerateLegalMoves()
		if len(legalMoves) == 0 {{
			if board.InCheck() {{
				var result GameResult
				if board.SideToMove == White {{
					result = ResultBlackWins
				}} else {{
					result = ResultWhiteWins
				}}
				globalBook.LearnFromGame(history, fenHistory, result)
				return result, len(history)
			}} else {{
				globalBook.LearnFromGame(history, fenHistory, ResultDraw)
				return ResultDraw, len(history)
			}}
		}}

		// Check for insufficient material
		if isInsufficientMaterial(board) {{
			fmt.Printf("[DEBUG] Game %d: Insufficient material\\n", gameNum)
			globalBook.LearnFromGame(history, fenHistory, ResultDraw)
			return ResultDraw, len(history)
		}}

		// AI search with side-specific depth
		ply := len(history) + 1
		start := time.Now()
		var searchResult SearchResult
		if board.SideToMove == White {{{{
			searchResult = SearchWithBook(board, ply, {depth_white}, {WORKERS})
		}}}} else {{{{
			searchResult = SearchWithBook(board, ply, {depth_black}, {WORKERS})
		}}}}
		elapsed := time.Since(start)

		if searchResult.BestMove.IsNull() {{
			fmt.Printf("[DEBUG] Game %d: No move found\\n", gameNum)
			break
		}}

		move := searchResult.BestMove

		// Track pawn moves and captures for 50-move rule
		piece := board.Squares[move.From]
		if piece == WPawn || piece == BPawn || move.Captured != Empty {{
			halfMoveClock = 0
		}} else {{
			halfMoveClock++
		}}

		board.MakeMove(&move)
		history = append(history, move)

		// Show move with score and repetition info
		scoreStr := fmt.Sprintf("%+.2f", float64(searchResult.Score)/100.0)
		repCount := board.RepetitionCount()
		histLen := len(board.History)
		fmt.Printf("[DEBUG] Move %d: %s score=%s rep=%d hist=%d (d%d %dms)\\n",
			moveNum, move.String(), scoreStr, repCount, histLen, searchResult.Depth, elapsed.Milliseconds())
	}}

	fmt.Printf("[DEBUG] Game %d: Max moves reached\\n", gameNum)
	globalBook.LearnFromGame(history, fenHistory, ResultDraw)
	return ResultDraw, len(history)
}}

func fenPositionKey(fen string) string {{
	parts := make([]byte, 0, len(fen))
	spaceCount := 0
	for i := 0; i < len(fen); i++ {{
		if fen[i] == ' ' {{
			spaceCount++
			if spaceCount >= 4 {{
				break
			}}
		}}
		parts = append(parts, fen[i])
	}}
	return string(parts)
}}

func isInsufficientMaterial(b *Board) bool {{
	whitePieces := 0
	blackPieces := 0
	whiteMinor := 0
	blackMinor := 0

	for sq := Square(0); sq < 64; sq++ {{
		p := b.Squares[sq]
		switch p {{
		case WPawn, WRook, WQueen:
			whitePieces++
		case BPawn, BRook, BQueen:
			blackPieces++
		case WKnight, WBishop:
			whitePieces++
			whiteMinor++
		case BKnight, BBishop:
			blackPieces++
			blackMinor++
		}}
	}}

	if whitePieces == 0 && blackPieces == 0 {{
		return true
	}}
	if whitePieces == 0 && blackPieces == 1 && blackMinor == 1 {{
		return true
	}}
	if blackPieces == 0 && whitePieces == 1 && whiteMinor == 1 {{
		return true
	}}

	return false
}}
'''

    with open(TEST_FILE, 'w') as f:
        f.write(test_code)

    log_debug(f"Generated test file: {TEST_FILE}")


def run_games():
    """Execute go test and stream output to terminal."""

    log_debug(f"Running: go test -v -run TestAutoGames -timeout 0")

    try:
        # Use line buffering and unbuffered stderr
        env = os.environ.copy()
        env["PYTHONUNBUFFERED"] = "1"

        process = subprocess.Popen(
            ["go", "test", "-v", "-run", "TestAutoGames", "-timeout", "0"],
            cwd=CHESS_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env
        )

        for line in process.stdout:
            line = line.rstrip()
            # Pass through lines that already have our log format
            if line.startswith("[INFO]") or line.startswith("[DEBUG]") or line.startswith("[ERROR]"):
                if line.startswith("[DEBUG]") and not DEBUG:
                    _write_log("DEBUG", line)  # Still log to file
                    continue
                print(line, flush=True)
                # Extract level and message for file logging
                level = line.split("]")[0][1:]
                msg = line.split("]", 1)[1].strip() if "]" in line else line
                _write_log(level, msg)
            # Skip Go test framework noise
            elif line.startswith("=== RUN") or line.startswith("--- PASS") or line.startswith("PASS"):
                pass
            elif line.startswith("ok") or line.startswith("FAIL"):
                pass
            elif line.strip():
                # Other output - show as debug
                log_debug(line)
                sys.stdout.flush()

        process.wait()
        if process.returncode != 0:
            log_error(f"Go test exited with code {process.returncode}")
        return process.returncode == 0

    except FileNotFoundError:
        log_error("Go not found. Please install Go.")
        return False
    except Exception as e:
        log_error(f"Failed to run tests: {e}")
        return False


def print_summary(before, after):
    """Show learning statistics delta."""

    new_positions = after["positions_with_learning"] - before["positions_with_learning"]
    new_games = after["total_our_games"] - before["total_our_games"]
    new_wins = after["total_our_wins"] - before["total_our_wins"]
    new_losses = after["total_our_losses"] - before["total_our_losses"]
    new_draws = after["total_our_draws"] - before["total_our_draws"]

    log_info("=== Learning Statistics ===")
    log_info(f"Positions in book: {after['total_positions']}")
    log_info(f"Positions with learning: {after['positions_with_learning']} (+{new_positions})")
    log_info(f"Total games recorded: {after['total_our_games']} (+{new_games})")
    log_info(f"Win/Loss/Draw records: {after['total_our_wins']}/{after['total_our_losses']}/{after['total_our_draws']}")

    # Show recent games from the book
    if BOOK_PATH.exists():
        try:
            with open(BOOK_PATH) as f:
                book = json.load(f)
            games = book.get('games', [])
            if games:
                log_info(f"=== Recent Games ({len(games)} total) ===")
                for g in games[-3:]:  # Last 3 games
                    moves = ' '.join(g['moves'][:8])
                    if len(g['moves']) > 8:
                        moves += '...'
                    log_info(f"  {g['result'].upper():5} ({g['move_count']} moves): {moves}")
        except:
            pass


def cleanup():
    """Remove generated test file."""
    if TEST_FILE.exists():
        TEST_FILE.unlink()
        log_debug(f"Cleaned up: {TEST_FILE}")


def reset_book():
    """Reset learning data by removing the book file."""
    if BOOK_PATH.exists():
        BOOK_PATH.unlink()
        log_info(f"Reset: removed {BOOK_PATH}")
        log_info("Learning data cleared. Fresh start on next run.")
    else:
        log_info("No book file to reset.")


def main():
    # Handle reset flag first
    if "--reset" in sys.argv:
        reset_book()
        # If only resetting (no game count), exit
        args = [a for a in sys.argv[1:] if not a.startswith("-")]
        if not args:
            return 0

    # Parse args
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    num_games = int(args[0]) if args else 5

    depth_str = f"depth={DEPTH_WHITE}" if DEPTH_WHITE == DEPTH_BLACK else f"W={DEPTH_WHITE}/B={DEPTH_BLACK}"

    # Build contempt string for log
    if CONTEMPT_WHITE is not None and CONTEMPT_BLACK is not None:
        cw_cp = int((0.5 - CONTEMPT_WHITE) * 100)
        cb_cp = int((0.5 - CONTEMPT_BLACK) * 100)
        contempt_str = f", contempt W={cw_cp}cp/B={cb_cp}cp"
    elif DRAW_VALUE != 0.5:
        contempt_str = f", draw_value={DRAW_VALUE}"
    else:
        contempt_str = ""

    log_info("Chess AI Training")
    log_info(f"Running {num_games} game(s), {depth_str}, {WORKERS} workers{contempt_str}")

    # Get before stats
    before = get_learning_stats()
    log_debug(f"Before: {before['positions_with_learning']} positions with learning")

    # Generate and run
    generate_go_test(num_games, depth_white=DEPTH_WHITE, depth_black=DEPTH_BLACK,
                     contempt_white=CONTEMPT_WHITE, contempt_black=CONTEMPT_BLACK)

    try:
        success = run_games()

        if not success:
            log_error("Game execution failed")
            return 1

        # Get after stats
        after = get_learning_stats()
        print()
        print_summary(before, after)

    finally:
        cleanup()

    return 0


if __name__ == "__main__":
    sys.exit(main())
