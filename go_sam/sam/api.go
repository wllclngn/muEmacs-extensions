// Package sam implements Rob Pike's structural regular expressions.
//
// Structural regular expressions operate on text structure rather than
// just matching content. The key insight is that patterns describe
// regions of text, and commands operate on those regions hierarchically.
//
// Basic commands:
//   x/pattern/command  - for each match of pattern, run command
//   y/pattern/command  - for each text between matches, run command
//   g/pattern/command  - if pattern matches, run command on whole region
//   v/pattern/command  - if pattern doesn't match, run command on whole region
//
// Simple commands:
//   p     - print region
//   d     - delete region
//   c/text/ - change region to text
//   a/text/ - append text after region
//   i/text/ - insert text before region
//   |cmd  - pipe region through shell command
//
// Addresses:
//   .     - current selection (dot)
//   ,     - entire buffer (0,$)
//   #n    - character n
//   n     - line n
//   /re/  - next match of re
//   ?re?  - previous match of re
//   $     - end of file
//
// Commands can be grouped with braces: x/pattern/{g/sub/d}

package sam

// EditorAPI defines the interface to the editor.
// This abstraction allows the sam implementation to be tested independently.
type EditorAPI interface {
	// Message displays a message to the user
	Message(msg string)

	// GetBufferContents returns the entire buffer as a string
	GetBufferContents() (string, error)

	// ReplaceBufferContents replaces the entire buffer
	ReplaceBufferContents(text string) error

	// GetPoint returns current cursor position (line, col)
	GetPoint() (line, col int)

	// SetPoint moves cursor to position
	SetPoint(line, col int)

	// Prompt asks user for input, returns (response, ok)
	Prompt(prompt string) (string, bool)

	// CreateResultsBuffer creates/switches to a results buffer
	CreateResultsBuffer(name string, contents string)

	// Logging
	LogInfo(msg string)
	LogError(msg string)
}

// Region represents a range of text in the buffer.
// Start and End are byte offsets.
type Region struct {
	Start int
	End   int
}

// Text returns the text content of the region from the given buffer.
func (r Region) Text(buffer string) string {
	if r.Start < 0 || r.End > len(buffer) || r.Start > r.End {
		return ""
	}
	return buffer[r.Start:r.End]
}

// Length returns the length of the region in bytes.
func (r Region) Length() int {
	return r.End - r.Start
}

// Empty returns true if the region has zero length.
func (r Region) Empty() bool {
	return r.Start >= r.End
}

// Contains returns true if offset is within the region.
func (r Region) Contains(offset int) bool {
	return offset >= r.Start && offset < r.End
}
