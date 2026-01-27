//go:build ignore

// Lichess Opening Book Fetcher
// Run with: go run fetch_openings.go
//
// Fetches master game data from Lichess Opening Explorer API
// and saves to ~/.config/muemacs/chess_book.json
//
// Data source: Lichess Masters Database (https://lichess.org)
// License: CC0 Public Domain

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// LichessResponse is the API response structure
type LichessResponse struct {
	White   int            `json:"white"`
	Draws   int            `json:"draws"`
	Black   int            `json:"black"`
	Moves   []LichessMove  `json:"moves"`
	Opening *LichessOpening `json:"opening"`
}

type LichessMove struct {
	UCI           string          `json:"uci"`
	SAN           string          `json:"san"`
	AverageRating int             `json:"averageRating"`
	White         int             `json:"white"`
	Draws         int             `json:"draws"`
	Black         int             `json:"black"`
	Opening       *LichessOpening `json:"opening"`
}

type LichessOpening struct {
	ECO  string `json:"eco"`
	Name string `json:"name"`
}

// BookData is our cache file format
type BookData struct {
	Source    string                  `json:"source"`
	SourceURL string                  `json:"source_url"`
	License   string                  `json:"license"`
	Generated string                  `json:"generated"`
	Positions map[string]BookPosition `json:"positions"`
}

type BookPosition struct {
	FEN     string     `json:"fen"`
	ECO     string     `json:"eco,omitempty"`
	Name    string     `json:"name,omitempty"`
	Moves   []BookMove `json:"moves"`
}

type BookMove struct {
	UCI          string `json:"uci"`
	SAN          string `json:"san"`
	MasterGames  int    `json:"master_games"`
	MasterWhite  int    `json:"master_white"`
	MasterDraws  int    `json:"master_draws"`
	MasterBlack  int    `json:"master_black"`
	OurGames     int    `json:"our_games"`
	OurWins      int    `json:"our_wins"`
	OurLosses    int    `json:"our_losses"`
	OurDraws     int    `json:"our_draws"`
}

// Simple chess position tracker (minimal FEN handling)
type Position struct {
	board      [64]byte // piece placement
	sideToMove byte     // 'w' or 'b'
	castling   string   // KQkq or subsets
	enPassant  string   // e.g., "e3" or "-"
	halfmove   int
	fullmove   int
}

const initialFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

func main() {
	fmt.Println("Lichess Opening Book Fetcher")
	fmt.Println("============================")
	fmt.Println("Source: Lichess Masters Database (https://lichess.org)")
	fmt.Println("License: CC0 Public Domain")
	fmt.Println()

	book := &BookData{
		Source:    "Lichess Masters Database",
		SourceURL: "https://lichess.org",
		License:   "CC0 Public Domain",
		Generated: time.Now().Format("2006-01-02"),
		Positions: make(map[string]BookPosition),
	}

	// Crawl settings
	maxDepth := 10    // 10 plies = 5 moves per side
	maxBranch := 4    // Top 4 moves per position
	minGames := 1000  // Only include moves with 1000+ games

	fmt.Printf("Settings: depth=%d, branch=%d, min_games=%d\n\n", maxDepth, maxBranch, minGames)

	visited := make(map[string]bool)
	crawl(book, initialFEN, 0, maxDepth, maxBranch, minGames, visited)

	fmt.Printf("\nTotal positions: %d\n", len(book.Positions))

	// Save to file
	home, _ := os.UserHomeDir()
	outPath := filepath.Join(home, ".config", "muemacs", "chess_book.json")

	if err := os.MkdirAll(filepath.Dir(outPath), 0755); err != nil {
		fmt.Printf("Error creating directory: %v\n", err)
		return
	}

	data, err := json.MarshalIndent(book, "", "  ")
	if err != nil {
		fmt.Printf("Error marshaling JSON: %v\n", err)
		return
	}

	if err := os.WriteFile(outPath, data, 0644); err != nil {
		fmt.Printf("Error writing file: %v\n", err)
		return
	}

	fmt.Printf("Saved to: %s\n", outPath)

	// Also save a copy to /tmp for inspection
	tmpPath := "/tmp/go-chess-openers/chess_book.json"
	os.MkdirAll(filepath.Dir(tmpPath), 0755)
	os.WriteFile(tmpPath, data, 0644)
	fmt.Printf("Copy saved to: %s\n", tmpPath)
}

func crawl(book *BookData, fen string, depth, maxDepth, maxBranch, minGames int, visited map[string]bool) {
	if depth >= maxDepth {
		return
	}

	// Skip if already visited
	if visited[fen] {
		return
	}
	visited[fen] = true

	// Fetch from Lichess API
	resp, err := fetchLichess(fen)
	if err != nil {
		fmt.Printf("  Error fetching %s: %v\n", truncateFEN(fen), err)
		return
	}

	// Skip positions with very few games
	totalGames := resp.White + resp.Draws + resp.Black
	if totalGames < minGames {
		return
	}

	// Create position entry
	pos := BookPosition{
		FEN:   fen,
		Moves: []BookMove{},
	}
	if resp.Opening != nil {
		pos.ECO = resp.Opening.ECO
		pos.Name = resp.Opening.Name
	}

	// Process moves
	branchCount := 0
	for _, m := range resp.Moves {
		games := m.White + m.Draws + m.Black
		if games < minGames {
			continue
		}

		pos.Moves = append(pos.Moves, BookMove{
			UCI:         m.UCI,
			SAN:         m.SAN,
			MasterGames: games,
			MasterWhite: m.White,
			MasterDraws: m.Draws,
			MasterBlack: m.Black,
		})

		branchCount++
		if branchCount >= maxBranch {
			break
		}
	}

	// Only save if we have moves
	if len(pos.Moves) > 0 {
		// Use FEN as key (could use hash but FEN is more debuggable)
		book.Positions[fen] = pos

		indent := strings.Repeat("  ", depth)
		openingName := ""
		if pos.Name != "" {
			openingName = fmt.Sprintf(" (%s)", pos.Name)
		}
		fmt.Printf("%s[%d] %s%s - %d moves\n", indent, depth, truncateFEN(fen), openingName, len(pos.Moves))
	}

	// Recurse into top moves
	for i, m := range pos.Moves {
		if i >= maxBranch {
			break
		}

		newFEN := applyMove(fen, m.UCI)
		if newFEN != "" {
			crawl(book, newFEN, depth+1, maxDepth, maxBranch, minGames, visited)
		}
	}
}

func fetchLichess(fen string) (*LichessResponse, error) {
	// Rate limit - Lichess allows ~1 req/sec for unauthenticated
	time.Sleep(1100 * time.Millisecond)

	apiURL := "https://explorer.lichess.ovh/masters?fen=" + url.QueryEscape(fen)

	resp, err := http.Get(apiURL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("status %d", resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	var result LichessResponse
	if err := json.Unmarshal(body, &result); err != nil {
		return nil, err
	}

	return &result, nil
}

// applyMove applies a UCI move to a FEN and returns the new FEN
// This is a simplified implementation for common moves
func applyMove(fen, uci string) string {
	parts := strings.Split(fen, " ")
	if len(parts) < 6 {
		return ""
	}

	board := parseFENBoard(parts[0])
	side := parts[1]
	castling := parts[2]
	enPassant := parts[3]
	// halfmove := parts[4]
	// fullmove := parts[5]

	if len(uci) < 4 {
		return ""
	}

	fromFile := int(uci[0] - 'a')
	fromRank := int(uci[1] - '1')
	toFile := int(uci[2] - 'a')
	toRank := int(uci[3] - '1')

	fromSq := fromRank*8 + fromFile
	toSq := toRank*8 + toFile

	if fromSq < 0 || fromSq >= 64 || toSq < 0 || toSq >= 64 {
		return ""
	}

	piece := board[fromSq]
	// captured := board[toSq]

	// Handle promotion
	promotion := byte(0)
	if len(uci) == 5 {
		promotion = uci[4]
	}

	// Move the piece
	board[toSq] = piece
	board[fromSq] = '.'

	// Handle promotion
	if promotion != 0 {
		if side == "w" {
			board[toSq] = byte(promotion - 32) // uppercase
		} else {
			board[toSq] = promotion // lowercase
		}
	}

	// Handle castling
	if piece == 'K' || piece == 'k' {
		if uci == "e1g1" { // White kingside
			board[7] = '.'
			board[5] = 'R'
		} else if uci == "e1c1" { // White queenside
			board[0] = '.'
			board[3] = 'R'
		} else if uci == "e8g8" { // Black kingside
			board[63] = '.'
			board[61] = 'r'
		} else if uci == "e8c8" { // Black queenside
			board[56] = '.'
			board[59] = 'r'
		}
	}

	// Handle en passant capture
	if (piece == 'P' || piece == 'p') && enPassant != "-" {
		epFile := int(enPassant[0] - 'a')
		epRank := int(enPassant[1] - '1')
		epSq := epRank*8 + epFile
		if toSq == epSq {
			// Remove captured pawn
			if side == "w" {
				board[epSq-8] = '.'
			} else {
				board[epSq+8] = '.'
			}
		}
	}

	// Update castling rights
	newCastling := castling
	if piece == 'K' {
		newCastling = strings.ReplaceAll(newCastling, "K", "")
		newCastling = strings.ReplaceAll(newCastling, "Q", "")
	} else if piece == 'k' {
		newCastling = strings.ReplaceAll(newCastling, "k", "")
		newCastling = strings.ReplaceAll(newCastling, "q", "")
	} else if piece == 'R' {
		if fromSq == 0 {
			newCastling = strings.ReplaceAll(newCastling, "Q", "")
		} else if fromSq == 7 {
			newCastling = strings.ReplaceAll(newCastling, "K", "")
		}
	} else if piece == 'r' {
		if fromSq == 56 {
			newCastling = strings.ReplaceAll(newCastling, "q", "")
		} else if fromSq == 63 {
			newCastling = strings.ReplaceAll(newCastling, "k", "")
		}
	}
	// Also handle rook captures
	if toSq == 0 {
		newCastling = strings.ReplaceAll(newCastling, "Q", "")
	} else if toSq == 7 {
		newCastling = strings.ReplaceAll(newCastling, "K", "")
	} else if toSq == 56 {
		newCastling = strings.ReplaceAll(newCastling, "q", "")
	} else if toSq == 63 {
		newCastling = strings.ReplaceAll(newCastling, "k", "")
	}
	if newCastling == "" {
		newCastling = "-"
	}

	// Update en passant
	newEP := "-"
	if (piece == 'P' && fromRank == 1 && toRank == 3) {
		newEP = string(rune('a'+fromFile)) + "3"
	} else if (piece == 'p' && fromRank == 6 && toRank == 4) {
		newEP = string(rune('a'+fromFile)) + "6"
	}

	// Switch side
	newSide := "b"
	if side == "b" {
		newSide = "w"
	}

	// Build new FEN
	newBoard := boardToFEN(board)
	return fmt.Sprintf("%s %s %s %s 0 1", newBoard, newSide, newCastling, newEP)
}

func parseFENBoard(boardStr string) [64]byte {
	var board [64]byte
	for i := range board {
		board[i] = '.'
	}

	rank := 7
	file := 0
	for _, c := range boardStr {
		if c == '/' {
			rank--
			file = 0
		} else if c >= '1' && c <= '8' {
			file += int(c - '0')
		} else {
			sq := rank*8 + file
			if sq >= 0 && sq < 64 {
				board[sq] = byte(c)
			}
			file++
		}
	}
	return board
}

func boardToFEN(board [64]byte) string {
	var result strings.Builder
	for rank := 7; rank >= 0; rank-- {
		empty := 0
		for file := 0; file < 8; file++ {
			sq := rank*8 + file
			if board[sq] == '.' {
				empty++
			} else {
				if empty > 0 {
					result.WriteByte(byte('0' + empty))
					empty = 0
				}
				result.WriteByte(board[sq])
			}
		}
		if empty > 0 {
			result.WriteByte(byte('0' + empty))
		}
		if rank > 0 {
			result.WriteByte('/')
		}
	}
	return result.String()
}

func truncateFEN(fen string) string {
	parts := strings.Split(fen, " ")
	if len(parts) > 0 {
		board := parts[0]
		if len(board) > 30 {
			return board[:30] + "..."
		}
		return board
	}
	return fen
}
