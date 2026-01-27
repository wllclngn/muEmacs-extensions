package main

import (
	"fmt"
	"strings"
)

// Unicode chess pieces
var UnicodePieces = map[Piece]rune{
	WKing:   '♔',
	WQueen:  '♕',
	WRook:   '♖',
	WBishop: '♗',
	WKnight: '♘',
	WPawn:   '♙',
	BKing:   '♚',
	BQueen:  '♛',
	BRook:   '♜',
	BBishop: '♝',
	BKnight: '♞',
	BPawn:   '♟',
	Empty:   '·',
}

// ASCII pieces (fallback)
var ASCIIPieces = map[Piece]rune{
	WKing:   'K',
	WQueen:  'Q',
	WRook:   'R',
	WBishop: 'B',
	WKnight: 'N',
	WPawn:   'P',
	BKing:   'k',
	BQueen:  'q',
	BRook:   'r',
	BBishop: 'b',
	BKnight: 'n',
	BPawn:   'p',
	Empty:   '.',
}

// RenderBoard generates a Unicode board display
func RenderBoard(b *Board, flipped bool, lastMove Move, showCoords bool) string {
	var sb strings.Builder

	pieces := UnicodePieces

	// File labels
	files := "a b c d e f g h"
	if flipped {
		files = "h g f e d c b a"
	}

	if showCoords {
		sb.WriteString("   " + files + "\n")
	}

	// Ranks
	startRank, endRank, rankStep := 7, -1, -1
	if flipped {
		startRank, endRank, rankStep = 0, 8, 1
	}

	for rank := startRank; rank != endRank; rank += rankStep {
		if showCoords {
			sb.WriteString(fmt.Sprintf("%d  ", rank+1))
		}

		startFile, endFile, fileStep := 0, 8, 1
		if flipped {
			startFile, endFile, fileStep = 7, -1, -1
		}

		for file := startFile; file != endFile; file += fileStep {
			sq := FromRankFile(rank, file)
			p := b.Squares[sq]

			// Highlight last move squares
			isLastMove := !lastMove.IsNull() && (sq == lastMove.From || sq == lastMove.To)

			pieceChar := pieces[p]
			if isLastMove {
				sb.WriteString("[" + string(pieceChar) + "]")
			} else {
				sb.WriteString(string(pieceChar) + " ")
			}
		}

		if showCoords {
			sb.WriteString(fmt.Sprintf(" %d", rank+1))
		}
		sb.WriteString("\n")
	}

	if showCoords {
		sb.WriteString("   " + files + "\n")
	}

	return sb.String()
}

// RenderGameState generates the full game state display
func RenderGameState(g *Game, showEval bool) string {
	var sb strings.Builder

	// Board
	sb.WriteString(RenderBoard(g.Board, g.Flipped, g.LastMove, true))
	sb.WriteString("\n")

	// Status line
	sideStr := "White"
	if g.Board.SideToMove == Black {
		sideStr = "Black"
	}

	if g.Board.IsCheckmate() {
		winner := "Black"
		if g.Board.SideToMove == Black {
			winner = "White"
		}
		sb.WriteString(fmt.Sprintf("CHECKMATE! %s wins.\n", winner))
	} else if g.Board.IsStalemate() {
		sb.WriteString("STALEMATE! Draw.\n")
	} else if g.Board.IsDraw() {
		sb.WriteString("DRAW (50-move rule)\n")
	} else {
		sb.WriteString(fmt.Sprintf("%s to move", sideStr))
		if g.Board.InCheck() {
			sb.WriteString(" (CHECK)")
		}
		sb.WriteString("\n")
	}

	// Evaluation
	if showEval {
		eval := Evaluate(g.Board)
		evalStr := fmt.Sprintf("%+.2f", float64(eval)/100.0)
		sb.WriteString(fmt.Sprintf("Eval: %s\n", evalStr))
	}

	// Last move
	if !g.LastMove.IsNull() {
		sb.WriteString(fmt.Sprintf("Last: %s\n", g.LastMove.String()))
	}

	// Search depth
	sb.WriteString(fmt.Sprintf("Depth: %d\n", g.SearchDepth))

	return sb.String()
}

// RenderMoveList generates a list of moves in algebraic notation
func RenderMoveList(history []Move) string {
	var sb strings.Builder

	for i := 0; i < len(history); i += 2 {
		moveNum := i/2 + 1
		whiteMove := history[i].String()
		blackMove := ""
		if i+1 < len(history) {
			blackMove = history[i+1].String()
		}

		if blackMove != "" {
			sb.WriteString(fmt.Sprintf("%d. %s %s\n", moveNum, whiteMove, blackMove))
		} else {
			sb.WriteString(fmt.Sprintf("%d. %s\n", moveNum, whiteMove))
		}
	}

	return sb.String()
}

// RenderSearchInfo generates search metrics display
func RenderSearchInfo(result SearchResult) string {
	var sb strings.Builder

	sb.WriteString(fmt.Sprintf("Best: %s | ", result.BestMove.String()))
	sb.WriteString(fmt.Sprintf("Eval: %+.2f | ", float64(result.Score)/100.0))
	sb.WriteString(fmt.Sprintf("Depth: %d | ", result.Depth))

	if result.Metrics.NodesSearched > 0 {
		nodes := result.Metrics.NodesSearched
		var nodeStr string
		if nodes > 1000000 {
			nodeStr = fmt.Sprintf("%.1fM", float64(nodes)/1000000.0)
		} else if nodes > 1000 {
			nodeStr = fmt.Sprintf("%.1fK", float64(nodes)/1000.0)
		} else {
			nodeStr = fmt.Sprintf("%d", nodes)
		}
		sb.WriteString(fmt.Sprintf("Nodes: %s | ", nodeStr))

		if result.Metrics.Steals > 0 {
			sb.WriteString(fmt.Sprintf("Steals: %d | ", result.Metrics.Steals))
		}
	}

	sb.WriteString(fmt.Sprintf("Time: %dms", result.Metrics.ElapsedMs))

	return sb.String()
}

// FormatPGN generates PGN notation for a game
func FormatPGN(history []Move, result string) string {
	var sb strings.Builder

	sb.WriteString("[Event \"μEmacs Chess\"]\n")
	sb.WriteString("[Date \"????.??.??\"]\n")
	sb.WriteString("[White \"Human\"]\n")
	sb.WriteString("[Black \"go_chess AI\"]\n")
	sb.WriteString(fmt.Sprintf("[Result \"%s\"]\n", result))
	sb.WriteString("\n")

	for i := 0; i < len(history); i += 2 {
		moveNum := i/2 + 1
		whiteMove := history[i].String()
		sb.WriteString(fmt.Sprintf("%d. %s", moveNum, whiteMove))

		if i+1 < len(history) {
			blackMove := history[i+1].String()
			sb.WriteString(fmt.Sprintf(" %s ", blackMove))
		}
	}

	sb.WriteString(result)
	sb.WriteString("\n")

	return sb.String()
}
