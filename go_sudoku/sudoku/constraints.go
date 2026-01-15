// Active constraints (flattened layout)
package sudoku

import "math/bits"

// isValidMove checks if a value can be placed at a position without
// violating row, column, or box constraints using cached masks.
func (s *Solver) isValidMove(row, col, value int) bool {
    if row < 0 || row >= 9 || col < 0 || col >= 9 || value < 1 || value > 9 {
        return false
    }
    if s.grid[row][col] != 0 {
        return false
    }
    // Check row and column directly against current grid
    for i := 0; i < 9; i++ {
        if s.grid[row][i] == value || s.grid[i][col] == value {
            return false
        }
    }
    // Check 3x3 box
    startRow := (row / 3) * 3
    startCol := (col / 3) * 3
    for r := 0; r < 3; r++ {
        for c := 0; c < 3; c++ {
            if s.grid[startRow+r][startCol+c] == value {
                return false
            }
        }
    }
    return true
}

// makeMove places a value at a position and updates constraint masks
// and candidate masks for affected peers.
func (s *Solver) makeMove(row, col, value int) {
    s.grid[row][col] = value
    s.candidates[row][col] = 0

    box := (row/3)*3 + col/3
    valueMask := uint16(1 << value)

    s.rowMask[row] |= valueMask
    s.colMask[col] |= valueMask
    s.boxMask[box] |= valueMask

    // Update affected candidates in row/col/box
    s.updateRowCandidates(row, valueMask)
    s.updateColCandidates(col, valueMask)
    s.updateBoxCandidates(box, valueMask)
}

// unmakeMove removes a value from a position and recalculates constraints
// and candidates. This favors correctness over micro-optimizations.
func (s *Solver) unmakeMove(row, col, value int) {
    s.grid[row][col] = 0

    box := (row/3)*3 + col/3
    valueMask := uint16(1 << value)

    s.rowMask[row] &^= valueMask
    s.colMask[col] &^= valueMask
    s.boxMask[box] &^= valueMask

    // Recompute candidates for all empties under current masks
    s.updateAllCandidates()
}

// initializeCandidates sets up candidate tracking
func (s *Solver) initializeCandidates() {
	fullMask := uint16(0x3FE) // Bits 1-9 set

	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				s.candidates[i][j] = fullMask
			} else {
				s.candidates[i][j] = 0
			}
		}
	}
}

// updateConstraints rebuilds constraint masks
func (s *Solver) updateConstraints() {
	for i := 0; i < 9; i++ {
		s.rowMask[i] = 0
		s.colMask[i] = 0
		s.boxMask[i] = 0
	}

	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] != 0 {
				value := s.grid[i][j]
				s.rowMask[i] |= (1 << value)
				s.colMask[j] |= (1 << value)
				s.boxMask[(i/3)*3+j/3] |= (1 << value)
			}
		}
	}

	s.updateAllCandidates()
}

// propagateConstraints applies constraint propagation techniques
func (s *Solver) propagateConstraints() bool {
	progress := false

	for {
		oldProgress := progress

		progress = s.findNakedSingles() || progress
		progress = s.findHiddenSingles() || progress

		if s.advancedStrategies {
			// Apply medium-level tactics once per loop until no further change.
			progress = s.findNakedPairs() || progress
			progress = s.findHiddenPairs() || progress
			progress = s.findPointingPairs() || progress
		}

		if !progress || progress == oldProgress {
			break
		}

		s.stats.ConstraintSteps++
	}

	return progress
}

// findNakedSingles finds cells with only one candidate
func (s *Solver) findNakedSingles() bool {
	progress := false

	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 && bits.OnesCount16(s.candidates[i][j]) == 1 {
				for value := 1; value <= 9; value++ {
					if s.candidates[i][j]&(1<<value) != 0 {
						s.makeMove(i, j, value)
						progress = true
						break
					}
				}
			}
		}
	}

	return progress
}

// findHiddenSingles finds values that can only go in one place
func (s *Solver) findHiddenSingles() bool {
	progress := false

	// Check rows
	for i := 0; i < 9; i++ {
		progress = s.findHiddenSinglesInUnit(s.getRowCells(i)) || progress
	}

	// Check columns
	for j := 0; j < 9; j++ {
		progress = s.findHiddenSinglesInUnit(s.getColCells(j)) || progress
	}

	// Check boxes
	for box := 0; box < 9; box++ {
		progress = s.findHiddenSinglesInUnit(s.getBoxCells(box)) || progress
	}

	return progress
}

func (s *Solver) findHiddenSinglesInUnit(cells [][2]int) bool {
	progress := false

	for value := 1; value <= 9; value++ {
		possibleCells := []int{}

		for idx, cell := range cells {
			row, col := cell[0], cell[1]
			if s.grid[row][col] == 0 && s.candidates[row][col]&(1<<value) != 0 {
				possibleCells = append(possibleCells, idx)
			}
		}

		if len(possibleCells) == 1 {
			cell := cells[possibleCells[0]]
			row, col := cell[0], cell[1]
			s.makeMove(row, col, value)
			progress = true
		}
	}

	return progress
}

// Helper methods for constraint tracking (implemented above)

func (s *Solver) updateRowCandidates(row int, valueMask uint16) {
	for j := 0; j < 9; j++ {
		if s.grid[row][j] == 0 {
			s.candidates[row][j] &^= valueMask
		}
	}
}

func (s *Solver) updateColCandidates(col int, valueMask uint16) {
	for i := 0; i < 9; i++ {
		if s.grid[i][col] == 0 {
			s.candidates[i][col] &^= valueMask
		}
	}
}

func (s *Solver) updateBoxCandidates(box int, valueMask uint16) {
	startRow := (box / 3) * 3
	startCol := (box % 3) * 3

	for i := 0; i < 3; i++ {
		for j := 0; j < 3; j++ {
			row, col := startRow+i, startCol+j
			if s.grid[row][col] == 0 {
				s.candidates[row][col] &^= valueMask
			}
		}
	}
}

func (s *Solver) updateAllCandidates() {
	for i := 0; i < 9; i++ {
		for j := 0; j < 9; j++ {
			if s.grid[i][j] == 0 {
				s.updateCandidates(i, j)
			}
		}
	}
}

func (s *Solver) updateCandidates(row, col int) {
	if s.grid[row][col] != 0 {
		s.candidates[row][col] = 0
		return
	}

	box := (row/3)*3 + col/3
	usedMask := s.rowMask[row] | s.colMask[col] | s.boxMask[box]
	s.candidates[row][col] = uint16(0x3FE) &^ usedMask

	s.stats.CandidateUpdates++
}

// --- Advanced Strategies (Pairs / Pointing) ---

// findNakedPairs eliminates other candidates when exactly two cells in a unit share the same pair.
func (s *Solver) findNakedPairs() bool {
	changed := false
	// Process rows, columns, boxes
	// Rows
	for r := 0; r < 9; r++ {
		changed = s.nakedPairsInCells(s.getRowCells(r)) || changed
	}
	// Cols
	for c := 0; c < 9; c++ {
		changed = s.nakedPairsInCells(s.getColCells(c)) || changed
	}
	// Boxes
	for b := 0; b < 9; b++ {
		changed = s.nakedPairsInCells(s.getBoxCells(b)) || changed
	}
	return changed
}

func (s *Solver) nakedPairsInCells(cells [][2]int) bool {
	pairMaskCount := make(map[uint16]int)
	indicesByMask := make(map[uint16][]int)
	for idx, cell := range cells {
		r, c := cell[0], cell[1]
		if s.grid[r][c] == 0 {
			mask := s.candidates[r][c]
			if bits.OnesCount16(mask) == 2 {
				pairMaskCount[mask]++
				indicesByMask[mask] = append(indicesByMask[mask], idx)
			}
		}
	}
	changed := false
	for mask, cnt := range pairMaskCount {
		if cnt == 2 {
			// eliminate mask bits from all other cells in unit
			for idx, cell := range cells {
				if containsIndex(indicesByMask[mask], idx) {
					continue
				}
				r, c := cell[0], cell[1]
				if s.grid[r][c] == 0 && (s.candidates[r][c]&mask) != 0 {
					s.candidates[r][c] &^= mask
					changed = true
				}
			}
		}
	}
	return changed
}

// findHiddenPairs restricts two cells to only their shared two candidates.
func (s *Solver) findHiddenPairs() bool {
	changed := false
	for r := 0; r < 9; r++ {
		changed = s.hiddenPairsInCells(s.getRowCells(r)) || changed
	}
	for c := 0; c < 9; c++ {
		changed = s.hiddenPairsInCells(s.getColCells(c)) || changed
	}
	for b := 0; b < 9; b++ {
		changed = s.hiddenPairsInCells(s.getBoxCells(b)) || changed
	}
	return changed
}

func (s *Solver) hiddenPairsInCells(cells [][2]int) bool {
	// Count candidate occurrences per value
	occ := make(map[int][]int)
	for idx, cell := range cells {
		r, c := cell[0], cell[1]
		if s.grid[r][c] != 0 {
			continue
		}
		mask := s.candidates[r][c]
		for v := 1; v <= 9; v++ {
			if mask&(1<<v) != 0 {
				occ[v] = append(occ[v], idx)
			}
		}
	}
	changed := false
	// Find value pairs that occupy exactly the same two cells
	for v1 := 1; v1 <= 9; v1++ {
		idxs1 := occ[v1]
		if len(idxs1) != 2 {
			continue
		}
		for v2 := v1 + 1; v2 <= 9; v2++ {
			idxs2 := occ[v2]
			if len(idxs2) != 2 {
				continue
			}
			if idxs1[0] == idxs2[0] && idxs1[1] == idxs2[1] {
				// Hidden pair (v1,v2) -> strip other candidates from those two cells
				pairMask := uint16((1 << v1) | (1 << v2))
				for _, idx := range idxs1 {
					r, c := cells[idx][0], cells[idx][1]
					if s.candidates[r][c] != pairMask {
						s.candidates[r][c] = pairMask
						changed = true
					}
				}
			}
		}
	}
	return changed
}

// findPointingPairs implements basic pointing pair/box-line reduction.
func (s *Solver) findPointingPairs() bool {
	changed := false
	// For each box, if candidate for value appears only in one row (or column) inside box, eliminate from that row (or column) outside box.
	for b := 0; b < 9; b++ {
		cells := s.getBoxCells(b)
		// Track positions by value
		for v := 1; v <= 9; v++ {
			var rows, cols [9]int
			count := 0
			var posList [9][2]int
			for _, cell := range cells {
				r, c := cell[0], cell[1]
				if s.grid[r][c] == 0 && (s.candidates[r][c]&(1<<v)) != 0 {
					rows[r]++
					cols[c]++
					posList[count] = cell
					count++
				}
			}
			if count <= 1 {
				continue
			}
			sameRow := -1
			sameCol := -1
			for i := 0; i < 9; i++ {
				if rows[i] == count && count > 1 {
					sameRow = i
				}
				if cols[i] == count && count > 1 {
					sameCol = i
				}
			}
			if sameRow != -1 {
				// eliminate v from row outside this box
				for c := 0; c < 9; c++ {
					// skip cells inside the box
					if (sameRow/3)*3+0 == (posList[0][0]/3)*3 && c/3 == (posList[0][1]/3) {
						// still inside some sub-box column; precise exclusion handled below
					}
					if s.grid[sameRow][c] == 0 && !cellListContains(posList[:count], sameRow, c) {
						if (s.candidates[sameRow][c] & (1 << v)) != 0 {
							s.candidates[sameRow][c] &^= (1 << v)
							changed = true
						}
					}
				}
			}
			if sameCol != -1 {
				for r := 0; r < 9; r++ {
					if s.grid[r][sameCol] == 0 && !cellListContains(posList[:count], r, sameCol) {
						if (s.candidates[r][sameCol] & (1 << v)) != 0 {
							s.candidates[r][sameCol] &^= (1 << v)
							changed = true
						}
					}
				}
			}
		}
	}
	return changed
}

// Helper utilities for advanced strategies
func containsIndex(list []int, idx int) bool {
	for _, v := range list {
		if v == idx {
			return true
		}
	}
	return false
}
func cellListContains(cells [][2]int, r, c int) bool {
	for _, cell := range cells {
		if cell[0] == r && cell[1] == c {
			return true
		}
	}
	return false
}
