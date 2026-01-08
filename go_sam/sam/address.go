package sam

import (
	"fmt"
	"regexp"
	"strings"
)

// Address represents a text address in sam.
type Address interface {
	// Resolve converts the address to a concrete region.
	// The current region provides context for relative addresses.
	Resolve(buffer string, current Region) (Region, error)
}

// DotAddress represents '.' - the current selection
type DotAddress struct{}

func (a *DotAddress) Resolve(buffer string, current Region) (Region, error) {
	return current, nil
}

// EndAddress represents '$' - end of file
type EndAddress struct{}

func (a *EndAddress) Resolve(buffer string, current Region) (Region, error) {
	return Region{Start: len(buffer), End: len(buffer)}, nil
}

// CharAddress represents '#n' - character offset
type CharAddress struct {
	Offset int
}

func (a *CharAddress) Resolve(buffer string, current Region) (Region, error) {
	if a.Offset < 0 || a.Offset > len(buffer) {
		return Region{}, fmt.Errorf("character offset %d out of range", a.Offset)
	}
	return Region{Start: a.Offset, End: a.Offset}, nil
}

// LineAddress represents 'n' - line number
type LineAddress struct {
	Line int
}

func (a *LineAddress) Resolve(buffer string, current Region) (Region, error) {
	lines := strings.Split(buffer, "\n")

	if a.Line < 0 {
		return Region{}, fmt.Errorf("negative line number")
	}

	// Line 0 means beginning of file
	if a.Line == 0 {
		return Region{Start: 0, End: 0}, nil
	}

	// Adjust for 1-based line numbers
	lineIdx := a.Line - 1
	if lineIdx >= len(lines) {
		// Beyond end of file - return end
		return Region{Start: len(buffer), End: len(buffer)}, nil
	}

	// Calculate byte offset
	offset := 0
	for i := 0; i < lineIdx; i++ {
		offset += len(lines[i]) + 1 // +1 for newline
	}

	lineEnd := offset + len(lines[lineIdx])
	if lineIdx < len(lines)-1 {
		lineEnd++ // Include newline except for last line
	}

	return Region{Start: offset, End: lineEnd}, nil
}

// RegexAddress represents '/pattern/' or '?pattern?'
type RegexAddress struct {
	Pattern *regexp.Regexp
	Forward bool // true for /, false for ?
}

func (a *RegexAddress) Resolve(buffer string, current Region) (Region, error) {
	if a.Forward {
		return a.resolveForward(buffer, current)
	}
	return a.resolveBackward(buffer, current)
}

func (a *RegexAddress) resolveForward(buffer string, current Region) (Region, error) {
	// Search from end of current selection
	searchStart := current.End
	if searchStart > len(buffer) {
		searchStart = len(buffer)
	}

	// First try searching after current position
	loc := a.Pattern.FindStringIndex(buffer[searchStart:])
	if loc != nil {
		return Region{
			Start: searchStart + loc[0],
			End:   searchStart + loc[1],
		}, nil
	}

	// Wrap around to beginning
	loc = a.Pattern.FindStringIndex(buffer[:searchStart])
	if loc != nil {
		return Region{
			Start: loc[0],
			End:   loc[1],
		}, nil
	}

	return Region{}, fmt.Errorf("pattern not found: %s", a.Pattern.String())
}

func (a *RegexAddress) resolveBackward(buffer string, current Region) (Region, error) {
	// Search backward from start of current selection
	searchEnd := current.Start

	// Find all matches in the buffer before current position
	matches := a.Pattern.FindAllStringIndex(buffer[:searchEnd], -1)
	if len(matches) > 0 {
		last := matches[len(matches)-1]
		return Region{
			Start: last[0],
			End:   last[1],
		}, nil
	}

	// Wrap around to end
	matches = a.Pattern.FindAllStringIndex(buffer[searchEnd:], -1)
	if len(matches) > 0 {
		last := matches[len(matches)-1]
		return Region{
			Start: searchEnd + last[0],
			End:   searchEnd + last[1],
		}, nil
	}

	return Region{}, fmt.Errorf("pattern not found: %s", a.Pattern.String())
}

// RangeAddress represents 'addr1,addr2' - a range of text
type RangeAddress struct {
	Start Address
	End   Address
}

func (a *RangeAddress) Resolve(buffer string, current Region) (Region, error) {
	startRegion, err := a.Start.Resolve(buffer, current)
	if err != nil {
		return Region{}, fmt.Errorf("resolving start address: %w", err)
	}

	// Use start region as context for end address
	endRegion, err := a.End.Resolve(buffer, startRegion)
	if err != nil {
		return Region{}, fmt.Errorf("resolving end address: %w", err)
	}

	return Region{
		Start: startRegion.Start,
		End:   endRegion.End,
	}, nil
}

// CompositeAddress represents 'addr1 op addr2' operations like +, -
type CompositeAddress struct {
	Base   Address
	Offset Address
	Op     byte // '+' or '-'
}

func (a *CompositeAddress) Resolve(buffer string, current Region) (Region, error) {
	baseRegion, err := a.Base.Resolve(buffer, current)
	if err != nil {
		return Region{}, err
	}

	// Resolve offset relative to base
	return a.Offset.Resolve(buffer, baseRegion)
}
