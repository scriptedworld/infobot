#include "render.hpp"

#include <string>

#include "platform.hpp"

namespace infobot::render {

int statusline(int input, int /*output*/) noexcept {
    try {
        // The payload is read and discarded: what turns it into rows is
        // payload, config, host, transcripts and the row builder, none of which
        // exist yet. Reading it anyway is what leaves no writer holding a pipe.
        static_cast<void>(platform::read_all(input));
    } catch (...) {
        // A render that cannot finish prints nothing and still exits 0, which
        // is FR-1.2. Everything is caught, not just std::exception: the
        // promise is about what the status bar shows, and it holds whatever
        // was thrown. There is no second row to fall back to and no log to
        // write, since the bar is the only output this program has.
        return 0;
    }
    return 0;
}

}  // namespace infobot::render
