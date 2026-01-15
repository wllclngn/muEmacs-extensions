// Active strategies registry (flattened layout)
package sudoku

// solveBasic implements simple backtracking
func (s *Solver) solveBasic(row, col int) bool {
	if col == 9 {
		row++
		col = 0
	}
	if row == 9 {
		return true
	}

	if s.grid[row][col] != 0 {
		return s.solveBasic(row, col+1)
	}

	for value := 1; value <= 9; value++ {
		if s.isValidMove(row, col, value) {
			s.grid[row][col] = value
			s.stats.BacktrackSteps++

			if s.solveBasic(row, col+1) {
				return true
			}

			s.grid[row][col] = 0
		}
	}

	return false
}

// solveWithConstraints uses constraint propagation with backtracking
func (s *Solver) solveWithConstraints() bool {
	for s.propagateConstraints() {
		if s.isComplete() {
			return true
		}
	}

	row, col := s.findMostConstrainedCell()
	if row == -1 {
		return false
	}

	candidates := s.candidates[row][col]
	for value := 1; value <= 9; value++ {
		if candidates&(1<<value) != 0 {
			if s.isValidMove(row, col, value) {
				s.makeMove(row, col, value)
				s.stats.BacktrackSteps++

				if s.solveWithConstraints() {
					return true
				}

				s.unmakeMove(row, col, value)
			}
		}
	}

	return false
}

// solveWithHeuristics uses MRV + Degree heuristics
func (s *Solver) solveWithHeuristics() bool {
	for s.propagateConstraints() {
		if s.isComplete() {
			return true
		}
	}

	row, col := s.findMRVCell()
	if row == -1 {
		return false
	}

	candidates := s.getOrderedCandidates(row, col)

	for _, value := range candidates {
		if s.isValidMove(row, col, value) {
			s.makeMove(row, col, value)
			s.stats.HeuristicSteps++

			if s.solveWithHeuristics() {
				return true
			}

			s.unmakeMove(row, col, value)
		}
	}

	return false
}

// (Concurrent and adaptive strategy implementations moved to concurrency.go & adaptive.go)
