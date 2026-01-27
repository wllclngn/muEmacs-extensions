// go_chess - Work-Stealing Chess Engine for μEmacs
//
// A chess extension that fully utilizes work-stealing DFS infrastructure
// for parallel alpha-beta search. Human vs AI with configurable depth.
//
// Commands:
//   chess         - Start new game (Human=White, AI=Black)
//   chess-move    - Make a move (e.g., "e2e4", "e7e8q" for promotion)
//   chess-undo    - Undo last move pair
//   chess-depth   - Set search depth (default: 6)
//   chess-eval    - Show current position evaluation
//   chess-hint    - Get AI suggestion for human
//   chess-flip    - Flip board orientation
//   chess-fen     - Show current FEN position
//   chess-auto    - AI vs AI mode (runs until game ends)
//   chess-workers - Set worker count (default: 2 for auto, NumCPU for hint)
//   chess-stop    - Stop AI vs AI game
//
// Built with CGO as a shared library for μEmacs extension system.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Bridge function declarations (implemented in bridge.c)
extern void api_message(const char *msg);
extern void *api_current_buffer(void);
extern char *api_buffer_contents(void *bp, size_t *len);
extern const char *api_buffer_filename(void *bp);
extern void api_get_point(int *line, int *col);
extern void api_set_point(int line, int col);
extern int api_buffer_insert(const char *text, size_t len);
extern void *api_buffer_create(const char *name);
extern int api_buffer_switch(void *bp);
extern int api_buffer_clear(void *bp);
extern void api_buffer_set_unmodified(void *bp);
extern void api_buffer_set_scratch(void *bp);
extern int api_prompt(const char *prompt, char *buf, size_t buflen);
extern void api_free(void *ptr);
extern void api_log_info(const char *msg);
extern void api_log_error(const char *msg);
extern void api_update_display(void);
extern int api_config_int(const char *key, int default_val);
extern _Bool api_config_bool(const char *key, _Bool default_val);
*/
import "C"

import (
	"fmt"
	"os"
	"runtime"
	"strings"
	"time"
	"unsafe"
)

// Game holds the current game state
type Game struct {
	Board        *Board
	History      []Move
	FENHistory   []string // Track FENs for opening book learning
	LastMove     Move
	Flipped      bool
	SearchDepth  int
	TimeLimit    time.Duration
	Workers      int  // Max workers for search (0 = NumCPU)
	AutoDelayMs  int  // Delay between moves in auto mode
	AutoStop     bool // Flag to stop AI vs AI
}

// Global game state
var currentGame *Game

// configInt reads an integer config value from TOML
func configInt(key string, defaultVal int) int {
	ckey := C.CString(key)
	defer C.free(unsafe.Pointer(ckey))
	return int(C.api_config_int(ckey, C.int(defaultVal)))
}

// NewGame creates a new game with config-driven defaults
func NewGame() *Game {
	board := NewBoard()
	return &Game{
		Board:       board,
		History:     make([]Move, 0, 100),
		FENHistory:  []string{board.ToFEN()}, // Track initial position
		Flipped:     false,
		SearchDepth: configInt("search_depth", 6),
		TimeLimit:   0,
		Workers:     configInt("workers", 2),      // Default 2 to save CPU
		AutoDelayMs: configInt("auto_delay_ms", 500),
		AutoStop:    false,
	}
}

// makeAIMove has the AI search and play (with opening book integration)
func (g *Game) makeAIMove() (Move, SearchResult) {
	workers := g.Workers
	if workers <= 0 {
		workers = runtime.NumCPU()
	}

	// Use hybrid search with book bonus and graduated depth
	ply := len(g.History) + 1
	result := SearchWithBook(g.Board, ply, g.SearchDepth, workers)

	if !result.BestMove.IsNull() {
		// Track FEN before move for learning
		g.FENHistory = append(g.FENHistory, g.Board.ToFEN())

		g.Board.MakeMove(&result.BestMove)
		g.History = append(g.History, result.BestMove)
		g.LastMove = result.BestMove
	}

	return result.BestMove, result
}

// displayGame shows the game in a buffer
func displayGame() {
	if currentGame == nil {
		return
	}

	// Clear modified flag on current buffer BEFORE switching (avoids "Discard changes?" prompt)
	prevBuf := C.api_current_buffer()
	if prevBuf != nil {
		C.api_buffer_set_unmodified(prevBuf)
	}

	// Create/switch to chess buffer
	bufName := C.CString("*chess*")
	resultBuf := C.api_buffer_create(bufName)
	C.free(unsafe.Pointer(bufName))

	if resultBuf == nil {
		return
	}
	C.api_buffer_switch(resultBuf)
	C.api_buffer_set_scratch(resultBuf)     // Mark as scratch - no save prompt on exit
	C.api_buffer_set_unmodified(resultBuf)  // Also clear chess buffer
	C.api_buffer_clear(resultBuf)

	// Render game state
	output := RenderGameState(currentGame, true)
	coutput := C.CString(output)
	C.api_buffer_insert(coutput, C.size_t(len(output)))
	C.free(unsafe.Pointer(coutput))

	C.api_set_point(1, 1)
	C.api_buffer_set_unmodified(resultBuf)  // Clear after insert too
	C.api_update_display()
}

//export chess_init
func chess_init(api unsafe.Pointer) {
	// Initialize opening book from Lichess master data
	InitOpeningBook()
}

//export go_chess_new
func go_chess_new(f, n C.int) C.int {
	currentGame = NewGame()
	displayGame()

	msg := C.CString("New game started. You are White. Use chess-move to play.")
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_move
func go_chess_move(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	// Check if game is over
	if currentGame.Board.IsCheckmate() || currentGame.Board.IsDraw() {
		msg := C.CString("Game is over. Use chess to start a new game.")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Check if it's human's turn (human plays White)
	if currentGame.Board.SideToMove == Black {
		msg := C.CString("It's the AI's turn. Please wait...")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Prompt for move
	var moveBuf [16]C.char
	prompt := C.CString("Your move (e.g., e2e4): ")
	if C.api_prompt(prompt, &moveBuf[0], 16) < 0 {
		C.free(unsafe.Pointer(prompt))
		return 0
	}
	C.free(unsafe.Pointer(prompt))

	moveStr := C.GoString(&moveBuf[0])
	moveStr = strings.TrimSpace(moveStr)

	if moveStr == "" {
		return 0
	}

	// Parse and validate move
	move, ok := currentGame.Board.ParseMove(moveStr)
	if !ok {
		msg := C.CString(fmt.Sprintf("Invalid move: %s", moveStr))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Make human move
	currentGame.Board.MakeMove(&move)
	currentGame.History = append(currentGame.History, move)
	currentGame.LastMove = move

	// Display after human move
	displayGame()

	// Check if game ended
	if currentGame.Board.IsCheckmate() {
		msg := C.CString("Checkmate! You win!")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 1
	}
	if currentGame.Board.IsDraw() {
		msg := C.CString("Draw!")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 1
	}

	// AI responds
	thinkingMsg := C.CString("Thinking...")
	C.api_message(thinkingMsg)
	C.free(unsafe.Pointer(thinkingMsg))
	C.api_update_display()

	aiMove, result := currentGame.makeAIMove()

	// Display after AI move
	displayGame()

	// Show AI move info
	info := RenderSearchInfo(result)
	msg := C.CString(fmt.Sprintf("AI plays: %s | %s", aiMove.String(), info))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	// Check if game ended
	if currentGame.Board.IsCheckmate() {
		endMsg := C.CString("Checkmate! AI wins.")
		C.api_message(endMsg)
		C.free(unsafe.Pointer(endMsg))
	} else if currentGame.Board.IsDraw() {
		endMsg := C.CString("Draw!")
		C.api_message(endMsg)
		C.free(unsafe.Pointer(endMsg))
	} else if currentGame.Board.InCheck() {
		checkMsg := C.CString("Check!")
		C.api_message(checkMsg)
		C.free(unsafe.Pointer(checkMsg))
	}

	return 1
}

//export go_chess_undo
func go_chess_undo(f, n C.int) C.int {
	if currentGame == nil || len(currentGame.History) < 2 {
		msg := C.CString("Nothing to undo")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Undo AI move
	aiMove := currentGame.History[len(currentGame.History)-1]
	currentGame.Board.UnmakeMove(&aiMove)
	currentGame.History = currentGame.History[:len(currentGame.History)-1]

	// Undo human move
	humanMove := currentGame.History[len(currentGame.History)-1]
	currentGame.Board.UnmakeMove(&humanMove)
	currentGame.History = currentGame.History[:len(currentGame.History)-1]

	// Update last move
	if len(currentGame.History) > 0 {
		currentGame.LastMove = currentGame.History[len(currentGame.History)-1]
	} else {
		currentGame.LastMove = Move{}
	}

	displayGame()

	msg := C.CString("Undid last move pair")
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_depth
func go_chess_depth(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	// If numeric prefix argument given (f != 0), use it directly
	if int(f) != 0 && int(n) >= 1 && int(n) <= 12 {
		currentGame.SearchDepth = int(n)
		msg := C.CString(fmt.Sprintf("Search depth set to %d", currentGame.SearchDepth))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 1
	}

	// Otherwise prompt
	var depthBuf [8]C.char
	prompt := C.CString(fmt.Sprintf("Search depth (1-12, current=%d): ", currentGame.SearchDepth))
	if C.api_prompt(prompt, &depthBuf[0], 8) < 0 {
		C.free(unsafe.Pointer(prompt))
		return 0
	}
	C.free(unsafe.Pointer(prompt))

	depthStr := C.GoString(&depthBuf[0])
	var depth int
	if _, err := fmt.Sscanf(depthStr, "%d", &depth); err == nil && depth >= 1 && depth <= 12 {
		currentGame.SearchDepth = depth
		msg := C.CString(fmt.Sprintf("Search depth set to %d", depth))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
	} else {
		msg := C.CString("Invalid depth (must be 1-12)")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	return 1
}

//export go_chess_eval
func go_chess_eval(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	eval := Evaluate(currentGame.Board)
	evalStr := fmt.Sprintf("%+.2f", float64(eval)/100.0)

	var advantage string
	if eval > 100 {
		advantage = "White advantage"
	} else if eval < -100 {
		advantage = "Black advantage"
	} else {
		advantage = "Equal"
	}

	msg := C.CString(fmt.Sprintf("Evaluation: %s (%s)", evalStr, advantage))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_hint
func go_chess_hint(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	if currentGame.Board.IsCheckmate() || currentGame.Board.IsDraw() {
		msg := C.CString("Game is over")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	thinkingMsg := C.CString("Analyzing...")
	C.api_message(thinkingMsg)
	C.free(unsafe.Pointer(thinkingMsg))
	C.api_update_display()

	opts := DefaultSearchOptions(runtime.NumCPU())
	opts.MaxDepth = currentGame.SearchDepth
	result := Search(currentGame.Board, opts)

	info := RenderSearchInfo(result)
	msg := C.CString(fmt.Sprintf("Suggestion: %s | %s", result.BestMove.String(), info))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_flip
func go_chess_flip(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	currentGame.Flipped = !currentGame.Flipped
	displayGame()

	var orientation string
	if currentGame.Flipped {
		orientation = "Black"
	} else {
		orientation = "White"
	}
	msg := C.CString(fmt.Sprintf("Board flipped (%s at bottom)", orientation))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_fen
func go_chess_fen(f, n C.int) C.int {
	if currentGame == nil {
		currentGame = NewGame()
	}

	// Generate FEN string
	var sb strings.Builder

	// Piece placement
	for rank := 7; rank >= 0; rank-- {
		empty := 0
		for file := 0; file < 8; file++ {
			sq := FromRankFile(rank, file)
			p := currentGame.Board.Squares[sq]
			if p == Empty {
				empty++
			} else {
				if empty > 0 {
					sb.WriteString(fmt.Sprintf("%d", empty))
					empty = 0
				}
				chars := map[Piece]byte{
					WKing: 'K', WQueen: 'Q', WRook: 'R', WBishop: 'B', WKnight: 'N', WPawn: 'P',
					BKing: 'k', BQueen: 'q', BRook: 'r', BBishop: 'b', BKnight: 'n', BPawn: 'p',
				}
				sb.WriteByte(chars[p])
			}
		}
		if empty > 0 {
			sb.WriteString(fmt.Sprintf("%d", empty))
		}
		if rank > 0 {
			sb.WriteString("/")
		}
	}

	// Side to move
	if currentGame.Board.SideToMove == White {
		sb.WriteString(" w ")
	} else {
		sb.WriteString(" b ")
	}

	// Castling
	castling := ""
	if currentGame.Board.Castling&CastleWK != 0 {
		castling += "K"
	}
	if currentGame.Board.Castling&CastleWQ != 0 {
		castling += "Q"
	}
	if currentGame.Board.Castling&CastleBK != 0 {
		castling += "k"
	}
	if currentGame.Board.Castling&CastleBQ != 0 {
		castling += "q"
	}
	if castling == "" {
		castling = "-"
	}
	sb.WriteString(castling)

	// En passant
	if currentGame.Board.EnPassant == NoSquare {
		sb.WriteString(" -")
	} else {
		files := "abcdefgh"
		ranks := "12345678"
		ep := currentGame.Board.EnPassant
		sb.WriteString(fmt.Sprintf(" %c%c", files[ep.File()], ranks[ep.Rank()]))
	}

	// Half-move clock and full move number
	sb.WriteString(fmt.Sprintf(" %d %d", currentGame.Board.HalfMoves, currentGame.Board.FullMoves))

	fen := sb.String()
	msg := C.CString(fmt.Sprintf("FEN: %s", fen))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

// autoGameLoop runs the AI vs AI game in the background
func autoGameLoop(g *Game, delayMs int) {
	moveNum := 0

	for !g.AutoStop {
		// Check if game is over
		if g.Board.IsCheckmate() {
			var winner string
			var result GameResult
			if g.Board.SideToMove == White {
				winner = "Black"
				result = ResultBlackWins
			} else {
				winner = "White"
				result = ResultWhiteWins
			}

			// Learn from this game
			if globalBook != nil && len(g.History) > 0 {
				globalBook.LearnFromGame(g.History, g.FENHistory, result)
			}

			endMsg := C.CString(fmt.Sprintf("Checkmate! %s wins after %d moves.", winner, moveNum))
			C.api_message(endMsg)
			C.free(unsafe.Pointer(endMsg))
			displayGame()
			return
		}
		if g.Board.IsDraw() {
			// Learn from this game (draw)
			if globalBook != nil && len(g.History) > 0 {
				globalBook.LearnFromGame(g.History, g.FENHistory, ResultDraw)
			}

			endMsg := C.CString(fmt.Sprintf("Draw after %d moves.", moveNum))
			C.api_message(endMsg)
			C.free(unsafe.Pointer(endMsg))
			displayGame()
			return
		}

		// Show thinking message
		side := "White"
		if g.Board.SideToMove == Black {
			side = "Black"
		}
		thinkMsg := C.CString(fmt.Sprintf("%s thinking... (move %d)", side, moveNum+1))
		C.api_message(thinkMsg)
		C.free(unsafe.Pointer(thinkMsg))
		C.api_update_display()

		// Make AI move
		aiMove, result := g.makeAIMove()
		moveNum++

		// Display the board
		displayGame()

		// Show move info
		info := RenderSearchInfo(result)
		moveMsg := C.CString(fmt.Sprintf("%s: %s | %s", side, aiMove.String(), info))
		C.api_message(moveMsg)
		C.free(unsafe.Pointer(moveMsg))
		C.api_update_display()

		// Delay between moves
		time.Sleep(time.Duration(delayMs) * time.Millisecond)
	}

	// Stopped by user
	stopMsg := C.CString(fmt.Sprintf("AI vs AI stopped after %d moves.", moveNum))
	C.api_message(stopMsg)
	C.free(unsafe.Pointer(stopMsg))
}

//export go_chess_auto
func go_chess_auto(f, n C.int) C.int {
	// Start a new game if needed
	if currentGame == nil {
		currentGame = NewGame()
	}

	// Reset stop flag
	currentGame.AutoStop = false

	// Get delay from config (re-read in case it changed)
	delayMs := currentGame.AutoDelayMs
	if delayMs < 50 {
		delayMs = 50 // Minimum 50ms to allow display updates
	}

	workers := currentGame.Workers
	if workers <= 0 {
		workers = 2 // Default to 2 for auto mode to save CPU
	}

	startMsg := C.CString(fmt.Sprintf("AI vs AI: depth=%d, workers=%d, delay=%dms (use chess-stop to abort)",
		currentGame.SearchDepth, workers, delayMs))
	C.api_message(startMsg)
	C.free(unsafe.Pointer(startMsg))

	displayGame()

	// Run game loop in background goroutine - returns immediately
	go autoGameLoop(currentGame, delayMs)

	return 1
}

//export go_chess_stop
func go_chess_stop(f, n C.int) C.int {
	if currentGame == nil {
		msg := C.CString("No game in progress")
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	currentGame.AutoStop = true
	msg := C.CString("Stopping AI vs AI...")
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_chess_cleanup
func go_chess_cleanup() {
	// Signal any running goroutine to stop
	if currentGame != nil {
		currentGame.AutoStop = true
	}
	// Force exit Go runtime - goroutine may be blocked in search for seconds
	// Without this, the ext_runner process orphans until the search completes
	os.Exit(0)
}

func main() {}
