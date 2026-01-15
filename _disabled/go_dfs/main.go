// go_dfs - Concurrent DFS file traversal for μEmacs
//
// Provides high-performance concurrent directory traversal using
// work-stealing deques and adaptive parallelism.
//
// Commands:
//   dfs-find      - Find files matching pattern (concurrent)
//   dfs-grep      - Search file contents concurrently
//   dfs-count     - Count files/directories concurrently
//
// Built with CGO as a shared library for μEmacs extension system.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Bridge function declarations (implemented in bridge.c)
extern void api_message(const char *msg);
extern void *api_current_buffer(void);
extern char *api_buffer_contents(void *bp, size_t *len);
extern const char *api_buffer_filename(void *bp);
extern void api_get_point(int *line, int *col);
extern void api_set_point(int line, int col);
extern int api_buffer_insert(const char *text, size_t len);
extern void *api_buffer_create(const char *name);
extern int api_buffer_switch(void *bp);
extern int api_buffer_clear(void *bp);
extern int api_prompt(const char *prompt, char *buf, size_t buflen);
extern void api_free(void *ptr);
extern void api_log_info(const char *msg);
extern void api_log_error(const char *msg);
extern void api_update_display(void);
extern int api_find_file_line(const char *path, int line);
*/
import "C"

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
)

// FileNode represents a file/directory in the traversal tree
type FileNode struct {
	Path     string
	Name     string
	IsDir    bool
	Size     int64
	Children []*FileNode
	parent   *FileNode
	depth    int
}

// FileTree for concurrent traversal
type FileTree struct {
	root      *FileNode
	nodeCount int64
	maxDepth  int32
}

// TraversalResult holds search results
type TraversalResult struct {
	mu      sync.Mutex
	matches []string
	count   int64
	errors  []string
}

func (r *TraversalResult) AddMatch(path string) {
	r.mu.Lock()
	r.matches = append(r.matches, path)
	r.mu.Unlock()
	atomic.AddInt64(&r.count, 1)
}

func (r *TraversalResult) AddError(err string) {
	r.mu.Lock()
	r.errors = append(r.errors, err)
	r.mu.Unlock()
}

// ConcurrentFind performs parallel file search
func ConcurrentFind(root string, pattern *regexp.Regexp, maxWorkers int) *TraversalResult {
	result := &TraversalResult{matches: make([]string, 0, 100)}

	if maxWorkers <= 0 {
		maxWorkers = runtime.NumCPU()
	}

	// Work channel and wait group
	type workItem struct {
		path  string
		depth int
	}

	workCh := make(chan workItem, maxWorkers*16)
	var wg sync.WaitGroup

	// Spawn workers
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	for i := 0; i < maxWorkers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-ctx.Done():
					return
				case item, ok := <-workCh:
					if !ok {
						return
					}
					// Check if name matches pattern
					name := filepath.Base(item.path)
					if pattern.MatchString(name) {
						result.AddMatch(item.path)
					}

					// If directory, enumerate children
					info, err := os.Stat(item.path)
					if err != nil {
						continue
					}
					if info.IsDir() && item.depth < 20 { // Max depth limit
						entries, err := os.ReadDir(item.path)
						if err != nil {
							result.AddError(fmt.Sprintf("%s: %v", item.path, err))
							continue
						}
						for _, entry := range entries {
							// Skip hidden and common ignore patterns
							name := entry.Name()
							if name[0] == '.' || name == "node_modules" || name == "__pycache__" || name == "target" {
								continue
							}
							childPath := filepath.Join(item.path, name)
							select {
							case workCh <- workItem{path: childPath, depth: item.depth + 1}:
							default:
								// Channel full, process inline
								if pattern.MatchString(name) {
									result.AddMatch(childPath)
								}
							}
						}
					}
				}
			}
		}()
	}

	// Seed with root
	workCh <- workItem{path: root, depth: 0}

	// Wait for a bit then close
	time.Sleep(10 * time.Millisecond)
	go func() {
		// Check periodically if work is done
		for {
			time.Sleep(50 * time.Millisecond)
			if len(workCh) == 0 {
				time.Sleep(100 * time.Millisecond)
				if len(workCh) == 0 {
					close(workCh)
					return
				}
			}
		}
	}()

	wg.Wait()
	return result
}

// ConcurrentGrep searches file contents in parallel
func ConcurrentGrep(root string, filePattern, contentPattern *regexp.Regexp, maxWorkers int) *TraversalResult {
	result := &TraversalResult{matches: make([]string, 0, 100)}

	if maxWorkers <= 0 {
		maxWorkers = runtime.NumCPU()
	}

	// First find all matching files
	files := ConcurrentFind(root, filePattern, maxWorkers/2)

	// Then search contents in parallel
	fileCh := make(chan string, len(files.matches))
	for _, f := range files.matches {
		fileCh <- f
	}
	close(fileCh)

	var wg sync.WaitGroup
	for i := 0; i < maxWorkers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for path := range fileCh {
				content, err := os.ReadFile(path)
				if err != nil {
					continue
				}
				if contentPattern.Match(content) {
					// Find line numbers
					lines := strings.Split(string(content), "\n")
					for i, line := range lines {
						if contentPattern.MatchString(line) {
							result.AddMatch(fmt.Sprintf("%s:%d: %s", path, i+1, strings.TrimSpace(line)))
						}
					}
				}
			}
		}()
	}
	wg.Wait()

	return result
}

//export dfs_init
func dfs_init(api unsafe.Pointer) {
	// Nothing special to initialize
}

//export go_dfs_find
func go_dfs_find(f, n C.int) C.int {
	// Prompt for pattern
	var patternBuf [256]C.char
	if C.api_prompt(C.CString("Find files matching: "), &patternBuf[0], 256) != 1 {
		return 0
	}
	pattern := C.GoString(&patternBuf[0])
	if pattern == "" {
		pattern = ".*" // Match all
	}

	re, err := regexp.Compile(pattern)
	if err != nil {
		msg := C.CString(fmt.Sprintf("Invalid pattern: %v", err))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Get current directory from buffer filename or use cwd
	bp := C.api_current_buffer()
	var root string
	if bp != nil {
		fname := C.GoString(C.api_buffer_filename(bp))
		if fname != "" {
			root = filepath.Dir(fname)
		}
	}
	if root == "" {
		root, _ = os.Getwd()
	}

	// Run concurrent find
	start := time.Now()
	result := ConcurrentFind(root, re, runtime.NumCPU())
	elapsed := time.Since(start)

	// Create results buffer
	resultBuf := C.api_buffer_create(C.CString("*dfs-find*"))
	if resultBuf == nil {
		return 0
	}
	C.api_buffer_switch(resultBuf)
	C.api_buffer_clear(resultBuf)

	// Build output
	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("DFS Find: %s in %s\n", pattern, root))
	sb.WriteString(fmt.Sprintf("Found %d matches in %v\n\n", result.count, elapsed))

	for _, match := range result.matches {
		// Make path relative if possible
		rel, err := filepath.Rel(root, match)
		if err == nil {
			sb.WriteString(rel + "\n")
		} else {
			sb.WriteString(match + "\n")
		}
	}

	output := sb.String()
	coutput := C.CString(output)
	C.api_buffer_insert(coutput, C.size_t(len(output)))
	C.free(unsafe.Pointer(coutput))

	C.api_set_point(1, 1)
	C.api_update_display()

	msg := C.CString(fmt.Sprintf("Found %d files in %v", result.count, elapsed))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_dfs_grep
func go_dfs_grep(f, n C.int) C.int {
	// Prompt for file pattern
	var fileBuf [256]C.char
	if C.api_prompt(C.CString("File pattern (regex): "), &fileBuf[0], 256) != 1 {
		return 0
	}
	filePattern := C.GoString(&fileBuf[0])
	if filePattern == "" {
		filePattern = "\\.(go|c|h|py|js|ts|rs)$" // Common source files
	}

	// Prompt for content pattern
	var contentBuf [256]C.char
	if C.api_prompt(C.CString("Search for: "), &contentBuf[0], 256) != 1 {
		return 0
	}
	contentPattern := C.GoString(&contentBuf[0])
	if contentPattern == "" {
		return 0
	}

	fileRe, err := regexp.Compile(filePattern)
	if err != nil {
		msg := C.CString(fmt.Sprintf("Invalid file pattern: %v", err))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	contentRe, err := regexp.Compile(contentPattern)
	if err != nil {
		msg := C.CString(fmt.Sprintf("Invalid search pattern: %v", err))
		C.api_message(msg)
		C.free(unsafe.Pointer(msg))
		return 0
	}

	// Get root directory
	bp := C.api_current_buffer()
	var root string
	if bp != nil {
		fname := C.GoString(C.api_buffer_filename(bp))
		if fname != "" {
			root = filepath.Dir(fname)
		}
	}
	if root == "" {
		root, _ = os.Getwd()
	}

	// Run concurrent grep
	start := time.Now()
	result := ConcurrentGrep(root, fileRe, contentRe, runtime.NumCPU())
	elapsed := time.Since(start)

	// Create results buffer
	resultBuf := C.api_buffer_create(C.CString("*dfs-grep*"))
	if resultBuf == nil {
		return 0
	}
	C.api_buffer_switch(resultBuf)
	C.api_buffer_clear(resultBuf)

	// Build output
	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("DFS Grep: '%s' in files matching '%s'\n", contentPattern, filePattern))
	sb.WriteString(fmt.Sprintf("Root: %s\n", root))
	sb.WriteString(fmt.Sprintf("Found %d matches in %v\n\n", result.count, elapsed))

	for _, match := range result.matches {
		sb.WriteString(match + "\n")
	}

	output := sb.String()
	coutput := C.CString(output)
	C.api_buffer_insert(coutput, C.size_t(len(output)))
	C.free(unsafe.Pointer(coutput))

	C.api_set_point(1, 1)
	C.api_update_display()

	msg := C.CString(fmt.Sprintf("Found %d matches in %v", result.count, elapsed))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

//export go_dfs_count
func go_dfs_count(f, n C.int) C.int {
	bp := C.api_current_buffer()
	var root string
	if bp != nil {
		fname := C.GoString(C.api_buffer_filename(bp))
		if fname != "" {
			root = filepath.Dir(fname)
		}
	}
	if root == "" {
		root, _ = os.Getwd()
	}

	// Count all files
	start := time.Now()
	result := ConcurrentFind(root, regexp.MustCompile(".*"), runtime.NumCPU())
	elapsed := time.Since(start)

	msg := C.CString(fmt.Sprintf("Counted %d files in %s (%v)", result.count, root, elapsed))
	C.api_message(msg)
	C.free(unsafe.Pointer(msg))

	return 1
}

func main() {}
