package main

import (
    "context"
    "math/rand"
    "runtime"
    "strings"
    "sync"
    "time"
)

// TraverseConcurrent is the unified entry point for all modes
func (tree *Tree) TraverseConcurrent() {
    mode := tree.selectMode()
    start := time.Now()

    // Create semaphore sized appropriately for mode
    semaphoreSize := tree.calculateSemaphoreSize(mode)
    semaphore := make(chan struct{}, semaphoreSize)

    if mode == ModeSimple {
        var wg sync.WaitGroup
        wg.Add(1)
        go tree.root.simpleTraversal(&wg, semaphore)
        wg.Wait()
        tree.executionTime = time.Since(start).Nanoseconds()
        return
    }

    // Advanced/Auto: Use the advanced scheduler with options
    tree.resetVisited(tree.root)
    opts := DefaultOptions(semaphoreSize)
    opts.Strategy = StrategyDepthFirst
    _ = tree.Traverse(context.Background(), opts, nil)
    tree.executionTime = time.Since(start).Nanoseconds()
}

// simpleTraversal implements non-blocking semaphore DFS with sync fallback
func (node *Node) simpleTraversal(wg *sync.WaitGroup, semaphore chan struct{}) {
    defer wg.Done()
    if node == nil {
        return
    }
    if node.left != nil {
        wg.Add(1)
        select {
        case semaphore <- struct{}{}:
            go func(left *Node) {
                defer func() { <-semaphore }()
                left.simpleTraversal(wg, semaphore)
            }(node.left)
        default:
            node.left.simpleTraversal(wg, semaphore)
        }
    }
    if node.right != nil {
        wg.Add(1)
        select {
        case semaphore <- struct{}{}:
            go func(right *Node) {
                defer func() { <-semaphore }()
                right.simpleTraversal(wg, semaphore)
            }(node.right)
        default:
            node.right.simpleTraversal(wg, semaphore)
        }
    }
}

// Traverse executes an advanced, context-aware parallel DFS using per-worker
// deques with opportunistic work-stealing. The visitor is optional; if nil,
// nodes are just traversed. Returns the first visitor error or context error.
func (tree *Tree) Traverse(ctx context.Context, opts Options, visit func(*Node) error) error {
    if tree.root == nil {
        return nil
    }
    if opts.MaxWorkers <= 0 { opts.MaxWorkers = max(1, runtime.NumCPU()) }
    if opts.DepthParallelThreshold <= 0 { opts.DepthParallelThreshold = 12 }

    // Ensure subtree sizes are available for coarsening/heuristics
    tree.computeSubtreeSizes()

    ctx, cancel := context.WithCancel(ctx)
    defer cancel()

    type task struct { n *Node; depth int }
    type deque struct { mu sync.Mutex; items []task }
    pushTop := func(d *deque, t task) { d.mu.Lock(); d.items = append(d.items, t); d.mu.Unlock() }
    popTop := func(d *deque) (task, bool) {
        d.mu.Lock(); l := len(d.items)
        if l == 0 { d.mu.Unlock(); return task{}, false }
        t := d.items[l-1]; d.items = d.items[:l-1]; d.mu.Unlock(); return t, true
    }
    stealBottom := func(d *deque) (task, bool) {
        d.mu.Lock(); if len(d.items) == 0 { d.mu.Unlock(); return task{}, false }
        t := d.items[0]; copy(d.items[0:], d.items[1:]); d.items = d.items[:len(d.items)-1]; d.mu.Unlock(); return t, true
    }
    stealChunkBottom := func(d *deque, k int) ([]task, bool) {
        if k <= 1 { if t, ok := stealBottom(d); ok { return []task{t}, true }; return nil, false }
        d.mu.Lock(); n := len(d.items); if n == 0 { d.mu.Unlock(); return nil, false }
        if k > n { k = n }
        chunk := make([]task, k); copy(chunk, d.items[:k]); d.items = d.items[k:]; d.mu.Unlock(); return chunk, true
    }

    deques := make([]*deque, opts.MaxWorkers)
    for i := range deques { deques[i] = &deque{items: make([]task, 0, 64)} }
    var tasksWG, workersWG sync.WaitGroup
    var metrics Metrics; var metricsMu sync.Mutex
    var setErrOnce sync.Once; var firstErr error
    setErr := func(err error) { if err == nil { return }; setErrOnce.Do(func(){ firstErr = err; if opts.CancelOnError { cancel() } }) }

    // Optional periodic metrics hook
    if opts.MetricsHook != nil && opts.MetricsInterval > 0 {
        ticker := time.NewTicker(opts.MetricsInterval)
        go func() {
            defer ticker.Stop()
            for {
                select {
                case <-ctx.Done():
                    return
                case <-ticker.C:
                    m := metrics
                    opts.MetricsHook(m)
                }
            }
        }()
    }

    // Optional two-stage evaluation channel
    var evalCh chan *Node
    var evalWG sync.WaitGroup
    if opts.TwoStage && visit != nil {
        evalCh = make(chan *Node, opts.MaxWorkers*4)
        // Evaluation workers
        for i := 0; i < opts.MaxWorkers; i++ {
            evalWG.Add(1)
            go func() {
                defer evalWG.Done()
                for {
                    select {
                    case <-ctx.Done():
                        return
                    case n, ok := <-evalCh:
                        if !ok { return }
                        if visit != nil {
                            if err := visit(n); err != nil { setErr(err) }
                        }
                    }
                }
            }()
        }
    }

    worker := func(id int) {
        defer workersWG.Done()
        if opts.PinWorkers {
            runtime.LockOSThread()
            defer runtime.UnlockOSThread()
        }
        var rnd *rand.Rand
        if !opts.Deterministic { rnd = rand.New(rand.NewSource(time.Now().UnixNano() + int64(id))) }
        for {
            select { case <-ctx.Done(): return; default: }
            if t, ok := popTop(deques[id]); ok {
                if t.n.visited.Load() || !t.n.visited.CompareAndSwap(false, true) { tasksWG.Done() } else {
                    if !opts.TwoStage {
                        if visit != nil { if err := visit(t.n); err != nil { setErr(err) } }
                    } else if visit != nil {
                        // Offload to evaluation stage
                        select { case evalCh <- t.n: default: evalCh <- t.n }
                    }
                    if opts.Heuristics.Goal != nil && opts.Heuristics.Goal(t.n) { cancel(); tasksWG.Done(); continue }
                    children := make([]*Node, 0, 2)
                    if t.n.left != nil && (opts.Heuristics.Prune == nil || !opts.Heuristics.Prune(t.n.left)) { children = append(children, t.n.left) }
                    if t.n.right != nil && (opts.Heuristics.Prune == nil || !opts.Heuristics.Prune(t.n.right)) { children = append(children, t.n.right) }
                    if opts.Heuristics.OrderChildren != nil {
                        children = opts.Heuristics.OrderChildren(t.n)
                    } else if opts.Heuristics.CompareChildren != nil && len(children) > 1 {
                        if opts.Heuristics.CompareChildren(children[0], children[1]) > 0 { children[0], children[1] = children[1], children[0] }
                    } else if opts.Heuristics.Score != nil && len(children) > 1 {
                        if opts.Heuristics.Score(children[0]) > opts.Heuristics.Score(children[1]) { children[0], children[1] = children[1], children[0] }
                    }
                    limit := len(children)
                    if bw := opts.Heuristics.BeamWidth; bw > 0 && bw < limit { limit = bw; if ld := opts.Heuristics.LimitedDiscrepancy; ld > 0 && bw+ld < len(children) { limit = bw+ld } }
                    if limit < len(children) { children = children[:limit] }
                    deques[id].mu.Lock(); qlen := len(deques[id].items); deques[id].mu.Unlock()
                    if qlen >= opts.QueuePressureHigh && len(children) > 1 {
                        children = children[:1]
                        metricsMu.Lock(); metrics.QueueHighHits++; if uint64(qlen) > metrics.QueueLenMax { metrics.QueueLenMax = uint64(qlen) }; metricsMu.Unlock()
                    } else { if uint64(qlen) > metrics.QueueLenMax { metricsMu.Lock(); if uint64(qlen) > metrics.QueueLenMax { metrics.QueueLenMax = uint64(qlen) }; metricsMu.Unlock() } }
                    allowAll := qlen <= opts.QueuePressureLow
                    for i, c := range children {
                        // Coarsening: skip parallelizing tiny siblings beyond the best when EstimateWork is provided
                        if i > 0 && opts.Heuristics.EstimateWork != nil && opts.MinWorkToParallelize > 0 {
                            if w := opts.Heuristics.EstimateWork(c); w < opts.MinWorkToParallelize {
                                continue
                            }
                        }
                        if !allowAll {
                            if t.depth < opts.DepthParallelThreshold && i > 0 { break }
                        }
                        tasksWG.Add(1); pushTop(deques[id], task{n: c, depth: t.depth + 1}); metricsMu.Lock(); metrics.Pushes++; metricsMu.Unlock()
                    }
                    tasksWG.Done(); metricsMu.Lock(); metrics.TasksProcessed++; metrics.Pops++; metricsMu.Unlock()
                }
                continue
            }
            stole := false
            if opts.Deterministic {
                for v := 0; v < opts.MaxWorkers; v++ { if v == id { continue }
                    if opts.ChunkStealSize > 1 {
                        if chunk, ok := stealChunkBottom(deques[v], opts.ChunkStealSize); ok {
                            filtered := chunk[:0]
                            for _, t := range chunk { if t.depth >= opts.StealDepthMin { filtered = append(filtered, t) } }
                            if len(filtered) > 0 { stole = true; metricsMu.Lock(); metrics.Steals += uint64(len(filtered)); metrics.StealChunks++; metricsMu.Unlock(); for _, t := range filtered { pushTop(deques[id], t) }; break }
                        }
                    } else {
                        if t, ok := stealBottom(deques[v]); ok { if t.depth >= opts.StealDepthMin { stole = true; metricsMu.Lock(); metrics.Steals++; metricsMu.Unlock(); pushTop(deques[id], t); break } }
                    }
                }
            } else {
                for tries := 0; tries < opts.MaxWorkers-1; tries++ { v := rnd.Intn(opts.MaxWorkers); if v == id { continue }
                    if opts.ChunkStealSize > 1 {
                        if chunk, ok := stealChunkBottom(deques[v], opts.ChunkStealSize); ok { filtered := chunk[:0]; for _, t := range chunk { if t.depth >= opts.StealDepthMin { filtered = append(filtered, t) } }
                            if len(filtered) > 0 { stole = true; metricsMu.Lock(); metrics.Steals += uint64(len(filtered)); metrics.StealChunks++; metricsMu.Unlock(); for _, t := range filtered { pushTop(deques[id], t) }; break }
                        }
                    } else {
                        if t, ok := stealBottom(deques[v]); ok { if t.depth >= opts.StealDepthMin { stole = true; metricsMu.Lock(); metrics.Steals++; metricsMu.Unlock(); pushTop(deques[id], t); break } }
                    }
                }
            }
            if !stole {
                select {
                case <-ctx.Done():
                    return
                default:
                    time.Sleep(100 * time.Microsecond)
                    metricsMu.Lock(); metrics.IdleYields++; metricsMu.Unlock()
                }
            }
        }
    }

    // seed root task and launch workers
    tasksWG.Add(1)
    deques[0].items = append(deques[0].items, task{n: tree.root, depth: 0})
    for i := 0; i < opts.MaxWorkers; i++ { workersWG.Add(1); go worker(i) }
    go func(){ tasksWG.Wait(); cancel(); if evalCh != nil { close(evalCh) } }()
    workersWG.Wait()
    if evalCh != nil { evalWG.Wait() }
    if opts.MetricsHook != nil { opts.MetricsHook(metrics) }
    if ctx.Err() != nil && firstErr == nil && ctx.Err() != context.Canceled { return ctx.Err() }
    return firstErr
}

// Workers exit via ctx.Done when tasksWG reaches zero and cancel() is invoked.

// RunDemoModes prints a quick demo header to show modes
func RunDemoModes() {
    println("UNIFIED CONCURRENT DFS - Progressive Enhancement")
    println("=" + strings.Repeat("=", 50))
}
