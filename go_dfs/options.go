package main

import (
    "runtime"
    "time"
)

// Options controls the advanced traversal behavior
type Options struct {
    MaxWorkers             int
    DepthParallelThreshold int
    Deterministic          bool
    CancelOnError          bool
    Strategy               TraversalStrategy
    // MetricsHook, if non-nil, is called with internal counters
    MetricsHook func(Metrics)

    // Heuristic controls (optional)
    Heuristics Heuristics

    // Scheduling knobs
    StealDepthMin     int // do not steal tasks shallower than this depth
    ChunkStealSize    int // tasks to steal at once (>=1)
    QueuePressureLow  int // local deque length below which we expand aggressively
    QueuePressureHigh int // local deque length above which we restrict expansion

    // Ergonomics
    PinWorkers bool // best-effort LockOSThread for locality

    // Pipeline
    TwoStage bool // if true, decouple generation and evaluation (visitor runs in separate pool)

    // Observability
    MetricsInterval time.Duration // if > 0 and MetricsHook != nil, call periodically

    // Coarsening via estimated work
    MinWorkToParallelize int // if > 0, only parallelize siblings whose EstimateWork >= this
}

// Metrics provides lightweight observability of the traversal engine
type Metrics struct {
    TasksProcessed uint64
    Steals         uint64
    Pushes         uint64
    Pops           uint64
    StealChunks    uint64
    QueueHighHits  uint64
    QueueLenMax    uint64
    IdleYields     uint64
}

// Heuristics configures heuristic-driven behavior
type Heuristics struct {
    OrderChildren     func(*Node) []*Node
    CompareChildren   func(a, b *Node) int
    Score             func(*Node) float64
    Goal              func(*Node) bool
    Prune             func(*Node) bool
    EstimateWork      func(*Node) int
    BeamWidth         int
    LimitedDiscrepancy int
}

// DefaultOptions returns sane defaults for the given worker count
func DefaultOptions(workers int) Options {
    if workers <= 0 {
        workers = max(1, runtime.NumCPU())
    }
    return Options{
        MaxWorkers:             workers,
        DepthParallelThreshold: 12,
        Deterministic:          false,
        CancelOnError:          true,
        Strategy:               StrategyDepthFirst,
        Heuristics:             Heuristics{},
        StealDepthMin:          8,
        ChunkStealSize:         1,
        QueuePressureLow:       2,
        QueuePressureHigh:      64,
        PinWorkers:             false,
        TwoStage:               false,
    }
}
