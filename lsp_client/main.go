package main

/*
#include <stdlib.h>

// External functions from bridge.c
extern void api_message(const char *msg);
extern void api_log_info(const char *msg);
extern void* api_current_buffer(void);
extern const char* api_buffer_filename(void *bp);
extern void api_get_point(int *line, int *col);
extern int api_find_file_line(const char *path, int line);
extern void* api_buffer_create(const char *name);
extern int api_buffer_switch(void *bp);
extern int api_buffer_clear(void *bp);
extern int api_buffer_insert(const char *text, size_t len);
*/
import "C"

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"unsafe"
)

// Global state
var (
	lspProcess *exec.Cmd
	lspStdin   io.WriteCloser
	lspStdout  *bufio.Reader
	lspMu      sync.Mutex
	requestID  int
)

// JSON-RPC structures
type jsonRPCRequest struct {
	JSONRPC string      `json:"jsonrpc"`
	ID      int         `json:"id"`
	Method  string      `json:"method"`
	Params  interface{} `json:"params,omitempty"`
}

type jsonRPCResponse struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      int             `json:"id"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *jsonRPCError   `json:"error,omitempty"`
}

type jsonRPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// LSP structures
type Position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type Location struct {
	URI   string `json:"uri"`
	Range Range  `json:"range"`
}

type Range struct {
	Start Position `json:"start"`
	End   Position `json:"end"`
}

type TextDocumentIdentifier struct {
	URI string `json:"uri"`
}

// Helper to send message
func message(format string, args ...interface{}) {
	msg := C.CString(fmt.Sprintf(format, args...))
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)
}

// Send LSP request
func lspRequest(method string, params interface{}) (*jsonRPCResponse, error) {
	lspMu.Lock()
	defer lspMu.Unlock()

	if lspStdin == nil || lspStdout == nil {
		return nil, fmt.Errorf("LSP server not running")
	}

	requestID++
	req := jsonRPCRequest{
		JSONRPC: "2.0",
		ID:      requestID,
		Method:  method,
		Params:  params,
	}

	reqBytes, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}

	header := fmt.Sprintf("Content-Length: %d\r\n\r\n", len(reqBytes))
	if _, err := lspStdin.Write([]byte(header)); err != nil {
		return nil, err
	}
	if _, err := lspStdin.Write(reqBytes); err != nil {
		return nil, err
	}

	// Read response
	headerLine, err := lspStdout.ReadString('\n')
	if err != nil {
		return nil, err
	}

	var contentLength int
	if strings.HasPrefix(headerLine, "Content-Length:") {
		lengthStr := strings.TrimSpace(strings.TrimPrefix(headerLine, "Content-Length:"))
		contentLength, _ = strconv.Atoi(lengthStr)
	}

	lspStdout.ReadString('\n') // Skip empty line

	body := make([]byte, contentLength)
	if _, err := io.ReadFull(lspStdout, body); err != nil {
		return nil, err
	}

	var resp jsonRPCResponse
	if err := json.Unmarshal(body, &resp); err != nil {
		return nil, err
	}

	return &resp, nil
}

// Detect language server
func detectLanguageServer(filename string) (string, []string) {
	ext := filepath.Ext(filename)
	switch ext {
	case ".py":
		return "pyright-langserver", []string{"--stdio"}
	case ".rs":
		return "rust-analyzer", nil
	case ".go":
		return "gopls", nil
	case ".c", ".h", ".cpp", ".hpp":
		return "clangd", nil
	case ".js", ".ts":
		return "typescript-language-server", []string{"--stdio"}
	case ".zig":
		return "zls", nil
	}
	return "", nil
}

// Get current buffer info
func getCurrentBufferInfo() (filename string, line, col int) {
	buf := C.api_current_buffer()
	if buf == nil {
		return
	}

	cFilename := C.api_buffer_filename(buf)
	if cFilename != nil {
		filename = C.GoString(cFilename)
	}

	var cLine, cCol C.int
	C.api_get_point(&cLine, &cCol)
	line = int(cLine)
	col = int(cCol)

	return
}

//export go_lsp_start
func go_lsp_start(f, n C.int) C.int {
	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		message("lsp-start: No file in buffer")
		return 0
	}

	serverCmd, args := detectLanguageServer(filename)
	if serverCmd == "" {
		message("lsp-start: No language server for this file type")
		return 0
	}

	if _, err := exec.LookPath(serverCmd); err != nil {
		message("lsp-start: %s not found", serverCmd)
		return 0
	}

	if lspProcess != nil {
		lspProcess.Process.Kill()
	}

	lspProcess = exec.Command(serverCmd, args...)
	lspProcess.Stderr = os.Stderr

	var err error
	lspStdin, err = lspProcess.StdinPipe()
	if err != nil {
		message("lsp-start: stdin error: %v", err)
		return 0
	}

	stdout, err := lspProcess.StdoutPipe()
	if err != nil {
		message("lsp-start: stdout error: %v", err)
		return 0
	}
	lspStdout = bufio.NewReader(stdout)

	if err := lspProcess.Start(); err != nil {
		message("lsp-start: %v", err)
		return 0
	}

	// Initialize
	workDir := filepath.Dir(filename)
	initParams := map[string]interface{}{
		"processId": os.Getpid(),
		"rootUri":   "file://" + workDir,
		"capabilities": map[string]interface{}{
			"textDocument": map[string]interface{}{
				"hover":      map[string]interface{}{},
				"definition": map[string]interface{}{},
				"references": map[string]interface{}{},
			},
		},
	}

	if _, err := lspRequest("initialize", initParams); err != nil {
		message("lsp-start: init failed: %v", err)
		lspProcess.Process.Kill()
		lspProcess = nil
		return 0
	}

	lspRequest("initialized", map[string]interface{}{})
	message("lsp-start: %s started", serverCmd)
	return 1
}

//export go_lsp_stop
func go_lsp_stop(f, n C.int) C.int {
	if lspProcess == nil {
		message("lsp-stop: No server running")
		return 0
	}

	lspRequest("shutdown", nil)
	lspRequest("exit", nil)
	lspProcess.Process.Kill()
	lspProcess = nil
	lspStdin = nil
	lspStdout = nil

	message("lsp-stop: Server stopped")
	return 1
}

//export go_lsp_hover
func go_lsp_hover(f, n C.int) C.int {
	if lspProcess == nil {
		message("lsp-hover: No server (use lsp-start)")
		return 0
	}

	filename, line, col := getCurrentBufferInfo()
	if filename == "" {
		message("lsp-hover: No file")
		return 0
	}

	params := map[string]interface{}{
		"textDocument": map[string]string{"uri": "file://" + filename},
		"position":     map[string]int{"line": line - 1, "character": col},
	}

	resp, err := lspRequest("textDocument/hover", params)
	if err != nil {
		message("lsp-hover: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-hover: No info at point")
		return 1
	}

	var hover struct {
		Contents interface{} `json:"contents"`
	}
	json.Unmarshal(resp.Result, &hover)

	var text string
	switch v := hover.Contents.(type) {
	case string:
		text = v
	case map[string]interface{}:
		if val, ok := v["value"]; ok {
			text = fmt.Sprintf("%v", val)
		}
	}

	if text != "" {
		if len(text) > 80 {
			text = text[:77] + "..."
		}
		text = strings.ReplaceAll(text, "\n", " ")
		message("%s", text)
	}

	return 1
}

//export go_lsp_definition
func go_lsp_definition(f, n C.int) C.int {
	if lspProcess == nil {
		message("lsp-definition: No server")
		return 0
	}

	filename, line, col := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	params := map[string]interface{}{
		"textDocument": map[string]string{"uri": "file://" + filename},
		"position":     map[string]int{"line": line - 1, "character": col},
	}

	resp, err := lspRequest("textDocument/definition", params)
	if err != nil {
		message("lsp-definition: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-definition: Not found")
		return 1
	}

	var locations []Location
	if err := json.Unmarshal(resp.Result, &locations); err != nil {
		var single Location
		if json.Unmarshal(resp.Result, &single) == nil {
			locations = []Location{single}
		}
	}

	if len(locations) == 0 {
		message("lsp-definition: Not found")
		return 1
	}

	loc := locations[0]
	defFile := strings.TrimPrefix(loc.URI, "file://")
	defLine := loc.Range.Start.Line + 1

	cPath := C.CString(defFile)
	defer C.free(unsafe.Pointer(cPath))
	C.api_find_file_line(cPath, C.int(defLine))
	message("%s:%d", defFile, defLine)

	return 1
}

//export go_lsp_references
func go_lsp_references(f, n C.int) C.int {
	if lspProcess == nil {
		message("lsp-references: No server")
		return 0
	}

	filename, line, col := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	params := map[string]interface{}{
		"textDocument": map[string]string{"uri": "file://" + filename},
		"position":     map[string]int{"line": line - 1, "character": col},
		"context":      map[string]bool{"includeDeclaration": true},
	}

	resp, err := lspRequest("textDocument/references", params)
	if err != nil {
		message("lsp-references: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-references: None found")
		return 1
	}

	var locations []Location
	json.Unmarshal(resp.Result, &locations)

	if len(locations) == 0 {
		message("lsp-references: None found")
		return 1
	}

	// Create results buffer
	bufName := C.CString("*lsp-references*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for _, loc := range locations {
			refFile := strings.TrimPrefix(loc.URI, "file://")
			refLine := loc.Range.Start.Line + 1
			entry := fmt.Sprintf("%s:%d\n", refFile, refLine)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d references", len(locations))
	return 1
}

// main is required but unused for c-shared
func main() {}
