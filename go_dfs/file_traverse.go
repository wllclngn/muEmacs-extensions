package main

import (
	"context"
	"math/rand"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

// FileTraverseOptions controls the work-stealing file traversal
type FileTraverseOptions struct {
	MaxWorkers             int
	MaxDepth               int
	DepthParallelThreshold int
	Deterministic          bool
	CancelOnError          bool

	// Heuristics for file traversal
	Prune        func(path string, isDir bool) bool // Return true to skip this path
	Match        func(path string, isDir bool) bool // Return true if this path matches
	EstimateWork func(path string) int              // Estimate children count

	// Scheduling knobs
	StealDepthMin     int
	ChunkStealSize    int
	QueuePressureLow  int
	QueuePressureHigh int

	// Metrics
	MetricsHook     func(FileMetrics)
	MetricsInterval time.Duration
}

// FileMetrics provides observability into the traversal
type FileMetrics struct {
	FilesVisited   uint64
	DirsVisited    uint64
	Matches        uint64
	Pruned         uint64
	Steals         uint64
	StealChunks    uint64
	Pushes         uint64
	Pops           uint64
	QueueHighHits  uint64
	QueueLenMax    uint64
	IdleYields     uint64
	ElapsedNs      int64
}

// FileTraverseResult holds the results of a file traversal
type FileTraverseResult struct {
	Matches []string
	Errors  []string
	Metrics FileMetrics
}

// DefaultFileOptions returns sensible defaults for file traversal
func DefaultFileOptions(workers int) FileTraverseOptions {
	if workers <= 0 {
		workers = runtime.NumCPU()
	}
	return FileTraverseOptions{
		MaxWorkers:             workers,
		MaxDepth:               20,
		DepthParallelThreshold: 3,
		Deterministic:          false,
		CancelOnError:          false,
		StealDepthMin:          2,
		ChunkStealSize:         4,
		QueuePressureLow:       4,
		QueuePressureHigh:      64,
	}
}

// Common prune patterns
func DefaultPrune(path string, isDir bool) bool {
	if !isDir {
		return false
	}
	name := filepath.Base(path)
	if len(name) > 0 && name[0] == '.' {
		return true
	}
	switch name {
	case "node_modules", "__pycache__", "target", "vendor", ".git", ".svn", ".hg", "build", "dist":
		return true
	}
	return false
}

// FileTraverse performs work-stealing parallel DFS on a file system tree
func FileTraverse(ctx context.Context, root string, opts FileTraverseOptions, pattern *regexp.Regexp) *FileTraverseResult {
	result := &FileTraverseResult{
		Matches: make([]string, 0, 256),
		Errors:  make([]string, 0),
	}

	if opts.MaxWorkers <= 0 {
		opts.MaxWorkers = runtime.NumCPU()
	}
	if opts.MaxDepth <= 0 {
		opts.MaxDepth = 20
	}
	if opts.DepthParallelThreshold <= 0 {
		opts.DepthParallelThreshold = 3
	}

	// Set default prune if not provided
	if opts.Prune == nil {
		opts.Prune = DefaultPrune
	}

	// Set default match if not provided
	if opts.Match == nil && pattern != nil {
		opts.Match = func(path string, isDir bool) bool {
			return pattern.MatchString(filepath.Base(path))
		}
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	start := time.Now()

	// Task represents a directory to process
	type task struct {
		path  string
		depth int
	}

	// Work-stealing deque
	type deque struct {
		mu    sync.Mutex
		items []task
	}

	pushTop := func(d *deque, t task) {
		d.mu.Lock()
		d.items = append(d.items, t)
		d.mu.Unlock()
	}

	popTop := func(d *deque) (task, bool) {
		d.mu.Lock()
		l := len(d.items)
		if l == 0 {
			d.mu.Unlock()
			return task{}, false
		}
		t := d.items[l-1]
		d.items = d.items[:l-1]
		d.mu.Unlock()
		return t, true
	}

	stealBottom := func(d *deque) (task, bool) {
		d.mu.Lock()
		if len(d.items) == 0 {
			d.mu.Unlock()
			return task{}, false
		}
		t := d.items[0]
		copy(d.items[0:], d.items[1:])
		d.items = d.items[:len(d.items)-1]
		d.mu.Unlock()
		return t, true
	}

	stealChunkBottom := func(d *deque, k int) ([]task, bool) {
		if k <= 1 {
			if t, ok := stealBottom(d); ok {
				return []task{t}, true
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
		chunk := make([]task, k)
		copy(chunk, d.items[:k])
		d.items = d.items[k:]
		d.mu.Unlock()
		return chunk, true
	}

	// Create per-worker deques
	deques := make([]*deque, opts.MaxWorkers)
	for i := range deques {
		deques[i] = &deque{items: make([]task, 0, 64)}
	}

	var tasksWG, workersWG sync.WaitGroup
	var metrics FileMetrics
	var metricsMu sync.Mutex
	var matchesMu sync.Mutex
	var errorsMu sync.Mutex

	// Visited set to avoid re-processing (handles symlinks)
	visited := sync.Map{}

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
					metricsMu.Lock()
					m := metrics
					metricsMu.Unlock()
					opts.MetricsHook(m)
				}
			}
		}()
	}

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

			if t, ok := popTop(deques[id]); ok {
				// Check if already visited
				if _, loaded := visited.LoadOrStore(t.path, true); loaded {
					tasksWG.Done()
					continue
				}

				// Read directory entries
				entries, err := os.ReadDir(t.path)
				if err != nil {
					errorsMu.Lock()
					result.Errors = append(result.Errors, t.path+": "+err.Error())
					errorsMu.Unlock()
					tasksWG.Done()
					metricsMu.Lock()
					metrics.Pops++
					metricsMu.Unlock()
					continue
				}

				metricsMu.Lock()
				metrics.DirsVisited++
				metrics.Pops++
				metricsMu.Unlock()

				// Process entries
				var subdirs []task
				for _, entry := range entries {
					childPath := filepath.Join(t.path, entry.Name())
					isDir := entry.IsDir()

					// Check prune
					if opts.Prune != nil && opts.Prune(childPath, isDir) {
						atomic.AddUint64(&metrics.Pruned, 1)
						continue
					}

					// Check match
					if opts.Match != nil && opts.Match(childPath, isDir) {
						matchesMu.Lock()
						result.Matches = append(result.Matches, childPath)
						matchesMu.Unlock()
						atomic.AddUint64(&metrics.Matches, 1)
					}

					if isDir && t.depth < opts.MaxDepth {
						subdirs = append(subdirs, task{path: childPath, depth: t.depth + 1})
					} else if !isDir {
						atomic.AddUint64(&metrics.FilesVisited, 1)
					}
				}

				// Queue pressure management
				deques[id].mu.Lock()
				qlen := len(deques[id].items)
				deques[id].mu.Unlock()

				if qlen >= opts.QueuePressureHigh && len(subdirs) > 1 {
					// Under pressure: only queue first subdir, process rest synchronously
					subdirs = subdirs[:1]
					atomic.AddUint64(&metrics.QueueHighHits, 1)
				}

				if uint64(qlen) > metrics.QueueLenMax {
					metricsMu.Lock()
					if uint64(qlen) > metrics.QueueLenMax {
						metrics.QueueLenMax = uint64(qlen)
					}
					metricsMu.Unlock()
				}

				// Add subdirectories to deque
				allowAll := qlen <= opts.QueuePressureLow
				for i, subdir := range subdirs {
					// Coarsening: skip parallelizing beyond first if shallow and under pressure
					if !allowAll && t.depth < opts.DepthParallelThreshold && i > 0 {
						break
					}

					tasksWG.Add(1)
					pushTop(deques[id], subdir)
					atomic.AddUint64(&metrics.Pushes, 1)
				}

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
							filtered := chunk[:0]
							for _, t := range chunk {
								if t.depth >= opts.StealDepthMin {
									filtered = append(filtered, t)
								}
							}
							if len(filtered) > 0 {
								stole = true
								atomic.AddUint64(&metrics.Steals, uint64(len(filtered)))
								atomic.AddUint64(&metrics.StealChunks, 1)
								for _, t := range filtered {
									pushTop(deques[id], t)
								}
								break
							}
						}
					} else {
						if t, ok := stealBottom(deques[v]); ok {
							if t.depth >= opts.StealDepthMin {
								stole = true
								atomic.AddUint64(&metrics.Steals, 1)
								pushTop(deques[id], t)
								break
							}
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
							filtered := chunk[:0]
							for _, t := range chunk {
								if t.depth >= opts.StealDepthMin {
									filtered = append(filtered, t)
								}
							}
							if len(filtered) > 0 {
								stole = true
								atomic.AddUint64(&metrics.Steals, uint64(len(filtered)))
								atomic.AddUint64(&metrics.StealChunks, 1)
								for _, t := range filtered {
									pushTop(deques[id], t)
								}
								break
							}
						}
					} else {
						if t, ok := stealBottom(deques[v]); ok {
							if t.depth >= opts.StealDepthMin {
								stole = true
								atomic.AddUint64(&metrics.Steals, 1)
								pushTop(deques[id], t)
								break
							}
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

	// Seed with root directory
	rootInfo, err := os.Stat(root)
	if err != nil {
		result.Errors = append(result.Errors, root+": "+err.Error())
		return result
	}

	if !rootInfo.IsDir() {
		// Root is a file, just check if it matches
		if opts.Match != nil && opts.Match(root, false) {
			result.Matches = append(result.Matches, root)
			metrics.Matches = 1
		}
		metrics.FilesVisited = 1
		metrics.ElapsedNs = time.Since(start).Nanoseconds()
		result.Metrics = metrics
		return result
	}

	// Start workers
	tasksWG.Add(1)
	deques[0].items = append(deques[0].items, task{path: root, depth: 0})

	for i := 0; i < opts.MaxWorkers; i++ {
		workersWG.Add(1)
		go worker(i)
	}

	// Wait for all tasks to complete, then cancel workers
	go func() {
		tasksWG.Wait()
		cancel()
	}()

	workersWG.Wait()

	metrics.ElapsedNs = time.Since(start).Nanoseconds()
	if opts.MetricsHook != nil {
		opts.MetricsHook(metrics)
	}
	result.Metrics = metrics

	return result
}
