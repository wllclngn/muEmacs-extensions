package main

/*
#include <stdlib.h>
#include <stdint.h>

// External functions from bridge.c
extern void api_message(const char *msg);
extern void api_log_info(const char *msg);
extern void api_log_error(const char *msg);
extern void* api_current_buffer(void);
extern const char* api_buffer_filename(void *bp);
extern char* api_buffer_contents(void *bp, size_t *len);
extern void api_get_point(int *line, int *col);
extern int api_find_file_line(const char *path, int line);
extern void* api_buffer_create(const char *name);
extern int api_buffer_switch(void *bp);
extern int api_buffer_clear(void *bp);
extern int api_buffer_insert(const char *text, size_t len);
extern void api_free(void *ptr);
extern int api_syntax_add_token(void *tokens, int end_col, int face);
extern void api_syntax_invalidate_buffer(void *bp);

// Diagnostic event types for linter integration
typedef struct {
    const char *uri;
    int line;
    int col;
    int end_col;
    int severity;
    const char *message;
} lsp_diag_entry_t;

extern void api_emit_diagnostics(const char *uri, lsp_diag_entry_t *diags, int count);
*/
import "C"

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
)

// =============================================================================
// Face IDs (must match UEMACS_FACE_* in extension_api.h)
// =============================================================================

const (
	FaceDefault      = 0
	FaceKeyword      = 1
	FaceString       = 2
	FaceComment      = 3
	FaceNumber       = 4
	FaceType         = 5
	FaceFunction     = 6
	FaceOperator     = 7
	FacePreprocessor = 8
	FaceConstant     = 9
	FaceVariable     = 10
	FaceAttribute    = 11
	FaceEscape       = 12
	FaceRegex        = 13
	FaceSpecial      = 14
)

// =============================================================================
// Concurrent LSP Client
// =============================================================================

// outboundMsg is sent to the writer actor
type outboundMsg struct {
	data []byte
	done chan error
}

type LSPClient struct {
	process  *exec.Cmd
	stdin    io.WriteCloser
	stdout   *bufio.Reader
	stderr   io.ReadCloser

	// Writer actor channel (replaces mutex on stdin)
	writeReqs chan *outboundMsg

	// Request/response handling
	nextID   atomic.Int64
	pending  sync.Map // map[int64]chan *jsonRPCResponse
	ctx      context.Context
	cancel   context.CancelFunc
	wg       sync.WaitGroup

	// Server info
	serverCmd  string
	serverArgs []string
	rootURI    string

	// Capabilities
	hasSemanticTokens bool
	tokenTypes        []string
	tokenModifiers    []string
}

type jsonRPCRequest struct {
	JSONRPC string      `json:"jsonrpc"`
	ID      int64       `json:"id,omitempty"`
	Method  string      `json:"method"`
	Params  interface{} `json:"params,omitempty"`
}

type jsonRPCResponse struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      int64           `json:"id"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *jsonRPCError   `json:"error,omitempty"`
}

type jsonRPCNotification struct {
	JSONRPC string          `json:"jsonrpc"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}

type jsonRPCError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// =============================================================================
// Semantic Token Cache
// =============================================================================

type SemanticToken struct {
	Line      int
	StartChar int
	Length    int
	TokenType int
	Modifiers int
}

type BufferTokens struct {
	mu       sync.RWMutex
	tokens   []SemanticToken
	version  int
	fetching atomic.Bool
}

// Diagnostic represents an LSP diagnostic
type Diagnostic struct {
	Range    Range
	Severity int
	Message  string
	Source   string
}

var (
	clientPtr       atomic.Pointer[LSPClient]
	tokenCache      sync.Map // map[unsafe.Pointer]*BufferTokens (buffer ptr -> tokens)
	diagnosticCache sync.Map // map[string][]Diagnostic (URI -> diagnostics)
)

// =============================================================================
// LSP Client Methods
// =============================================================================

func NewLSPClient(serverCmd string, serverArgs []string, rootURI string) (*LSPClient, error) {
	ctx, cancel := context.WithCancel(context.Background())

	c := &LSPClient{
		serverCmd:  serverCmd,
		serverArgs: serverArgs,
		rootURI:    rootURI,
		ctx:        ctx,
		cancel:     cancel,
	}

	return c, nil
}

func (c *LSPClient) Start() error {
	c.process = exec.Command(c.serverCmd, c.serverArgs...)
	c.process.Stderr = os.Stderr

	var err error
	c.stdin, err = c.process.StdinPipe()
	if err != nil {
		return fmt.Errorf("stdin pipe: %w", err)
	}

	stdout, err := c.process.StdoutPipe()
	if err != nil {
		return fmt.Errorf("stdout pipe: %w", err)
	}
	c.stdout = bufio.NewReader(stdout)

	if err := c.process.Start(); err != nil {
		return fmt.Errorf("start: %w", err)
	}

	// Initialize writer channel
	c.writeReqs = make(chan *outboundMsg, 64)

	// Start reader and writer goroutines
	c.wg.Add(2)
	go c.responseReader()
	go c.writerActor()

	return nil
}

func (c *LSPClient) Stop() {
	c.cancel()

	// Close stdin to unblock writer and signal server
	if c.stdin != nil {
		c.stdin.Close()
	}
	if c.process != nil && c.process.Process != nil {
		c.process.Process.Kill()
	}

	c.wg.Wait()
}

// responseReader runs in a goroutine, reading and dispatching responses
func (c *LSPClient) responseReader() {
	defer c.wg.Done()

	for {
		select {
		case <-c.ctx.Done():
			return
		default:
		}

		msg, err := c.readMessage()
		if err != nil {
			if c.ctx.Err() != nil {
				return // Context cancelled
			}
			logError("LSP read error: %v", err)
			return
		}

		// Try to parse as response (has ID)
		var resp jsonRPCResponse
		if err := json.Unmarshal(msg, &resp); err == nil && resp.ID != 0 {
			// Find pending request
			if ch, ok := c.pending.LoadAndDelete(resp.ID); ok {
				ch.(chan *jsonRPCResponse) <- &resp
			}
			continue
		}

		// Try to parse as notification
		var notif jsonRPCNotification
		if err := json.Unmarshal(msg, &notif); err == nil {
			c.handleNotification(&notif)
		}
	}
}

// writerActor runs in a goroutine, serializing writes to stdin
func (c *LSPClient) writerActor() {
	defer c.wg.Done()

	for {
		select {
		case <-c.ctx.Done():
			return
		case msg := <-c.writeReqs:
			_, err := c.stdin.Write(msg.data)
			if msg.done != nil {
				msg.done <- err
			}
		}
	}
}

func (c *LSPClient) readMessage() ([]byte, error) {
	// c.stdout is immutable after Start(), no lock needed
	if c.stdout == nil {
		return nil, fmt.Errorf("no stdout")
	}

	// Read headers
	var contentLength int
	for {
		line, err := c.stdout.ReadString('\n')
		if err != nil {
			return nil, err
		}
		line = strings.TrimSpace(line)
		if line == "" {
			break // End of headers
		}
		if strings.HasPrefix(line, "Content-Length:") {
			lengthStr := strings.TrimSpace(strings.TrimPrefix(line, "Content-Length:"))
			contentLength, _ = strconv.Atoi(lengthStr)
		}
	}

	if contentLength == 0 {
		return nil, fmt.Errorf("no content length")
	}

	body := make([]byte, contentLength)
	if _, err := io.ReadFull(c.stdout, body); err != nil {
		return nil, err
	}

	return body, nil
}

func (c *LSPClient) handleNotification(notif *jsonRPCNotification) {
	switch notif.Method {
	case "textDocument/publishDiagnostics":
		var params struct {
			URI         string `json:"uri"`
			Diagnostics []struct {
				Range    Range  `json:"range"`
				Severity int    `json:"severity"`
				Message  string `json:"message"`
				Source   string `json:"source"`
			} `json:"diagnostics"`
		}
		if err := json.Unmarshal(notif.Params, &params); err != nil {
			return
		}

		// Convert to our Diagnostic type
		diags := make([]Diagnostic, len(params.Diagnostics))
		for i, d := range params.Diagnostics {
			diags[i] = Diagnostic{
				Range:    d.Range,
				Severity: d.Severity,
				Message:  d.Message,
				Source:   d.Source,
			}
		}
		diagnosticCache.Store(params.URI, diags)

		// Emit lsp:diagnostics event for linter integration
		emitDiagnosticsEvent(params.URI, diags)

		// Show first error in message line
		if len(diags) > 0 {
			d := diags[0]
			severity := "info"
			switch d.Severity {
			case 1:
				severity = "error"
			case 2:
				severity = "warning"
			}
			msg := fmt.Sprintf("[%s] %s:%d: %s", severity, filepath.Base(strings.TrimPrefix(params.URI, "file://")), d.Range.Start.Line+1, d.Message)
			if len(msg) > 80 {
				msg = msg[:77] + "..."
			}
			message("%s", msg)
		}

	case "window/logMessage":
		// Could log to debug file
	}
}

// Request timeout (30 seconds should be plenty for any LSP operation)
const requestTimeout = 30 * time.Second

// Request sends a request and waits for response with timeout
func (c *LSPClient) Request(method string, params interface{}) (*jsonRPCResponse, error) {
	id := c.nextID.Add(1)

	req := jsonRPCRequest{
		JSONRPC: "2.0",
		ID:      id,
		Method:  method,
		Params:  params,
	}

	// Create response channel
	respCh := make(chan *jsonRPCResponse, 1)
	c.pending.Store(id, respCh)
	defer c.pending.Delete(id)

	// Send request
	if err := c.sendMessage(req); err != nil {
		return nil, err
	}

	// Wait for response with timeout
	select {
	case resp := <-respCh:
		return resp, nil
	case <-time.After(requestTimeout):
		return nil, fmt.Errorf("request timeout after %v", requestTimeout)
	case <-c.ctx.Done():
		return nil, c.ctx.Err()
	}
}

// Notify sends a notification (no response expected)
func (c *LSPClient) Notify(method string, params interface{}) error {
	req := jsonRPCRequest{
		JSONRPC: "2.0",
		Method:  method,
		Params:  params,
	}
	return c.sendMessage(req)
}

func (c *LSPClient) sendMessage(msg interface{}) error {
	body, err := json.Marshal(msg)
	if err != nil {
		return err
	}

	header := fmt.Sprintf("Content-Length: %d\r\n\r\n", len(body))
	data := append([]byte(header), body...)

	done := make(chan error, 1)
	select {
	case c.writeReqs <- &outboundMsg{data: data, done: done}:
		return <-done
	case <-c.ctx.Done():
		return c.ctx.Err()
	}
}

// =============================================================================
// LSP Protocol Methods
// =============================================================================

func (c *LSPClient) Initialize() error {
	params := map[string]interface{}{
		"processId": os.Getpid(),
		"rootUri":   c.rootURI,
		"capabilities": map[string]interface{}{
			"textDocument": map[string]interface{}{
				"hover":      map[string]interface{}{},
				"definition": map[string]interface{}{},
				"references": map[string]interface{}{},
				"completion": map[string]interface{}{
					"completionItem": map[string]interface{}{
						"snippetSupport": false,
					},
				},
				"codeAction": map[string]interface{}{
					"codeActionLiteralSupport": map[string]interface{}{
						"codeActionKind": map[string]interface{}{
							"valueSet": []string{
								"quickfix",
								"refactor",
								"refactor.extract",
								"refactor.inline",
								"refactor.rewrite",
								"source",
								"source.organizeImports",
							},
						},
					},
				},
				"documentSymbol": map[string]interface{}{
					"hierarchicalDocumentSymbolSupport": true,
				},
				"publishDiagnostics": map[string]interface{}{
					"relatedInformation": true,
				},
				"synchronization": map[string]interface{}{
					"didSave":   true,
					"willSave":  false,
					"didChange": 1, // TextDocumentSyncKind.Full
				},
				"semanticTokens": map[string]interface{}{
					"requests": map[string]interface{}{
						"full": true,
					},
					"tokenTypes": []string{
						"namespace", "type", "class", "enum", "interface",
						"struct", "typeParameter", "parameter", "variable",
						"property", "enumMember", "event", "function",
						"method", "macro", "keyword", "modifier", "comment",
						"string", "number", "regexp", "operator",
					},
					"tokenModifiers": []string{
						"declaration", "definition", "readonly", "static",
						"deprecated", "abstract", "async", "modification",
						"documentation", "defaultLibrary",
					},
					"formats": []string{"relative"},
				},
			},
			"workspace": map[string]interface{}{
				"symbol": map[string]interface{}{
					"symbolKind": map[string]interface{}{
						"valueSet": []int{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26},
					},
				},
			},
		},
	}

	resp, err := c.Request("initialize", params)
	if err != nil {
		return err
	}

	if resp.Error != nil {
		return fmt.Errorf("initialize: %s", resp.Error.Message)
	}

	// Parse capabilities
	var result struct {
		Capabilities struct {
			SemanticTokensProvider interface{} `json:"semanticTokensProvider"`
		} `json:"capabilities"`
	}
	if err := json.Unmarshal(resp.Result, &result); err == nil {
		c.hasSemanticTokens = result.Capabilities.SemanticTokensProvider != nil

		// Extract token legend if available
		if provider, ok := result.Capabilities.SemanticTokensProvider.(map[string]interface{}); ok {
			if legend, ok := provider["legend"].(map[string]interface{}); ok {
				if types, ok := legend["tokenTypes"].([]interface{}); ok {
					c.tokenTypes = make([]string, len(types))
					for i, t := range types {
						c.tokenTypes[i] = fmt.Sprint(t)
					}
				}
				if mods, ok := legend["tokenModifiers"].([]interface{}); ok {
					c.tokenModifiers = make([]string, len(mods))
					for i, m := range mods {
						c.tokenModifiers[i] = fmt.Sprint(m)
					}
				}
			}
		}
	}

	// Send initialized notification
	return c.Notify("initialized", map[string]interface{}{})
}

func (c *LSPClient) DidOpen(uri, languageID, text string) error {
	return c.Notify("textDocument/didOpen", map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri":        uri,
			"languageId": languageID,
			"version":    1,
			"text":       text,
		},
	})
}

func (c *LSPClient) DidChange(uri string, version int, text string) error {
	return c.Notify("textDocument/didChange", map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri":     uri,
			"version": version,
		},
		"contentChanges": []map[string]interface{}{
			{"text": text},
		},
	})
}

func (c *LSPClient) DidSave(uri string, text string) error {
	return c.Notify("textDocument/didSave", map[string]interface{}{
		"textDocument": map[string]string{
			"uri": uri,
		},
		"text": text, // Include text for servers that want it
	})
}

func (c *LSPClient) DidClose(uri string) error {
	return c.Notify("textDocument/didClose", map[string]interface{}{
		"textDocument": map[string]string{
			"uri": uri,
		},
	})
}

// FetchSemanticTokens requests semantic tokens for a file
func (c *LSPClient) FetchSemanticTokens(uri string) ([]SemanticToken, error) {
	if !c.hasSemanticTokens {
		return nil, nil
	}

	resp, err := c.Request("textDocument/semanticTokens/full", map[string]interface{}{
		"textDocument": map[string]string{"uri": uri},
	})
	if err != nil {
		return nil, err
	}

	if resp.Error != nil {
		return nil, fmt.Errorf("semanticTokens: %s", resp.Error.Message)
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		return nil, nil
	}

	var result struct {
		Data []int `json:"data"`
	}
	if err := json.Unmarshal(resp.Result, &result); err != nil {
		return nil, err
	}

	// Decode relative tokens to absolute
	tokens := make([]SemanticToken, 0, len(result.Data)/5)
	var line, startChar int

	for i := 0; i+4 < len(result.Data); i += 5 {
		deltaLine := result.Data[i]
		deltaStartChar := result.Data[i+1]
		length := result.Data[i+2]
		tokenType := result.Data[i+3]
		tokenMods := result.Data[i+4]

		if deltaLine > 0 {
			line += deltaLine
			startChar = deltaStartChar
		} else {
			startChar += deltaStartChar
		}

		tokens = append(tokens, SemanticToken{
			Line:      line,
			StartChar: startChar,
			Length:    length,
			TokenType: tokenType,
			Modifiers: tokenMods,
		})
	}

	return tokens, nil
}

// =============================================================================
// Semantic Token to Face Mapping
// =============================================================================

func (c *LSPClient) tokenTypeToFace(tokenType int) int {
	if tokenType < 0 || tokenType >= len(c.tokenTypes) {
		return FaceDefault
	}

	switch c.tokenTypes[tokenType] {
	case "keyword", "modifier":
		return FaceKeyword
	case "string":
		return FaceString
	case "comment":
		return FaceComment
	case "number":
		return FaceNumber
	case "type", "class", "struct", "enum", "interface", "typeParameter":
		return FaceType
	case "function", "method":
		return FaceFunction
	case "operator":
		return FaceOperator
	case "macro":
		return FacePreprocessor
	case "enumMember":
		return FaceConstant
	case "variable", "parameter", "property":
		return FaceVariable
	case "namespace":
		return FaceAttribute
	case "regexp":
		return FaceRegex
	default:
		return FaceDefault
	}
}

// =============================================================================
// Buffer Token Management
// =============================================================================

func getOrCreateBufferTokens(bp unsafe.Pointer) *BufferTokens {
	if val, ok := tokenCache.Load(bp); ok {
		return val.(*BufferTokens)
	}

	bt := &BufferTokens{}
	actual, _ := tokenCache.LoadOrStore(bp, bt)
	return actual.(*BufferTokens)
}

func updateBufferTokens(bp unsafe.Pointer, tokens []SemanticToken, version int) {
	bt := getOrCreateBufferTokens(bp)
	bt.mu.Lock()
	defer bt.mu.Unlock()

	bt.tokens = tokens
	bt.version = version
}

func getTokensForLine(bp unsafe.Pointer, lineNum int) []SemanticToken {
	bt := getOrCreateBufferTokens(bp)
	bt.mu.RLock()
	defer bt.mu.RUnlock()

	var result []SemanticToken
	for _, tok := range bt.tokens {
		if tok.Line == lineNum {
			result = append(result, tok)
		}
	}
	return result
}

// =============================================================================
// Async Token Fetcher
// =============================================================================

func fetchTokensAsync(bp unsafe.Pointer, uri string) {
	bt := getOrCreateBufferTokens(bp)

	// Prevent concurrent fetches for same buffer
	if !bt.fetching.CompareAndSwap(false, true) {
		return
	}
	defer bt.fetching.Store(false)

	c := clientPtr.Load()
	if c == nil {
		return
	}

	tokens, err := c.FetchSemanticTokens(uri)
	if err != nil {
		logError("fetchTokens: %v", err)
		return
	}

	if tokens != nil {
		updateBufferTokens(bp, tokens, 1)
		// Invalidate buffer to trigger redraw
		C.api_syntax_invalidate_buffer(bp)
	}
}

// =============================================================================
// Exported Functions (Called from C)
// =============================================================================

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

	// Stop existing client
	if old := clientPtr.Load(); old != nil {
		old.Stop()
	}

	// Create new client
	workDir := filepath.Dir(filename)
	c, err := NewLSPClient(serverCmd, args, "file://"+workDir)
	if err != nil {
		message("lsp-start: %v", err)
		return 0
	}

	if err := c.Start(); err != nil {
		message("lsp-start: %v", err)
		return 0
	}

	if err := c.Initialize(); err != nil {
		c.Stop()
		message("lsp-start: init failed: %v", err)
		return 0
	}

	clientPtr.Store(c)

	// Open current file
	bp := C.api_current_buffer()
	if bp != nil {
		var contentLen C.size_t
		cContent := C.api_buffer_contents(bp, &contentLen)
		if cContent != nil {
			content := C.GoStringN(cContent, C.int(contentLen))
			C.api_free(unsafe.Pointer(cContent))

			uri := "file://" + filename
			langID := detectLanguageID(filename)
			c.DidOpen(uri, langID, content)

			// Fetch semantic tokens in background
			go fetchTokensAsync(unsafe.Pointer(bp), uri)
		}
	}

	if c.hasSemanticTokens {
		message("lsp-start: %s started (semantic tokens enabled)", serverCmd)
	} else {
		message("lsp-start: %s started", serverCmd)
	}
	return 1
}

//export go_lsp_stop
func go_lsp_stop(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		message("lsp-stop: No server running")
		return 0
	}

	c.Request("shutdown", nil)
	c.Notify("exit", nil)
	c.Stop()
	clientPtr.Store(nil)

	// Clear token cache
	tokenCache.Range(func(key, value interface{}) bool {
		tokenCache.Delete(key)
		return true
	})

	message("lsp-stop: Server stopped")
	return 1
}

//export go_lsp_hover
func go_lsp_hover(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
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

	resp, err := c.Request("textDocument/hover", params)
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

	text := extractHoverText(hover.Contents)
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
	c := clientPtr.Load()
	if c == nil {
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

	resp, err := c.Request("textDocument/definition", params)
	if err != nil {
		message("lsp-definition: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-definition: Not found")
		return 1
	}

	locations := parseLocations(resp.Result)
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
	c := clientPtr.Load()
	if c == nil {
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

	resp, err := c.Request("textDocument/references", params)
	if err != nil {
		message("lsp-references: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-references: None found")
		return 1
	}

	locations := parseLocations(resp.Result)
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

//export go_lsp_refresh_tokens
func go_lsp_refresh_tokens(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil || !c.hasSemanticTokens {
		message("lsp-refresh-tokens: No semantic tokens support")
		return 0
	}

	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	bp := C.api_current_buffer()
	if bp != nil {
		go fetchTokensAsync(unsafe.Pointer(bp), "file://"+filename)
		message("lsp-refresh-tokens: Fetching...")
	}

	return 1
}

//export go_lsp_did_save
func go_lsp_did_save(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		return 0 // Silent - no server running
	}

	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	bp := C.api_current_buffer()
	if bp == nil {
		return 0
	}

	var contentLen C.size_t
	cContent := C.api_buffer_contents(bp, &contentLen)
	if cContent == nil {
		return 0
	}
	content := C.GoStringN(cContent, C.int(contentLen))
	C.api_free(unsafe.Pointer(cContent))

	uri := "file://" + filename
	if err := c.DidSave(uri, content); err != nil {
		logError("didSave: %v", err)
		return 0
	}

	// Refresh tokens after save
	go fetchTokensAsync(unsafe.Pointer(bp), uri)
	return 1
}

//export go_lsp_did_close
func go_lsp_did_close(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		return 0 // Silent - no server running
	}

	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	uri := "file://" + filename
	if err := c.DidClose(uri); err != nil {
		logError("didClose: %v", err)
		return 0
	}

	// Clear tokens for this buffer
	bp := C.api_current_buffer()
	if bp != nil {
		tokenCache.Delete(unsafe.Pointer(bp))
	}

	return 1
}

//export go_lsp_completion
func go_lsp_completion(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		message("lsp-completion: No server")
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

	resp, err := c.Request("textDocument/completion", params)
	if err != nil {
		message("lsp-completion: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-completion: No completions")
		return 1
	}

	// Parse completion items (can be CompletionList or []CompletionItem)
	var items []struct {
		Label      string `json:"label"`
		Kind       int    `json:"kind"`
		Detail     string `json:"detail"`
		InsertText string `json:"insertText"`
	}

	// Try as CompletionList first
	var list struct {
		Items []struct {
			Label      string `json:"label"`
			Kind       int    `json:"kind"`
			Detail     string `json:"detail"`
			InsertText string `json:"insertText"`
		} `json:"items"`
	}
	if err := json.Unmarshal(resp.Result, &list); err == nil && len(list.Items) > 0 {
		items = list.Items
	} else {
		// Try as []CompletionItem
		json.Unmarshal(resp.Result, &items)
	}

	if len(items) == 0 {
		message("lsp-completion: No completions")
		return 1
	}

	// Create completion buffer
	bufName := C.CString("*lsp-completion*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for _, item := range items {
			kind := completionKindName(item.Kind)
			entry := fmt.Sprintf("%s\t[%s]\t%s\n", item.Label, kind, item.Detail)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d completions", len(items))
	return 1
}

//export go_lsp_diagnostics
func go_lsp_diagnostics(f, n C.int) C.int {
	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		message("lsp-diagnostics: No file")
		return 0
	}

	uri := "file://" + filename
	val, ok := diagnosticCache.Load(uri)
	if !ok {
		message("lsp-diagnostics: No diagnostics")
		return 1
	}

	diags := val.([]Diagnostic)
	if len(diags) == 0 {
		message("lsp-diagnostics: No diagnostics")
		return 1
	}

	// Create diagnostics buffer
	bufName := C.CString("*lsp-diagnostics*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for _, d := range diags {
			severity := "info"
			switch d.Severity {
			case 1:
				severity = "error"
			case 2:
				severity = "warning"
			case 3:
				severity = "info"
			case 4:
				severity = "hint"
			}
			entry := fmt.Sprintf("%d:%d [%s] %s\n", d.Range.Start.Line+1, d.Range.Start.Character+1, severity, d.Message)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d diagnostics", len(diags))
	return 1
}

//export go_lsp_code_action
func go_lsp_code_action(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		message("lsp-code-action: No server")
		return 0
	}

	filename, line, col := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	// Get diagnostics for context
	uri := "file://" + filename
	var diagnostics []map[string]interface{}
	if val, ok := diagnosticCache.Load(uri); ok {
		for _, d := range val.([]Diagnostic) {
			// Only include diagnostics that overlap with cursor
			if d.Range.Start.Line <= line-1 && d.Range.End.Line >= line-1 {
				diagnostics = append(diagnostics, map[string]interface{}{
					"range":    d.Range,
					"severity": d.Severity,
					"message":  d.Message,
				})
			}
		}
	}

	params := map[string]interface{}{
		"textDocument": map[string]string{"uri": uri},
		"range": map[string]interface{}{
			"start": map[string]int{"line": line - 1, "character": col},
			"end":   map[string]int{"line": line - 1, "character": col},
		},
		"context": map[string]interface{}{
			"diagnostics": diagnostics,
		},
	}

	resp, err := c.Request("textDocument/codeAction", params)
	if err != nil {
		message("lsp-code-action: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-code-action: No actions available")
		return 1
	}

	var actions []struct {
		Title string `json:"title"`
		Kind  string `json:"kind"`
	}
	json.Unmarshal(resp.Result, &actions)

	if len(actions) == 0 {
		message("lsp-code-action: No actions available")
		return 1
	}

	// Create actions buffer
	bufName := C.CString("*lsp-actions*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for i, action := range actions {
			entry := fmt.Sprintf("%d. %s [%s]\n", i+1, action.Title, action.Kind)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d code actions", len(actions))
	return 1
}

//export go_lsp_document_symbols
func go_lsp_document_symbols(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		message("lsp-document-symbols: No server")
		return 0
	}

	filename, _, _ := getCurrentBufferInfo()
	if filename == "" {
		return 0
	}

	params := map[string]interface{}{
		"textDocument": map[string]string{"uri": "file://" + filename},
	}

	resp, err := c.Request("textDocument/documentSymbol", params)
	if err != nil {
		message("lsp-document-symbols: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-document-symbols: No symbols")
		return 1
	}

	// Can be DocumentSymbol[] or SymbolInformation[]
	var symbols []struct {
		Name           string `json:"name"`
		Kind           int    `json:"kind"`
		Range          Range  `json:"range"`
		SelectionRange Range  `json:"selectionRange"`
		// For SymbolInformation
		Location struct {
			Range Range `json:"range"`
		} `json:"location"`
	}
	json.Unmarshal(resp.Result, &symbols)

	if len(symbols) == 0 {
		message("lsp-document-symbols: No symbols")
		return 1
	}

	// Create symbols buffer
	bufName := C.CString("*lsp-symbols*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for _, sym := range symbols {
			kind := symbolKindName(sym.Kind)
			line := sym.Range.Start.Line + 1
			if line == 1 && sym.Location.Range.Start.Line > 0 {
				line = sym.Location.Range.Start.Line + 1
			}
			entry := fmt.Sprintf("%s:%d\t[%s]\t%s\n", filepath.Base(filename), line, kind, sym.Name)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d symbols", len(symbols))
	return 1
}

//export go_lsp_workspace_symbols
func go_lsp_workspace_symbols(f, n C.int) C.int {
	c := clientPtr.Load()
	if c == nil {
		message("lsp-workspace-symbols: No server")
		return 0
	}

	// For now, search with empty query to get all symbols
	// Could prompt for query in future
	params := map[string]interface{}{
		"query": "",
	}

	resp, err := c.Request("workspace/symbol", params)
	if err != nil {
		message("lsp-workspace-symbols: %v", err)
		return 0
	}

	if resp.Result == nil || string(resp.Result) == "null" {
		message("lsp-workspace-symbols: No symbols")
		return 1
	}

	var symbols []struct {
		Name     string `json:"name"`
		Kind     int    `json:"kind"`
		Location struct {
			URI   string `json:"uri"`
			Range Range  `json:"range"`
		} `json:"location"`
	}
	json.Unmarshal(resp.Result, &symbols)

	if len(symbols) == 0 {
		message("lsp-workspace-symbols: No symbols")
		return 1
	}

	// Create symbols buffer
	bufName := C.CString("*lsp-workspace-symbols*")
	defer C.free(unsafe.Pointer(bufName))

	buf := C.api_buffer_create(bufName)
	if buf != nil {
		C.api_buffer_switch(buf)
		C.api_buffer_clear(buf)

		for _, sym := range symbols {
			kind := symbolKindName(sym.Kind)
			file := strings.TrimPrefix(sym.Location.URI, "file://")
			line := sym.Location.Range.Start.Line + 1
			entry := fmt.Sprintf("%s:%d\t[%s]\t%s\n", file, line, kind, sym.Name)

			cEntry := C.CString(entry)
			C.api_buffer_insert(cEntry, C.size_t(len(entry)))
			C.free(unsafe.Pointer(cEntry))
		}
	}

	message("%d workspace symbols", len(symbols))
	return 1
}

// Lexer callback - called from C for each line
//export go_lsp_lex_line
func go_lsp_lex_line(
	userData unsafe.Pointer,
	buffer unsafe.Pointer,
	lineNum C.int,
	line *C.char,
	lineLen C.int,
	outTokens unsafe.Pointer,
) {
	if buffer == nil || outTokens == nil {
		return
	}

	c := clientPtr.Load()
	if c == nil {
		return
	}

	// Get tokens for this line
	tokens := getTokensForLine(buffer, int(lineNum))
	if len(tokens) == 0 {
		return
	}

	// Emit tokens
	for _, tok := range tokens {
		face := c.tokenTypeToFace(tok.TokenType)
		endCol := tok.StartChar + tok.Length
		C.api_syntax_add_token(outTokens, C.int(endCol), C.int(face))
	}
}

// =============================================================================
// Helper Functions
// =============================================================================

type Position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type Range struct {
	Start Position `json:"start"`
	End   Position `json:"end"`
}

type Location struct {
	URI   string `json:"uri"`
	Range Range  `json:"range"`
}

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

func detectLanguageID(filename string) string {
	ext := filepath.Ext(filename)
	switch ext {
	case ".py":
		return "python"
	case ".rs":
		return "rust"
	case ".go":
		return "go"
	case ".c", ".h":
		return "c"
	case ".cpp", ".hpp":
		return "cpp"
	case ".js":
		return "javascript"
	case ".ts":
		return "typescript"
	case ".zig":
		return "zig"
	}
	return "plaintext"
}

func parseLocations(data json.RawMessage) []Location {
	var locations []Location
	if err := json.Unmarshal(data, &locations); err != nil {
		var single Location
		if json.Unmarshal(data, &single) == nil {
			locations = []Location{single}
		}
	}
	return locations
}

func extractHoverText(contents interface{}) string {
	switch v := contents.(type) {
	case string:
		return v
	case map[string]interface{}:
		if val, ok := v["value"]; ok {
			return fmt.Sprint(val)
		}
	case []interface{}:
		if len(v) > 0 {
			return extractHoverText(v[0])
		}
	}
	return ""
}

func completionKindName(kind int) string {
	switch kind {
	case 1:
		return "Text"
	case 2:
		return "Method"
	case 3:
		return "Function"
	case 4:
		return "Constructor"
	case 5:
		return "Field"
	case 6:
		return "Variable"
	case 7:
		return "Class"
	case 8:
		return "Interface"
	case 9:
		return "Module"
	case 10:
		return "Property"
	case 11:
		return "Unit"
	case 12:
		return "Value"
	case 13:
		return "Enum"
	case 14:
		return "Keyword"
	case 15:
		return "Snippet"
	case 16:
		return "Color"
	case 17:
		return "File"
	case 18:
		return "Reference"
	case 19:
		return "Folder"
	case 20:
		return "EnumMember"
	case 21:
		return "Constant"
	case 22:
		return "Struct"
	case 23:
		return "Event"
	case 24:
		return "Operator"
	case 25:
		return "TypeParam"
	default:
		return "Unknown"
	}
}

func symbolKindName(kind int) string {
	switch kind {
	case 1:
		return "File"
	case 2:
		return "Module"
	case 3:
		return "Namespace"
	case 4:
		return "Package"
	case 5:
		return "Class"
	case 6:
		return "Method"
	case 7:
		return "Property"
	case 8:
		return "Field"
	case 9:
		return "Constructor"
	case 10:
		return "Enum"
	case 11:
		return "Interface"
	case 12:
		return "Function"
	case 13:
		return "Variable"
	case 14:
		return "Constant"
	case 15:
		return "String"
	case 16:
		return "Number"
	case 17:
		return "Boolean"
	case 18:
		return "Array"
	case 19:
		return "Object"
	case 20:
		return "Key"
	case 21:
		return "Null"
	case 22:
		return "EnumMember"
	case 23:
		return "Struct"
	case 24:
		return "Event"
	case 25:
		return "Operator"
	case 26:
		return "TypeParam"
	default:
		return "Unknown"
	}
}

func message(format string, args ...interface{}) {
	msg := C.CString(fmt.Sprintf(format, args...))
	defer C.free(unsafe.Pointer(msg))
	C.api_message(msg)
}

func logInfo(format string, args ...interface{}) {
	msg := C.CString(fmt.Sprintf(format, args...))
	defer C.free(unsafe.Pointer(msg))
	C.api_log_info(msg)
}

func logError(format string, args ...interface{}) {
	msg := C.CString(fmt.Sprintf(format, args...))
	defer C.free(unsafe.Pointer(msg))
	C.api_log_error(msg)
}

// emitDiagnosticsEvent sends lsp:diagnostics event to the linter
func emitDiagnosticsEvent(uri string, diags []Diagnostic) {
	if len(diags) == 0 {
		return
	}

	// Allocate C array for diagnostics
	cDiags := C.malloc(C.size_t(len(diags)) * C.size_t(unsafe.Sizeof(C.lsp_diag_entry_t{})))
	if cDiags == nil {
		return
	}
	defer C.free(cDiags)

	// Convert to slice for easier indexing
	diagSlice := (*[1 << 20]C.lsp_diag_entry_t)(cDiags)[:len(diags):len(diags)]

	// Allocate C strings (must be freed after emit)
	cURI := C.CString(uri)
	defer C.free(unsafe.Pointer(cURI))

	cMessages := make([]*C.char, len(diags))
	for i, d := range diags {
		cMessages[i] = C.CString(d.Message)
		defer C.free(unsafe.Pointer(cMessages[i]))

		diagSlice[i] = C.lsp_diag_entry_t{
			uri:      cURI,
			line:     C.int(d.Range.Start.Line + 1), // Convert to 1-based
			col:      C.int(d.Range.Start.Character),
			end_col:  C.int(d.Range.End.Character),
			severity: C.int(d.Severity),
			message:  cMessages[i],
		}
	}

	C.api_emit_diagnostics(cURI, (*C.lsp_diag_entry_t)(cDiags), C.int(len(diags)))
}

// main is required but unused for c-shared
func main() {}
