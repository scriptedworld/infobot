// The SessionEnd cleanup, from a payload naming a session to its two state
// files removed.
//
// `cmd/forget.cpp` calls this and nothing else. It exits 0 whatever it is given
// (FR-1.11f). The removal arrives with the rebuild's cut-over task; until then it
// reads its input to the end and removes nothing.
#pragma once

namespace infobot::forget {

// Reads the payload from input and logs what was removed to log. Always 0.
int run(int input, int log) noexcept;

}  // namespace infobot::forget
