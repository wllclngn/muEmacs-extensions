package sam

import (
	"bytes"
	"fmt"
	"os/exec"
	"regexp"
	"sort"
	"strings"
)

// ExtractCommand implements x/pattern/command and y/pattern/command
type ExtractCommand struct {
	Pattern *regexp.Regexp
	SubCmd  Command
	Inverse bool // true for 'y' (complement)
}

func (c *ExtractCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	text := region.Text(ctx.Buffer)

	if c.Inverse {
		return c.executeY(ctx, region, text)
	}
	return c.executeX(ctx, region, text)
}

func (c *ExtractCommand) executeX(ctx *ExecutionContext, region Region, text string) (string, error) {
	matches := c.Pattern.FindAllStringIndex(text, -1)
	if len(matches) == 0 {
		return "", nil
	}

	// Process matches in reverse order to preserve offsets
	for i := len(matches) - 1; i >= 0; i-- {
		match := matches[i]
		subRegion := Region{
			Start: region.Start + match[0],
			End:   region.Start + match[1],
		}
		_, err := c.SubCmd.Execute(ctx, subRegion)
		if err != nil {
			return "", err
		}
	}

	return ctx.Output.String(), nil
}

func (c *ExtractCommand) executeY(ctx *ExecutionContext, region Region, text string) (string, error) {
	matches := c.Pattern.FindAllStringIndex(text, -1)

	// Build list of non-matching regions
	var regions []Region
	lastEnd := 0

	for _, match := range matches {
		if match[0] > lastEnd {
			regions = append(regions, Region{
				Start: region.Start + lastEnd,
				End:   region.Start + match[0],
			})
		}
		lastEnd = match[1]
	}

	// Add final region after last match
	if lastEnd < len(text) {
		regions = append(regions, Region{
			Start: region.Start + lastEnd,
			End:   region.End,
		})
	}

	// Process in reverse order
	for i := len(regions) - 1; i >= 0; i-- {
		_, err := c.SubCmd.Execute(ctx, regions[i])
		if err != nil {
			return "", err
		}
	}

	return ctx.Output.String(), nil
}

// GuardCommand implements g/pattern/command and v/pattern/command
type GuardCommand struct {
	Pattern *regexp.Regexp
	SubCmd  Command
	Inverse bool // true for 'v' (negative guard)
}

func (c *GuardCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	text := region.Text(ctx.Buffer)
	matches := c.Pattern.MatchString(text)

	// XOR with inverse to determine if we should run
	shouldRun := matches != c.Inverse

	if shouldRun {
		return c.SubCmd.Execute(ctx, region)
	}

	return "", nil
}

// PrintCommand implements 'p' - print region
type PrintCommand struct{}

func (c *PrintCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	text := region.Text(ctx.Buffer)
	ctx.Output.WriteString(text)
	ctx.Output.WriteString("\n")
	return text, nil
}

// DeleteCommand implements 'd' - delete region
type DeleteCommand struct{}

func (c *DeleteCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	ctx.Changes = append(ctx.Changes, Change{
		Start:   region.Start,
		End:     region.End,
		NewText: "",
	})
	return "", nil
}

// ChangeCommand implements c/text/ - replace region with text
type ChangeCommand struct {
	Text string
}

func (c *ChangeCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	ctx.Changes = append(ctx.Changes, Change{
		Start:   region.Start,
		End:     region.End,
		NewText: c.Text,
	})
	return "", nil
}

// AppendCommand implements a/text/ - append text after region
type AppendCommand struct {
	Text string
}

func (c *AppendCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	ctx.Changes = append(ctx.Changes, Change{
		Start:   region.End,
		End:     region.End,
		NewText: c.Text,
	})
	return "", nil
}

// InsertCommand implements i/text/ - insert text before region
type InsertCommand struct {
	Text string
}

func (c *InsertCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	ctx.Changes = append(ctx.Changes, Change{
		Start:   region.Start,
		End:     region.Start,
		NewText: c.Text,
	})
	return "", nil
}

// PipeCommand implements |cmd - pipe region through shell command
type PipeCommand struct {
	ShellCmd string
}

func (c *PipeCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	text := region.Text(ctx.Buffer)

	cmd := exec.Command("sh", "-c", c.ShellCmd)
	cmd.Stdin = strings.NewReader(text)

	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err := cmd.Run()
	if err != nil {
		return "", fmt.Errorf("pipe command failed: %v: %s", err, stderr.String())
	}

	output := stdout.String()

	// Replace region with command output
	ctx.Changes = append(ctx.Changes, Change{
		Start:   region.Start,
		End:     region.End,
		NewText: output,
	})

	return output, nil
}

// GroupCommand implements {cmd1 cmd2 ...}
type GroupCommand struct {
	Commands []Command
}

func (c *GroupCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	for _, cmd := range c.Commands {
		_, err := cmd.Execute(ctx, region)
		if err != nil {
			return "", err
		}
	}
	return ctx.Output.String(), nil
}

// AddressedCommand wraps a command with an address
type AddressedCommand struct {
	Addr Address
	Cmd  Command
}

func (c *AddressedCommand) Execute(ctx *ExecutionContext, region Region) (string, error) {
	// Resolve address relative to current region
	newRegion, err := c.Addr.Resolve(ctx.Buffer, region)
	if err != nil {
		return "", err
	}
	return c.Cmd.Execute(ctx, newRegion)
}

// ApplyChanges applies accumulated changes to the buffer.
// Changes are sorted by start position and applied in reverse order
// to preserve offsets.
func ApplyChanges(buffer string, changes []Change) string {
	if len(changes) == 0 {
		return buffer
	}

	// Sort by start position descending (apply from end to start)
	sorted := make([]Change, len(changes))
	copy(sorted, changes)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i].Start > sorted[j].Start
	})

	result := buffer
	for _, change := range sorted {
		if change.Start < 0 || change.End > len(result) || change.Start > change.End {
			continue // Skip invalid changes
		}
		result = result[:change.Start] + change.NewText + result[change.End:]
	}

	return result
}
