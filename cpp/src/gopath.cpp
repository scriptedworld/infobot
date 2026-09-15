#include "gopath.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gotext.hpp"

namespace infobot::gopath {

namespace {

constexpr char separator = '/';

bool is_separator(char c) { return c == separator; }

// Whether the bytes at r begin a `.` or `..` element.
bool dot_at(std::string_view path, std::size_t r) {
    return path[r] == '.' && (r + 1 == path.size() || is_separator(path[r + 1]));
}

bool dot_dot_at(std::string_view path, std::size_t r) {
    return path[r] == '.' && r + 1 < path.size() && path[r + 1] == '.' &&
           (r + 2 == path.size() || is_separator(path[r + 2]));
}

// Undo the last element written, stopping at the floor `..` elements set.
void back_up(std::string& out, std::size_t floor) {
    while (out.size() > floor && !is_separator(out.back())) {
        out.pop_back();
    }
    if (out.size() > floor) {
        out.pop_back();
    }
}

// Clean's working state: the path being read, where the read is, what has been
// written, and how far back a `..` may undo.
struct Cleaning {
    std::string_view path;
    bool rooted = false;
    std::string out;
    std::size_t r = 0;
    std::size_t dotdot = 0;
};

// A `..` element: undo the last name written, or keep the `..` when there is
// nothing left to undo and the path is relative.
void clean_parent(Cleaning& c) {
    c.r += 2;
    if (c.out.size() > c.dotdot) {
        back_up(c.out, c.dotdot);
        return;
    }
    if (c.rooted) {
        return;
    }
    if (!c.out.empty()) {
        c.out.push_back(separator);
    }
    c.out.append("..");
    c.dotdot = c.out.size();
}

// A name element, copied through with one separator before it.
void clean_name(Cleaning& c) {
    const bool first = c.rooted ? c.out.size() == 1 : c.out.empty();
    if (!first) {
        c.out.push_back(separator);
    }
    for (; c.r < c.path.size() && !is_separator(c.path[c.r]); ++c.r) {
        c.out.push_back(c.path[c.r]);
    }
}

}  // namespace

std::string clean(std::string_view path) {
    if (path.empty()) {
        return ".";
    }
    Cleaning c{.path = path, .rooted = is_separator(path[0]), .out = {}, .r = 0, .dotdot = 0};
    if (c.rooted) {
        c.out.push_back(separator);
        c.r = 1;
        c.dotdot = 1;
    }
    while (c.r < path.size()) {
        if (is_separator(path[c.r]) || dot_at(path, c.r)) {
            ++c.r;
        } else if (dot_dot_at(path, c.r)) {
            clean_parent(c);
        } else {
            clean_name(c);
        }
    }
    if (c.out.empty()) {
        c.out.push_back('.');
    }
    return c.out;
}

std::string join(std::initializer_list<std::string_view> elements) {
    std::string joined;
    bool started = false;
    for (const std::string_view element : elements) {
        if (!started && element.empty()) {
            continue;
        }
        if (started) {
            joined.push_back(separator);
        }
        joined.append(element);
        started = true;
    }
    return started ? clean(joined) : std::string{};
}

Split split(std::string_view path) {
    const std::size_t last = path.rfind(separator);
    if (last == std::string_view::npos) {
        return {.dir = std::string_view{}, .file = path};
    }
    return {.dir = path.substr(0, last + 1), .file = path.substr(last + 1)};
}

std::string base(std::string_view path) {
    if (path.empty()) {
        return ".";
    }
    while (!path.empty() && is_separator(path.back())) {
        path.remove_suffix(1);
    }
    const std::size_t last = path.rfind(separator);
    if (last != std::string_view::npos) {
        path.remove_prefix(last + 1);
    }
    if (path.empty()) {
        return "/";
    }
    return std::string(path);
}

std::string dir(std::string_view path) { return clean(split(path).dir); }

std::string_view ext(std::string_view path) {
    for (std::size_t i = path.size(); i > 0 && !is_separator(path[i - 1]); --i) {
        if (path[i - 1] == '.') {
            return path.substr(i - 1);
        }
    }
    return {};
}

std::string stem(std::string_view path) {
    const std::string name = base(path);
    return name.substr(0, name.size() - ext(name).size());
}

namespace {

struct Chunk {
    bool star;
    std::string_view chunk;
    std::string_view rest;
};

// scanChunk: leading stars, then everything up to the next star outside a
// character class.
Chunk scan_chunk(std::string_view pattern) {
    bool star = false;
    while (!pattern.empty() && pattern[0] == '*') {
        pattern.remove_prefix(1);
        star = true;
    }
    bool in_range = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        switch (pattern[i]) {
            case '\\':
                if (i + 1 < pattern.size()) {
                    ++i;
                }
                break;
            case '[':
                in_range = true;
                break;
            case ']':
                in_range = false;
                break;
            case '*':
                if (!in_range) {
                    return {.star = star,
                            .chunk = pattern.substr(0, i),
                            .rest = pattern.substr(i)};
                }
                break;
            default:
                break;
        }
    }
    return {.star = star, .chunk = pattern, .rest = std::string_view{}};
}

struct Escaped {
    char32_t rune;
    std::string_view rest;
};

// getEsc: one possibly escaped rune from a character class. nullopt is a bad
// pattern.
std::optional<Escaped> get_esc(std::string_view chunk) {
    if (chunk.empty() || chunk[0] == '-' || chunk[0] == ']') {
        return std::nullopt;
    }
    if (chunk[0] == '\\') {
        chunk.remove_prefix(1);
        if (chunk.empty()) {
            return std::nullopt;
        }
    }
    const gotext::Decoded decoded = gotext::decode_rune(chunk);
    const std::string_view rest = chunk.substr(decoded.size);
    if ((decoded.rune == gotext::rune_error && decoded.size == 1) || rest.empty()) {
        return std::nullopt;
    }
    return Escaped{.rune = decoded.rune, .rest = rest};
}

struct Matched {
    std::string_view rest;
    bool ok;
};

// A character class at the front of chunk, matched against rune. It reports
// the chunk after the class, or nullopt for a bad pattern.
struct Class {
    std::string_view rest;
    bool matched;
};

std::optional<Class> match_class(std::string_view chunk, char32_t rune) {
    bool negated = false;
    if (!chunk.empty() && chunk[0] == '^') {
        negated = true;
        chunk.remove_prefix(1);
    }
    bool matched = false;
    for (int ranges = 0;; ++ranges) {
        if (!chunk.empty() && chunk[0] == ']' && ranges > 0) {
            chunk.remove_prefix(1);
            break;
        }
        const auto low = get_esc(chunk);
        if (!low) {
            return std::nullopt;
        }
        char32_t high = low->rune;
        chunk = low->rest;
        if (chunk[0] == '-') {
            const auto upper = get_esc(chunk.substr(1));
            if (!upper) {
                return std::nullopt;
            }
            high = upper->rune;
            chunk = upper->rest;
        }
        matched = matched || (low->rune <= rune && rune <= high);
    }
    return Class{.rest = chunk, .matched = matched != negated};
}

// matchChunk's working state: the chunk still to read, the name still to match,
// and whether the match has already failed. After a failure the chunk is still
// walked to the end, so a malformed pattern is reported whatever the name was.
struct Chunking {
    std::string_view chunk;
    std::string_view s;
    bool failed = false;
};

// A character class. False for a bad pattern.
bool step_class(Chunking& c) {
    char32_t rune = 0;
    if (!c.failed) {
        const gotext::Decoded decoded = gotext::decode_rune(c.s);
        rune = decoded.rune;
        c.s.remove_prefix(decoded.size);
    }
    const auto cls = match_class(c.chunk.substr(1), rune);
    if (!cls) {
        return false;
    }
    c.chunk = cls->rest;
    c.failed = c.failed || !cls->matched;
    return true;
}

// `?`, any one rune but a separator.
void step_any(Chunking& c) {
    if (!c.failed) {
        c.failed = is_separator(c.s[0]);
        c.s.remove_prefix(gotext::decode_rune(c.s).size);
    }
    c.chunk.remove_prefix(1);
}

// A literal byte, possibly escaped. False for a trailing backslash.
bool step_literal(Chunking& c) {
    if (c.chunk[0] == '\\') {
        c.chunk.remove_prefix(1);
        if (c.chunk.empty()) {
            return false;
        }
    }
    if (!c.failed) {
        c.failed = c.chunk[0] != c.s[0];
        c.s.remove_prefix(1);
    }
    c.chunk.remove_prefix(1);
    return true;
}

std::optional<Matched> match_chunk(std::string_view chunk, std::string_view s) {
    Chunking c{.chunk = chunk, .s = s, .failed = false};
    while (!c.chunk.empty()) {
        c.failed = c.failed || c.s.empty();
        bool well_formed = true;
        if (c.chunk[0] == '[') {
            well_formed = step_class(c);
        } else if (c.chunk[0] == '?') {
            step_any(c);
        } else {
            well_formed = step_literal(c);
        }
        if (!well_formed) {
            return std::nullopt;
        }
    }
    if (c.failed) {
        return Matched{.rest = std::string_view{}, .ok = false};
    }
    return Matched{.rest = c.s, .ok = true};
}

// Where one chunk leaves the name. The outer nullopt is a bad pattern, and an
// inner nullopt is no match.
using Advance = std::optional<std::optional<std::string_view>>;

// The star's retry: the chunk matched at each later position the star can
// reach without crossing a separator.
Advance match_after_star(std::string_view chunk, std::string_view name, bool last) {
    for (std::size_t i = 0; i < name.size() && !is_separator(name[i]); ++i) {
        const auto tried = match_chunk(chunk, name.substr(i + 1));
        if (!tried) {
            return std::nullopt;
        }
        if (tried->ok && (!last || tried->rest.empty())) {
            return std::optional<std::string_view>{tried->rest};
        }
    }
    return std::optional<std::string_view>{};
}

Advance advance(const Chunk& scanned, std::string_view name, bool last) {
    const auto here = match_chunk(scanned.chunk, name);
    if (!here) {
        return std::nullopt;
    }
    if (here->ok && (here->rest.empty() || !last)) {
        return std::optional<std::string_view>{here->rest};
    }
    if (!scanned.star) {
        return std::optional<std::string_view>{};
    }
    return match_after_star(scanned.chunk, name, last);
}

}  // namespace

std::optional<bool> match(std::string_view pattern, std::string_view name) {
    while (!pattern.empty()) {
        const Chunk scanned = scan_chunk(pattern);
        pattern = scanned.rest;
        if (scanned.star && scanned.chunk.empty()) {
            return name.find(separator) == std::string_view::npos;
        }
        const Advance next = advance(scanned, name, pattern.empty());
        if (!next) {
            return std::nullopt;
        }
        if (!*next) {
            return false;
        }
        name = **next;
    }
    return name.empty();
}

namespace {

constexpr std::size_t separators_limit = 10000;

bool has_meta(std::string_view path) {
    return path.find_first_of("*?[\\") != std::string_view::npos;
}

std::string clean_glob_path(std::string_view path) {
    if (path.empty()) {
        return ".";
    }
    if (path == "/") {
        return std::string(path);
    }
    return std::string(path.substr(0, path.size() - 1));
}

// glob: the names in one directory matching one pattern, sorted, appended.
// False is a bad pattern; an unreadable directory adds nothing.
bool glob_dir(const std::string& directory,
              std::string_view pattern,
              std::vector<std::string>& matches) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return true;
    }
    std::vector<std::string> names;
    std::filesystem::directory_iterator entries(directory, error);
    for (const std::filesystem::directory_iterator end; !error && entries != end;
         entries.increment(error)) {
        names.push_back(entries->path().filename().string());
    }
    std::ranges::sort(names);
    for (const std::string& name : names) {
        const auto matched = match(pattern, name);
        if (!matched) {
            return false;
        }
        if (*matched) {
            matches.push_back(join({directory, name}));
        }
    }
    return true;
}

std::optional<std::vector<std::string>> glob_with_limit(std::string_view pattern,
                                                        std::size_t depth) {
    if (depth == separators_limit || !match(pattern, "")) {
        return std::nullopt;
    }
    if (!has_meta(pattern)) {
        std::error_code error;
        if (!std::filesystem::exists(std::filesystem::symlink_status(pattern, error))) {
            return std::vector<std::string>{};
        }
        return std::vector<std::string>{std::string(pattern)};
    }
    const Split parts = split(pattern);
    const std::string directory = clean_glob_path(parts.dir);
    std::vector<std::string> matches;
    if (!has_meta(directory)) {
        if (!glob_dir(directory, parts.file, matches)) {
            return std::nullopt;
        }
        return matches;
    }
    if (directory == pattern) {
        return std::nullopt;
    }
    const auto directories = glob_with_limit(directory, depth + 1);
    if (!directories) {
        return std::nullopt;
    }
    const bool well_formed = std::ranges::all_of(*directories, [&](const std::string& each) {
        return glob_dir(each, parts.file, matches);
    });
    if (!well_formed) {
        return std::nullopt;
    }
    return matches;
}

}  // namespace

std::vector<std::string> glob(std::string_view pattern) {
    return glob_with_limit(pattern, 0).value_or(std::vector<std::string>{});
}

}  // namespace infobot::gopath
