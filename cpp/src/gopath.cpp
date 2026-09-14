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

}  // namespace

std::string clean(std::string_view path) {
    if (path.empty()) {
        return ".";
    }
    const bool rooted = is_separator(path[0]);
    std::string out;
    std::size_t r = 0;
    std::size_t dotdot = 0;
    if (rooted) {
        out.push_back(separator);
        r = 1;
        dotdot = 1;
    }
    while (r < path.size()) {
        if (is_separator(path[r]) || dot_at(path, r)) {
            ++r;
        } else if (dot_dot_at(path, r)) {
            r += 2;
            if (out.size() > dotdot) {
                back_up(out, dotdot);
            } else if (!rooted) {
                if (!out.empty()) {
                    out.push_back(separator);
                }
                out.append("..");
                dotdot = out.size();
            }
        } else {
            if ((rooted && out.size() != 1) || (!rooted && !out.empty())) {
                out.push_back(separator);
            }
            for (; r < path.size() && !is_separator(path[r]); ++r) {
                out.push_back(path[r]);
            }
        }
    }
    if (out.empty()) {
        out.push_back('.');
    }
    return out;
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
        return std::string(1, separator);
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

// matchChunk. After a match fails the chunk is still walked to the end, so a
// malformed pattern is reported whatever the name was.
std::optional<Matched> match_chunk(std::string_view chunk, std::string_view s) {
    bool failed = false;
    while (!chunk.empty()) {
        failed = failed || s.empty();
        if (chunk[0] == '[') {
            char32_t rune = 0;
            if (!failed) {
                const gotext::Decoded decoded = gotext::decode_rune(s);
                rune = decoded.rune;
                s.remove_prefix(decoded.size);
            }
            const auto cls = match_class(chunk.substr(1), rune);
            if (!cls) {
                return std::nullopt;
            }
            chunk = cls->rest;
            failed = failed || !cls->matched;
            continue;
        }
        if (chunk[0] == '?') {
            if (!failed) {
                failed = is_separator(s[0]);
                s.remove_prefix(gotext::decode_rune(s).size);
            }
            chunk.remove_prefix(1);
            continue;
        }
        if (chunk[0] == '\\') {
            chunk.remove_prefix(1);
            if (chunk.empty()) {
                return std::nullopt;
            }
        }
        if (!failed) {
            failed = chunk[0] != s[0];
            s.remove_prefix(1);
        }
        chunk.remove_prefix(1);
    }
    if (failed) {
        return Matched{.rest = std::string_view{}, .ok = false};
    }
    return Matched{.rest = s, .ok = true};
}

// The star's retry: the chunk matched at each later position the star can
// reach without crossing a separator.
std::optional<std::optional<std::string_view>> match_after_star(std::string_view chunk,
                                                                std::string_view name,
                                                                bool last) {
    for (std::size_t i = 0; i < name.size() && !is_separator(name[i]); ++i) {
        const auto tried = match_chunk(chunk, name.substr(i + 1));
        if (!tried) {
            return std::nullopt;
        }
        if (tried->ok) {
            if (last && !tried->rest.empty()) {
                continue;
            }
            return std::optional<std::string_view>{tried->rest};
        }
    }
    return std::optional<std::string_view>{};
}

}  // namespace

std::optional<bool> match(std::string_view pattern, std::string_view name) {
    while (!pattern.empty()) {
        const Chunk scanned = scan_chunk(pattern);
        pattern = scanned.rest;
        if (scanned.star && scanned.chunk.empty()) {
            return name.find(separator) == std::string_view::npos;
        }
        const auto here = match_chunk(scanned.chunk, name);
        if (here && here->ok && (here->rest.empty() || !pattern.empty())) {
            name = here->rest;
            continue;
        }
        if (!here) {
            return std::nullopt;
        }
        if (!scanned.star) {
            return false;
        }
        const auto later = match_after_star(scanned.chunk, name, pattern.empty());
        if (!later) {
            return std::nullopt;
        }
        if (!*later) {
            return false;
        }
        name = **later;
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
// Nullopt is a bad pattern; an unreadable directory adds nothing.
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
    for (const std::string& each : *directories) {
        if (!glob_dir(each, parts.file, matches)) {
            return std::nullopt;
        }
    }
    return matches;
}

}  // namespace

std::vector<std::string> glob(std::string_view pattern) {
    return glob_with_limit(pattern, 0).value_or(std::vector<std::string>{});
}

}  // namespace infobot::gopath
