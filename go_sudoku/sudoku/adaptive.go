// Active adaptive strategy logic (flattened layout)
package sudoku

// solveAdaptive progressively escalates strategy sophistication based on
// remaining empty cells and average candidate branching factor.
func (s *Solver) solveAdaptive() bool {
    if s.propagateConstraints() && s.isComplete() {
        return true
    }
    remaining := s.countEmptyCells()
    avg := s.calculateAverageCandidates()
    switch {
    case remaining <= 20 && avg <= 3.0:
        return s.solveBasic(0, 0)
    case remaining <= 40 && avg <= 5.0:
        return s.solveWithHeuristics()
    default:
        return s.solveConcurrent()
    }
}
