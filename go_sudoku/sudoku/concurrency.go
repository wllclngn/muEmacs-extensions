// Active concurrency logic (flattened layout)
package sudoku

import (
    "runtime"
    "sync/atomic"
    "time"
)

// solveConcurrent launches multiple core strategies in parallel and returns
// once the first strategy succeeds or the timeout elapses. It purposely limits
// the number of racing strategies to avoid oversubscription on low core counts.
func (s *Solver) solveConcurrent() bool {
    numStrategies := min(3, runtime.NumCPU())
    strategies := []Strategy{StrategyBasic, StrategyConstraint, StrategyHeuristic}

    // Signal when all workers are complete (for early exit if no solution).
    done := make(chan struct{})

    for i := 0; i < numStrategies; i++ {
        s.workerPool.Add(1)
        select {
        case s.semaphore <- struct{}{}:
            go func(strat Strategy) {
                defer func() { <-s.semaphore; s.workerPool.Done() }()
                s.concurrentSolverWorker(strat)
            }(strategies[i])
        default:
            // Semaphore saturated; skip spawning to prevent unbounded goroutines.
            s.workerPool.Done()
            atomic.AddUint64(&s.stats.DeadlocksAvoided, 1)
        }
    }

    // Wait in a separate goroutine; when all workers are finished, close 'done'.
    // We intentionally never close s.solutions to avoid races with in-flight sends.
    go func() {
        s.workerPool.Wait()
        close(done)
    }()

    select {
    case solution, ok := <-s.solutions:
        if ok {
            s.grid = solution
            return true
        }
    case <-done:
        // All workers completed without producing a solution.
        return false
    case <-time.After(s.concurrentTimeout):
        return false
    }
    return false
}

// concurrentSolverWorker executes a single strategy on an isolated clone and
// attempts to publish the first successful solution.
func (s *Solver) concurrentSolverWorker(strategy Strategy) {
    solver := s.cloneForWorker(strategy)
    var solved bool
    switch strategy {
    case StrategyBasic:
        solved = solver.solveBasic(0, 0)
    case StrategyConstraint:
        solved = solver.solveWithConstraints()
    case StrategyHeuristic:
        solved = solver.solveWithHeuristics()
    }
    if solved && s.firstSolution.CompareAndSwap(false, true) {
        select {
        case s.solutions <- solver.grid:
            atomic.AddUint64(&s.stats.ConcurrentTasks, 1)
        default:
        }
    }
}

// cloneForWorker creates an isolated copy for concurrent racing without sharing
// synchronization primitives or channels.
func (s *Solver) cloneForWorker(strategy Strategy) *Solver {
    clone := &Solver{}
    clone.grid = s.grid
    clone.original = s.original
    clone.candidates = s.candidates
    clone.rowMask = s.rowMask
    clone.colMask = s.colMask
    clone.boxMask = s.boxMask
    clone.strategy = strategy
    clone.difficulty = s.difficulty
    clone.autoStrategy = false
    clone.advancedStrategies = s.advancedStrategies
    clone.stats = Stats{}
    clone.startTime = time.Now()
    clone.semaphore = nil
    clone.solutions = make(chan Grid, 1)
    clone.firstSolution.Store(false)
    clone.concurrentTimeout = s.concurrentTimeout
    clone.patternStats = s.patternStats
    clone.avgComplexity = s.avgComplexity
    clone.totalSolved = s.totalSolved
    return clone
}
