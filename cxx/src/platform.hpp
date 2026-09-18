// What the program needs from the operating system, as values and small calls,
// so every other module is tested with nothing patched.
#pragma once

namespace infobot::platform {

// Reads a descriptor to its end and discards what it read, so whatever writes
// to it is never left holding a pipe nobody reads.
void drain(int descriptor) noexcept;

}  // namespace infobot::platform
