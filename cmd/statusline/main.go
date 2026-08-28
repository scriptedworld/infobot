// Command statusline is the status line's entry point, and nothing else.
//
// The render lives in internal/render, where a test can reach it and a checker
// can read it. A body written in an entry point can only be exercised by
// running the binary.
package main

import (
	"os"

	"github.com/scriptedworld/infobot/internal/render"
)

func main() {
	os.Exit(render.Main(os.Stdin, os.Stdout))
}
