package main

// Evaluation function for chess positions
// Material values in centipawns, plus piece-square tables for positional bonuses

// Material values (centipawns) - indexed by piece type (1-6)
var PieceValue = [7]int{
	0,   // Empty/unused
	100, // Pawn
	320, // Knight
	330, // Bishop
	500, // Rook
	900, // Queen
	0,   // King (special handling)
}

// Piece-square tables for positional evaluation
// From White's perspective, index 0 = a1, index 63 = h8

// Pawn PST: Central pawns, advanced pawns are good
var PawnPST = [64]int{
	0, 0, 0, 0, 0, 0, 0, 0,
	5, 10, 10, -20, -20, 10, 10, 5,
	5, -5, -10, 0, 0, -10, -5, 5,
	0, 0, 0, 20, 20, 0, 0, 0,
	5, 5, 10, 25, 25, 10, 5, 5,
	10, 10, 20, 30, 30, 20, 10, 10,
	50, 50, 50, 50, 50, 50, 50, 50,
	0, 0, 0, 0, 0, 0, 0, 0,
}

// Knight PST: Knights love the center, hate corners
var KnightPST = [64]int{
	-50, -40, -30, -30, -30, -30, -40, -50,
	-40, -20, 0, 5, 5, 0, -20, -40,
	-30, 5, 10, 15, 15, 10, 5, -30,
	-30, 0, 15, 20, 20, 15, 0, -30,
	-30, 5, 15, 20, 20, 15, 5, -30,
	-30, 0, 10, 15, 15, 10, 0, -30,
	-40, -20, 0, 0, 0, 0, -20, -40,
	-50, -40, -30, -30, -30, -30, -40, -50,
}

// Bishop PST: Bishops like long diagonals
var BishopPST = [64]int{
	-20, -10, -10, -10, -10, -10, -10, -20,
	-10, 5, 0, 0, 0, 0, 5, -10,
	-10, 10, 10, 10, 10, 10, 10, -10,
	-10, 0, 10, 10, 10, 10, 0, -10,
	-10, 5, 5, 10, 10, 5, 5, -10,
	-10, 0, 5, 10, 10, 5, 0, -10,
	-10, 0, 0, 0, 0, 0, 0, -10,
	-20, -10, -10, -10, -10, -10, -10, -20,
}

// Rook PST: Rooks on 7th rank, open files
var RookPST = [64]int{
	0, 0, 0, 5, 5, 0, 0, 0,
	-5, 0, 0, 0, 0, 0, 0, -5,
	-5, 0, 0, 0, 0, 0, 0, -5,
	-5, 0, 0, 0, 0, 0, 0, -5,
	-5, 0, 0, 0, 0, 0, 0, -5,
	-5, 0, 0, 0, 0, 0, 0, -5,
	5, 10, 10, 10, 10, 10, 10, 5,
	0, 0, 0, 0, 0, 0, 0, 0,
}

// Queen PST: Keep queen safe early, centralize later
var QueenPST = [64]int{
	-20, -10, -10, -5, -5, -10, -10, -20,
	-10, 0, 0, 0, 0, 0, 0, -10,
	-10, 5, 5, 5, 5, 5, 0, -10,
	0, 0, 5, 5, 5, 5, 0, -5,
	-5, 0, 5, 5, 5, 5, 0, -5,
	-10, 0, 5, 5, 5, 5, 0, -10,
	-10, 0, 0, 0, 0, 0, 0, -10,
	-20, -10, -10, -5, -5, -10, -10, -20,
}

// King PST: Castle early (middlegame)
var KingMiddlegamePST = [64]int{
	20, 30, 10, 0, 0, 10, 30, 20,
	20, 20, 0, 0, 0, 0, 20, 20,
	-10, -20, -20, -20, -20, -20, -20, -10,
	-20, -30, -30, -40, -40, -30, -30, -20,
	-30, -40, -40, -50, -50, -40, -40, -30,
	-30, -40, -40, -50, -50, -40, -40, -30,
	-30, -40, -40, -50, -50, -40, -40, -30,
	-30, -40, -40, -50, -50, -40, -40, -30,
}

// King PST: Centralize in endgame
var KingEndgamePST = [64]int{
	-50, -30, -30, -30, -30, -30, -30, -50,
	-30, -30, 0, 0, 0, 0, -30, -30,
	-30, -10, 20, 30, 30, 20, -10, -30,
	-30, -10, 30, 40, 40, 30, -10, -30,
	-30, -10, 30, 40, 40, 30, -10, -30,
	-30, -10, 20, 30, 30, 20, -10, -30,
	-30, -20, -10, 0, 0, -10, -20, -30,
	-50, -40, -30, -20, -20, -30, -40, -50,
}

// FlipSquare mirrors a square for black's perspective
func FlipSquare(sq Square) Square {
	return Square(56 - 8*(sq/8) + sq%8)
}

// GetPST returns the piece-square table value for a piece on a square
func GetPST(p Piece, sq Square) int {
	if p == Empty {
		return 0
	}

	// For black pieces, flip the square to get correct table index
	idx := int(sq)
	if p.IsBlack() {
		idx = int(FlipSquare(sq))
	}

	var pst *[64]int
	switch p {
	case WPawn, BPawn:
		pst = &PawnPST
	case WKnight, BKnight:
		pst = &KnightPST
	case WBishop, BBishop:
		pst = &BishopPST
	case WRook, BRook:
		pst = &RookPST
	case WQueen, BQueen:
		pst = &QueenPST
	case WKing, BKing:
		pst = &KingMiddlegamePST // TODO: endgame detection
	default:
		return 0
	}

	val := pst[idx]
	if p.IsBlack() {
		return -val
	}
	return val
}

// IsEndgame detects if we're in an endgame (based on material)
func IsEndgame(b *Board) bool {
	// Simple heuristic: endgame if no queens or little material
	hasWQueen, hasBQueen := false, false
	whiteMaterial, blackMaterial := 0, 0

	for sq := Square(0); sq < 64; sq++ {
		p := b.Squares[sq]
		if p == Empty {
			continue
		}
		val := PieceValue[p.Type()]
		if p.IsWhite() {
			whiteMaterial += val
			if p == WQueen {
				hasWQueen = true
			}
		} else {
			blackMaterial += val
			if p == BQueen {
				hasBQueen = true
			}
		}
	}

	// Endgame if both sides have no queens, or material < 1300 (rook + minor)
	if !hasWQueen && !hasBQueen {
		return true
	}
	return whiteMaterial < 1300 || blackMaterial < 1300
}

// Evaluate returns the static evaluation of a position
// Positive = White advantage, Negative = Black advantage (in centipawns)
func Evaluate(b *Board) int {
	// Check for terminal positions first
	if b.IsCheckmate() {
		if b.SideToMove == White {
			return -100000 // Black wins
		}
		return 100000 // White wins
	}
	if b.IsDraw() {
		return 0
	}

	score := 0
	endgame := IsEndgame(b)

	// Material and piece-square evaluation
	for sq := Square(0); sq < 64; sq++ {
		p := b.Squares[sq]
		if p == Empty {
			continue
		}

		// Material
		val := PieceValue[p.Type()]
		if p.IsWhite() {
			score += val
		} else {
			score -= val
		}

		// Piece-square bonus
		if p == WKing || p == BKing {
			// Use appropriate king table based on game phase
			idx := int(sq)
			if p.IsBlack() {
				idx = int(FlipSquare(sq))
			}
			var pst *[64]int
			if endgame {
				pst = &KingEndgamePST
			} else {
				pst = &KingMiddlegamePST
			}
			psVal := pst[idx]
			if p.IsWhite() {
				score += psVal
			} else {
				score -= psVal
			}
		} else {
			score += GetPST(p, sq)
		}
	}

	// Mobility bonus (simplified - count legal moves)
	origSide := b.SideToMove
	whiteMobility := 0
	blackMobility := 0

	b.SideToMove = White
	whiteMobility = len(b.GenerateMoves())

	b.SideToMove = Black
	blackMobility = len(b.GenerateMoves())

	b.SideToMove = origSide

	score += (whiteMobility - blackMobility) * 2 // 2 centipawns per move

	// Bishop pair bonus
	whiteBishops, blackBishops := 0, 0
	for sq := Square(0); sq < 64; sq++ {
		if b.Squares[sq] == WBishop {
			whiteBishops++
		} else if b.Squares[sq] == BBishop {
			blackBishops++
		}
	}
	if whiteBishops >= 2 {
		score += 30
	}
	if blackBishops >= 2 {
		score -= 30
	}

	return score
}

// MVV_LVA returns a score for move ordering (Most Valuable Victim - Least Valuable Attacker)
// Higher scores should be searched first
func MVV_LVA(b *Board, m Move) int {
	captured := b.PieceAt(m.To)
	attacker := b.PieceAt(m.From)

	if captured == Empty {
		// Non-capture moves get lower priority
		// But promotions are good
		if m.Promotion != Empty {
			return 800 + PieceValue[m.Promotion.Type()]
		}
		return 0
	}

	// MVV-LVA: prioritize capturing valuable pieces with less valuable attackers
	// Score = VictimValue * 10 - AttackerValue
	return PieceValue[captured.Type()]*10 - PieceValue[attacker.Type()]
}

// OrderMoves sorts moves by MVV-LVA heuristic (in place)
// Returns the sorted slice (same backing array)
func OrderMoves(b *Board, moves []Move) []Move {
	// Simple insertion sort (fast for small arrays)
	for i := 1; i < len(moves); i++ {
		key := moves[i]
		keyScore := MVV_LVA(b, key)
		j := i - 1
		for j >= 0 && MVV_LVA(b, moves[j]) < keyScore {
			moves[j+1] = moves[j]
			j--
		}
		moves[j+1] = key
	}
	return moves
}

// MoveScore computes a priority score for move ordering with all heuristics
// Higher = search first
// Priority: TT move > Captures (MVV-LVA) > Killers > History
func MoveScore(b *Board, m Move, ply int, ttMove Move) int {
	// TT move is always first
	if ttMove.From == m.From && ttMove.To == m.To && ttMove.Promotion == m.Promotion {
		return 10000000 // Highest priority
	}

	// Captures scored by MVV-LVA (range: ~0-9000)
	if m.Captured != Empty {
		return 1000000 + MVV_LVA(b, m)
	}

	// Promotions without capture
	if m.Promotion != Empty {
		return 900000 + PieceValue[m.Promotion.Type()]
	}

	// Killer moves
	if isKiller(ply, m) {
		return 800000
	}

	// History heuristic (range: 0-10000)
	return getHistory(b.SideToMove, m)
}

// OrderMovesWithHeuristics sorts moves using TT, MVV-LVA, killers, and history
func OrderMovesWithHeuristics(b *Board, moves []Move, ply int, ttMove Move) []Move {
	// Compute scores for all moves
	type scoredMove struct {
		move  Move
		score int
	}
	scored := make([]scoredMove, len(moves))
	for i, m := range moves {
		scored[i] = scoredMove{m, MoveScore(b, m, ply, ttMove)}
	}

	// Insertion sort (fast for small arrays, stable)
	for i := 1; i < len(scored); i++ {
		key := scored[i]
		j := i - 1
		for j >= 0 && scored[j].score < key.score {
			scored[j+1] = scored[j]
			j--
		}
		scored[j+1] = key
	}

	// Copy back
	for i, sm := range scored {
		moves[i] = sm.move
	}
	return moves
}

const (
	Infinity = 1000000
)
