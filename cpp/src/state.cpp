#include "state.hpp"

#include <array>
#include <optional>
#include <string_view>

#include "payload.hpp"

namespace infobot::state {

namespace {

// used_percentage is computed from the input side only, never output.
constexpr std::array<std::string_view, 3> input_keys = {
    "input_tokens",
    "cache_creation_input_tokens",
    "cache_read_input_tokens",
};

constexpr double hundred = 100;

}  // namespace

std::optional<Window> figures(const payload::Map& context_window) {
    const auto size = context_window.num("context_window_size");
    if (!size || *size == 0) {
        return std::nullopt;
    }
    Window window{.used = 0, .size = *size, .percent = 0};
    if (const payload::Map current = context_window.obj("current_usage");
        current.has_entries()) {
        for (const std::string_view key : input_keys) {
            window.used += current.count(key);
        }
    } else {
        window.used = context_window.count("total_input_tokens");
    }
    if (const auto given = context_window.num("used_percentage")) {
        window.percent = *given;
        if (window.used == 0) {
            window.used = window.size * window.percent / hundred;
        }
    } else {
        window.percent = window.used / window.size * hundred;
    }
    return window;
}

}  // namespace infobot::state
