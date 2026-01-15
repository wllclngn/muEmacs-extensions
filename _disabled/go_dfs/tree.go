package main

import (
    "runtime"
    "sync/atomic"
)

// Tree structure supporting modes
type Tree struct {
    root *Node
    mode DFSMode

    // Stats/metadata
    nodeCount int64
    maxDepth  int32
    strategy  TraversalStrategy

    executionTime  int64 // nanoseconds
    goroutinesUsed int64
}

// Node represents a binary node
type Node struct {
    key   int
    left  *Node
    right *Node

    depth       int32
    subtreeSize int32
    visited     atomic.Bool
}

// Key returns the node key (for heuristic consumers)
func (n *Node) Key() int { return n.key }

// SubtreeSize returns the cached subtree size including this node
func (n *Node) SubtreeSize() int { return int(n.subtreeSize) }

// computeSubtreeSizes recalculates subtree sizes bottom-up and returns the size
func (tree *Tree) computeSubtreeSizes() int32 {
    var dfs func(*Node) int32
    dfs = func(n *Node) int32 {
        if n == nil { return 0 }
        l := dfs(n.left)
        r := dfs(n.right)
        n.subtreeSize = 1 + l + r
        return n.subtreeSize
    }
    return dfs(tree.root)
}

// NewTree creates a tree with specified DFS mode
func NewTree(mode DFSMode) *Tree {
    return &Tree{mode: mode, strategy: StrategyAdaptive}
}

// Insert adds a node using the original logic
func (tree *Tree) Insert(data int) {
    if tree.root == nil {
        tree.root = &Node{key: data, depth: 0}
    } else {
        tree.root.insert(data, 0)
    }
    atomic.AddInt64(&tree.nodeCount, 1)
}

func (node *Node) insert(data int, depth int32) {
    if node.left == nil || node.right == nil {
        newNode := &Node{key: data, depth: depth + 1}
        if node.left == nil {
            node.left = newNode
        } else {
            node.right = newNode
        }
        node.subtreeSize++
        return
    }
    if (data % 2) == 0 {
        if ((node.left.key % 2) == 0) && node.left.left == nil {
            node.left.left = &Node{key: data, depth: depth + 1}
        } else if ((node.left.key % 2) == 0) && node.left.right == nil {
            node.left.right = &Node{key: data, depth: depth + 1}
        } else {
            node.right.insert(data, depth+1)
        }
    } else {
        if ((node.right.key % 2) != 0) && node.right.left == nil {
            node.right.left = &Node{key: data, depth: depth + 1}
        } else if ((node.right.key % 2) != 0) && node.right.right == nil {
            node.right.right = &Node{key: data, depth: depth + 1}
        } else {
            node.left.insert(data, depth+1)
        }
    }
    node.subtreeSize++
}

// selectMode chooses optimal mode based on tree characteristics
func (tree *Tree) selectMode() DFSMode {
    if tree.mode != ModeAuto {
        return tree.mode
    }
    n := atomic.LoadInt64(&tree.nodeCount)
    if n < 100 {
        return ModeSimple
    } else if n < 10000 {
        return ModeAdvanced
    }
    return ModeAdvanced
}

// calculateSemaphoreSize determines optimal goroutine limit with safety bounds
func (tree *Tree) calculateSemaphoreSize(mode DFSMode) int {
    maxCPUs := runtime.NumCPU()
    n := atomic.LoadInt64(&tree.nodeCount)
    var size int
    switch mode {
    case ModeSimple:
        size = min(20, maxCPUs*2)
    case ModeAdvanced, ModeAuto:
        if n < 100 {
            size = min(10, maxCPUs)
        } else if n < 1000 {
            size = min(maxCPUs*2, 32)
        } else {
            size = min(maxCPUs*3, 64)
        }
    }
    if size < 1 {
        size = 1
    }
    if size > 128 {
        size = 128
    }
    return size
}

// resetVisited clears visited flags
func (tree *Tree) resetVisited(node *Node) {
    if node == nil {
        return
    }
    node.visited.Store(false)
    tree.resetVisited(node.left)
    tree.resetVisited(node.right)
}

// helpers
func min(a, b int) int { if a < b { return a }; return b }
func max(a, b int) int { if a > b { return a }; return b }
