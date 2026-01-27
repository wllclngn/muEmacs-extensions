package main

import (
	"context"
	crand "crypto/rand"
	"encoding/binary"
	"math"
	"math/rand"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

// Global contempt values in centipawns (per side)
// Set via SetContempt() or SetAsymmetricContempt() before search
// Higher values = more aggressive play, avoiding draws
// Formula: contempt = (0.5 - draw_value) * 100
var globalContempt int       // Legacy: used when both sides have same contempt
var globalContemptWhite int  // White's contempt (0-50cp)
var globalContemptBlack int  // Black's contempt (0-50cp)

// getContempt returns the contempt value for the given side
func getContempt(side Color) int {
	if side == White {
		return globalContemptWhite
	}
	return globalContemptBlack
}

// Training mode enables exploration via softmax temperature
// When true, moves are selected probabilistically rather than greedily
// This prevents identical self-play games (AlphaZero/Leela approach)
var trainingMode bool

// SetTrainingMode enables or disables exploration for self-play
func SetTrainingMode(enabled bool) {
	trainingMode = enabled
	SetBookTrainingMode(enabled) // Also enable temperature exploration in book
}

// Task ID generator and cancellation registry for YBWC parallel search
var (
	taskIDCounter    atomic.Uint64
	cancelledParents sync.Map // parentID -> struct{}
)

// ============================================================================
// Transposition Table
// ============================================================================

// TT flag types for bound information
const (
	TTFlagExact = iota // Exact score (PV node)
	TTFlagLower        // Lower bound (failed high / beta cutoff)
	TTFlagUpper        // Upper bound (failed low / all node)
)

// TTEntry stores a cached search result
// Layout: 16 bytes total for cache-line efficiency
type TTEntry struct {
	Hash     uint64 // Position hash (for verification)
	BestMove uint16 // Encoded move: from(6) | to(6) | promo(4)
	Score    int16  // Evaluation score (centipawns)
	Depth    int8   // Search depth
	Flag     uint8  // TTFlagExact, TTFlagLower, or TTFlagUpper
}

// TT size: 1M entries × 16 bytes = 16MB
// Power of 2 for fast modulo via bitwise AND
const TTSize = 1 << 20
const TTMask = TTSize - 1

var transpositionTable [TTSize]TTEntry

// encodeMove packs a move into 16 bits: from(6) | to(6) | promo(4)
func encodeMove(m Move) uint16 {
	promo := uint16(0)
	if m.Promotion != Empty {
		// Map promotion piece to 1-4 (Q=1, R=2, B=3, N=4)
		switch m.Promotion {
		case WQueen, BQueen:
			promo = 1
		case WRook, BRook:
			promo = 2
		case WBishop, BBishop:
			promo = 3
		case WKnight, BKnight:
			promo = 4
		}
	}
	return uint16(m.From) | (uint16(m.To) << 6) | (promo << 12)
}

// decodeMove unpacks a 16-bit encoded move
// Note: doesn't restore Piece/Captured - caller must validate against legal moves
func decodeMove(encoded uint16, sideToMove Color) Move {
	from := Square(encoded & 0x3F)
	to := Square((encoded >> 6) & 0x3F)
	promo := (encoded >> 12) & 0xF

	m := Move{From: from, To: to}
	if promo != 0 {
		// Map back to piece type based on side to move
		if sideToMove == White {
			switch promo {
			case 1:
				m.Promotion = WQueen
			case 2:
				m.Promotion = WRook
			case 3:
				m.Promotion = WBishop
			case 4:
				m.Promotion = WKnight
			}
		} else {
			switch promo {
			case 1:
				m.Promotion = BQueen
			case 2:
				m.Promotion = BRook
			case 3:
				m.Promotion = BBishop
			case 4:
				m.Promotion = BKnight
			}
		}
	}
	return m
}

// ttProbe looks up a position in the transposition table
// Returns: score, bestMove, hit (true if usable score found)
// Even on miss, may return a best move for move ordering
func ttProbe(hash uint64, depth, alpha, beta int, sideToMove Color) (int, Move, bool) {
	entry := &transpositionTable[hash&TTMask]

	// Verify hash match (avoid collision false positives)
	if entry.Hash != hash {
		return 0, Move{}, false
	}

	bestMove := decodeMove(entry.BestMove, sideToMove)

	// Only use score if searched to sufficient depth
	if int(entry.Depth) >= depth {
		score := int(entry.Score)

		switch entry.Flag {
		case TTFlagExact:
			return score, bestMove, true
		case TTFlagLower:
			// Score is a lower bound (real score >= stored)
			if score >= beta {
				return score, bestMove, true
			}
		case TTFlagUpper:
			// Score is an upper bound (real score <= stored)
			if score <= alpha {
				return score, bestMove, true
			}
		}
	}

	// No usable score, but return best move for ordering
	return 0, bestMove, false
}

// ttStore saves a search result to the transposition table
// Uses replace-if-deeper policy
func ttStore(hash uint64, depth int, score int, flag uint8, bestMove Move) {
	idx := hash & TTMask
	entry := &transpositionTable[idx]

	// Replace if: different position OR deeper/equal depth search
	// This prioritizes deeper results while allowing updates for same position
	if entry.Hash != hash || int(entry.Depth) <= depth {
		entry.Hash = hash
		entry.Depth = int8(depth)
		entry.Score = int16(score)
		entry.Flag = flag
		entry.BestMove = encodeMove(bestMove)
	}
}

// ttClear resets the transposition table (call between games)
func ttClear() {
	for i := range transpositionTable {
		transpositionTable[i] = TTEntry{}
	}
}

// ============================================================================
// Quiescence Search
// ============================================================================

// Maximum quiescence depth to prevent explosion
const MaxQDepth = 8

// quiescence searches captures until the position is "quiet"
// This prevents the horizon effect where evaluation happens mid-tactic
func quiescence(b *Board, alpha, beta int, maximizing bool, qdepth int) int {
	// Stand-pat: evaluate the current position
	// If we're already winning, we can choose to not capture
	standPat := Evaluate(b)

	// Check for terminal conditions
	if b.IsCheckmate() {
		if b.SideToMove == White {
			return -100000
		}
		return 100000
	}
	if b.IsDraw() {
		return 0
	}

	// Depth limit to prevent explosion
	if qdepth >= MaxQDepth {
		return standPat
	}

	if maximizing {
		if standPat >= beta {
			return beta // Beta cutoff - standing pat is good enough
		}
		if standPat > alpha {
			alpha = standPat
		}
	} else {
		if standPat <= alpha {
			return alpha // Alpha cutoff - standing pat is good enough
		}
		if standPat < beta {
			beta = standPat
		}
	}

	// Generate and search only captures (and promotions)
	moves := b.GenerateLegalMoves()

	// Filter to captures and promotions only
	var captures []Move
	for _, m := range moves {
		if m.Captured != Empty || m.Promotion != Empty {
			captures = append(captures, m)
		}
	}

	// Order captures by MVV-LVA (Most Valuable Victim - Least Valuable Attacker)
	captures = OrderMoves(b, captures)

	if maximizing {
		for _, m := range captures {
			// Delta pruning: skip if capture can't possibly improve alpha
			// Even capturing the best piece won't be enough
			if standPat+pieceValue(m.Captured)+200 < alpha {
				continue
			}

			b.MakeMove(&m)
			score := quiescence(b, alpha, beta, false, qdepth+1)
			b.UnmakeMove(&m)

			if score >= beta {
				return beta
			}
			if score > alpha {
				alpha = score
			}
		}
		return alpha
	} else {
		for _, m := range captures {
			// Delta pruning for minimizing
			if standPat-pieceValue(m.Captured)-200 > beta {
				continue
			}

			b.MakeMove(&m)
			score := quiescence(b, alpha, beta, true, qdepth+1)
			b.UnmakeMove(&m)

			if score <= alpha {
				return alpha
			}
			if score < beta {
				beta = score
			}
		}
		return beta
	}
}

// pieceValue returns the material value of a piece for delta pruning
func pieceValue(p Piece) int {
	switch p {
	case WPawn, BPawn:
		return 100
	case WKnight, BKnight:
		return 320
	case WBishop, BBishop:
		return 330
	case WRook, BRook:
		return 500
	case WQueen, BQueen:
		return 900
	default:
		return 0
	}
}

// ============================================================================
// Killer Moves and History Heuristic
// ============================================================================

// Killer move table: 2 slots per ply (depth from root)
// Non-capture moves that caused beta cutoffs
const MaxPly = 64

var killerMoves [MaxPly][2]Move

// History table: [side][from][to] - tracks move success
// Higher values = moves that frequently cause cutoffs
var historyTable [2][64][64]int

// recordKiller stores a move that caused a beta cutoff
// Only for non-captures (captures are already highly ranked by MVV-LVA)
func recordKiller(ply int, m Move) {
	if ply >= MaxPly || m.Captured != Empty {
		return
	}
	// Don't store duplicates
	if movesEqual(killerMoves[ply][0], m) {
		return
	}
	// Shift: slot 0 -> slot 1, new move -> slot 0
	killerMoves[ply][1] = killerMoves[ply][0]
	killerMoves[ply][0] = m
}

// isKiller checks if a move is a killer move at this ply
func isKiller(ply int, m Move) bool {
	if ply >= MaxPly {
		return false
	}
	return movesEqual(killerMoves[ply][0], m) || movesEqual(killerMoves[ply][1], m)
}

// recordHistory increments history score for a move that caused cutoff
// Bonus is depth^2 to give more weight to deeper cutoffs
func recordHistory(side Color, m Move, depth int) {
	if m.Captured != Empty {
		return // Only for quiet moves
	}
	bonus := depth * depth
	historyTable[side][m.From][m.To] += bonus

	// Cap history values to prevent overflow
	if historyTable[side][m.From][m.To] > 10000 {
		// Age: divide all entries by 2 when any gets too high
		for s := 0; s < 2; s++ {
			for f := 0; f < 64; f++ {
				for t := 0; t < 64; t++ {
					historyTable[s][f][t] /= 2
				}
			}
		}
	}
}

// getHistory returns the history score for a move
func getHistory(side Color, m Move) int {
	return historyTable[side][m.From][m.To]
}

// clearKillersAndHistory resets search heuristics (call between games)
func clearKillersAndHistory() {
	for i := range killerMoves {
		killerMoves[i][0] = Move{}
		killerMoves[i][1] = Move{}
	}
	for s := 0; s < 2; s++ {
		for f := 0; f < 64; f++ {
			for t := 0; t < 64; t++ {
				historyTable[s][f][t] = 0
			}
		}
	}
}

// movesEqual compares two moves for equality (by from/to/promo)
func movesEqual(a, b Move) bool {
	return a.From == b.From && a.To == b.To && a.Promotion == b.Promotion
}

func newTaskID() uint64                      { return taskIDCounter.Add(1) }
func cancelSiblings(parentID uint64)         { cancelledParents.Store(parentID, struct{}{}) }
func isCancelled(parentID uint64) bool       { _, ok := cancelledParents.Load(parentID); return ok }
func clearCancelled()                        { cancelledParents = sync.Map{} }

// parallelTask represents a node in the YBWC parallel search tree
type parallelTask struct {
	board    *Board
	move     Move   // Move that led here
	rootMove Move   // Original root move (for result tracking)
	depth    int
	alpha    int
	beta     int

	// Hierarchy tracking
	parentID uint64
	taskID   uint64

	// Result communication
	resultCh chan taskResult

	// Sibling coordination
	siblingAlpha *atomic.Int64 // Shared bound among siblings
}

// taskResult carries search results back to parent
type taskResult struct {
	taskID   uint64
	score    int
	rootMove Move
}

// deque is a double-ended queue for work-stealing
type deque struct {
	mu    sync.Mutex
	items []parallelTask
}

func pushTop(d *deque, t parallelTask) {
	d.mu.Lock()
	d.items = append(d.items, t)
	d.mu.Unlock()
}

func popTop(d *deque) (parallelTask, bool) {
	d.mu.Lock()
	l := len(d.items)
	if l == 0 {
		d.mu.Unlock()
		return parallelTask{}, false
	}
	t := d.items[l-1]
	d.items = d.items[:l-1]
	d.mu.Unlock()
	return t, true
}

func stealBottom(d *deque) (parallelTask, bool) {
	d.mu.Lock()
	if len(d.items) == 0 {
		d.mu.Unlock()
		return parallelTask{}, false
	}
	t := d.items[0]
	copy(d.items[0:], d.items[1:])
	d.items = d.items[:len(d.items)-1]
	d.mu.Unlock()
	return t, true
}

func stealChunkBottom(d *deque, k int) ([]parallelTask, bool) {
	if k <= 1 {
		if t, ok := stealBottom(d); ok {
			return []parallelTask{t}, true
		}
		return nil, false
	}
	d.mu.Lock()
	n := len(d.items)
	if n == 0 {
		d.mu.Unlock()
		return nil, false
	}
	if k > n {
		k = n
	}
	chunk := make([]parallelTask, k)
	copy(chunk, d.items[:k])
	d.items = d.items[k:]
	d.mu.Unlock()
	return chunk, true
}

// SetContempt sets symmetric contempt for both sides from draw_value
// draw_value: 0.0 (draws=losses) to 0.5 (neutral) to 1.0 (draws=wins)
func SetContempt(drawValue float64) {
	contempt := int((0.5 - drawValue) * 100)
	if contempt < 0 {
		contempt = 0 // Don't reward draws
	}
	globalContempt = contempt
	globalContemptWhite = contempt
	globalContemptBlack = contempt
}

// SetAsymmetricContempt sets different contempt values for White and Black
// whiteDV/blackDV: 0.0 (aggressive) to 0.5 (neutral)
// Example: SetAsymmetricContempt(0.3, 0.5) makes White aggressive, Black neutral
func SetAsymmetricContempt(whiteDV, blackDV float64) {
	globalContemptWhite = int((0.5 - whiteDV) * 100)
	if globalContemptWhite < 0 {
		globalContemptWhite = 0
	}
	globalContemptBlack = int((0.5 - blackDV) * 100)
	if globalContemptBlack < 0 {
		globalContemptBlack = 0
	}
	// Set legacy global to max for backward compat (filters, etc.)
	globalContempt = globalContemptWhite
	if globalContemptBlack > globalContempt {
		globalContempt = globalContemptBlack
	}
}


// SearchOptions controls the work-stealing search behavior
// Adapted from concurrent-dfs/options.go
type SearchOptions struct {
	MaxWorkers             int
	MaxDepth               int
	DepthParallelThreshold int // Only parallelize below this depth
	Deterministic          bool
	TimeLimit              time.Duration // 0 = no limit

	// Contempt: centipawns added to eval to discourage draws
	// Higher contempt = more aggressive play, avoiding repetitions
	// Formula: contempt = (0.5 - draw_value) * 100
	// Range: 0 (neutral) to 50 (maximum aggression at draw_value=0)
	Contempt int

	// Scheduling knobs
	StealDepthMin     int // Don't steal tasks shallower than this
	ChunkStealSize    int // Tasks to steal at once
	QueuePressureLow  int // Deque length below which we expand aggressively
	QueuePressureHigh int // Deque length above which we restrict expansion

	// Metrics hook
	MetricsHook     func(SearchMetrics)
	MetricsInterval time.Duration
}

// SearchMetrics provides observability into the search
type SearchMetrics struct {
	NodesSearched uint64
	Steals        uint64
	StealChunks   uint64
	Cutoffs       uint64
	Pushes        uint64
	Pops          uint64
	QueueHighHits uint64
	QueueLenMax   uint64
	IdleYields    uint64
	ElapsedMs     int64
}

// SearchResult holds the result of a search
type SearchResult struct {
	BestMove Move
	Score    int
	Depth    int
	Metrics  SearchMetrics
}

// DefaultSearchOptions returns sensible defaults
func DefaultSearchOptions(workers int) SearchOptions {
	if workers <= 0 {
		workers = runtime.NumCPU()
	}
	return SearchOptions{
		MaxWorkers:             workers,
		MaxDepth:               6,
		DepthParallelThreshold: 3, // Parallelize top 3 ply
		Deterministic:          false,
		TimeLimit:              0,
		Contempt:               0, // Neutral by default
		StealDepthMin:          1,
		ChunkStealSize:         2,
		QueuePressureLow:       4,
		QueuePressureHigh:      32,
	}
}

// ChessNode represents a node in the game tree for work-stealing traversal
type ChessNode struct {
	Board       *Board
	Move        Move  // Move that led to this position
	Depth       int   // Remaining depth to search
	Alpha       int   // Alpha bound
	Beta        int   // Beta bound
	IsMaximizer bool  // True if White to move
	PV          bool  // Principal Variation node
	Score       int   // Result (set after search)
	visited     int32 // Atomic flag
}

// Search performs iterative deepening with work-stealing parallel alpha-beta
// Aspiration window parameters
const (
	aspirationInitialWindow = 50   // Initial window size (centipawns)
	aspirationMaxWindow     = 500  // Max window before falling back to full
)

func Search(b *Board, opts SearchOptions) SearchResult {
	start := time.Now()

	ctx := context.Background()
	if opts.TimeLimit > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, opts.TimeLimit)
		defer cancel()
	}

	result := SearchResult{
		Depth: opts.MaxDepth,
	}

	prevScore := 0 // Score from previous iteration for aspiration

	// Iterative deepening
	for depth := 1; depth <= opts.MaxDepth; depth++ {
		select {
		case <-ctx.Done():
			goto done
		default:
		}

		var score int
		var move Move

		// At depth 1-2, just do sequential search with full window
		if depth <= 2 || opts.MaxWorkers <= 1 {
			if depth == 1 {
				// First iteration: full window
				score, move = sequentialAlphaBeta(b, depth, 0, -Infinity, Infinity, b.SideToMove == White, true)
			} else {
				// Aspiration window search
				score, move = aspirationSearch(b, depth, prevScore, b.SideToMove == White)
			}
			if !move.IsNull() {
				result.BestMove = move
				result.Score = score
				result.Depth = depth
				prevScore = score
			}
			continue
		}

		// Parallel search using work-stealing
		// TODO: Add aspiration to parallel search (more complex)
		move, score, metrics := parallelSearch(ctx, b, depth, opts)
		if !move.IsNull() {
			result.BestMove = move
			result.Score = score
			result.Depth = depth
			result.Metrics = metrics
			prevScore = score
		}
	}

done:
	result.Metrics.ElapsedMs = time.Since(start).Milliseconds()
	return result
}

// aspirationSearch performs search with aspiration windows
// Starts with a narrow window around expected score, widens on fail high/low
func aspirationSearch(b *Board, depth int, expected int, maximizing bool) (int, Move) {
	window := aspirationInitialWindow

	alpha := expected - window
	beta := expected + window

	for window <= aspirationMaxWindow {
		score, move := sequentialAlphaBeta(b, depth, 0, alpha, beta, maximizing, true)

		// Check if score is within window
		if score > alpha && score < beta {
			return score, move
		}

		// Fail low - widen alpha
		if score <= alpha {
			alpha = expected - window*2
			window *= 2
			continue
		}

		// Fail high - widen beta
		if score >= beta {
			beta = expected + window*2
			window *= 2
			continue
		}
	}

	// Window got too wide, fall back to full search
	return sequentialAlphaBeta(b, depth, 0, -Infinity, Infinity, maximizing, true)
}

// Null-move reduction depth
const NullMoveR = 3

// sequentialAlphaBeta is the standard recursive alpha-beta
// canNullMove prevents consecutive null-move searches
func sequentialAlphaBeta(b *Board, depth, ply int, alpha, beta int, maximizing bool, canNullMove bool) (int, Move) {
	origAlpha := alpha

	// Check for repetition - penalized based on side-specific contempt
	// Repetition = draw, which is bad when trying to win
	if b.IsRepetition() {
		// Scale penalty by side's contempt: at max (50cp), treat as -500cp
		// This makes the engine avoid repetitive lines based on its aggression level
		contempt := getContempt(b.SideToMove)
		penalty := contempt * 10 // Max: 50 * 10 = 500cp

		// Penalize whoever creates the repetition:
		// - White maximizes, so negative is bad for White
		// - Black minimizes, so positive is bad for Black
		if b.SideToMove == White {
			return -penalty, Move{} // Bad for White (maximizer)
		}
		return penalty, Move{} // Bad for Black (minimizer)
	}

	// Transposition table probe
	hash := b.ZobristHash()
	ttScore, ttMove, hit := ttProbe(hash, depth, alpha, beta, b.SideToMove)
	if hit {
		return ttScore, ttMove
	}

	if b.IsCheckmate() || b.IsDraw() {
		return Evaluate(b), Move{}
	}

	if depth == 0 {
		// Quiescence search: continue searching captures until position is quiet
		return quiescence(b, alpha, beta, maximizing, 0), Move{}
	}

	inCheck := b.InCheck()

	// Null-move pruning: try passing to see if position is so good we can prune
	// Conditions: not in check, have non-pawn material (avoid zugzwang), sufficient depth
	if canNullMove && !inCheck && depth >= NullMoveR && b.HasNonPawnMaterial(b.SideToMove) {
		// Make null move (pass)
		nullInfo := b.MakeNullMove()

		// Search with reduced depth and flipped maximizing
		// Use zero-window around beta for efficiency
		var nullScore int
		if maximizing {
			nullScore, _ = sequentialAlphaBeta(b, depth-NullMoveR, ply+1, beta-1, beta, false, false)
		} else {
			nullScore, _ = sequentialAlphaBeta(b, depth-NullMoveR, ply+1, alpha, alpha+1, true, false)
		}

		b.UnmakeNullMove(nullInfo)

		// If null-move search fails high, prune this node
		if maximizing && nullScore >= beta {
			return beta, Move{}
		}
		if !maximizing && nullScore <= alpha {
			return alpha, Move{}
		}
	}

	// Generate and order moves with all heuristics (TT move, MVV-LVA, killers, history)
	moves := OrderMovesWithHeuristics(b, b.GenerateLegalMoves(), ply, ttMove)
	if len(moves) == 0 {
		if b.InCheck() {
			// Side to move is checkmated - bad for them, good for opponent
			// Return value is from White's perspective (positive = White winning)
			// Use depth bonus to prefer FASTER checkmates (higher remaining depth = closer to root)
			if b.SideToMove == White {
				return -100000 - depth, Move{} // White mated: more negative for faster mates (Black prefers faster)
			}
			return 100000 + depth, Move{} // Black mated: more positive for faster mates (White prefers faster)
		}
		return 0, Move{} // Stalemate
	}

	var bestMove Move
	var bestScore int

	// LMR parameters
	const lmrFullDepthMoves = 4  // Search first N moves at full depth
	const lmrReductionLimit = 3  // Minimum depth to apply LMR
	const lmrReduction = 1       // Depth reduction for late moves

	if maximizing {
		bestScore = -Infinity
		for i, m := range moves {
			b.MakeMove(&m)

			var eval int
			// LMR: reduce depth for late quiet moves
			if i >= lmrFullDepthMoves && depth >= lmrReductionLimit && m.Captured == Empty && m.Promotion == Empty && !inCheck {
				// Search at reduced depth first
				eval, _ = sequentialAlphaBeta(b, depth-1-lmrReduction, ply+1, alpha, beta, false, true)
				// If it looks good, re-search at full depth
				if eval > alpha {
					eval, _ = sequentialAlphaBeta(b, depth-1, ply+1, alpha, beta, false, true)
				}
			} else {
				eval, _ = sequentialAlphaBeta(b, depth-1, ply+1, alpha, beta, false, true)
			}

			b.UnmakeMove(&m)

			if eval > bestScore {
				bestScore = eval
				bestMove = m
			}
			alpha = max(alpha, eval)
			if beta <= alpha {
				// Beta cutoff - record killer and history
				recordKiller(ply, m)
				recordHistory(b.SideToMove, m, depth)
				break
			}
		}
	} else {
		bestScore = Infinity
		for i, m := range moves {
			b.MakeMove(&m)

			var eval int
			// LMR: reduce depth for late quiet moves
			if i >= lmrFullDepthMoves && depth >= lmrReductionLimit && m.Captured == Empty && m.Promotion == Empty && !inCheck {
				// Search at reduced depth first
				eval, _ = sequentialAlphaBeta(b, depth-1-lmrReduction, ply+1, alpha, beta, true, true)
				// If it looks good, re-search at full depth
				if eval < beta {
					eval, _ = sequentialAlphaBeta(b, depth-1, ply+1, alpha, beta, true, true)
				}
			} else {
				eval, _ = sequentialAlphaBeta(b, depth-1, ply+1, alpha, beta, true, true)
			}

			b.UnmakeMove(&m)

			if eval < bestScore {
				bestScore = eval
				bestMove = m
			}
			beta = min(beta, eval)
			if beta <= alpha {
				// Alpha cutoff - record killer and history
				recordKiller(ply, m)
				recordHistory(b.SideToMove, m, depth)
				break
			}
		}
	}

	// Transposition table store
	var flag uint8
	if maximizing {
		if bestScore <= origAlpha {
			flag = TTFlagUpper // Failed low - upper bound
		} else if bestScore >= beta {
			flag = TTFlagLower // Failed high - lower bound
		} else {
			flag = TTFlagExact // Exact score
		}
	} else {
		// For minimizing: logic is inverted
		if bestScore >= beta {
			flag = TTFlagLower
		} else if bestScore <= origAlpha {
			flag = TTFlagUpper
		} else {
			flag = TTFlagExact
		}
	}
	ttStore(hash, depth, bestScore, flag, bestMove)

	return bestScore, bestMove
}

// processNodeParallel handles a node in the parallel search tree using YBWC
// It searches the first child sequentially (to establish bounds), then pushes
// remaining children to deques for parallel processing, and waits for results.
func processNodeParallel(t parallelTask, deques []*deque, workerID int, opts SearchOptions,
	tasksWG *sync.WaitGroup, rootSide Color) int {

	moves := OrderMoves(t.board, t.board.GenerateLegalMoves())
	if len(moves) == 0 {
		if t.board.InCheck() {
			// Checkmate - return from White's perspective
			if t.board.SideToMove == White {
				return -100000 - t.depth
			}
			return 100000 + t.depth
		}
		return 0 // Stalemate
	}

	// YBWC: Search first child sequentially to establish bounds
	firstBoard := t.board.Copy()
	firstBoard.MakeMove(&moves[0])

	var bestScore int
	if t.depth-1 <= opts.DepthParallelThreshold {
		// Below threshold - finish first child sequentially
		bestScore, _ = sequentialAlphaBeta(firstBoard, t.depth-1, 0, -t.beta, -t.alpha,
			firstBoard.SideToMove == White, true)
	} else {
		// Above threshold - recurse with parallel processing
		// Create a result channel for this single child
		childCh := make(chan taskResult, 1)
		childTask := parallelTask{
			board:        firstBoard,
			move:         moves[0],
			rootMove:     t.rootMove,
			depth:        t.depth - 1,
			alpha:        -t.beta,
			beta:         -t.alpha,
			parentID:     t.taskID,
			taskID:       newTaskID(),
			resultCh:     childCh,
			siblingAlpha: nil,
		}
		// Process inline (we wait for it anyway)
		score := processNodeParallel(childTask, deques, workerID, opts, tasksWG, rootSide)
		bestScore = score
	}
	bestScore = -bestScore // Negamax

	if bestScore >= t.beta {
		return bestScore // Beta cutoff
	}

	alpha := t.alpha
	if bestScore > alpha {
		alpha = bestScore
	}

	if len(moves) == 1 {
		return bestScore
	}

	// Push remaining children to deques for parallel processing
	childResultCh := make(chan taskResult, len(moves)-1)
	siblingAlpha := &atomic.Int64{}
	siblingAlpha.Store(int64(alpha))

	childCount := 0
	for _, m := range moves[1:] {
		childBoard := t.board.Copy()
		childBoard.MakeMove(&m)

		tasksWG.Add(1)
		childCount++

		pushTop(deques[workerID], parallelTask{
			board:        childBoard,
			move:         m,
			rootMove:     t.rootMove,
			depth:        t.depth - 1,
			alpha:        -t.beta,
			beta:         -alpha,
			parentID:     t.taskID,
			taskID:       newTaskID(),
			resultCh:     childResultCh,
			siblingAlpha: siblingAlpha,
		})
	}

	// Wait for children while helping process other tasks (avoid deadlock)
	resultsReceived := 0
	cutoffFound := false
	for resultsReceived < childCount {
		select {
		case result := <-childResultCh:
			resultsReceived++
			if cutoffFound {
				continue // Just draining
			}
			childScore := -result.score // Negamax

			if childScore > bestScore {
				bestScore = childScore
			}
			if bestScore >= t.beta {
				// Cutoff - cancel remaining siblings
				cancelSiblings(t.taskID)
				cutoffFound = true
				continue
			}
			if bestScore > alpha {
				alpha = bestScore
				siblingAlpha.Store(int64(alpha))
			}
		default:
			// No result available - help by processing a task
			helpProcessTask(deques, workerID, opts, tasksWG, rootSide)
		}
	}

	return bestScore
}

// helpProcessTask tries to pop or steal a task and process it inline
// This prevents deadlock when all workers are waiting for children
func helpProcessTask(deques []*deque, workerID int, opts SearchOptions,
	tasksWG *sync.WaitGroup, rootSide Color) {

	// Try our own deque first
	if task, ok := popTop(deques[workerID]); ok {
		processTaskInline(task, deques, workerID, opts, tasksWG, rootSide)
		return
	}

	// Try stealing from other workers
	for i := 0; i < opts.MaxWorkers; i++ {
		victim := (workerID + i + 1) % opts.MaxWorkers
		if victim == workerID {
			continue
		}
		if task, ok := stealBottom(deques[victim]); ok {
			processTaskInline(task, deques, workerID, opts, tasksWG, rootSide)
			return
		}
	}

	// Nothing to do - yield to let other goroutines run
	runtime.Gosched()
}

// processTaskInline processes a single task, sending result to its channel
func processTaskInline(task parallelTask, deques []*deque, workerID int,
	opts SearchOptions, tasksWG *sync.WaitGroup, rootSide Color) {

	// Check cancellation
	if task.parentID != 0 && isCancelled(task.parentID) {
		task.resultCh <- taskResult{taskID: task.taskID, score: -Infinity, rootMove: task.rootMove}
		tasksWG.Done()
		return
	}

	// Check sibling alpha improvement
	if task.siblingAlpha != nil {
		newAlpha := int(task.siblingAlpha.Load())
		if newAlpha > task.alpha {
			task.alpha = newAlpha
		}
	}

	var score int
	if task.depth > opts.DepthParallelThreshold {
		score = processNodeParallel(task, deques, workerID, opts, tasksWG, rootSide)
	} else {
		score, _ = sequentialAlphaBeta(task.board, task.depth, 0, task.alpha, task.beta,
			task.board.SideToMove == White, true)
	}

	task.resultCh <- taskResult{taskID: task.taskID, score: score, rootMove: task.rootMove}
	tasksWG.Done()
}

// parallelSearch uses work-stealing for the top few ply
// Based on Young Brothers Wait Concept (YBWC)
// Workers push children to deques, allowing work to flow DOWN the tree
func parallelSearch(ctx context.Context, b *Board, maxDepth int, opts SearchOptions) (Move, int, SearchMetrics) {
	clearCancelled() // Reset cancellation state

	// Generate root moves
	moves := OrderMoves(b, b.GenerateLegalMoves())
	if len(moves) == 0 {
		return Move{}, 0, SearchMetrics{}
	}

	rootSide := b.SideToMove

	// Search first move sequentially (YBWC: establishes good bounds)
	firstBoard := b.Copy()
	firstBoard.MakeMove(&moves[0])
	firstScore, _ := sequentialAlphaBeta(firstBoard, maxDepth-1, 0, -Infinity, Infinity, firstBoard.SideToMove == White, true)
	// Negate to get score from root side's perspective
	if rootSide == Black {
		firstScore = -firstScore
	}

	bestMove := moves[0]
	bestScore := firstScore
	alpha := firstScore

	// Remaining moves searched in parallel with work-stealing
	if len(moves) <= 1 {
		return bestMove, bestScore, SearchMetrics{NodesSearched: 1}
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	// Per-worker deques
	deques := make([]*deque, opts.MaxWorkers)
	for i := range deques {
		deques[i] = &deque{items: make([]parallelTask, 0, 64)}
	}

	var metrics SearchMetrics
	var tasksWG, workersWG sync.WaitGroup

	// Root result channel
	rootResultCh := make(chan taskResult, len(moves)-1)

	// Worker function - now uses YBWC with processNodeParallel
	worker := func(id int) {
		defer workersWG.Done()

		var rnd *rand.Rand
		if !opts.Deterministic {
			rnd = rand.New(rand.NewSource(time.Now().UnixNano() + int64(id)))
		}

		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			// Try local deque first
			if t, ok := popTop(deques[id]); ok {
				atomic.AddUint64(&metrics.Pops, 1)

				// Check cancellation (sibling found cutoff)
				if t.parentID != 0 && isCancelled(t.parentID) {
					t.resultCh <- taskResult{taskID: t.taskID, score: -Infinity, rootMove: t.rootMove}
					tasksWG.Done()
					continue
				}

				// Check sibling alpha improvement
				if t.siblingAlpha != nil {
					newAlpha := int(t.siblingAlpha.Load())
					if newAlpha > t.alpha {
						t.alpha = newAlpha
					}
				}

				var score int
				if t.depth > opts.DepthParallelThreshold {
					// Above threshold: use YBWC parallel processing
					score = processNodeParallel(t, deques, id, opts, &tasksWG, rootSide)
				} else {
					// Below threshold: finish subtree sequentially
					score, _ = sequentialAlphaBeta(t.board, t.depth, 0, t.alpha, t.beta,
						t.board.SideToMove == White, true)
				}
				atomic.AddUint64(&metrics.NodesSearched, 1)

				// Send result to parent
				t.resultCh <- taskResult{taskID: t.taskID, score: score, rootMove: t.rootMove}
				tasksWG.Done()
				continue
			}

			// Try to steal work
			stole := false
			if opts.Deterministic {
				for v := 0; v < opts.MaxWorkers; v++ {
					if v == id {
						continue
					}
					if opts.ChunkStealSize > 1 {
						if chunk, ok := stealChunkBottom(deques[v], opts.ChunkStealSize); ok {
							stole = true
							atomic.AddUint64(&metrics.Steals, uint64(len(chunk)))
							atomic.AddUint64(&metrics.StealChunks, 1)
							for _, ct := range chunk {
								pushTop(deques[id], ct)
							}
							break
						}
					} else {
						if st, ok := stealBottom(deques[v]); ok {
							stole = true
							atomic.AddUint64(&metrics.Steals, 1)
							pushTop(deques[id], st)
							break
						}
					}
				}
			} else {
				for tries := 0; tries < opts.MaxWorkers-1; tries++ {
					v := rnd.Intn(opts.MaxWorkers)
					if v == id {
						continue
					}
					if opts.ChunkStealSize > 1 {
						if chunk, ok := stealChunkBottom(deques[v], opts.ChunkStealSize); ok {
							stole = true
							atomic.AddUint64(&metrics.Steals, uint64(len(chunk)))
							atomic.AddUint64(&metrics.StealChunks, 1)
							for _, ct := range chunk {
								pushTop(deques[id], ct)
							}
							break
						}
					} else {
						if st, ok := stealBottom(deques[v]); ok {
							stole = true
							atomic.AddUint64(&metrics.Steals, 1)
							pushTop(deques[id], st)
							break
						}
					}
				}
			}

			if !stole {
				select {
				case <-ctx.Done():
					return
				default:
					time.Sleep(50 * time.Microsecond)
					atomic.AddUint64(&metrics.IdleYields, 1)
				}
			}
		}
	}

	// Seed remaining root moves into deques (round-robin distribution)
	for i, m := range moves[1:] {
		moveBoard := b.Copy()
		moveBoard.MakeMove(&m)
		tasksWG.Add(1)
		dequeIdx := i % opts.MaxWorkers
		pushTop(deques[dequeIdx], parallelTask{
			board:        moveBoard,
			move:         m,
			rootMove:     m, // This IS the root move
			depth:        maxDepth - 1,
			alpha:        -Infinity,
			beta:         -alpha,
			parentID:     0, // Root level
			taskID:       newTaskID(),
			resultCh:     rootResultCh,
			siblingAlpha: nil, // Root moves use sharedAlpha differently
		})
		atomic.AddUint64(&metrics.Pushes, 1)
	}

	// Start workers
	for i := 0; i < opts.MaxWorkers; i++ {
		workersWG.Add(1)
		go worker(i)
	}

	// Wait for all tasks to complete
	go func() {
		tasksWG.Wait()
		cancel()
		close(rootResultCh)
	}()

	workersWG.Wait()

	// Collect root results
	for r := range rootResultCh {
		score := -r.score // Negamax: child's score is negated
		if rootSide == Black {
			// Additional negation for Black root (scores are from White's perspective)
			score = -score
		}
		if score > bestScore {
			bestScore = score
			bestMove = r.rootMove
		}
	}

	return bestMove, bestScore, metrics
}

// SearchWithBook performs search with opening book integration
// This is the main entry point for the hybrid system:
// - Uses probabilistic book selection in opening (weighted by master game frequency)
// - Filters weakening moves (blunder avoidance)
// - Uses graduated depth based on ply
// - Adds book bonus to move scores
func SearchWithBook(b *Board, ply int, configuredDepth int, workers int) SearchResult {
	// Generate and filter moves
	moves := b.GenerateLegalMoves()
	if len(moves) == 0 {
		return SearchResult{}
	}

	// Blunder filter disabled - let search evaluate aggression properly
	// moves = FilterWeakeningMoves(b, moves, ply)

	// Filter out repetition moves when this side has contempt active
	// This prevents the engine from playing into threefold repetition
	sideContempt := getContempt(b.SideToMove)
	if sideContempt > 0 && len(moves) > 1 {
		var nonRepMoves []Move
		for _, m := range moves {
			bc := b.Copy()
			bc.MakeMove(&m)
			if !bc.IsRepetition() {
				nonRepMoves = append(nonRepMoves, m)
			}
		}
		if len(nonRepMoves) > 0 {
			moves = nonRepMoves
		}
	}

	// Early game (ply 1-36): Use probabilistic book selection with sanity check
	// Extended from 24 to 36 to use book knowledge deeper into the game
	if ply <= 36 && globalBook != nil {
		if bookMove, ok := globalBook.PickBookMove(b, moves, ply); ok {
			bc := b.Copy()
			bc.MakeMove(&bookMove)
			bookScore, _ := sequentialAlphaBeta(bc, 3, 0, -Infinity, Infinity, bc.SideToMove == White, true)
			if b.SideToMove == Black {
				bookScore = -bookScore
			}

			// Sanity check: reject book moves that are significantly worse than search
			// This filters out dubious gambits that lose material
			if bookScore < -150 { // More than 1.5 pawns worse = reject
				// Fall through to regular search
			} else {
				return SearchResult{
					BestMove: bookMove,
					Score:    bookScore,
					Depth:    3,
				}
			}
		}
	}

	// Not in book or past opening - use search
	fen := b.ToFEN()
	searchDepth := GetDepthForPly(ply, configuredDepth)
	bookBonus := GetBookBonusForPly(ply)

	// Order moves (MVV-LVA)
	moves = OrderMoves(b, moves)

	// If we have book bonus, do move-by-move evaluation with bonus
	if bookBonus > 0 && globalBook != nil {
		return searchWithBookBonus(b, fen, moves, searchDepth, bookBonus, workers)
	}

	// Standard search (no book bonus)
	opts := DefaultSearchOptions(workers)
	opts.MaxDepth = searchDepth
	result := Search(b, opts)

	// TRAINING MODE: Use temperature-based exploration
	// This prevents identical self-play games (AlphaZero/Leela approach)
	// Temperature = 1.0 for first 20 ply, decays to 0.1 by ply 30
	if trainingMode && ply <= 30 && len(moves) > 1 {
		temperature := 1.0
		if ply > 20 {
			// Linear decay: 1.0 at ply 20, 0.1 at ply 30
			temperature = 1.0 - 0.09*float64(ply-20)
		}

		// Get top 5 moves with quick evaluation for softmax
		topMoves, topScores := getTopMovesWithScores(b, moves, 5)
		if len(topMoves) > 1 {
			selected := SoftmaxSelect(topMoves, topScores, temperature)
			// Find the score for selected move
			selectedScore := topScores[0]
			for i, m := range topMoves {
				if m.From == selected.From && m.To == selected.To {
					selectedScore = topScores[i]
					break
				}
			}
			return SearchResult{
				BestMove: selected,
				Score:    selectedScore,
				Depth:    searchDepth,
				Metrics:  result.Metrics,
			}
		}
	}

	return result
}

// searchWithBookBonus runs ONE search and applies book bonus to move selection
// FIXED: Previous implementation called Search() for EACH move (N times), causing
// 30-minute timeouts. Now we pre-compute bonuses and run a single search.
func searchWithBookBonus(b *Board, fen string, moves []Move, depth int, bookBonus int, workers int) SearchResult {
	start := time.Now()

	// Pre-compute book bonuses ONCE (not N times!)
	bonuses := make(map[string]int)
	if globalBook != nil {
		for _, m := range moves {
			mStr := m.String()
			bonus := globalBook.GetBookBonus(fen, mStr, 0)
			if bonus == 0 && globalBook.IsInBook(fen, mStr) {
				bonus = bookBonus / 2 // Half bonus just for being in book
			}

			// Tier 2: Position penalty for where this move leads
			bc := b.Copy()
			bc.MakeMove(&m)
			bonus += globalBook.GetPositionPenalty(bc.ToFEN(), b.SideToMove)

			bonuses[mStr] = bonus
		}
	}

	// Run ONE search (the parallel search handles all moves efficiently)
	opts := DefaultSearchOptions(workers)
	opts.MaxDepth = depth
	result := Search(b, opts)

	// Check if another move with high book bonus beats the search result
	bestMove := result.BestMove
	bestTotal := result.Score + bonuses[bestMove.String()]

	// Quick sanity check: if search found a clearly best move (good score),
	// only consider book moves that aren't significantly worse
	// This prevents book bonus from overriding tactically critical moves
	for _, m := range moves {
		mStr := m.String()
		if mStr == bestMove.String() {
			continue
		}

		moveBonus := bonuses[mStr]
		if moveBonus <= 0 {
			continue // No book bonus, skip
		}

		// Quick evaluation to get this move's approximate score
		bc := b.Copy()
		bc.MakeMove(&m)
		moveScore, _ := sequentialAlphaBeta(bc, 3, 0, -Infinity, Infinity, bc.SideToMove == White, true)
		if b.SideToMove == Black {
			moveScore = -moveScore
		}

		// Only consider if not tactically bad (within 100cp of search result)
		if moveScore >= result.Score-100 {
			total := moveScore + moveBonus
			if total > bestTotal {
				bestTotal = total
				bestMove = m
			}
		}
	}

	return SearchResult{
		BestMove: bestMove,
		Score:    result.Score, // Return actual search score, not bonus-inflated
		Depth:    depth,
		Metrics: SearchMetrics{
			NodesSearched: result.Metrics.NodesSearched,
			ElapsedMs:     time.Since(start).Milliseconds(),
		},
	}
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// SoftmaxSelect picks a move using temperature-scaled probabilities
// Formula: P(move_i) = exp(score_i/τ) / Σexp(score_j/τ)
// Uses crypto/rand (getrandom syscall on Linux) for true randomness
// Research: AlphaZero/Leela Chess Zero use this for training exploration
func SoftmaxSelect(moves []Move, scores []int, temperature float64) Move {
	if len(moves) == 0 {
		return Move{}
	}
	if len(moves) == 1 || temperature <= 0.01 {
		// Greedy selection
		bestIdx := 0
		for i, s := range scores {
			if s > scores[bestIdx] {
				bestIdx = i
			}
		}
		return moves[bestIdx]
	}

	// Find max score for numerical stability (subtract from all)
	maxScore := scores[0]
	for _, s := range scores {
		if s > maxScore {
			maxScore = s
		}
	}

	// Convert scores to probabilities
	// Scale by 100 to convert centipawns to pawns for reasonable temperature effect
	weights := make([]float64, len(moves))
	var totalWeight float64
	for i, s := range scores {
		// Subtract max for numerical stability, scale to pawn units
		w := math.Exp(float64(s-maxScore) / (temperature * 100))
		weights[i] = w
		totalWeight += w
	}

	// Weighted random selection using getrandom()
	var rb [8]byte
	crand.Read(rb[:])
	r := float64(binary.LittleEndian.Uint64(rb[:])>>11) / (1 << 53) * totalWeight

	for i, w := range weights {
		r -= w
		if r <= 0 {
			return moves[i]
		}
	}
	return moves[0]
}

// getTopMovesWithScores evaluates top N moves and returns them with scores
// Used for temperature-based exploration in training mode
func getTopMovesWithScores(b *Board, moves []Move, n int) ([]Move, []int) {
	if len(moves) == 0 {
		return nil, nil
	}
	if n > len(moves) {
		n = len(moves)
	}

	// Quick evaluation of each move at shallow depth
	type scored struct {
		move  Move
		score int
	}
	var all []scored
	for _, m := range moves {
		bc := b.Copy()
		bc.MakeMove(&m)
		score, _ := sequentialAlphaBeta(bc, 3, 0, -Infinity, Infinity, bc.SideToMove == White, true)
		if b.SideToMove == Black {
			score = -score
		}
		all = append(all, scored{m, score})
	}

	// Sort by score descending
	for i := 0; i < len(all)-1; i++ {
		for j := i + 1; j < len(all); j++ {
			if all[j].score > all[i].score {
				all[i], all[j] = all[j], all[i]
			}
		}
	}

	// Return top N
	resultMoves := make([]Move, n)
	resultScores := make([]int, n)
	for i := 0; i < n; i++ {
		resultMoves[i] = all[i].move
		resultScores[i] = all[i].score
	}
	return resultMoves, resultScores
}
