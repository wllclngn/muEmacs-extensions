// Active difficulty evaluation logic (flattened layout)
package sudoku

import "math/bits"

// detectDifficulty performs a lightweight classification based on filled cell
// count and average candidate density across remaining cells.
func (s *Solver) detectDifficulty() Difficulty {
    filled := 0
    totalCandidates := 0
    empties := 0
    for i := 0; i < 9; i++ {
        for j := 0; j < 9; j++ {
            if s.grid[i][j] != 0 {
                filled++
            } else {
                empties++
                totalCandidates += bits.OnesCount16(s.candidates[i][j])
            }
        }
    }
    if empties == 0 { // Already solved or full.
        return DifficultyEasy
    }
    avg := float64(totalCandidates) / float64(empties)
    switch {
    case filled >= 50 && avg <= 3.0:
        return DifficultyEasy
    case filled >= 35 && avg <= 5.0:
        return DifficultyMedium
    case filled >= 25 && avg <= 7.0:
        return DifficultyHard
    case filled >= 17:
        return DifficultyExtreme
    default:
        return DifficultyUnknown
    }
}

// selectOptimalStrategy chooses a base strategy for the detected difficulty.
func (s *Solver) selectOptimalStrategy() Strategy {
    switch s.difficulty {
    case DifficultyEasy:
        return StrategyBasic
    case DifficultyMedium:
        return StrategyConstraint
    case DifficultyHard:
        return StrategyHeuristic
    case DifficultyExtreme:
        return StrategyConcurrent
    default:
        return StrategyAdaptive
    }
}

// updateGlobalStatistics maintains rolling complexity statistics & timing per strategy.
func (s *Solver) updateGlobalStatistics() {
    s.totalSolved++
    score := s.calculateComplexityScore()
    s.avgComplexity = s.avgComplexity*0.95 + score*0.05
    idx := int(s.strategy)
    if idx >= 0 && idx < len(s.stats.StrategySuccess) {
        s.stats.StrategySuccess[idx]++
        s.stats.StrategyTime[idx] += s.elapsed()
    }
}

// calculateComplexityScore estimates effort using weighted step counts.
func (s *Solver) calculateComplexityScore() float64 {
    back := float64(s.stats.BacktrackSteps) * 1.0
    cons := float64(s.stats.ConstraintSteps) * 0.1
    heur := float64(s.stats.HeuristicSteps) * 0.5
    return back + cons + heur
}
