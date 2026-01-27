package main

// Zobrist hashing for fast position identification
// Used for opening book lookup and transposition tables

import (
	"math/rand"
)

// Zobrist hash tables - initialized with deterministic pseudo-random values
var (
	zobristPieces     [13][64]uint64 // piece type (0-12) × square (0-63)
	zobristCastling   [16]uint64     // 4-bit castling rights (0-15)
	zobristEnPassant  [8]uint64      // en passant file (0-7)
	zobristSideToMove uint64         // XOR when black to move
)

func init() {
	// Use fixed seed for reproducibility across sessions
	// Same position must always produce same hash
	rng := rand.New(rand.NewSource(0x1234567890ABCDEF))

	// Initialize piece-square table
	for piece := 0; piece < 13; piece++ {
		for sq := 0; sq < 64; sq++ {
			zobristPieces[piece][sq] = rng.Uint64()
		}
	}

	// Initialize castling rights table
	for i := 0; i < 16; i++ {
		zobristCastling[i] = rng.Uint64()
	}

	// Initialize en passant file table
	for file := 0; file < 8; file++ {
		zobristEnPassant[file] = rng.Uint64()
	}

	// Initialize side to move
	zobristSideToMove = rng.Uint64()
}

// ZobristHash computes the Zobrist hash of the current position
func (b *Board) ZobristHash() uint64 {
	var h uint64

	// Hash pieces on squares
	for sq := Square(0); sq < 64; sq++ {
		piece := b.Squares[sq]
		if piece != Empty {
			h ^= zobristPieces[piece][sq]
		}
	}

	// Hash castling rights
	h ^= zobristCastling[b.Castling]

	// Hash en passant file (only if there's an en passant square)
	if b.EnPassant != NoSquare {
		h ^= zobristEnPassant[b.EnPassant.File()]
	}

	// Hash side to move
	if b.SideToMove == Black {
		h ^= zobristSideToMove
	}

	return h
}

// ZobristHashString returns the hash as a hex string (for JSON keys)
func (b *Board) ZobristHashString() string {
	return formatHash(b.ZobristHash())
}

// formatHash converts a uint64 hash to a 16-character hex string
func formatHash(h uint64) string {
	const hexDigits = "0123456789abcdef"
	buf := make([]byte, 16)
	for i := 15; i >= 0; i-- {
		buf[i] = hexDigits[h&0xF]
		h >>= 4
	}
	return string(buf)
}
