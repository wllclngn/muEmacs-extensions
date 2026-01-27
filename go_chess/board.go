package main

import (
	"strings"
	"sync"
)

// Board representation and move generation for chess
// Uses 8x8 array-based representation for simplicity

// Move slice pool to reduce allocations during search
// Average chess position has ~35 legal moves, cap at 256 for promotions
var movePool = sync.Pool{
	New: func() interface{} {
		moves := make([]Move, 0, 256)
		return &moves
	},
}

// GetMoveSlice gets a move slice from the pool
func GetMoveSlice() []Move {
	return (*movePool.Get().(*[]Move))[:0]
}

// PutMoveSlice returns a move slice to the pool
func PutMoveSlice(moves []Move) {
	if cap(moves) >= 256 {
		moves = moves[:0]
		movePool.Put(&moves)
	}
}

// Color represents a player
type Color uint8

const (
	White Color = iota
	Black
)

func (c Color) Opponent() Color {
	return 1 - c
}

// Piece represents a chess piece (with color encoded)
type Piece uint8

const (
	Empty Piece = iota
	WPawn
	WKnight
	WBishop
	WRook
	WQueen
	WKing
	BPawn
	BKnight
	BBishop
	BRook
	BQueen
	BKing
)

// PieceColor returns the color of a piece
func (p Piece) Color() Color {
	if p >= BPawn {
		return Black
	}
	return White
}

// IsWhite returns true if piece is white
func (p Piece) IsWhite() bool {
	return p >= WPawn && p <= WKing
}

// IsBlack returns true if piece is black
func (p Piece) IsBlack() bool {
	return p >= BPawn && p <= BKing
}

// PieceType returns the type without color (1-6)
func (p Piece) Type() int {
	if p == Empty {
		return 0
	}
	if p >= BPawn {
		return int(p - BPawn + 1)
	}
	return int(p)
}

// Castling flags
const (
	CastleWK uint8 = 1 << iota // White kingside
	CastleWQ                   // White queenside
	CastleBK                   // Black kingside
	CastleBQ                   // Black queenside
)

// Square represents a board position (0-63)
type Square int8

const NoSquare Square = -1

// Rank returns rank (0-7, where 0 is rank 1)
func (sq Square) Rank() int { return int(sq) / 8 }

// File returns file (0-7, where 0 is file a)
func (sq Square) File() int { return int(sq) % 8 }

// FromRankFile creates a square from rank and file
func FromRankFile(rank, file int) Square {
	return Square(rank*8 + file)
}

// IsValid returns true if square is on the board
func (sq Square) IsValid() bool {
	return sq >= 0 && sq < 64
}

// Move represents a chess move
type Move struct {
	From      Square
	To        Square
	Promotion Piece // 0 if not promotion
	// Undo info (set when move is made)
	Captured  Piece
	OldCastle uint8
	OldEP     Square
	OldHalf   int
}

// MoveFlag bits for special moves
const (
	MoveNormal    = 0
	MoveCastle    = 1 << 0
	MoveEnPassant = 1 << 1
	MovePromotion = 1 << 2
)

// IsNull returns true if this is a null/empty move
func (m Move) IsNull() bool {
	return m.From == m.To && m.From == 0
}

// String returns algebraic notation (e.g., "e2e4", "e7e8q")
func (m Move) String() string {
	if m.IsNull() {
		return "0000"
	}
	files := "abcdefgh"
	ranks := "12345678"
	s := string(files[m.From.File()]) + string(ranks[m.From.Rank()])
	s += string(files[m.To.File()]) + string(ranks[m.To.Rank()])
	if m.Promotion != Empty {
		promoChars := map[Piece]byte{
			WQueen: 'q', WRook: 'r', WBishop: 'b', WKnight: 'n',
			BQueen: 'q', BRook: 'r', BBishop: 'b', BKnight: 'n',
		}
		if c, ok := promoChars[m.Promotion]; ok {
			s += string(c)
		}
	}
	return s
}

// Board represents the chess position
type Board struct {
	Squares    [64]Piece
	SideToMove Color
	Castling   uint8   // KQkq flags
	EnPassant  Square  // En passant target square, or NoSquare
	HalfMoves  int     // Half-move clock (for 50-move rule)
	FullMoves  int     // Full move number
	KingSquare [2]Square // King positions indexed by color
	History    []uint64  // Zobrist hashes for repetition detection
}

// NewBoard creates the starting position
func NewBoard() *Board {
	b := &Board{
		SideToMove: White, // White moves first (standard chess)
		Castling:   CastleWK | CastleWQ | CastleBK | CastleBQ,
		EnPassant:  NoSquare,
		HalfMoves:  0,
		FullMoves:  1,
	}

	// Place pieces
	backRank := []Piece{WRook, WKnight, WBishop, WQueen, WKing, WBishop, WKnight, WRook}
	for i, p := range backRank {
		b.Squares[i] = p
		b.Squares[56+i] = p + (BPawn - WPawn) // Black pieces
	}
	for i := 0; i < 8; i++ {
		b.Squares[8+i] = WPawn  // Rank 2
		b.Squares[48+i] = BPawn // Rank 7
	}

	b.KingSquare[White] = 4  // e1
	b.KingSquare[Black] = 60 // e8

	return b
}

// Copy creates a deep copy of the board
func (b *Board) Copy() *Board {
	newBoard := *b
	// Deep copy the History slice to avoid shared state
	if len(b.History) > 0 {
		newBoard.History = make([]uint64, len(b.History))
		copy(newBoard.History, b.History)
	}
	return &newBoard
}

// ToFEN generates the FEN string for the current position
func (b *Board) ToFEN() string {
	var sb strings.Builder

	// Piece placement (from rank 8 to rank 1)
	pieceChars := map[Piece]byte{
		WKing: 'K', WQueen: 'Q', WRook: 'R', WBishop: 'B', WKnight: 'N', WPawn: 'P',
		BKing: 'k', BQueen: 'q', BRook: 'r', BBishop: 'b', BKnight: 'n', BPawn: 'p',
	}

	for rank := 7; rank >= 0; rank-- {
		empty := 0
		for file := 0; file < 8; file++ {
			sq := FromRankFile(rank, file)
			p := b.Squares[sq]
			if p == Empty {
				empty++
			} else {
				if empty > 0 {
					sb.WriteByte(byte('0' + empty))
					empty = 0
				}
				sb.WriteByte(pieceChars[p])
			}
		}
		if empty > 0 {
			sb.WriteByte(byte('0' + empty))
		}
		if rank > 0 {
			sb.WriteByte('/')
		}
	}

	// Side to move
	if b.SideToMove == White {
		sb.WriteString(" w ")
	} else {
		sb.WriteString(" b ")
	}

	// Castling rights
	castling := ""
	if b.Castling&CastleWK != 0 {
		castling += "K"
	}
	if b.Castling&CastleWQ != 0 {
		castling += "Q"
	}
	if b.Castling&CastleBK != 0 {
		castling += "k"
	}
	if b.Castling&CastleBQ != 0 {
		castling += "q"
	}
	if castling == "" {
		castling = "-"
	}
	sb.WriteString(castling)

	// En passant
	if b.EnPassant == NoSquare {
		sb.WriteString(" -")
	} else {
		files := "abcdefgh"
		ranks := "12345678"
		sb.WriteByte(' ')
		sb.WriteByte(files[b.EnPassant.File()])
		sb.WriteByte(ranks[b.EnPassant.Rank()])
	}

	// Half-move clock and full move number
	sb.WriteByte(' ')
	sb.WriteString(itoa(b.HalfMoves))
	sb.WriteByte(' ')
	sb.WriteString(itoa(b.FullMoves))

	return sb.String()
}

// itoa converts int to string without fmt dependency
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var buf [20]byte
	i := len(buf)
	neg := n < 0
	if neg {
		n = -n
	}
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		buf[i] = '-'
	}
	return string(buf[i:])
}

// PieceAt returns the piece at a square
func (b *Board) PieceAt(sq Square) Piece {
	if !sq.IsValid() {
		return Empty
	}
	return b.Squares[sq]
}

// IsEmpty returns true if square is empty
func (b *Board) IsEmpty(sq Square) bool {
	return b.PieceAt(sq) == Empty
}

// IsOccupied returns true if square has a piece
func (b *Board) IsOccupied(sq Square) bool {
	return b.PieceAt(sq) != Empty
}

// GenerateMoves generates all pseudo-legal moves
// (Does not check if king is left in check)
func (b *Board) GenerateMoves() []Move {
	moves := make([]Move, 0, 40)
	us := b.SideToMove
	them := us.Opponent()

	for sq := Square(0); sq < 64; sq++ {
		p := b.Squares[sq]
		if p == Empty {
			continue
		}
		if (us == White && !p.IsWhite()) || (us == Black && !p.IsBlack()) {
			continue
		}

		switch p {
		case WPawn:
			moves = b.genPawnMoves(sq, White, moves)
		case BPawn:
			moves = b.genPawnMoves(sq, Black, moves)
		case WKnight, BKnight:
			moves = b.genKnightMoves(sq, us, moves)
		case WBishop, BBishop:
			moves = b.genBishopMoves(sq, us, moves)
		case WRook, BRook:
			moves = b.genRookMoves(sq, us, moves)
		case WQueen, BQueen:
			moves = b.genQueenMoves(sq, us, moves)
		case WKing, BKing:
			moves = b.genKingMoves(sq, us, them, moves)
		}
	}

	return moves
}

// Direction offsets for sliding pieces
var (
	rookDirs   = []int{-8, 8, -1, 1}             // N, S, W, E
	bishopDirs = []int{-9, -7, 7, 9}             // NW, NE, SW, SE
	queenDirs  = []int{-8, 8, -1, 1, -9, -7, 7, 9}
	knightDirs = []int{-17, -15, -10, -6, 6, 10, 15, 17}
	kingDirs   = []int{-9, -8, -7, -1, 1, 7, 8, 9}
)

func (b *Board) genPawnMoves(sq Square, c Color, moves []Move) []Move {
	rank := sq.Rank()
	file := sq.File()

	var dir int
	var startRank, promoRank int
	var promoPieces []Piece

	if c == White {
		dir = 8
		startRank = 1
		promoRank = 7
		promoPieces = []Piece{WQueen, WRook, WBishop, WKnight}
	} else {
		dir = -8
		startRank = 6
		promoRank = 0
		promoPieces = []Piece{BQueen, BRook, BBishop, BKnight}
	}

	// Single push
	to := sq + Square(dir)
	if to.IsValid() && b.IsEmpty(to) {
		if to.Rank() == promoRank {
			for _, promo := range promoPieces {
				moves = append(moves, Move{From: sq, To: to, Promotion: promo})
			}
		} else {
			moves = append(moves, Move{From: sq, To: to})
		}

		// Double push from start rank
		if rank == startRank {
			to2 := sq + Square(2*dir)
			if b.IsEmpty(to2) {
				moves = append(moves, Move{From: sq, To: to2})
			}
		}
	}

	// Captures (including en passant)
	for _, capDir := range []int{dir - 1, dir + 1} {
		capTo := sq + Square(capDir)
		if !capTo.IsValid() {
			continue
		}
		// Check we haven't wrapped around files
		capFile := capTo.File()
		if abs(capFile-file) != 1 {
			continue
		}

		target := b.PieceAt(capTo)
		isEnemy := (c == White && target.IsBlack()) || (c == Black && target.IsWhite())
		isEP := capTo == b.EnPassant

		if isEnemy || isEP {
			if capTo.Rank() == promoRank {
				for _, promo := range promoPieces {
					moves = append(moves, Move{From: sq, To: capTo, Promotion: promo})
				}
			} else {
				moves = append(moves, Move{From: sq, To: capTo})
			}
		}
	}

	return moves
}

func (b *Board) genKnightMoves(sq Square, c Color, moves []Move) []Move {
	rank := sq.Rank()
	file := sq.File()

	for _, dir := range knightDirs {
		to := sq + Square(dir)
		if !to.IsValid() {
			continue
		}
		toRank := to.Rank()
		toFile := to.File()
		// Verify knight move geometry (L-shape)
		dr := abs(toRank - rank)
		df := abs(toFile - file)
		if !((dr == 2 && df == 1) || (dr == 1 && df == 2)) {
			continue
		}

		target := b.PieceAt(to)
		if target == Empty || target.Color() != c {
			moves = append(moves, Move{From: sq, To: to})
		}
	}

	return moves
}

func (b *Board) genSlidingMoves(sq Square, c Color, dirs []int, moves []Move) []Move {
	for _, dir := range dirs {
		to := sq
		for {
			prevRank := to.Rank()
			prevFile := to.File()
			to += Square(dir)

			if !to.IsValid() {
				break
			}
			toRank := to.Rank()
			toFile := to.File()

			// Check for wrapping (rank/file delta should be at most 1)
			if abs(toRank-prevRank) > 1 || abs(toFile-prevFile) > 1 {
				break
			}

			target := b.PieceAt(to)
			if target == Empty {
				moves = append(moves, Move{From: sq, To: to})
			} else if target.Color() != c {
				moves = append(moves, Move{From: sq, To: to})
				break
			} else {
				break // Own piece
			}
		}
	}

	return moves
}

func (b *Board) genBishopMoves(sq Square, c Color, moves []Move) []Move {
	return b.genSlidingMoves(sq, c, bishopDirs, moves)
}

func (b *Board) genRookMoves(sq Square, c Color, moves []Move) []Move {
	return b.genSlidingMoves(sq, c, rookDirs, moves)
}

func (b *Board) genQueenMoves(sq Square, c Color, moves []Move) []Move {
	return b.genSlidingMoves(sq, c, queenDirs, moves)
}

func (b *Board) genKingMoves(sq Square, us Color, them Color, moves []Move) []Move {
	rank := sq.Rank()
	file := sq.File()

	// Normal king moves
	for _, dir := range kingDirs {
		to := sq + Square(dir)
		if !to.IsValid() {
			continue
		}
		toRank := to.Rank()
		toFile := to.File()
		if abs(toRank-rank) > 1 || abs(toFile-file) > 1 {
			continue
		}

		target := b.PieceAt(to)
		if target == Empty || target.Color() != us {
			moves = append(moves, Move{From: sq, To: to})
		}
	}

	// Castling
	if us == White && sq == 4 { // e1
		if b.Castling&CastleWK != 0 && b.IsEmpty(5) && b.IsEmpty(6) {
			// Kingside castling: check squares not attacked
			if !b.IsAttacked(4, them) && !b.IsAttacked(5, them) && !b.IsAttacked(6, them) {
				moves = append(moves, Move{From: 4, To: 6})
			}
		}
		if b.Castling&CastleWQ != 0 && b.IsEmpty(1) && b.IsEmpty(2) && b.IsEmpty(3) {
			// Queenside castling
			if !b.IsAttacked(4, them) && !b.IsAttacked(3, them) && !b.IsAttacked(2, them) {
				moves = append(moves, Move{From: 4, To: 2})
			}
		}
	} else if us == Black && sq == 60 { // e8
		if b.Castling&CastleBK != 0 && b.IsEmpty(61) && b.IsEmpty(62) {
			if !b.IsAttacked(60, them) && !b.IsAttacked(61, them) && !b.IsAttacked(62, them) {
				moves = append(moves, Move{From: 60, To: 62})
			}
		}
		if b.Castling&CastleBQ != 0 && b.IsEmpty(57) && b.IsEmpty(58) && b.IsEmpty(59) {
			if !b.IsAttacked(60, them) && !b.IsAttacked(59, them) && !b.IsAttacked(58, them) {
				moves = append(moves, Move{From: 60, To: 58})
			}
		}
	}

	return moves
}

// IsAttacked returns true if square is attacked by given color
func (b *Board) IsAttacked(sq Square, by Color) bool {
	// Check pawn attacks
	var pawnDir int
	var enemyPawn Piece
	if by == White {
		pawnDir = -8 // Pawns attack diagonally forward
		enemyPawn = WPawn
	} else {
		pawnDir = 8
		enemyPawn = BPawn
	}
	for _, fd := range []int{-1, 1} {
		from := sq + Square(pawnDir+fd)
		if from.IsValid() && abs(from.File()-sq.File()) == 1 {
			if b.PieceAt(from) == enemyPawn {
				return true
			}
		}
	}

	// Check knight attacks
	var enemyKnight Piece
	if by == White {
		enemyKnight = WKnight
	} else {
		enemyKnight = BKnight
	}
	for _, dir := range knightDirs {
		from := sq + Square(dir)
		if from.IsValid() {
			dr := abs(from.Rank() - sq.Rank())
			df := abs(from.File() - sq.File())
			if (dr == 2 && df == 1) || (dr == 1 && df == 2) {
				if b.PieceAt(from) == enemyKnight {
					return true
				}
			}
		}
	}

	// Check king attacks
	var enemyKing Piece
	if by == White {
		enemyKing = WKing
	} else {
		enemyKing = BKing
	}
	for _, dir := range kingDirs {
		from := sq + Square(dir)
		if from.IsValid() && abs(from.Rank()-sq.Rank()) <= 1 && abs(from.File()-sq.File()) <= 1 {
			if b.PieceAt(from) == enemyKing {
				return true
			}
		}
	}

	// Check sliding attacks (rook/queen on ranks/files)
	var enemyRook, enemyQueen Piece
	if by == White {
		enemyRook, enemyQueen = WRook, WQueen
	} else {
		enemyRook, enemyQueen = BRook, BQueen
	}
	for _, dir := range rookDirs {
		if b.slidingAttack(sq, dir, enemyRook, enemyQueen) {
			return true
		}
	}

	// Check sliding attacks (bishop/queen on diagonals)
	var enemyBishop Piece
	if by == White {
		enemyBishop = WBishop
	} else {
		enemyBishop = BBishop
	}
	for _, dir := range bishopDirs {
		if b.slidingAttack(sq, dir, enemyBishop, enemyQueen) {
			return true
		}
	}

	return false
}

func (b *Board) slidingAttack(sq Square, dir int, slider1, slider2 Piece) bool {
	to := sq
	for {
		prevRank := to.Rank()
		prevFile := to.File()
		to += Square(dir)
		if !to.IsValid() {
			return false
		}
		if abs(to.Rank()-prevRank) > 1 || abs(to.File()-prevFile) > 1 {
			return false
		}
		p := b.PieceAt(to)
		if p == Empty {
			continue
		}
		return p == slider1 || p == slider2
	}
}

// MakeMove applies a move to the board (modifies in place)
func (b *Board) MakeMove(m *Move) {
	// Save undo info
	m.Captured = b.PieceAt(m.To)
	m.OldCastle = b.Castling
	m.OldEP = b.EnPassant
	m.OldHalf = b.HalfMoves

	piece := b.Squares[m.From]
	us := b.SideToMove
	them := us.Opponent()

	// Handle en passant capture
	if piece == WPawn || piece == BPawn {
		if m.To == b.EnPassant {
			// Remove captured pawn
			if us == White {
				b.Squares[m.To-8] = Empty
			} else {
				b.Squares[m.To+8] = Empty
			}
			m.Captured = Empty // EP capture doesn't capture on target square
		}
	}

	// Move the piece
	b.Squares[m.To] = piece
	b.Squares[m.From] = Empty

	// Handle promotion
	if m.Promotion != Empty {
		b.Squares[m.To] = m.Promotion
	}

	// Handle castling move
	if piece == WKing || piece == BKing {
		b.KingSquare[us] = m.To

		// Kingside castling
		if m.From == 4 && m.To == 6 { // White kingside
			b.Squares[5] = WRook
			b.Squares[7] = Empty
		} else if m.From == 4 && m.To == 2 { // White queenside
			b.Squares[3] = WRook
			b.Squares[0] = Empty
		} else if m.From == 60 && m.To == 62 { // Black kingside
			b.Squares[61] = BRook
			b.Squares[63] = Empty
		} else if m.From == 60 && m.To == 58 { // Black queenside
			b.Squares[59] = BRook
			b.Squares[56] = Empty
		}
	}

	// Update castling rights
	// King moves
	if m.From == 4 {
		b.Castling &^= (CastleWK | CastleWQ)
	}
	if m.From == 60 {
		b.Castling &^= (CastleBK | CastleBQ)
	}
	// Rook moves or captures
	if m.From == 0 || m.To == 0 {
		b.Castling &^= CastleWQ
	}
	if m.From == 7 || m.To == 7 {
		b.Castling &^= CastleWK
	}
	if m.From == 56 || m.To == 56 {
		b.Castling &^= CastleBQ
	}
	if m.From == 63 || m.To == 63 {
		b.Castling &^= CastleBK
	}

	// Update en passant square
	b.EnPassant = NoSquare
	if piece == WPawn && m.To-m.From == 16 {
		b.EnPassant = m.From + 8
	} else if piece == BPawn && m.From-m.To == 16 {
		b.EnPassant = m.From - 8
	}

	// Update clocks
	if m.Captured != Empty || piece == WPawn || piece == BPawn {
		b.HalfMoves = 0
	} else {
		b.HalfMoves++
	}

	if us == Black {
		b.FullMoves++
	}

	b.SideToMove = them

	// Track position for repetition detection
	b.History = append(b.History, b.ZobristHash())
}

// UnmakeMove reverses a move
func (b *Board) UnmakeMove(m *Move) {
	them := b.SideToMove
	us := them.Opponent()
	b.SideToMove = us

	piece := b.Squares[m.To]

	// Undo promotion
	if m.Promotion != Empty {
		if us == White {
			piece = WPawn
		} else {
			piece = BPawn
		}
	}

	// Move piece back
	b.Squares[m.From] = piece
	b.Squares[m.To] = m.Captured

	// Undo en passant capture
	if (piece == WPawn || piece == BPawn) && m.To == m.OldEP {
		if us == White {
			b.Squares[m.To-8] = BPawn
		} else {
			b.Squares[m.To+8] = WPawn
		}
	}

	// Undo castling rook move
	if piece == WKing || piece == BKing {
		b.KingSquare[us] = m.From

		if m.From == 4 && m.To == 6 {
			b.Squares[7] = WRook
			b.Squares[5] = Empty
		} else if m.From == 4 && m.To == 2 {
			b.Squares[0] = WRook
			b.Squares[3] = Empty
		} else if m.From == 60 && m.To == 62 {
			b.Squares[63] = BRook
			b.Squares[61] = Empty
		} else if m.From == 60 && m.To == 58 {
			b.Squares[56] = BRook
			b.Squares[59] = Empty
		}
	}

	// Restore state
	b.Castling = m.OldCastle
	b.EnPassant = m.OldEP
	b.HalfMoves = m.OldHalf
	if us == Black {
		b.FullMoves--
	}

	// Pop position from history
	if len(b.History) > 0 {
		b.History = b.History[:len(b.History)-1]
	}
}

// NullMoveInfo stores state needed to unmake a null move
type NullMoveInfo struct {
	OldEP Square
}

// MakeNullMove passes the turn without moving (for null-move pruning)
// Returns info needed to unmake
func (b *Board) MakeNullMove() NullMoveInfo {
	info := NullMoveInfo{OldEP: b.EnPassant}
	b.SideToMove = b.SideToMove.Opponent()
	b.EnPassant = NoSquare // Clear en passant (can't capture en passant after a pass)
	// Don't update history - null moves shouldn't count for repetition
	return info
}

// UnmakeNullMove restores the position after a null move
func (b *Board) UnmakeNullMove(info NullMoveInfo) {
	b.SideToMove = b.SideToMove.Opponent()
	b.EnPassant = info.OldEP
}

// HasNonPawnMaterial returns true if the side to move has pieces other than pawns and king
// Used to detect zugzwang-prone positions where null-move pruning is unsafe
func (b *Board) HasNonPawnMaterial(side Color) bool {
	for sq := Square(0); sq < 64; sq++ {
		p := b.Squares[sq]
		if p == Empty {
			continue
		}
		if side == White {
			if p == WKnight || p == WBishop || p == WRook || p == WQueen {
				return true
			}
		} else {
			if p == BKnight || p == BBishop || p == BRook || p == BQueen {
				return true
			}
		}
	}
	return false
}

// IsRepetition returns true if the current position has occurred before in this game
// Used for draw detection and contempt penalties
func (b *Board) IsRepetition() bool {
	if len(b.History) < 4 {
		return false
	}

	currentHash := b.ZobristHash()
	count := 0
	// Check history EXCLUDING the last element (which is the current position)
	// MakeMove appends the hash after the move, so History[last] == currentHash
	for i := 0; i < len(b.History)-1; i++ {
		if b.History[i] == currentHash {
			count++
		}
	}
	// count >= 1 means we've seen this position before
	return count >= 1
}

// RepetitionCount returns how many times the current position has occurred
// (including the current occurrence)
func (b *Board) RepetitionCount() int {
	currentHash := b.ZobristHash()
	count := 1 // Count current position
	// Check history EXCLUDING the last element (which is the current position)
	for i := 0; i < len(b.History)-1; i++ {
		if b.History[i] == currentHash {
			count++
		}
	}
	return count
}

// InCheck returns true if the side to move is in check
func (b *Board) InCheck() bool {
	return b.IsAttacked(b.KingSquare[b.SideToMove], b.SideToMove.Opponent())
}

// GenerateLegalMoves generates only legal moves (filters out moves that leave king in check)
func (b *Board) GenerateLegalMoves() []Move {
	pseudo := b.GenerateMoves()
	legal := make([]Move, 0, len(pseudo))

	for _, m := range pseudo {
		b.MakeMove(&m)
		// After making move, check if our king (now 'them' since side switched) is in check
		if !b.IsAttacked(b.KingSquare[b.SideToMove.Opponent()], b.SideToMove) {
			legal = append(legal, m)
		}
		b.UnmakeMove(&m)
	}

	return legal
}

// IsCheckmate returns true if current side is checkmated
func (b *Board) IsCheckmate() bool {
	if !b.InCheck() {
		return false
	}
	return len(b.GenerateLegalMoves()) == 0
}

// IsStalemate returns true if current side is stalemated
func (b *Board) IsStalemate() bool {
	if b.InCheck() {
		return false
	}
	return len(b.GenerateLegalMoves()) == 0
}

// IsDraw returns true if the position is a draw (stalemate, 50-move, insufficient material)
//
// 50-MOVE RULE IMPLEMENTATION NOTES (2026-01-24):
// ------------------------------------------------
// The 50-move rule (HalfMoves >= 100) is CRITICAL for AI vs AI training because:
//
// 1. WITHOUT IT: Games spiral into 200+ move shuffling matches where neither side
//    can force checkmate. We observed K+B vs K+B endgames going to the 200-move
//    limit, wasting training compute on positions that teach nothing.
//
// 2. THE MATH: HalfMoves counts HALF-moves (each side's move = 1 half-move).
//    So 100 half-moves = 50 full moves = the FIDE 50-move rule.
//    The counter resets on pawn moves or captures (see MakeMove).
//
// 3. TRAINING IMPACT: Without this rule, the learning system records hundreds of
//    meaningless endgame shuffles as "draws", polluting the book with noise.
//    With it, drawn endgames terminate cleanly once progress becomes impossible.
//
// 4. WHY GAMES STILL HIT 200 MOVES: If pawns are still advancing (e.g., passed
//    pawn races in K+P endgames), each pawn push resets HalfMoves. The 200-move
//    limit in run_games.py is a hard safety cap, not the primary draw detector.
//
// 5. INTERACTION WITH CONTEMPT: The contempt system (globalContempt) penalizes
//    repetitions to avoid EARLY draws. The 50-move rule handles LATE draws in
//    genuinely drawn endgames. They work together: contempt fights lazy draws,
//    50-move rule terminates hopeless positions.
func (b *Board) IsDraw() bool {
	if b.IsStalemate() {
		return true
	}
	if b.HalfMoves >= 100 {
		return true
	}
	// TODO: Insufficient material, threefold repetition
	return false
}

// ParseMove parses algebraic notation like "e2e4" or "e7e8q"
func (b *Board) ParseMove(s string) (Move, bool) {
	if len(s) < 4 {
		return Move{}, false
	}

	fromFile := int(s[0] - 'a')
	fromRank := int(s[1] - '1')
	toFile := int(s[2] - 'a')
	toRank := int(s[3] - '1')

	if fromFile < 0 || fromFile > 7 || fromRank < 0 || fromRank > 7 ||
		toFile < 0 || toFile > 7 || toRank < 0 || toRank > 7 {
		return Move{}, false
	}

	from := FromRankFile(fromRank, fromFile)
	to := FromRankFile(toRank, toFile)

	var promo Piece
	if len(s) >= 5 {
		promoChar := s[4]
		if b.SideToMove == White {
			switch promoChar {
			case 'q':
				promo = WQueen
			case 'r':
				promo = WRook
			case 'b':
				promo = WBishop
			case 'n':
				promo = WKnight
			}
		} else {
			switch promoChar {
			case 'q':
				promo = BQueen
			case 'r':
				promo = BRook
			case 'b':
				promo = BBishop
			case 'n':
				promo = BKnight
			}
		}
	}

	// Validate move exists in legal moves
	for _, m := range b.GenerateLegalMoves() {
		if m.From == from && m.To == to && (promo == Empty || m.Promotion == promo) {
			return m, true
		}
	}

	return Move{}, false
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}
