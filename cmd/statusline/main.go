// Command statusline is the status line's entry point, and nothing else.
//
// The render lives in internal/render, because a body written in an entry point
// can only be exercised by running the binary.
package main

import (
	"os"

	"github.com/scriptedworld/infobot/internal/render"
)

func main() {
	os.Exit(render.Main(os.Stdin, os.Stdout))
}
