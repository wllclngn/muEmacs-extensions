// Active utilities (flattened layout)
package sudoku

import (
	"fmt"
	"strings"
)

// DifficultyString returns a string representation of difficulty
func (d Difficulty) String() string {
	names := []string{"Easy", "Medium", "Hard", "Extreme", "Unknown"}
	if int(d) < len(names) {
		return names[d]
	}
	return "Unknown"
}

// StrategyString returns a string representation of strategy
func (s Strategy) String() string {
	names := []string{"Basic", "Constraint", "Heuristic", "Concurrent", "Adaptive"}
	if int(s) < len(names) {
		return names[s]
	}
	return "Unknown"
}

// IsValid checks if a grid represents a valid Sudoku puzzle
func (g Grid) IsValid() bool {
	// Check rows
	for i := 0; i < 9; i++ {
		seen := make(map[int]bool)
		for j := 0; j < 9; j++ {
			if g[i][j] != 0 {
				if seen[g[i][j]] {
					return false
				}
				seen[g[i][j]] = true
			}
		}
	}
	
	// Check columns
	for j := 0; j < 9; j++ {
		seen := make(map[int]bool)
		for i := 0; i < 9; i++ {
			if g[i][j] != 0 {
				if seen[g[i][j]] {
					return false
				}
				seen[g[i][j]] = true
			}
		}
	}
	
	// Check 3x3 boxes
	for box := 0; box < 9; box++ {
		seen := make(map[int]bool)
		startRow := (box / 3) * 3
		startCol := (box % 3) * 3
		
		for i := 0; i < 3; i++ {
			for j := 0; j < 3; j++ {
				value := g[startRow+i][startCol+j]
				if value != 0 {
					if seen[value] {
						return false
					}
					seen[value] = true
				}
			}
		}
	}
	
	return true
}

// IsSolved checks if a grid is completely and correctly solved
func (g Grid) IsSolved() bool {
	if !g.IsValid() {
		return false
	}
	
	// Check that all cells are filled
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if g[i][j] == 0 {
				return false
			}
		}
	}
	
	return true
}

// CountFilledCells returns the number of filled cells
func (g Grid) CountFilledCells() int {
	count := 0
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if g[i][j] != 0 {
				count++
			}
		}
	}
	return count
}

// String returns a formatted string representation of the grid
func (g Grid) String() string {
	var sb strings.Builder
	
	sb.WriteString("+" + strings.Repeat("---+", 9) + "\n")
	
	for i := 0; i < 9; i++ {
		sb.WriteString("|")
		for j := 0; j < 9; j++ {
			if g[i][j] == 0 {
				sb.WriteString(" . ")
			} else {
				sb.WriteString(fmt.Sprintf(" %d ", g[i][j]))
			}
			if (j+1)%3 == 0 {
				sb.WriteString("|")
			} else {
				sb.WriteString(" ")
			}
		}
		sb.WriteString("\n")
		
		if (i+1)%3 == 0 {
			sb.WriteString("+" + strings.Repeat("---+", 9) + "\n")
		}
	}
	
	return sb.String()
}

// GetPerformanceReport returns a formatted performance report
func (s *Solver) GetPerformanceReport() string {
	report := fmt.Sprintf("SUDOKU SOLVER PERFORMANCE REPORT\n")
	report += fmt.Sprintf("================================\n")
	report += fmt.Sprintf("Total Puzzles Solved: %d\n", s.totalSolved)
	report += fmt.Sprintf("Average Complexity: %.2f\n", s.avgComplexity)
	report += fmt.Sprintf("Current Strategy: %s\n", s.strategy.String())
	report += fmt.Sprintf("Detected Difficulty: %s\n", s.difficulty.String())
	report += fmt.Sprintf("\nSolving Statistics:\n")
	report += fmt.Sprintf("  Backtrack Steps: %d\n", s.stats.BacktrackSteps)
	report += fmt.Sprintf("  Constraint Steps: %d\n", s.stats.ConstraintSteps)
	report += fmt.Sprintf("  Heuristic Steps: %d\n", s.stats.HeuristicSteps)
	report += fmt.Sprintf("  Candidate Updates: %d\n", s.stats.CandidateUpdates)
	report += fmt.Sprintf("\nConcurrency Metrics:\n")
	report += fmt.Sprintf("  Concurrent Tasks: %d\n", s.stats.ConcurrentTasks)
	report += fmt.Sprintf("  Deadlocks Avoided: %d\n", s.stats.DeadlocksAvoided)
	
	return report
}