// Active heuristics (flattened layout)
package sudoku

import "math/bits"

// findMRVCell finds the cell with minimum remaining values
func (s *Solver) findMRVCell() (int, int) {
	minCandidates := 10
	bestRow, bestCol := -1, -1
	
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				candidateCount := bits.OnesCount16(s.candidates[i][j])
				if candidateCount > 0 && candidateCount < minCandidates {
					minCandidates = candidateCount
					bestRow, bestCol = i, j
				}
			}
		}
	}
	
	return bestRow, bestCol
}

// findMostConstrainedCell finds the most constrained empty cell
func (s *Solver) findMostConstrainedCell() (int, int) {
	return s.findMRVCell()
}

// getOrderedCandidates returns candidates ordered by heuristic value
func (s *Solver) getOrderedCandidates(row, col int) []int {
	var candidates []int
	candidateMask := s.candidates[row][col]
	
	for value := 1; value <= 9; value++ {
		if candidateMask&(1<<value) != 0 {
			candidates = append(candidates, value)
		}
	}
	
	return candidates
}

// isComplete checks if the puzzle is fully solved
func (s *Solver) isComplete() bool {
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				return false
			}
		}
	}
	return true
}

// countEmptyCells returns the number of empty cells
func (s *Solver) countEmptyCells() int {
	count := 0
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				count++
			}
		}
	}
	return count
}

// calculateAverageCandidates computes average candidates per empty cell
func (s *Solver) calculateAverageCandidates() float64 {
	total := 0
	empty := 0
	
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				total += bits.OnesCount16(s.candidates[i][j])
				empty++
			}
		}
	}
	
	if empty == 0 {
		return 0
	}
	return float64(total) / float64(empty)
}

// Unit helper functions for constraint analysis
func (s *Solver) getRowCells(row int) [][2]int {
	cells := make([][2]int, 9)
	for j := 0; j < 9; j++ {
		cells[j] = [2]int{row, j}
	}
	return cells
}

func (s *Solver) getColCells(col int) [][2]int {
	cells := make([][2]int, 9)
	for i := 0; i < 9; i++ {
		cells[i] = [2]int{i, col}
	}
	return cells
}

func (s *Solver) getBoxCells(box int) [][2]int {
	cells := make([][2]int, 9)
	startRow := (box / 3) * 3
	startCol := (box % 3) * 3
	
	idx := 0
	for i := 0; i < 3; i++ {
		for j := 0; j < 3; j++ {
			cells[idx] = [2]int{startRow + i, startCol + j}
			idx++
		}
	}
	return cells
}