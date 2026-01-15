package sam

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"unicode/utf8"
)

// Command represents a parsed sam command.
type Command interface {
	// Execute runs the command on the given region of the buffer.
	// Returns the modified buffer and any output.
	Execute(ctx *ExecutionContext, region Region) (output string, err error)
}

// ExecutionContext holds state during command execution.
type ExecutionContext struct {
	Buffer  string    // Current buffer contents
	Changes []Change  // Accumulated changes (applied in reverse order)
	Output  strings.Builder // Accumulated output
	API     EditorAPI // Editor interface
}

// Change represents a text modification.
type Change struct {
	Start   int    // Start offset
	End     int    // End offset (text to delete)
	NewText string // Replacement text
}

// Parser parses sam command strings into Command trees.
type Parser struct {
	input string
	pos   int
}

// Parse parses a complete sam command string.
func Parse(input string) (Command, error) {
	p := &Parser{input: input, pos: 0}
	return p.parseCommand()
}

func (p *Parser) parseCommand() (Command, error) {
	p.skipWhitespace()

	if p.atEnd() {
		return nil, fmt.Errorf("empty command")
	}

	// Check for address prefix
	var addr Address
	if p.isAddressStart() {
		var err error
		addr, err = p.parseAddress()
		if err != nil {
			return nil, err
		}
	}

	p.skipWhitespace()
	if p.atEnd() {
		// Address alone means print
		if addr != nil {
			return &AddressedCommand{Addr: addr, Cmd: &PrintCommand{}}, nil
		}
		return nil, fmt.Errorf("expected command after address")
	}

	cmd, err := p.parseSingleCommand()
	if err != nil {
		return nil, err
	}

	if addr != nil {
		return &AddressedCommand{Addr: addr, Cmd: cmd}, nil
	}

	return cmd, nil
}

func (p *Parser) parseSingleCommand() (Command, error) {
	c := p.peek()

	switch c {
	case 'x':
		return p.parseExtract(false)
	case 'y':
		return p.parseExtract(true)
	case 'g':
		return p.parseGuard(false)
	case 'v':
		return p.parseGuard(true)
	case 'p':
		p.advance()
		return &PrintCommand{}, nil
	case 'd':
		p.advance()
		return &DeleteCommand{}, nil
	case 'c':
		return p.parseChange()
	case 'a':
		return p.parseAppend()
	case 'i':
		return p.parseInsert()
	case '|':
		return p.parsePipe()
	case '{':
		return p.parseGroup()
	default:
		return nil, fmt.Errorf("unknown command: %c", c)
	}
}

// parseExtract parses x/pattern/command or y/pattern/command
func (p *Parser) parseExtract(inverse bool) (Command, error) {
	p.advance() // consume 'x' or 'y'

	delim, err := p.readDelimiter()
	if err != nil {
		return nil, err
	}

	pattern, err := p.readDelimited(delim)
	if err != nil {
		return nil, fmt.Errorf("reading pattern: %w", err)
	}

	re, err := regexp.Compile(pattern)
	if err != nil {
		return nil, fmt.Errorf("invalid regex %q: %w", pattern, err)
	}

	p.skipWhitespace()

	var subCmd Command
	if !p.atEnd() && p.peek() != '}' {
		subCmd, err = p.parseSingleCommand()
		if err != nil {
			return nil, err
		}
	} else {
		subCmd = &PrintCommand{} // Default to print
	}

	return &ExtractCommand{
		Pattern: re,
		SubCmd:  subCmd,
		Inverse: inverse,
	}, nil
}

// parseGuard parses g/pattern/command or v/pattern/command
func (p *Parser) parseGuard(inverse bool) (Command, error) {
	p.advance() // consume 'g' or 'v'

	delim, err := p.readDelimiter()
	if err != nil {
		return nil, err
	}

	pattern, err := p.readDelimited(delim)
	if err != nil {
		return nil, fmt.Errorf("reading pattern: %w", err)
	}

	re, err := regexp.Compile(pattern)
	if err != nil {
		return nil, fmt.Errorf("invalid regex %q: %w", pattern, err)
	}

	p.skipWhitespace()

	var subCmd Command
	if !p.atEnd() && p.peek() != '}' {
		subCmd, err = p.parseSingleCommand()
		if err != nil {
			return nil, err
		}
	} else {
		subCmd = &PrintCommand{}
	}

	return &GuardCommand{
		Pattern: re,
		SubCmd:  subCmd,
		Inverse: inverse,
	}, nil
}

// parseChange parses c/replacement/
func (p *Parser) parseChange() (Command, error) {
	p.advance() // consume 'c'

	delim, err := p.readDelimiter()
	if err != nil {
		return nil, err
	}

	text, err := p.readDelimited(delim)
	if err != nil {
		return nil, err
	}

	return &ChangeCommand{Text: text}, nil
}

// parseAppend parses a/text/
func (p *Parser) parseAppend() (Command, error) {
	p.advance() // consume 'a'

	delim, err := p.readDelimiter()
	if err != nil {
		return nil, err
	}

	text, err := p.readDelimited(delim)
	if err != nil {
		return nil, err
	}

	return &AppendCommand{Text: text}, nil
}

// parseInsert parses i/text/
func (p *Parser) parseInsert() (Command, error) {
	p.advance() // consume 'i'

	delim, err := p.readDelimiter()
	if err != nil {
		return nil, err
	}

	text, err := p.readDelimited(delim)
	if err != nil {
		return nil, err
	}

	return &InsertCommand{Text: text}, nil
}

// parsePipe parses |command
func (p *Parser) parsePipe() (Command, error) {
	p.advance() // consume '|'

	// Read rest of line as shell command
	cmd := strings.TrimSpace(p.remaining())
	p.pos = len(p.input)

	if cmd == "" {
		return nil, fmt.Errorf("empty pipe command")
	}

	return &PipeCommand{ShellCmd: cmd}, nil
}

// parseGroup parses {cmd1 cmd2 ...}
func (p *Parser) parseGroup() (Command, error) {
	p.advance() // consume '{'

	var commands []Command

	for {
		p.skipWhitespace()

		if p.atEnd() {
			return nil, fmt.Errorf("unclosed brace")
		}

		if p.peek() == '}' {
			p.advance()
			break
		}

		cmd, err := p.parseSingleCommand()
		if err != nil {
			return nil, err
		}
		commands = append(commands, cmd)
	}

	return &GroupCommand{Commands: commands}, nil
}

// Address parsing

func (p *Parser) isAddressStart() bool {
	if p.atEnd() {
		return false
	}
	c := p.peek()
	return c == '.' || c == ',' || c == '$' || c == '#' ||
		c == '/' || c == '?' || (c >= '0' && c <= '9')
}

func (p *Parser) parseAddress() (Address, error) {
	var left Address

	// Parse simple address
	c := p.peek()
	switch {
	case c == '.':
		p.advance()
		left = &DotAddress{}
	case c == ',':
		// Comma alone means 0,$
		left = &RangeAddress{
			Start: &LineAddress{Line: 0},
			End:   &EndAddress{},
		}
		p.advance()
		return left, nil
	case c == '$':
		p.advance()
		left = &EndAddress{}
	case c == '#':
		p.advance()
		n, err := p.parseNumber()
		if err != nil {
			return nil, err
		}
		left = &CharAddress{Offset: n}
	case c >= '0' && c <= '9':
		n, err := p.parseNumber()
		if err != nil {
			return nil, err
		}
		left = &LineAddress{Line: n}
	case c == '/':
		p.advance()
		pattern, err := p.readDelimited('/')
		if err != nil {
			return nil, err
		}
		re, err := regexp.Compile(pattern)
		if err != nil {
			return nil, err
		}
		left = &RegexAddress{Pattern: re, Forward: true}
	case c == '?':
		p.advance()
		pattern, err := p.readDelimited('?')
		if err != nil {
			return nil, err
		}
		re, err := regexp.Compile(pattern)
		if err != nil {
			return nil, err
		}
		left = &RegexAddress{Pattern: re, Forward: false}
	default:
		return nil, fmt.Errorf("invalid address start: %c", c)
	}

	// Check for range
	p.skipWhitespace()
	if !p.atEnd() && p.peek() == ',' {
		p.advance()
		right, err := p.parseAddress()
		if err != nil {
			return nil, err
		}
		return &RangeAddress{Start: left, End: right}, nil
	}

	return left, nil
}

func (p *Parser) parseNumber() (int, error) {
	start := p.pos
	for !p.atEnd() && p.peek() >= '0' && p.peek() <= '9' {
		p.advance()
	}
	if start == p.pos {
		return 0, fmt.Errorf("expected number")
	}
	return strconv.Atoi(p.input[start:p.pos])
}

// Helper methods

func (p *Parser) peek() byte {
	if p.atEnd() {
		return 0
	}
	return p.input[p.pos]
}

func (p *Parser) advance() {
	if !p.atEnd() {
		p.pos++
	}
}

func (p *Parser) atEnd() bool {
	return p.pos >= len(p.input)
}

func (p *Parser) skipWhitespace() {
	for !p.atEnd() && (p.peek() == ' ' || p.peek() == '\t' || p.peek() == '\n') {
		p.advance()
	}
}

func (p *Parser) remaining() string {
	return p.input[p.pos:]
}

func (p *Parser) readDelimiter() (byte, error) {
	if p.atEnd() {
		return 0, fmt.Errorf("expected delimiter")
	}
	delim := p.peek()
	p.advance()
	return delim, nil
}

func (p *Parser) readDelimited(delim byte) (string, error) {
	var result strings.Builder

	for !p.atEnd() {
		c := p.peek()
		if c == delim {
			p.advance()
			return result.String(), nil
		}
		if c == '\\' && p.pos+1 < len(p.input) {
			p.advance()
			next := p.peek()
			// Handle escape sequences
			switch next {
			case 'n':
				result.WriteByte('\n')
			case 't':
				result.WriteByte('\t')
			case '\\':
				result.WriteByte('\\')
			default:
				if next == delim {
					result.WriteByte(delim)
				} else {
					result.WriteByte('\\')
					result.WriteByte(next)
				}
			}
			p.advance()
		} else {
			_, size := utf8.DecodeRuneInString(p.input[p.pos:])
			result.WriteString(p.input[p.pos : p.pos+size])
			p.pos += size
		}
	}

	return "", fmt.Errorf("unterminated delimited string")
}
