package main

import (
	"testing"
	"time"
)

func TestParallelSearchBasic(t *testing.T) {
	b := NewBoard() // Already sets up starting position

	opts := DefaultSearchOptions(4)
	opts.MaxDepth = 3

	start := time.Now()
	result := Search(b, opts)
	elapsed := time.Since(start)

	t.Logf("Depth 3: Best=%s Score=%d Time=%v", result.BestMove.String(), result.Score, elapsed)

	if result.BestMove.From == result.BestMove.To {
		t.Error("Search returned null move")
	}
	if elapsed > 10*time.Second {
		t.Errorf("Search took too long: %v", elapsed)
	}
}

func TestParallelSearchDepth4(t *testing.T) {
	b := NewBoard() // Already sets up starting position

	opts := DefaultSearchOptions(4)
	opts.MaxDepth = 4

	start := time.Now()
	result := Search(b, opts)
	elapsed := time.Since(start)

	t.Logf("Depth 4: Best=%s Score=%d Time=%v", result.BestMove.String(), result.Score, elapsed)

	if elapsed > 30*time.Second {
		t.Errorf("Search took too long: %v", elapsed)
	}
}

func TestParallelSearchDepth5(t *testing.T) {
	b := NewBoard()

	opts := DefaultSearchOptions(4)
	opts.MaxDepth = 5

	start := time.Now()
	result := Search(b, opts)
	elapsed := time.Since(start)

	t.Logf("Depth 5: Best=%s Score=%d Time=%v", result.BestMove.String(), result.Score, elapsed)

	if elapsed > 60*time.Second {
		t.Errorf("Search took too long: %v", elapsed)
	}
}
