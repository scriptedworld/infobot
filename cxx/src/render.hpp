// The status line: the session payload in, the rows out.
//
// `cmd/statusline.cpp` calls this and nothing else. The modules that turn a
// payload into rows arrive task by task (clank `cpp-clean/rebuild/`), so today
// a render reads its input and prints nothing.
#pragma once

namespace infobot::render {

// Reads the payload from input, writes the rows to output. Always 0: the status
// line exits 0 whatever it is given (FR-1.2).
int statusline(int input, int output) noexcept;

}  // namespace infobot::render
