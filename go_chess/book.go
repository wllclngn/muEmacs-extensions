package main

// Opening book system with three layers:
// 1. Blunder avoidance - filter obviously bad early moves
// 2. Master book - proven grandmaster openings (from Lichess)
// 3. Learning overlay - adapt based on game outcomes
//
// Data source: Lichess Masters Database (https://lichess.org)
// License: CC0 Public Domain

import (
	crand "crypto/rand"
	_ "embed"
	"encoding/binary"
	"encoding/json"
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

//go:embed lichess_seed.json
var lichessSeedData []byte

// GameResult represents the outcome of a game
type GameResult int

const (
	ResultUnknown GameResult = iota
	ResultWhiteWins
	ResultBlackWins
	ResultDraw
)

// OpeningBook stores opening positions and their recommended moves
// JSON format matches the Lichess fetcher output
type OpeningBook struct {
	Source    string                  `json:"source"`
	SourceURL string                  `json:"source_url"`
	License   string                  `json:"license"`
	Generated string                  `json:"generated"`
	Positions map[string]BookPosition `json:"positions"` // FEN → position data
	Games     []GameRecord            `json:"games,omitempty"` // Game history log

	mu        sync.RWMutex
	filepath  string
	hashIndex map[uint64]*BookPosition // Zobrist hash → position (built on load)
}

// GameRecord stores a completed game for review
type GameRecord struct {
	Date      string   `json:"date"`
	Moves     []string `json:"moves"`
	Result    string   `json:"result"` // "white", "black", "draw"
	MoveCount int      `json:"move_count"`
}

// BookPosition stores move recommendations for a position
type BookPosition struct {
	FEN   string     `json:"fen"`
	ECO   string     `json:"eco,omitempty"`
	Name  string     `json:"name,omitempty"`
	Moves []BookMove `json:"moves"`

	// Tier 2: Position-level aggregate stats
	// Tracks overall game outcomes that reached this position
	PosWhiteWins int `json:"pos_white_wins,omitempty"`
	PosBlackWins int `json:"pos_black_wins,omitempty"`
	PosDraws     int `json:"pos_draws,omitempty"`
}

// BookMove stores statistics for a single move from a position
type BookMove struct {
	UCI         string `json:"uci"`
	SAN         string `json:"san,omitempty"`
	MasterGames int    `json:"master_games"`
	MasterWhite int    `json:"master_white"`
	MasterDraws int    `json:"master_draws"`
	MasterBlack int    `json:"master_black"`
	OurGames    int    `json:"our_games"`
	OurWins     int    `json:"our_wins"`
	OurLosses   int    `json:"our_losses"`
	OurDraws    int    `json:"our_draws"`
}

// Global opening book instance
var globalBook *OpeningBook

// Square constants for blunder detection
const (
	sqF2 Square = 13 // f2 (rank 1, file 5)
	sqF7 Square = 53 // f7 (rank 6, file 5)
	sqG2 Square = 14 // g2 (rank 1, file 6)
	sqG7 Square = 54 // g7 (rank 6, file 6)
)

// Book bonus values in centipawns (graduated by ply)
const (
	BookBonusEarly = 30 // Ply 1-12: +0.30 pawn
	BookBonusMid   = 20 // Ply 13-24: +0.20 pawn
	BookBonusLate  = 10 // Ply 25-36: +0.10 pawn
	BookBonusNone  = 0  // Ply 37+: no bonus
)

// Unified draw weight for symmetric self-play training
// Both colors are the same AI learning together
// 0.5 = standard Elo weighting (draw worth half a win)
// Lower values encourage decisive games (faster learning)
const DrawWeight = 0.5

// Search depth by ply (graduated)
const (
	DepthEarly = 3 // Ply 1-12
	DepthMid   = 4 // Ply 13-24
	DepthLate  = 5 // Ply 25-36
	DepthFull  = 7 // Ply 37+ (configurable)
)

// InitOpeningBook loads or creates the opening book
func InitOpeningBook() {
	// Seed math/rand from crypto/rand (getrandom syscall on Linux)
	var seed int64
	binary.Read(crand.Reader, binary.LittleEndian, &seed)
	rand.Seed(seed)

	home, err := os.UserHomeDir()
	if err != nil {
		return
	}

	bookPath := filepath.Join(home, ".config", "muemacs", "chess_book.json")
	globalBook = loadBook(bookPath)
}

// loadBook loads the opening book from disk, seeding from embedded Lichess data if needed
func loadBook(path string) *OpeningBook {
	book := &OpeningBook{
		Positions: make(map[string]BookPosition),
		filepath:  path,
	}

	// Try to load existing book from disk
	var existingBook *OpeningBook
	data, err := os.ReadFile(path)
	if err == nil {
		existingBook = &OpeningBook{Positions: make(map[string]BookPosition)}
		json.Unmarshal(data, existingBook)
	}

	// Check if existing book has master data (by checking Source field)
	hasMasterData := existingBook != nil && existingBook.Source != ""

	if !hasMasterData {
		// Seed from embedded Lichess data
		if err := json.Unmarshal(lichessSeedData, book); err != nil {
			return book // Return empty on parse error (shouldn't happen)
		}

		// Merge any existing learning data into the seeded book
		if existingBook != nil {
			mergeLearnedData(book, existingBook)
		}

		book.filepath = path
		// Save the seeded+merged book to disk
		book.Save()
		book.buildHashIndex()
		return book
	}

	// Existing book has master data, use it
	book = existingBook
	book.filepath = path
	book.buildHashIndex()
	return book
}

// buildHashIndex creates a Zobrist hash → BookPosition index for fast lookup
// This avoids string comparison overhead during search
func (book *OpeningBook) buildHashIndex() {
	book.hashIndex = make(map[uint64]*BookPosition, len(book.Positions))

	for fen := range book.Positions {
		// Parse FEN to get board position, compute hash
		b := parseFENForHash(fen)
		if b != nil {
			hash := b.ZobristHash()
			pos := book.Positions[fen] // Get value
			posCopy := pos             // Copy to avoid loop variable issue
			book.hashIndex[hash] = &posCopy
		}
	}
}

// parseFENForHash creates a minimal Board from FEN just for hash computation
func parseFENForHash(fen string) *Board {
	parts := strings.Split(fen, " ")
	if len(parts) < 4 {
		return nil
	}

	b := &Board{
		EnPassant: NoSquare,
	}

	// Parse piece placement
	rank := 7
	file := 0
	for _, c := range parts[0] {
		if c == '/' {
			rank--
			file = 0
			continue
		}
		if c >= '1' && c <= '8' {
			file += int(c - '0')
			continue
		}
		sq := Square(rank*8 + file)
		switch c {
		case 'P':
			b.Squares[sq] = WPawn
		case 'N':
			b.Squares[sq] = WKnight
		case 'B':
			b.Squares[sq] = WBishop
		case 'R':
			b.Squares[sq] = WRook
		case 'Q':
			b.Squares[sq] = WQueen
		case 'K':
			b.Squares[sq] = WKing
			b.KingSquare[White] = sq
		case 'p':
			b.Squares[sq] = BPawn
		case 'n':
			b.Squares[sq] = BKnight
		case 'b':
			b.Squares[sq] = BBishop
		case 'r':
			b.Squares[sq] = BRook
		case 'q':
			b.Squares[sq] = BQueen
		case 'k':
			b.Squares[sq] = BKing
			b.KingSquare[Black] = sq
		}
		file++
	}

	// Parse side to move
	if parts[1] == "b" {
		b.SideToMove = Black
	} else {
		b.SideToMove = White
	}

	// Parse castling
	for _, c := range parts[2] {
		switch c {
		case 'K':
			b.Castling |= CastleWK
		case 'Q':
			b.Castling |= CastleWQ
		case 'k':
			b.Castling |= CastleBK
		case 'q':
			b.Castling |= CastleBQ
		}
	}

	// Parse en passant
	if parts[3] != "-" && len(parts[3]) == 2 {
		file := int(parts[3][0] - 'a')
		rank := int(parts[3][1] - '1')
		b.EnPassant = Square(rank*8 + file)
	}

	return b
}

// LookupHash finds a position by Zobrist hash (fast path)
func (book *OpeningBook) LookupHash(hash uint64) (*BookPosition, bool) {
	if book == nil || book.hashIndex == nil {
		return nil, false
	}

	book.mu.RLock()
	defer book.mu.RUnlock()

	pos, ok := book.hashIndex[hash]
	if ok && len(pos.Moves) > 0 {
		return pos, true
	}
	return nil, false
}

// mergeLearnedData merges learning data from src into dst
// Preserves dst's master data while adding src's our_* stats
func mergeLearnedData(dst, src *OpeningBook) {
	// Preserve game history
	dst.Games = append(dst.Games, src.Games...)

	for fen, srcPos := range src.Positions {
		dstPos, exists := dst.Positions[fen]
		if !exists {
			// Position only in learning data - add it
			dst.Positions[fen] = srcPos
			continue
		}

		// Merge learning stats into existing position
		dstMoves := make(map[string]*BookMove)
		for i := range dstPos.Moves {
			dstMoves[dstPos.Moves[i].UCI] = &dstPos.Moves[i]
		}

		for _, srcMove := range srcPos.Moves {
			if dstMove, ok := dstMoves[srcMove.UCI]; ok {
				// Move exists in master - add learning stats
				dstMove.OurGames = srcMove.OurGames
				dstMove.OurWins = srcMove.OurWins
				dstMove.OurLosses = srcMove.OurLosses
				dstMove.OurDraws = srcMove.OurDraws
			} else {
				// New move not in master - append it
				dstPos.Moves = append(dstPos.Moves, srcMove)
			}
		}

		// Merge Tier 2 position stats
		dstPos.PosWhiteWins = srcPos.PosWhiteWins
		dstPos.PosBlackWins = srcPos.PosBlackWins
		dstPos.PosDraws = srcPos.PosDraws

		dst.Positions[fen] = dstPos
	}
}

// Save persists the book to disk
func (book *OpeningBook) Save() error {
	if book == nil {
		return nil
	}

	book.mu.Lock()
	defer book.mu.Unlock()

	data, err := json.MarshalIndent(book, "", "  ")
	if err != nil {
		return err
	}

	dir := filepath.Dir(book.filepath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	return os.WriteFile(book.filepath, data, 0644)
}

// GetDepthForPly returns the search depth based on game phase
func GetDepthForPly(ply, configuredDepth int) int {
	// Use configured depth throughout for balance
	// Graduated depth was causing asymmetric play quality
	return configuredDepth
}

// GetBookBonusForPly returns the book bonus in centipawns based on game phase
func GetBookBonusForPly(ply int) int {
	switch {
	case ply <= 12:
		return BookBonusEarly
	case ply <= 24:
		return BookBonusMid
	case ply <= 36:
		return BookBonusLate
	default:
		return BookBonusNone
	}
}


// GetPositionPenalty returns a penalty (negative) or bonus (positive) based on
// how this position historically plays out for the given side.
// This is Tier 2 learning: position-level aggregate statistics.
func (book *OpeningBook) GetPositionPenalty(fen string, sideToMove Color) int {
	if book == nil {
		return 0
	}

	pos, ok := book.LookupFEN(fen)
	if !ok {
		return 0
	}

	total := pos.PosWhiteWins + pos.PosBlackWins + pos.PosDraws
	if total < 5 {
		return 0 // Not enough data to be meaningful
	}

	// Calculate win rate for the side that just moved into this position
	var myWinRate float64
	if sideToMove == White {
		myWinRate = float64(pos.PosWhiteWins) / float64(total)
	} else {
		myWinRate = float64(pos.PosBlackWins) / float64(total)
	}

	// Scale: 0% win rate = -30 cp, 50% = 0, 100% = +15 cp
	if myWinRate < 0.5 {
		penalty := int((0.5 - myWinRate) * 60) // Max -30 cp at 0%
		return -penalty
	}
	return int((myWinRate - 0.5) * 30) // Small bonus for favorable positions
}

// normalizeFEN strips halfmove and fullmove counters for book matching
// FEN format: <board> <side> <castling> <en-passant> <halfmove> <fullmove>
// We normalize to: <board> <side> <castling> <en-passant> 0 1
// This fixes the mismatch between Lichess master data (fullmove=1) and
// game-generated FENs (fullmove varies)
func normalizeFEN(fen string) string {
	parts := strings.Split(fen, " ")
	if len(parts) >= 4 {
		// Keep board, side, castling, en-passant; normalize counters
		return parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3] + " 0 1"
	}
	return fen
}

// LookupFEN finds a position in the book by FEN string
// Uses normalized FEN (ignoring halfmove/fullmove) for matching
// Prefers positions with moves over empty positions
func (book *OpeningBook) LookupFEN(fen string) (*BookPosition, bool) {
	if book == nil {
		return nil, false
	}

	book.mu.RLock()
	defer book.mu.RUnlock()

	// Try normalized FEN first (master data uses normalized FENs)
	normalized := normalizeFEN(fen)
	if pos, ok := book.Positions[normalized]; ok && len(pos.Moves) > 0 {
		return &pos, true
	}

	// Try exact match as fallback
	if pos, ok := book.Positions[fen]; ok && len(pos.Moves) > 0 {
		return &pos, true
	}

	// Return any match even if empty (for learning storage)
	if pos, ok := book.Positions[normalized]; ok {
		return &pos, true
	}
	if pos, ok := book.Positions[fen]; ok {
		return &pos, true
	}

	return nil, false
}

// GetBookBonus returns the bonus (in centipawns) for a move if it's in the book
// Takes the FEN of the position BEFORE the move and the move's UCI string
func (book *OpeningBook) GetBookBonus(fen string, moveUCI string, ply int) int {
	if book == nil {
		return 0
	}

	baseBonus := GetBookBonusForPly(ply)
	if baseBonus == 0 {
		return 0
	}

	pos, ok := book.LookupFEN(fen)
	if !ok {
		return 0
	}

	// Find the move in the book
	for _, bm := range pos.Moves {
		if bm.UCI == moveUCI {
			// Calculate win rate from master games
			totalGames := bm.MasterWhite + bm.MasterDraws + bm.MasterBlack
			if totalGames == 0 {
				return baseBonus / 2 // Half bonus if no stats
			}

			// Win rate from perspective of side that played this move
			// FEN shows who is to move NEXT, so we need the opposite
			var winRate float64
			parts := strings.Split(fen, " ")
			if len(parts) >= 2 && parts[1] == "w" {
				// White to move next = Black just played this move
				// Black wins are good for Black
				winRate = float64(bm.MasterBlack) + 0.5*float64(bm.MasterDraws)
			} else {
				// Black to move next = White just played this move
				// White wins are good for White
				winRate = float64(bm.MasterWhite) + 0.5*float64(bm.MasterDraws)
			}
			winRate /= float64(totalGames)

			// Scale bonus by win rate (normalize around 0.5)
			// A move with 55% win rate gets full bonus
			// A move with 45% win rate gets reduced bonus
			scaleFactor := winRate / 0.55
			if scaleFactor > 1.5 {
				scaleFactor = 1.5 // Cap at 150%
			}

			// Blend in our own experience if we have it
			if bm.OurGames > 0 {
				// Use unified draw weight (symmetric for self-play training)
				drawWeight := DrawWeight

				// Blend draw weight with master draw rate (position character)
				// High master draw rate = draws more acceptable in this position
				masterDrawRate := float64(bm.MasterDraws) / float64(totalGames)
				adjustedDrawWeight := drawWeight + (0.5-drawWeight)*masterDrawRate

				ourWinRate := float64(bm.OurWins) + adjustedDrawWeight*float64(bm.OurDraws)
				ourWinRate /= float64(bm.OurGames)

				// Blend: more weight to our data as we get more games
				blendFactor := minFloat(float64(bm.OurGames)/20.0, 0.5) // Max 50% our data
				winRate = winRate*(1-blendFactor) + ourWinRate*blendFactor
				scaleFactor = winRate / 0.55
			}

			return int(float64(baseBonus) * scaleFactor)
		}
	}

	return 0 // Move not in book
}

// IsInBook returns true if a move is in the opening book for the given position
func (book *OpeningBook) IsInBook(fen string, moveUCI string) bool {
	if book == nil {
		return false
	}

	pos, ok := book.LookupFEN(fen)
	if !ok {
		return false
	}

	for _, bm := range pos.Moves {
		if bm.UCI == moveUCI {
			return true
		}
	}
	return false
}

// GetBookMoves returns all book moves for a position, sorted by combined weight
func (book *OpeningBook) GetBookMoves(fen string) []BookMove {
	if book == nil {
		return nil
	}

	pos, ok := book.LookupFEN(fen)
	if !ok {
		return nil
	}

	return pos.Moves
}

// IsWeakeningMove returns true if a move is a known early-game blunder
// This is Layer 1: Blunder Avoidance (hard filter)
func IsWeakeningMove(b *Board, m Move, ply int) bool {
	// Rule 1: Don't advance f-pawn in first 10 plies unless capturing
	// The f-pawn move weakens the king diagonal and enables scholar's mate
	if ply <= 10 {
		if (m.From == sqF2 && b.SideToMove == White) ||
			(m.From == sqF7 && b.SideToMove == Black) {
			if m.Captured == Empty {
				return true
			}
		}
	}

	// Rule 2: Don't advance g-pawn early unless castled
	if ply <= 12 && b.Castling != 0 {
		if (m.From == sqG2 && b.SideToMove == White) ||
			(m.From == sqG7 && b.SideToMove == Black) {
			if m.Captured == Empty {
				return true
			}
		}
	}

	// Rule 3: Don't move queen out early (before ply 10) unless capturing
	if ply <= 10 {
		piece := b.Squares[m.From]
		if piece == WQueen || piece == BQueen {
			if m.Captured == Empty {
				return true
			}
		}
	}

	return false
}

// FilterWeakeningMoves removes known blunders from a move list
func FilterWeakeningMoves(b *Board, moves []Move, ply int) []Move {
	if ply > 12 {
		return moves // Past early opening, allow everything
	}

	var filtered []Move
	for _, m := range moves {
		if !IsWeakeningMove(b, m, ply) {
			filtered = append(filtered, m)
		}
	}

	if len(filtered) == 0 {
		return moves // Safety: don't filter everything
	}

	return filtered
}

// bookTrainingMode is set by search.go's SetTrainingMode
var bookTrainingMode bool

// SetBookTrainingMode enables temperature-based exploration in book selection
func SetBookTrainingMode(enabled bool) {
	bookTrainingMode = enabled
}

// PickBookMove selects a book move using weighted random selection
// Weight combines master game frequency and our learning data
// Filters out repetition moves when contempt is active
// In training mode, applies softmax temperature for exploration
func (book *OpeningBook) PickBookMove(b *Board, legalMoves []Move, ply int) (Move, bool) {
	if book == nil {
		return Move{}, false
	}

	// Try fast hash-based lookup first
	hash := b.ZobristHash()
	pos, ok := book.LookupHash(hash)
	if !ok || len(pos.Moves) == 0 {
		// Fall back to FEN lookup (handles edge cases, learning data)
		fen := b.ToFEN()
		pos, ok = book.LookupFEN(fen)
		if !ok || len(pos.Moves) == 0 {
			return Move{}, false
		}
	}

	// Build weighted candidates from legal moves that are in book
	type candidate struct {
		move   Move
		weight float64
	}
	var candidates []candidate

	for _, bm := range pos.Moves {
		for _, lm := range legalMoves {
			if lm.String() == bm.UCI {
				// Skip moves that create repetitions when this side has contempt
				sideContempt := getContempt(b.SideToMove)
				if sideContempt > 0 {
					bc := b.Copy()
					bc.MakeMove(&lm)
					if bc.IsRepetition() {
						break // Skip this move
					}
				}

				// WIN-RATE WEIGHTED SELECTION
				// Base weight from master games (proven grandmaster moves)
				weight := float64(bm.MasterGames)

				// Our learning: UNIFIED model - both sides contribute
				// OurWins = times this move led to win for the side that played it
				// OurLosses = times this move led to loss for the side that played it
				// High win rate = good move, low win rate = avoid
				if bm.OurGames > 0 {
					winRate := float64(bm.OurWins) / float64(bm.OurGames)

					if winRate > 0.4 {
						// Good move - boost proportionally to wins
						// Quadratic boost rewards high win rates more
						boost := winRate * winRate
						weight += float64(bm.OurWins) * boost * 15
					} else if bm.MasterGames == 0 {
						// Low win-rate move NOT in master book = learned bad move
						// Penalize heavily - the AI learned this doesn't work
						weight = 0.01
					}
					// Moves in master book with low our-win-rate keep master weight
					// (master games are proven, our sample might be small)
				}

				// Skip moves with no evidence of success
				if weight < 0.1 {
					break // Skip this move entirely
				}

				candidates = append(candidates, candidate{move: lm, weight: weight})
				break
			}
		}
	}

	if len(candidates) == 0 {
		return Move{}, false
	}

	// TRAINING MODE: Apply softmax temperature for exploration
	// This flattens the weight distribution to allow more move diversity
	// Without this, e2e4 (1.2M games) dominates d4 (900K games) 57% vs 43%
	// With temperature, the distribution becomes more uniform
	if bookTrainingMode && len(candidates) > 1 {
		temperature := 1.5 // Higher = more uniform distribution
		if ply > 20 {
			// Decay temperature: 1.5 at ply 20, 0.5 at ply 30
			temperature = 1.5 - 0.1*float64(ply-20)
		}

		// Apply softmax: convert weights to log scale, then exp(log(w)/temp)
		// This compresses the range between high and low weights
		var softmaxWeights []float64
		var maxLogW float64 = -1e9
		for _, c := range candidates {
			if c.weight > 0 {
				logW := math.Log(c.weight)
				if logW > maxLogW {
					maxLogW = logW
				}
			}
		}

		var totalWeight float64
		for _, c := range candidates {
			var w float64
			if c.weight > 0 {
				// Softmax on log-weights for numerical stability
				logW := math.Log(c.weight)
				w = math.Exp((logW - maxLogW) / temperature)
			} else {
				w = 0.001
			}
			softmaxWeights = append(softmaxWeights, w)
			totalWeight += w
		}

		// Weighted random selection with softmax weights
		var rb [8]byte
		crand.Read(rb[:])
		r := float64(binary.LittleEndian.Uint64(rb[:])>>11) / (1 << 53) * totalWeight
		for i, w := range softmaxWeights {
			r -= w
			if r <= 0 {
				return candidates[i].move, true
			}
		}
		return candidates[0].move, true
	}

	// NORMAL MODE: Standard weighted random selection
	var totalWeight float64
	for _, c := range candidates {
		totalWeight += c.weight
	}

	// Weighted random selection using crypto/rand for true randomness
	var rb [8]byte
	crand.Read(rb[:])
	r := float64(binary.LittleEndian.Uint64(rb[:])>>11) / (1 << 53) * totalWeight
	for _, c := range candidates {
		r -= c.weight
		if r <= 0 {
			return c.move, true
		}
	}

	return candidates[0].move, true
}

// LearnFromGame updates the book based on game outcome
// UNIFIED LEARNING: Both sides learn from every game
// - White's moves: recorded as WIN if White won, LOSS if Black won
// - Black's moves: recorded as WIN if Black won, LOSS if White won
// - This gives the unified AI model knowledge from BOTH perspectives
// - When a human plays against this AI, it knows what works AND what fails
func (book *OpeningBook) LearnFromGame(history []Move, fens []string, result GameResult) {
	if book == nil || len(history) == 0 || result == ResultUnknown {
		return
	}

	book.mu.Lock()
	defer book.mu.Unlock()

	// Learn from first 15 moves (30 plies) - both sides
	maxPly := min(len(history), 30)
	if maxPly > len(fens) {
		maxPly = len(fens)
	}

	for i := 0; i < maxPly; i++ {
		fen := normalizeFEN(fens[i]) // Normalize to match master book FENs
		moveStr := history[i].String()

		pos, ok := book.Positions[fen]
		if !ok {
			pos = BookPosition{
				FEN:   fen,
				Moves: []BookMove{},
			}
		}

		// Find or create move entry
		var found bool
		for j := range pos.Moves {
			if pos.Moves[j].UCI == moveStr {
				// Update stats: tracks win/loss from THIS SIDE's perspective
				updateMoveStats(&pos.Moves[j], result, i)
				found = true
				break
			}
		}

		if !found {
			// Add new move with proper stats
			newMove := BookMove{UCI: moveStr}
			updateMoveStats(&newMove, result, i)
			pos.Moves = append(pos.Moves, newMove)
		}

		book.Positions[fen] = pos

		// Tier 2: Position-level stats for resulting position
		if i+1 < len(fens) {
			resultFen := fens[i+1]
			resultPos, ok := book.Positions[resultFen]
			if !ok {
				resultPos = BookPosition{
					FEN:   resultFen,
					Moves: []BookMove{},
				}
			}
			switch result {
			case ResultWhiteWins:
				resultPos.PosWhiteWins++
			case ResultBlackWins:
				resultPos.PosBlackWins++
			case ResultDraw:
				resultPos.PosDraws++
			}
			book.Positions[resultFen] = resultPos
		}
	}

	book.recordGameHistory(history, result)

	// Persist asynchronously
	go book.Save()
}

// recordGameHistory adds a game to the history log (caller must hold lock)
func (book *OpeningBook) recordGameHistory(history []Move, result GameResult) {
	var resultStr string
	switch result {
	case ResultWhiteWins:
		resultStr = "white"
	case ResultBlackWins:
		resultStr = "black"
	case ResultDraw:
		resultStr = "draw"
	}

	moves := make([]string, len(history))
	for i, m := range history {
		moves[i] = m.String()
	}

	game := GameRecord{
		Date:      time.Now().Format(time.RFC3339),
		Moves:     moves,
		Result:    resultStr,
		MoveCount: len(history),
	}
	book.Games = append(book.Games, game)
}

// Learning thresholds for adaptive caching
const (
	LearningMinSamples   = 5  // Always record first 5 games
	LearningStableSamples = 10 // Check stability after 10 games
	LearningHardCap      = 20 // Never record more than 20 games
)

// Counters for learning diagnostics
var (
	learnSkipped      int64 // Moves skipped due to threshold
	learnSkippedStable int64 // Moves skipped due to stable win rate
	learnRecorded     int64 // Moves actually recorded
)

// GetLearningStats returns (recorded, skipped) counts for diagnostics
func GetLearningStats() (int64, int64) {
	return learnRecorded, learnSkipped + learnSkippedStable
}

// ResetLearningStats clears the diagnostic counters
func ResetLearningStats() {
	learnRecorded = 0
	learnSkipped = 0
	learnSkippedStable = 0
}

// shouldRecordMove implements adaptive caching:
// 1. Always record first 5 games (minimum samples)
// 2. After 10 games, stop if win rate is clearly good (>60%) or bad (<40%)
// 3. Hard cap at 20 games regardless
func shouldRecordMove(bm *BookMove) bool {
	// Always record first few games
	if bm.OurGames < LearningMinSamples {
		return true
	}

	// Hard cap - never exceed this
	if bm.OurGames >= LearningHardCap {
		return false
	}

	// After enough samples, check if win rate has stabilized
	if bm.OurGames >= LearningStableSamples {
		winRate := float64(bm.OurWins) / float64(bm.OurGames)
		// If clearly good (>60%) or clearly bad (<40%), we've learned enough
		// Moves in the 40-60% range are uncertain - keep recording
		if winRate > 0.60 || winRate < 0.40 {
			return false
		}
	}

	return true
}

func updateMoveStats(bm *BookMove, result GameResult, ply int) {
	// ADAPTIVE CACHING: Check if we should record this move
	if !shouldRecordMove(bm) {
		if bm.OurGames >= LearningHardCap {
			learnSkipped++
		} else {
			learnSkippedStable++
		}
		return
	}

	learnRecorded++
	bm.OurGames++

	// Determine if this was a win/loss/draw for the side that played the move
	// Index 0, 2, 4... = White's moves (1st, 3rd, 5th...)
	// Index 1, 3, 5... = Black's moves (2nd, 4th, 6th...)
	whiteMoved := (ply % 2) == 0

	switch result {
	case ResultWhiteWins:
		if whiteMoved {
			bm.OurWins++
		} else {
			bm.OurLosses++
		}
	case ResultBlackWins:
		if whiteMoved {
			bm.OurLosses++
		} else {
			bm.OurWins++
		}
	case ResultDraw:
		bm.OurDraws++
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func minFloat(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}
