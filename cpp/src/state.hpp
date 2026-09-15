// What the session's context window is doing, for the bar and for a file.
//
// The bar shows it to a person and the state file leaves it where a program can
// read it. Both come through figures() so the two cannot disagree.
#pragma once

#include <optional>

namespace infobot::payload {
class Map;
}  // namespace infobot::payload

namespace infobot::state {

struct Window {
    double used = 0;
    double size = 0;
    double percent = 0;
};

// The context window, or nullopt when it cannot be said at all.
//
// current_usage is the authority when present and total_input_tokens the
// fallback, matching used_percentage's own formula. Counts absent beside a
// percentage are derived from it, so the two halves agree rather than one
// reporting a literal zero.
[[nodiscard]] std::optional<Window> figures(const payload::Map& context_window);

}  // namespace infobot::state
