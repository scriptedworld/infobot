// What the program needs from the operating system, as values and small calls,
// so every other module is tested with nothing patched.
#pragma once

#include <string>

namespace infobot::platform {

// Everything a descriptor holds, read to its end. An interrupted read is not
// the end. Throws std::bad_alloc if what arrives cannot be held.
[[nodiscard]] std::string read_all(int descriptor);

}  // namespace infobot::platform
