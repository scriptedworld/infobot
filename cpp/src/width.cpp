#include "width.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "gotext.hpp"
#include "width_table.hpp"

namespace infobot::width {

namespace {

constexpr char escape = '\033';

// The length of an SGR escape at the front of text, `ESC [ digits-and-; m`, or
// 0 when text does not open with one. The Go renderer strips exactly this
// pattern and nothing wider.
std::size_t sgr_length(std::string_view text) {
    if (text.size() < 3 || text[0] != escape || text[1] != '[') {
        return 0;
    }
    std::size_t at = 2;
    while (at < text.size() &&
           ((text[at] >= '0' && text[at] <= '9') || text[at] == ';')) {
        ++at;
    }
    return at < text.size() && text[at] == 'm' ? at + 1 : 0;
}

}  // namespace

int rune_width(char32_t rune) {
    const auto ranges = table();
    const auto found = std::ranges::upper_bound(
        ranges, rune, {}, [](const Range& range) { return range.low; });
    if (found == ranges.begin()) {
        return 1;
    }
    const Range& candidate = *(found - 1);
    return rune <= candidate.high ? candidate.width : 1;
}

int visible(std::string_view text) {
    // Stripped first and decoded after, as the Go renderer does, so an escape
    // sitting between the bytes of one character joins them back up.
    std::string stripped;
    stripped.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t skip = sgr_length(text.substr(at));
        if (skip > 0) {
            at += skip;
            continue;
        }
        stripped.push_back(text[at]);
        ++at;
    }
    int total = 0;
    for (std::string_view rest = stripped; !rest.empty();) {
        const gotext::Decoded decoded = gotext::decode_rune(rest);
        total += rune_width(decoded.rune);
        rest.remove_prefix(decoded.size);
    }
    return total;
}

}  // namespace infobot::width
