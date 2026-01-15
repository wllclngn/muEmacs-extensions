// Active types definitions (flattened layout)
package sudoku

import "time"

// Difficulty represents puzzle complexity classification
type Difficulty int

const (
	DifficultyEasy Difficulty = iota
	DifficultyMedium
	DifficultyHard
	DifficultyExtreme
	DifficultyUnknown
)

// Strategy determines the algorithm approach
type Strategy int

const (
	StrategyBasic Strategy = iota      // Simple backtracking (minimal overhead)
	StrategyConstraint                  // Constraint propagation with backtracking
	StrategyHeuristic                   // Full heuristics + constraint propagation
	StrategyConcurrent                  // Parallel solving with multiple approaches
	StrategyAdaptive                    // Intelligent selection based on puzzle analysis
)

// Grid represents a 9x9 Sudoku puzzle
type Grid [9][9]int

// Stats tracks comprehensive performance metrics
type Stats struct {
	// Core metrics
	BacktrackSteps    uint64
	ConstraintSteps   uint64
	HeuristicSteps    uint64
	CandidateUpdates  uint64
	
	// Strategy effectiveness
	StrategySuccess   [5]uint64
	StrategyTime      [5]time.Duration
	
	// Concurrency metrics
	ConcurrentTasks   uint64
	DeadlocksAvoided  uint64
	ParallelSpeedup   float64
	
	// Pattern detection
	DifficultyMisses  uint64
	AdaptationCount   uint64
}