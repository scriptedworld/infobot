// Command forget-session is the SessionEnd entry point, and nothing else.
//
// Claude Code runs the status line once per transcript entry and never again
// once a session ends. Without this hook nothing removes what the last render
// left behind, and the state directory keeps one file for every session ever
// run.
package main

import (
	"os"

	"github.com/scriptedworld/infobot/internal/forget"
)

func main() {
	os.Exit(forget.Main(os.Stdin, os.Stderr))
}
