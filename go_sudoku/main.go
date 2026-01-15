// go_sudoku - Sudoku game extension for μEmacs
//
// Provides a playable Sudoku game with intelligent solving using
// constraint propagation and backtracking.
//
// Commands:
//   sudoku-new     - Start a new puzzle
//   sudoku-check   - Check for errors
//   sudoku-hint    - Reveal one cell
//   sudoku-solve   - Show the solution
//   sudoku-reset   - Reset to original puzzle

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Bridge function declarations (implemented in bridge.c)
extern void api_message(const char *msg);
extern void *api_current_buffer(void);
extern void *api_find_buffer(const char *name);
extern void *api_buffer_create(const char *name);
extern int api_buffer_switch(void *bp);
extern int api_buffer_clear(void *bp);
extern int api_buffer_insert(const char *text, size_t len);
extern void api_get_point(int *line, int *col);
extern void api_set_point(int line, int col);
extern void api_log_info(const char *msg);
extern void api_log_error(const char *msg);
extern void api_update_display(void);
extern int api_prompt_yn(const char *prompt);
*/
import "C"

import (
	"fmt"
	"strings"
	"unsafe"

	"go_sudoku/sudoku"
)

// Game state
type GameState struct {
	puzzle   sudoku.Grid // Current player state
	original sudoku.Grid // Original puzzle (fixed clues)
	solution sudoku.Grid // Solved version
	active   bool        // Is game active
}

var game GameState
var bufferName = "*sudoku*"

// Predefined puzzles
var easyPuzzle = sudoku.Grid{
	{5, 3, 0, 0, 7, 0, 0, 0, 0},
	{6, 0, 0, 1, 9, 5, 0, 0, 0},
	{0, 9, 8, 0, 0, 0, 0, 6, 0},
	{8, 0, 0, 0, 6, 0, 0, 0, 3},
	{4, 0, 0, 8, 0, 3, 0, 0, 1},
	{7, 0, 0, 0, 2, 0, 0, 0, 6},
	{0, 6, 0, 0, 0, 0, 2, 8, 0},
	{0, 0, 0, 4, 1, 9, 0, 0, 5},
	{0, 0, 0, 0, 8, 0, 0, 7, 9},
}

var mediumPuzzle = sudoku.Grid{
	{0, 2, 0, 6, 0, 8, 0, 0, 0},
	{5, 8, 0, 0, 0, 9, 7, 0, 0},
	{0, 0, 0, 0, 4, 0, 0, 0, 0},
	{3, 7, 0, 0, 0, 0, 5, 0, 0},
	{6, 0, 0, 0, 0, 0, 0, 0, 4},
	{0, 0, 8, 0, 0, 0, 0, 1, 3},
	{0, 0, 0, 0, 2, 0, 0, 0, 0},
	{0, 0, 9, 8, 0, 0, 0, 3, 6},
	{0, 0, 0, 3, 0, 6, 0, 9, 0},
}

var hardPuzzle = sudoku.Grid{
	{0, 0, 0, 6, 0, 0, 4, 0, 0},
	{7, 0, 0, 0, 0, 3, 6, 0, 0},
	{0, 0, 0, 0, 9, 1, 0, 8, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 5, 0, 1, 8, 0, 0, 0, 3},
	{0, 0, 0, 3, 0, 6, 0, 4, 5},
	{0, 4, 0, 2, 0, 0, 0, 6, 0},
	{9, 0, 3, 0, 0, 0, 0, 0, 0},
	{0, 2, 0, 0, 0, 0, 1, 0, 0},
}

// Render the grid to the buffer
func renderGrid() string {
	var sb strings.Builder

	// Title
	sb.WriteString("                    SUDOKU\n\n")

	// Grid with box-drawing characters
	topBorder := "      +-------+-------+-------+\n"
	midBorder := "      +-------+-------+-------+\n"

	sb.WriteString(topBorder)

	for row := 0; row < 9; row++ {
		sb.WriteString(fmt.Sprintf("    %d ", row+1))
		for col := 0; col < 9; col++ {
			if col%3 == 0 {
				sb.WriteString("| ")
			}

			val := game.puzzle[row][col]
			if val == 0 {
				sb.WriteString(". ")
			} else if game.original[row][col] != 0 {
				// Fixed clue (shown as-is)
				sb.WriteString(fmt.Sprintf("%d ", val))
			} else {
				// Player entry (shown as-is for now)
				sb.WriteString(fmt.Sprintf("%d ", val))
			}
		}
		sb.WriteString("|\n")

		if (row+1)%3 == 0 {
			sb.WriteString(midBorder)
		}
	}

	// Column numbers
	sb.WriteString("        1 2 3   4 5 6   7 8 9\n\n")

	// Instructions
	sb.WriteString("  Commands:\n")
	sb.WriteString("    M-x sudoku-new    - Start new game (prefix 2=medium, 3=hard)\n")
	sb.WriteString("    M-x sudoku-check  - Check for errors\n")
	sb.WriteString("    M-x sudoku-hint   - Reveal one cell\n")
	sb.WriteString("    M-x sudoku-solve  - Show solution\n")
	sb.WriteString("    M-x sudoku-reset  - Reset to original\n")

	return sb.String()
}

// Show or update the sudoku buffer
func updateBuffer() {
	// Find or create buffer
	cname := C.CString(bufferName)
	defer C.free(unsafe.Pointer(cname))

	bp := C.api_find_buffer(cname)
	if bp == nil {
		bp = C.api_buffer_create(cname)
	}
	if bp == nil {
		return
	}

	C.api_buffer_switch(bp)
	C.api_buffer_clear(bp)

	// Render and insert
	content := renderGrid()
	ccontent := C.CString(content)
	defer C.free(unsafe.Pointer(ccontent))
	C.api_buffer_insert(ccontent, C.size_t(len(content)))

	C.api_set_point(3, 9) // Position cursor at first cell
	C.api_update_display()
}

// GoSudokuNew starts a new game
//
//export GoSudokuNew
func GoSudokuNew(f, n C.int) C.int {
	// Default to easy, use prefix arg for difficulty
	difficulty := "easy"
	if n == 2 {
		difficulty = "medium"
	} else if n >= 3 {
		difficulty = "hard"
	}

	switch difficulty {
	case "medium":
		game.puzzle = mediumPuzzle
		game.original = mediumPuzzle
	case "hard":
		game.puzzle = hardPuzzle
		game.original = hardPuzzle
	default:
		game.puzzle = easyPuzzle
		game.original = easyPuzzle
	}

	// Solve to get solution
	solver := sudoku.New()
	solver.LoadPuzzle(game.original)
	solver.Solve()
	game.solution = solver.GetGrid()

	game.active = true

	updateBuffer()

	msg := C.CString(fmt.Sprintf("Sudoku (%s) - Good luck!", difficulty))
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)

	return 1
}

// GoSudokuCheck checks for errors
//
//export GoSudokuCheck
func GoSudokuCheck(f, n C.int) C.int {
	if !game.active {
		msg := C.CString("No active game. Use M-x sudoku-new")
		defer C.free(unsafe.Pointer(msg))
		C.api_message(msg)
		return 0
	}

	errors := 0
	complete := true

	for r := 0; r < 9; r++ {
		for c := 0; c < 9; c++ {
			if game.puzzle[r][c] == 0 {
				complete = false
			} else if game.puzzle[r][c] != game.solution[r][c] {
				errors++
			}
		}
	}

	var msgStr string
	if errors == 0 && complete {
		msgStr = "Congratulations! Puzzle solved correctly!"
	} else if errors == 0 {
		msgStr = "No errors so far. Keep going!"
	} else {
		msgStr = fmt.Sprintf("Found %d error(s)", errors)
	}

	msg := C.CString(msgStr)
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)

	return 1
}

// GoSudokuHint gives a hint
//
//export GoSudokuHint
func GoSudokuHint(f, n C.int) C.int {
	if !game.active {
		msg := C.CString("No active game")
		defer C.free(unsafe.Pointer(msg))
		C.api_message(msg)
		return 0
	}

	// Find first empty or incorrect cell
	for r := 0; r < 9; r++ {
		for c := 0; c < 9; c++ {
			if game.original[r][c] == 0 && game.puzzle[r][c] != game.solution[r][c] {
				game.puzzle[r][c] = game.solution[r][c]
				updateBuffer()

				msgStr := fmt.Sprintf("Hint: Row %d, Col %d = %d", r+1, c+1, game.solution[r][c])
				msg := C.CString(msgStr)
				defer C.free(unsafe.Pointer(msg))
				C.api_message(msg)
				return 1
			}
		}
	}

	msg := C.CString("No hints needed - puzzle complete!")
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)
	return 1
}

// GoSudokuSolve shows the solution
//
//export GoSudokuSolve
func GoSudokuSolve(f, n C.int) C.int {
	if !game.active {
		msg := C.CString("No active game")
		defer C.free(unsafe.Pointer(msg))
		C.api_message(msg)
		return 0
	}

	game.puzzle = game.solution
	updateBuffer()

	msg := C.CString("Solution revealed")
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)
	return 1
}

// GoSudokuReset resets to original
//
//export GoSudokuReset
func GoSudokuReset(f, n C.int) C.int {
	if !game.active {
		msg := C.CString("No active game")
		defer C.free(unsafe.Pointer(msg))
		C.api_message(msg)
		return 0
	}

	game.puzzle = game.original
	updateBuffer()

	msg := C.CString("Puzzle reset to original")
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)
	return 1
}

// GoSudokuInit initializes the extension
//
//export GoSudokuInit
func GoSudokuInit() {
	msg := C.CString("go_sudoku: Sudoku game extension loaded")
	C.api_log_info(msg)
	C.free(unsafe.Pointer(msg))
}

// GoSudokuCleanup cleans up the extension
//
//export GoSudokuCleanup
func GoSudokuCleanup() {
	game.active = false
}

func main() {}
