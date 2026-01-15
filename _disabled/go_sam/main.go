// go_sam - Rob Pike's structural regular expressions for μEmacs
//
// Implements sam-style text manipulation commands:
//   x/pattern/command  - extract: run command on each match
//   y/pattern/command  - inverse: run command on non-matches
//   g/pattern/command  - guard: run command if pattern exists
//   v/pattern/command  - inverse guard: run if no match
//
// Commands can nest for hierarchical text manipulation.
//
// Built with CGO as a shared library for μEmacs extension system.

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Bridge function declarations (implemented in bridge.c)
// These are the only C functions Go code needs to call.
extern void api_message(const char *msg);
extern void *api_current_buffer(void);
extern char *api_buffer_contents(void *bp, size_t *len);
extern const char *api_buffer_filename(void *bp);
extern const char *api_buffer_name(void *bp);
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
extern int api_delete_chars(int n);
*/
import "C"

import (
	"fmt"
	"go_sam/sam"
	"strings"
	"unsafe"
)


// Global executor instance
var executor *sam.Executor

//export sam_init
func sam_init(api unsafe.Pointer) {
	// Create executor with API bridge
	executor = sam.NewExecutor(&apiBridge{})
}

// apiBridge implements sam.EditorAPI using CGO calls
type apiBridge struct{}

func (a *apiBridge) Message(msg string) {
	cmsg := C.CString(msg)
	C.api_message(cmsg)
	C.free(unsafe.Pointer(cmsg))
}

func (a *apiBridge) GetBufferContents() (string, error) {
	bp := C.api_current_buffer()
	if bp == nil {
		return "", fmt.Errorf("no current buffer")
	}

	var length C.size_t
	cContents := C.api_buffer_contents(bp, &length)
	if cContents == nil {
		return "", fmt.Errorf("failed to get buffer contents")
	}
	defer C.api_free(unsafe.Pointer(cContents))

	return C.GoStringN(cContents, C.int(length)), nil
}

func (a *apiBridge) ReplaceBufferContents(text string) error {
	bp := C.api_current_buffer()
	if bp == nil {
		return fmt.Errorf("no current buffer")
	}

	// Clear buffer
	C.api_buffer_clear(bp)

	// Insert new content
	if len(text) > 0 {
		ctext := C.CString(text)
		C.api_buffer_insert(ctext, C.size_t(len(text)))
		C.free(unsafe.Pointer(ctext))
	}

	return nil
}

func (a *apiBridge) GetPoint() (line, col int) {
	var cline, ccol C.int
	C.api_get_point(&cline, &ccol)
	return int(cline), int(ccol)
}

func (a *apiBridge) SetPoint(line, col int) {
	C.api_set_point(C.int(line), C.int(col))
}

func (a *apiBridge) Prompt(prompt string) (string, bool) {
	cprompt := C.CString(prompt)
	defer C.free(unsafe.Pointer(cprompt))

	var buf [1024]C.char
	result := C.api_prompt(cprompt, &buf[0], 1024)
	if result != 0 {
		return "", false // Cancelled
	}

	return C.GoString(&buf[0]), true
}

func (a *apiBridge) CreateResultsBuffer(name string, contents string) {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))

	bp := C.api_buffer_create(cname)
	if bp != nil {
		C.api_buffer_switch(bp)
		C.api_buffer_clear(bp)
		if len(contents) > 0 {
			ctext := C.CString(contents)
			C.api_buffer_insert(ctext, C.size_t(len(contents)))
			C.free(unsafe.Pointer(ctext))
		}
	}
}

func (a *apiBridge) LogInfo(msg string) {
	cmsg := C.CString(msg)
	C.api_log_info(cmsg)
	C.free(unsafe.Pointer(cmsg))
}

func (a *apiBridge) LogError(msg string) {
	cmsg := C.CString(msg)
	C.api_log_error(cmsg)
	C.free(unsafe.Pointer(cmsg))
}

// Command handlers

//export go_sam_x
func go_sam_x(f, n C.int) C.int {
	return runStructuralCommand("x")
}

//export go_sam_y
func go_sam_y(f, n C.int) C.int {
	return runStructuralCommand("y")
}

//export go_sam_g
func go_sam_g(f, n C.int) C.int {
	return runStructuralCommand("g")
}

//export go_sam_v
func go_sam_v(f, n C.int) C.int {
	return runStructuralCommand("v")
}

//export go_sam_edit
func go_sam_edit(f, n C.int) C.int {
	// Full structural regex command line
	api := &apiBridge{}

	input, ok := api.Prompt("Sam command: ")
	if !ok || strings.TrimSpace(input) == "" {
		api.Message("Cancelled")
		return 0
	}

	err := executor.Execute(input)
	if err != nil {
		api.Message(fmt.Sprintf("Error: %v", err))
		return 0
	}

	return 1
}

//export go_sam_pipe
func go_sam_pipe(f, n C.int) C.int {
	api := &apiBridge{}

	cmd, ok := api.Prompt("Pipe through: ")
	if !ok || strings.TrimSpace(cmd) == "" {
		api.Message("Cancelled")
		return 0
	}

	// Pipe entire buffer through shell command
	err := executor.Execute(fmt.Sprintf(",|%s", cmd))
	if err != nil {
		api.Message(fmt.Sprintf("Error: %v", err))
		return 0
	}

	return 1
}

//export go_sam_help
func go_sam_help(f, n C.int) C.int {
	api := &apiBridge{}
	helpText := sam.Help()
	api.CreateResultsBuffer("*sam-help*", helpText)
	api.Message("Sam help displayed in *sam-help* buffer")
	return 1
}

// runStructuralCommand prompts for pattern and runs single structural command
func runStructuralCommand(cmdType string) C.int {
	api := &apiBridge{}

	pattern, ok := api.Prompt(fmt.Sprintf("%s/pattern/: ", cmdType))
	if !ok {
		api.Message("Cancelled")
		return 0
	}

	// For simple commands, default to 'p' (print) if no subcommand
	cmdStr := fmt.Sprintf("%s/%s/p", cmdType, pattern)

	err := executor.Execute(cmdStr)
	if err != nil {
		api.Message(fmt.Sprintf("Error: %v", err))
		return 0
	}

	return 1
}

func main() {
	// Required for CGO shared library, but never called
}
