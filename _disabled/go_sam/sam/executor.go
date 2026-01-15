package sam

import (
	"fmt"
	"strings"
)

// Executor runs sam commands against the editor.
type Executor struct {
	API EditorAPI
}

// NewExecutor creates a new executor with the given editor API.
func NewExecutor(api EditorAPI) *Executor {
	return &Executor{API: api}
}

// Execute parses and runs a sam command string.
func (e *Executor) Execute(cmdStr string) error {
	cmdStr = strings.TrimSpace(cmdStr)
	if cmdStr == "" {
		return fmt.Errorf("empty command")
	}

	// Parse command
	cmd, err := Parse(cmdStr)
	if err != nil {
		return fmt.Errorf("parse error: %w", err)
	}

	// Get buffer contents
	buffer, err := e.API.GetBufferContents()
	if err != nil {
		return fmt.Errorf("getting buffer: %w", err)
	}

	// Create execution context
	ctx := &ExecutionContext{
		Buffer:  buffer,
		Changes: nil,
		API:     e.API,
	}

	// Default region is entire buffer
	region := Region{Start: 0, End: len(buffer)}

	// Execute command
	output, err := cmd.Execute(ctx, region)
	if err != nil {
		return fmt.Errorf("execution error: %w", err)
	}

	// Apply any accumulated changes
	if len(ctx.Changes) > 0 {
		newBuffer := ApplyChanges(ctx.Buffer, ctx.Changes)
		if err := e.API.ReplaceBufferContents(newBuffer); err != nil {
			return fmt.Errorf("applying changes: %w", err)
		}
		e.API.Message(fmt.Sprintf("%d change(s) applied", len(ctx.Changes)))
	}

	// Show output if any
	if ctx.Output.Len() > 0 {
		output = strings.TrimSuffix(ctx.Output.String(), "\n")
		lines := strings.Split(output, "\n")

		if len(lines) <= 3 {
			// Show in message line for short output
			e.API.Message(output)
		} else {
			// Create results buffer for longer output
			e.API.CreateResultsBuffer("*sam-output*", ctx.Output.String())
			e.API.Message(fmt.Sprintf("%d lines of output", len(lines)))
		}
	} else if len(ctx.Changes) == 0 {
		// No output and no changes
		if output == "" {
			e.API.Message("No matches")
		}
	}

	return nil
}

// ExecuteOnRegion runs a command on a specific region.
// This is useful for running commands on selections.
func (e *Executor) ExecuteOnRegion(cmdStr string, start, end int) error {
	cmdStr = strings.TrimSpace(cmdStr)
	if cmdStr == "" {
		return fmt.Errorf("empty command")
	}

	cmd, err := Parse(cmdStr)
	if err != nil {
		return fmt.Errorf("parse error: %w", err)
	}

	buffer, err := e.API.GetBufferContents()
	if err != nil {
		return fmt.Errorf("getting buffer: %w", err)
	}

	// Validate region
	if start < 0 {
		start = 0
	}
	if end > len(buffer) {
		end = len(buffer)
	}
	if start > end {
		start, end = end, start
	}

	ctx := &ExecutionContext{
		Buffer:  buffer,
		Changes: nil,
		API:     e.API,
	}

	region := Region{Start: start, End: end}

	_, err = cmd.Execute(ctx, region)
	if err != nil {
		return err
	}

	if len(ctx.Changes) > 0 {
		newBuffer := ApplyChanges(ctx.Buffer, ctx.Changes)
		if err := e.API.ReplaceBufferContents(newBuffer); err != nil {
			return err
		}
	}

	if ctx.Output.Len() > 0 {
		e.API.Message(strings.TrimSuffix(ctx.Output.String(), "\n"))
	}

	return nil
}

// Help returns a help string describing sam commands.
func Help() string {
	return `Sam Structural Regular Expressions

Commands:
  x/pattern/cmd   For each match of pattern, run cmd
  y/pattern/cmd   For each text between matches, run cmd
  g/pattern/cmd   If pattern matches, run cmd on whole region
  v/pattern/cmd   If pattern doesn't match, run cmd

Simple Commands:
  p               Print region
  d               Delete region
  c/text/         Change region to text
  a/text/         Append text after region
  i/text/         Insert text before region
  |cmd            Pipe region through shell command

Addresses:
  .               Current selection
  ,               Entire buffer (same as 0,$)
  $               End of file
  n               Line n
  #n              Character n
  /pattern/       Next match of pattern
  ?pattern?       Previous match of pattern

Examples:
  x/TODO/p                   Print all lines containing TODO
  x/func.*{/d                Delete all function headers
  ,x/old/c/new/              Replace all 'old' with 'new'
  x/^import/a/ "fmt"/        Add "fmt" after each import
  ,|sort                     Sort entire buffer
  x/error/{g/nil/d}          Delete error checks that use nil

Commands can be grouped with braces:
  x/func/{g/error/p}         Print functions containing 'error'
`
}
