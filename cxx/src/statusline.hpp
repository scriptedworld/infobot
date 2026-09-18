// The status line, from the payload on one descriptor to the rows on another.
//
// This is the whole program as `cmd/statusline.cpp` calls it. It exits 0
// whatever it is given (FR-1.2). The rows are built by the modules docs/SPEC.md
// names, which arrive task by task; until they do, a render reads its input to
// the end and writes nothing.
#pragma once

namespace infobot::statusline {

// Reads the payload from input and writes the rows to output. Always 0.
int run(int input, int output) noexcept;

}  // namespace infobot::statusline
