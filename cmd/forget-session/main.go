// Command forget-session is the SessionEnd entry point, and nothing else.
//
// Claude Code runs the status line once per transcript entry and never again
// once a session ends, so nothing would remove what the last render left
// behind. One file per session, forever, is what this exists to prevent.
package main

import (
	"os"

	"github.com/scriptedworld/infobot/internal/forget"
)

func main() {
	os.Exit(forget.Main(os.Stdin, os.Stderr))
}
