#include "forget.hpp"

#include <string>

#include "platform.hpp"

namespace infobot::forget {

int run(int input, int /*log*/) noexcept {
    try {
        // As in render::statusline: the payload is read so nothing is left
        // writing into a pipe, and what names a session in it is read by the
        // module that removes its files, which arrives with the cut-over.
        static_cast<void>(platform::read_all(input));
    } catch (...) {
        // FR-1.11f: the cleanup exits 0 whatever it is given, and whatever
        // goes wrong while it reads it.
        return 0;
    }
    return 0;
}

}  // namespace infobot::forget
