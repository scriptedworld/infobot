// Paths handled the way Go's path/filepath handles them, on Unix.
//
// The Go port finds transcripts with filepath.Glob and builds every path with
// filepath.Join, and both do more than they look like. Join cleans, so a
// session id carrying `..` names a different file than concatenation would.
// Glob sorts one directory level at a time, which is not the order sorting the
// full paths gives, and its `*` matches a leading dot where glob(3)'s does not.
// Which transcript is found first, and so which origin a session is grouped
// under, rests on both.
#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infobot::gopath {

// filepath.Clean.
[[nodiscard]] std::string clean(std::string_view path);

// filepath.Join: the non-empty elements joined by a separator, then cleaned.
[[nodiscard]] std::string join(std::initializer_list<std::string_view> elements);

struct Split {
    std::string_view dir;
    std::string_view file;
};

// filepath.Split.
[[nodiscard]] Split split(std::string_view path);

// filepath.Base.
[[nodiscard]] std::string base(std::string_view path);

// filepath.Dir.
[[nodiscard]] std::string dir(std::string_view path);

// filepath.Ext.
[[nodiscard]] std::string_view ext(std::string_view path);

// The base name without its extension.
[[nodiscard]] std::string stem(std::string_view path);

// filepath.Match. nullopt is ErrBadPattern.
[[nodiscard]] std::optional<bool> match(std::string_view pattern,
                                        std::string_view name);

// filepath.Glob, with the error dropped the way every caller in the Go port
// drops it: a bad pattern and an unreadable directory both find nothing.
[[nodiscard]] std::vector<std::string> glob(std::string_view pattern);

}  // namespace infobot::gopath
