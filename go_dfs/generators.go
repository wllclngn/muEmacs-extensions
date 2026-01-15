package main

import (
    "math/rand"
    "time"
)

// generateSkewed builds a completely left-skewed tree of given size
func generateSkewed(size int, mode DFSMode) *Tree {
    tree := NewTree(mode)
    for i := 0; i < size; i++ { tree.Insert(i) }
    return tree
}

// generateRandom builds a tree inserting random keys in [0, size*10)
func generateRandom(size int, mode DFSMode) *Tree {
    tree := NewTree(mode)
    // Use a fast PRNG; seed once per call for variety but keep deterministic
    // enough by incorporating size. This avoids crypto/rand slowness.
    r := rand.New(rand.NewSource(time.Now().UnixNano() + int64(size)))
    for i := 0; i < size; i++ {
        tree.Insert(r.Intn(size * 10))
    }
    return tree
}
