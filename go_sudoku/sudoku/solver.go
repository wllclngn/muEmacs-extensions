// Restored active solver (flattened layout)
package sudoku

import (
	"math/bits"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

// Solver is the main Sudoku solving engine with adaptive intelligence
type Solver struct {
	// Core solving state
	grid     Grid
	original Grid

	// Candidate tracking (bitsets for performance)
	candidates [9][9]uint16 // Bits 1-9 represent possible values

	// Constraint tracking
	rowMask [9]uint16 // Bitmask of used values in each row
	colMask [9]uint16 // Bitmask of used values in each column
	boxMask [9]uint16 // Bitmask of used values in each 3x3 box

	// Adaptive strategy selection
	strategy     Strategy
	difficulty   Difficulty
	autoStrategy bool

	// Performance analytics
	stats     Stats
	startTime time.Time

	// Concurrency control
	semaphore         chan struct{}
	workerPool        sync.WaitGroup
	solutions         chan Grid
	firstSolution     atomic.Bool
	concurrentTimeout time.Duration

	// Pattern detection
	patternStats  [5]uint64 // Strategy usage statistics
	avgComplexity float64   // Running average of puzzle complexity
	totalSolved   uint64    // Total puzzles processed
	// Feature toggles
	advancedStrategies bool
}

// New creates a production-ready solver
func New() *Solver {
	return &Solver{
		strategy:          StrategyAdaptive,
		autoStrategy:      true,
		semaphore:         make(chan struct{}, min(runtime.NumCPU()*2, 8)),
		solutions:         make(chan Grid, 1),
		concurrentTimeout: 30 * time.Second,
	}
}

// LoadPuzzle initializes the solver with a new puzzle
func (s *Solver) LoadPuzzle(puzzle Grid) {
	s.grid = puzzle
	s.original = puzzle
	s.initializeCandidates()
	s.updateConstraints()

	// Adaptive difficulty detection
	s.difficulty = s.detectDifficulty()

	// Auto-select strategy based on difficulty
	if s.autoStrategy {
		s.strategy = s.selectOptimalStrategy()
	}

	s.startTime = time.Now()
	s.stats = Stats{} // Reset stats for new puzzle

	// Reset concurrent solve state between puzzles to avoid stale solutions
	s.firstSolution.Store(false)
	s.solutions = make(chan Grid, 1)
}

// Solve is the main entry point with adaptive strategy selection
func (s *Solver) Solve() (bool, time.Duration) {
	defer func() {
		s.updateGlobalStatistics()
	}()

	switch s.strategy {
	case StrategyBasic:
		return s.solveBasic(0, 0), time.Since(s.startTime)
	case StrategyConstraint:
		return s.solveWithConstraints(), time.Since(s.startTime)
	case StrategyHeuristic:
		return s.solveWithHeuristics(), time.Since(s.startTime)
	case StrategyConcurrent:
		ok := s.solveConcurrent()
		if !ok {
			// Ensure grid remains in original valid state if concurrent race failed or timed out
			s.grid = s.original
		}
		return ok, time.Since(s.startTime)
	case StrategyAdaptive:
		return s.solveAdaptive(), time.Since(s.startTime)
	default:
		return s.solveBasic(0, 0), time.Since(s.startTime)
	}
}

// GetGrid returns the current grid state
func (s *Solver) GetGrid() Grid {
	return s.grid
}

// GetStats returns performance statistics
func (s *Solver) GetStats() Stats {
	return s.stats
}

// SetStrategy sets the solving strategy
func (s *Solver) SetStrategy(strategy Strategy) {
	s.strategy = strategy
	s.autoStrategy = false
}

// SetAutoStrategy enables automatic strategy selection
func (s *Solver) SetAutoStrategy(enabled bool) {
	s.autoStrategy = enabled
}

// EnableAdvancedStrategies toggles use of higher-order deduction (pairs, pointing, etc.).
func (s *Solver) EnableAdvancedStrategies(enabled bool) {
	s.advancedStrategies = enabled
}

// AdvancedStrategiesEnabled returns whether advanced tactics are active.
func (s *Solver) AdvancedStrategiesEnabled() bool { return s.advancedStrategies }

// GetCandidateMask returns the bitmask of candidates for a cell (bits 1..9) or 0 if fixed.
func (s *Solver) GetCandidateMask(row, col int) uint16 {
	if row < 0 || row >= 9 || col < 0 || col >= 9 {
		return 0
	}
	return s.candidates[row][col]
}

// RestrictCandidates intersects the current candidate mask at (row,col) with mask.
// Returns true if any reduction occurred. If the mask collapses to a single value, the move is made.
func (s *Solver) RestrictCandidates(row, col int, mask uint16) bool {
	if row < 0 || row >= 9 || col < 0 || col >= 9 {
		return false
	}
	if s.grid[row][col] != 0 {
		return false
	}
	cur := s.candidates[row][col]
	newMask := cur & mask
	if newMask == 0 { // don't allow elimination to empty; keep original
		return false
	}
	if newMask == cur {
		return false
	}
	s.candidates[row][col] = newMask
	// If single candidate now, place it.
	if bits.OnesCount16(newMask) == 1 {
		for v := 1; v <= 9; v++ {
			if newMask&(1<<v) != 0 {
				if s.isValidMove(row, col, v) { // safety
					s.makeMove(row, col, v)
				}
				break
			}
		}
	}
	return true
}

// GetDifficulty returns the detected difficulty level
func (s *Solver) GetDifficulty() Difficulty {
	return s.difficulty
}

// SetConcurrentTimeout sets the maximum wait time for concurrent solving.
func (s *Solver) SetConcurrentTimeout(d time.Duration) {
	if d > 0 {
		s.concurrentTimeout = d
	}
}

// StepConstraints applies a round of constraint propagation (naked/hidden singles)
// and returns true if any progress was made. This allows orchestrators (e.g.,
// Samurai/Matrix solvers) to advance grids incrementally without committing to
// a full search.
func (s *Solver) StepConstraints() bool {
	return s.propagateConstraints()
}

// elapsed returns elapsed time since the current puzzle load.
func (s *Solver) elapsed() time.Duration { return time.Since(s.startTime) }

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// CountSolutions runs a backtracking search to count up to maxCount solutions for a puzzle
func CountSolutions(s *Solver, maxCount int) int {
	var count int
	var solve func(row, col int) bool
	solve = func(row, col int) bool {
		if row == 9 {
			count++
			return count >= maxCount
		}
		nextRow, nextCol := row, col+1
		if nextCol == 9 {
			nextRow++
			nextCol = 0
		}
		if s.grid[row][col] != 0 {
			return solve(nextRow, nextCol)
		}
		for v := 1; v <= 9; v++ {
			if s.isValidMove(row, col, v) {
				s.makeMove(row, col, v)
				if solve(nextRow, nextCol) {
					s.undoMove(row, col)
					return true
				}
				s.undoMove(row, col)
			}
		}
		return false
	}
	solve(0, 0)
	return count
}


// undoMove removes the value at (row, col) in the grid.
func (s *Solver) undoMove(row, col int) {
	s.grid[row][col] = 0
}
