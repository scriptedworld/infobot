#include "gopath.hpp"

#include <doctest/doctest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support.hpp"

namespace gopath = infobot::gopath;
using infobot::test::Scratch;

namespace {

struct PathTest {
    std::string_view path;
    std::string_view result;
};

// cleantests and nonwincleantests from path/filepath/path_test.go.
constexpr auto cleantests = std::to_array<PathTest>({
    // Already clean
    {.path = "abc", .result = "abc"},
    {.path = "abc/def", .result = "abc/def"},
    {.path = "a/b/c", .result = "a/b/c"},
    {.path = ".", .result = "."},
    {.path = "..", .result = ".."},
    {.path = "../..", .result = "../.."},
    {.path = "../../abc", .result = "../../abc"},
    {.path = "/abc", .result = "/abc"},
    {.path = "/", .result = "/"},

    // Empty is current dir
    {.path = "", .result = "."},

    // Remove trailing slash
    {.path = "abc/", .result = "abc"},
    {.path = "abc/def/", .result = "abc/def"},
    {.path = "a/b/c/", .result = "a/b/c"},
    {.path = "./", .result = "."},
    {.path = "../", .result = ".."},
    {.path = "../../", .result = "../.."},
    {.path = "/abc/", .result = "/abc"},

    // Remove doubled slash
    {.path = "abc//def//ghi", .result = "abc/def/ghi"},
    {.path = "abc//", .result = "abc"},

    // Remove . elements
    {.path = "abc/./def", .result = "abc/def"},
    {.path = "/./abc/def", .result = "/abc/def"},
    {.path = "abc/.", .result = "abc"},

    // Remove .. elements
    {.path = "abc/def/ghi/../jkl", .result = "abc/def/jkl"},
    {.path = "abc/def/../ghi/../jkl", .result = "abc/jkl"},
    {.path = "abc/def/..", .result = "abc"},
    {.path = "abc/def/../..", .result = "."},
    {.path = "/abc/def/../..", .result = "/"},
    {.path = "abc/def/../../..", .result = ".."},
    {.path = "/abc/def/../../..", .result = "/"},
    {.path = "abc/def/../../../ghi/jkl/../../../mno", .result = "../../mno"},
    {.path = "/../abc", .result = "/abc"},
    {.path = "a/../b:/../../c", .result = "../c"},

    // Combinations
    {.path = "abc/./../def", .result = "def"},
    {.path = "abc//./../def", .result = "def"},
    {.path = "abc/../../././../def", .result = "../../def"},

    // Remove leading doubled slash
    {.path = "//abc", .result = "/abc"},
    {.path = "///abc", .result = "/abc"},
    {.path = "//abc//", .result = "/abc"},
});

}  // namespace

// COVERS: FR-8.5 | property
//
// Every transcript path is built with Join, which cleans, so a session id
// carrying `..` resolves the way Go resolves it rather than escaping the project
// directory by concatenation. TestClean's rows, and Clean is idempotent on each.
TEST_CASE("clean cleans Go's rows") {
    for (const auto& test : cleantests) {
        CAPTURE(test.path);
        CHECK(gopath::clean(test.path) == test.result);
        CHECK(gopath::clean(test.result) == test.result);
    }
}

// COVERS: FR-8.5 | property
//
// TestJoin's Unix rows: empty elements are skipped, and the result is cleaned.
TEST_CASE("join joins Go's rows") {
    CHECK(gopath::join({}).empty());
    CHECK(gopath::join({""}).empty());
    CHECK(gopath::join({"/"}) == "/");
    CHECK(gopath::join({"a"}) == "a");
    CHECK(gopath::join({"a", "b"}) == "a/b");
    CHECK(gopath::join({"a", ""}) == "a");
    CHECK(gopath::join({"", "b"}) == "b");
    CHECK(gopath::join({"/", "a"}) == "/a");
    CHECK(gopath::join({"/", "a/b"}) == "/a/b");
    CHECK(gopath::join({"/", ""}) == "/");
    CHECK(gopath::join({"/a", "b"}) == "/a/b");
    CHECK(gopath::join({"a", "/b"}) == "a/b");
    CHECK(gopath::join({"/a", "/b"}) == "/a/b");
    CHECK(gopath::join({"a/", "b"}) == "a/b");
    CHECK(gopath::join({"a/", ""}) == "a");
    CHECK(gopath::join({"", ""}).empty());
    CHECK(gopath::join({"/", "a", "b"}) == "/a/b");
    CHECK(gopath::join({"//", "a"}) == "/a");
}

// COVERS: FR-8.5 | edge
//
// A session id is joined into the project directory, so `..` in it climbs out
// exactly as far as Go's Join lets it, and an empty one names the directory.
TEST_CASE("join cleans a session id carrying dot-dot") {
    CHECK(gopath::join({"/root/-p", "../-q/s.jsonl"}) == "/root/-q/s.jsonl");
    CHECK(gopath::join({"/root/-p", "", "s.jsonl"}) == "/root/-p/s.jsonl");
    CHECK(gopath::join({"", "", "/root"}) == "/root");
    // A name that only starts with dots is a name, as filepath.Clean printed.
    CHECK(gopath::join({"..a/..b", ".c/../.d"}) == "..a/..b/.d");
}

// COVERS: FR-8.2 | property
//
// TestSplit's Unix rows: the directory keeps its trailing separator.
TEST_CASE("split splits Go's rows") {
    struct SplitTest {
        std::string_view path;
        std::string_view dir;
        std::string_view file;
    };
    for (const auto test : {
             SplitTest{.path = "a/b", .dir = "a/", .file = "b"},
             SplitTest{.path = "a/b/", .dir = "a/b/", .file = ""},
             SplitTest{.path = "a/", .dir = "a/", .file = ""},
             SplitTest{.path = "a", .dir = "", .file = "a"},
             SplitTest{.path = "/", .dir = "/", .file = ""},
         }) {
        CAPTURE(test.path);
        const gopath::Split got = gopath::split(test.path);
        CHECK(got.dir == test.dir);
        CHECK(got.file == test.file);
    }
}

// COVERS: FR-8.4 | property
//
// A transcript with no recorded origin is grouped under its own name, which is
// Base without the extension. TestBase's rows.
TEST_CASE("base takes Go's rows") {
    for (const auto test : {
             PathTest{.path = "", .result = "."},
             PathTest{.path = ".", .result = "."},
             PathTest{.path = "/.", .result = "."},
             PathTest{.path = "/", .result = "/"},
             PathTest{.path = "////", .result = "/"},
             PathTest{.path = "x/", .result = "x"},
             PathTest{.path = "abc", .result = "abc"},
             PathTest{.path = "abc/def", .result = "def"},
             PathTest{.path = "a/b/.x", .result = ".x"},
             PathTest{.path = "a/b/c.", .result = "c."},
             PathTest{.path = "a/b/c.x", .result = "c.x"},
         }) {
        CAPTURE(test.path);
        CHECK(gopath::base(test.path) == test.result);
    }
}

// COVERS: FR-8.5 | property
//
// A session's subagents and siblings are looked for in the directory its
// transcript was found in, which is Dir. TestDir's Unix rows.
TEST_CASE("dir takes Go's rows") {
    for (const auto test : {
             PathTest{.path = "", .result = "."},
             PathTest{.path = ".", .result = "."},
             PathTest{.path = "/.", .result = "/"},
             PathTest{.path = "/", .result = "/"},
             PathTest{.path = "/foo", .result = "/"},
             PathTest{.path = "x/", .result = "x"},
             PathTest{.path = "abc", .result = "."},
             PathTest{.path = "abc/def", .result = "abc"},
             PathTest{.path = "a/b/.x", .result = "a/b"},
             PathTest{.path = "a/b/c.", .result = "a/b"},
             PathTest{.path = "a/b/c.x", .result = "a/b"},
             PathTest{.path = "////", .result = "/"},
         }) {
        CAPTURE(test.path);
        CHECK(gopath::dir(test.path) == test.result);
    }
}

// COVERS: FR-8.4 | property
//
// TestExt's rows, and the stem the Go port takes as a transcript's own name:
// base[:len(base)-len(filepath.Ext(base))], as usage.go writes it.
TEST_CASE("ext and stem take Go's rows") {
    struct ExtTest {
        std::string_view path;
        std::string_view ext;
        std::string_view stem;
    };
    for (const auto test : {
             ExtTest{.path = "path.go", .ext = ".go", .stem = "path"},
             ExtTest{.path = "path.pb.go", .ext = ".go", .stem = "path.pb"},
             ExtTest{.path = "a.dir/b", .ext = "", .stem = "b"},
             ExtTest{.path = "a.dir/b.go", .ext = ".go", .stem = "b"},
             ExtTest{.path = "a.dir/", .ext = "", .stem = "a"},
             ExtTest{
                 .path = "/p/-proj/session.jsonl", .ext = ".jsonl", .stem = "session"},
             ExtTest{.path = "/p/.jsonl", .ext = ".jsonl", .stem = ""},
         }) {
        CAPTURE(test.path);
        CHECK(gopath::ext(test.path) == test.ext);
        CHECK(gopath::stem(test.path) == test.stem);
    }
}

namespace {

struct MatchTest {
    std::string_view pattern;
    std::string_view s;
    bool match;
    bool bad;
};

// matchTests from path/filepath/match_test.go. bad is ErrBadPattern.
constexpr auto matchtests = std::to_array<MatchTest>({
    {.pattern = "abc", .s = "abc", .match = true, .bad = false},
    {.pattern = "*", .s = "abc", .match = true, .bad = false},
    {.pattern = "*c", .s = "abc", .match = true, .bad = false},
    {.pattern = "a*", .s = "a", .match = true, .bad = false},
    {.pattern = "a*", .s = "abc", .match = true, .bad = false},
    {.pattern = "a*", .s = "ab/c", .match = false, .bad = false},
    {.pattern = "a*/b", .s = "abc/b", .match = true, .bad = false},
    {.pattern = "a*/b", .s = "a/c/b", .match = false, .bad = false},
    {.pattern = "a*b*c*d*e*/f", .s = "axbxcxdxe/f", .match = true, .bad = false},
    {.pattern = "a*b*c*d*e*/f", .s = "axbxcxdxexxx/f", .match = true, .bad = false},
    {.pattern = "a*b*c*d*e*/f", .s = "axbxcxdxe/xxx/f", .match = false, .bad = false},
    {.pattern = "a*b*c*d*e*/f", .s = "axbxcxdxexxx/fff", .match = false, .bad = false},
    {.pattern = "a*b?c*x", .s = "abxbbxdbxebxczzx", .match = true, .bad = false},
    {.pattern = "a*b?c*x", .s = "abxbbxdbxebxczzy", .match = false, .bad = false},
    {.pattern = "ab[c]", .s = "abc", .match = true, .bad = false},
    {.pattern = "ab[b-d]", .s = "abc", .match = true, .bad = false},
    {.pattern = "ab[e-g]", .s = "abc", .match = false, .bad = false},
    {.pattern = "ab[^c]", .s = "abc", .match = false, .bad = false},
    {.pattern = "ab[^b-d]", .s = "abc", .match = false, .bad = false},
    {.pattern = "ab[^e-g]", .s = "abc", .match = true, .bad = false},
    {.pattern = "a\\*b", .s = "a*b", .match = true, .bad = false},
    {.pattern = "a\\*b", .s = "ab", .match = false, .bad = false},
    {.pattern = "a?b", .s = "a☺b", .match = true, .bad = false},
    {.pattern = "a[^a]b", .s = "a☺b", .match = true, .bad = false},
    {.pattern = "a???b", .s = "a☺b", .match = false, .bad = false},
    {.pattern = "a[^a][^a][^a]b", .s = "a☺b", .match = false, .bad = false},
    {.pattern = "[a-ζ]*", .s = "α", .match = true, .bad = false},
    {.pattern = "*[a-ζ]", .s = "A", .match = false, .bad = false},
    {.pattern = "a?b", .s = "a/b", .match = false, .bad = false},
    {.pattern = "a*b", .s = "a/b", .match = false, .bad = false},
    {.pattern = "[\\]a]", .s = "]", .match = true, .bad = false},
    {.pattern = "[\\-]", .s = "-", .match = true, .bad = false},
    {.pattern = "[x\\-]", .s = "x", .match = true, .bad = false},
    {.pattern = "[x\\-]", .s = "-", .match = true, .bad = false},
    {.pattern = "[x\\-]", .s = "z", .match = false, .bad = false},
    {.pattern = "[\\-x]", .s = "x", .match = true, .bad = false},
    {.pattern = "[\\-x]", .s = "-", .match = true, .bad = false},
    {.pattern = "[\\-x]", .s = "a", .match = false, .bad = false},
    {.pattern = "[]a]", .s = "]", .match = false, .bad = true},
    {.pattern = "[-]", .s = "-", .match = false, .bad = true},
    {.pattern = "[x-]", .s = "x", .match = false, .bad = true},
    {.pattern = "[x-]", .s = "-", .match = false, .bad = true},
    {.pattern = "[x-]", .s = "z", .match = false, .bad = true},
    {.pattern = "[-x]", .s = "x", .match = false, .bad = true},
    {.pattern = "[-x]", .s = "-", .match = false, .bad = true},
    {.pattern = "[-x]", .s = "a", .match = false, .bad = true},
    {.pattern = "\\", .s = "a", .match = false, .bad = true},
    {.pattern = "[a-b-c]", .s = "a", .match = false, .bad = true},
    {.pattern = "[", .s = "a", .match = false, .bad = true},
    {.pattern = "[^", .s = "a", .match = false, .bad = true},
    {.pattern = "[^bc", .s = "a", .match = false, .bad = true},
    {.pattern = "a[", .s = "a", .match = false, .bad = true},
    {.pattern = "a[", .s = "ab", .match = false, .bad = true},
    {.pattern = "a[", .s = "x", .match = false, .bad = true},
    {.pattern = "a/b[", .s = "x", .match = false, .bad = true},
    {.pattern = "*x", .s = "xxx", .match = true, .bad = false},
});

std::optional<bool> expected(const MatchTest& test) {
    if (test.bad) {
        return std::nullopt;
    }
    return test.match;
}

}  // namespace

// COVERS: FR-8.2 | property
//
// The session is found by matching its id against every name in a project
// directory, so Match has to agree with Go on every row of TestMatch, the
// ErrBadPattern rows included.
TEST_CASE("match agrees with Go's matchTests") {
    for (const auto& test : matchtests) {
        CAPTURE(test.pattern);
        CAPTURE(test.s);
        CHECK(gopath::match(test.pattern, test.s) == expected(test));
    }
}

// COVERS: FR-8.2 | edge
//
// Rows beyond Go's table, each printed by filepath.Match in Go 1.27.1: a star
// retry that meets a bad class later in the chunk, an escaped class bound, an
// invalid byte inside a class where a literal U+FFFD is valid, a class with no
// closing bracket after a range, and a star inside a class, which does not end
// a chunk.
TEST_CASE("match on the edges of the pattern grammar") {
    constexpr std::optional<bool> bad = std::nullopt;
    CHECK(gopath::match("*b[", "ab") == bad);
    CHECK(gopath::match("*[", "ab") == bad);
    CHECK(gopath::match("[\\a-\\c]", "b") == std::optional<bool>(true));
    CHECK(gopath::match("[\\", "a") == bad);
    CHECK(gopath::match("[\xff]", "a") == bad);
    CHECK(gopath::match("[a-c", "b") == bad);
    CHECK(gopath::match("[a-", "b") == bad);
    CHECK(gopath::match("a\\", "ab") == bad);
    CHECK(gopath::match("*", "") == std::optional<bool>(true));
    CHECK(gopath::match("", "") == std::optional<bool>(true));
    CHECK(gopath::match("", "a") == std::optional<bool>(false));
    CHECK(gopath::match("a*c", "abcbc") == std::optional<bool>(true));
    CHECK(gopath::match("a*c", "abcbd") == std::optional<bool>(false));
    CHECK(gopath::match("?", "") == std::optional<bool>(false));
    CHECK(gopath::match("a[*]b", "a*b") == std::optional<bool>(true));
    CHECK(gopath::match("[\uFFFD]", "\uFFFD") == std::optional<bool>(true));
    CHECK(gopath::match("[a\uFFFD]", "b") == std::optional<bool>(false));
}

namespace {

// The scratch tree the glob tests read. The names are chosen so that sorting
// per directory level and sorting the full paths disagree: `a` < `a b` < `a-b`
// as directory names, while `a b/` < `a-b/` < `a/` as path prefixes.
void plant(const Scratch& scratch) {
    for (const std::string_view name : {"a/z.jsonl",
                                        "a-b/a.jsonl",
                                        "a/.hidden.jsonl",
                                        "a/y.txt",
                                        "file",
                                        "a b/c.jsonl"}) {
        scratch.write(name, "");
    }
    std::filesystem::create_symlink(scratch.file("gone"), scratch.file("broken"));
}

std::vector<std::string> relative(const Scratch& scratch,
                                  const std::vector<std::string>& paths) {
    std::vector<std::string> out(paths.size());
    std::ranges::transform(paths, out.begin(), [&](const std::string& path) {
        return path.substr(scratch.path().size());
    });
    return out;
}

std::vector<std::string> glob_in(const Scratch& scratch, std::string_view pattern) {
    return relative(scratch, gopath::glob(scratch.path() + "/" + std::string(pattern)));
}

using Names = std::vector<std::string>;

}  // namespace

// COVERS: FR-8.4 | property
//
// Which transcript is found first decides the origin a session is grouped
// under, and Go's Glob sorts ONE DIRECTORY LEVEL AT A TIME. Sorting the full
// paths would put `a b/` and `a-b/` ahead of `a/`. The expected lists are what
// filepath.Glob printed against the same tree.
TEST_CASE("glob sorts one directory level at a time") {
    const Scratch scratch;
    plant(scratch);
    CHECK(glob_in(scratch, "*/*.jsonl") ==
          Names{"/a/.hidden.jsonl", "/a/z.jsonl", "/a b/c.jsonl", "/a-b/a.jsonl"});
    CHECK(glob_in(scratch, "*") == Names{"/a", "/a b", "/a-b", "/broken", "/file"});
    CHECK(glob_in(scratch, "*/z.jsonl") == Names{"/a/z.jsonl"});
}

// COVERS: FR-8.2 | property
//
// Go's `*` matches a leading dot where glob(3)'s does not, so a transcript whose
// name starts with one is still found.
TEST_CASE("glob's star matches a leading dot") {
    const Scratch scratch;
    plant(scratch);
    CHECK(glob_in(scratch, "a/*.jsonl") == Names{"/a/.hidden.jsonl", "/a/z.jsonl"});
    CHECK(glob_in(scratch, "a/*") ==
          Names{"/a/.hidden.jsonl", "/a/y.txt", "/a/z.jsonl"});
    CHECK(glob_in(scratch, "a/?.jsonl") == Names{"/a/z.jsonl"});
    CHECK(glob_in(scratch, "a/[x-z].jsonl") == Names{"/a/z.jsonl"});
    CHECK(glob_in(scratch, "a/\\z.jsonl") == Names{"/a/z.jsonl"});
}

// COVERS: FR-8.2 | negative
//
// TestGlobError and TestCVE202230632: a malformed pattern is ErrBadPattern,
// which every caller drops, so it finds nothing. That holds whether the bad
// class is caught before any directory is read or only when a name reaches it.
TEST_CASE("a bad pattern finds nothing") {
    const Scratch scratch;
    plant(scratch);
    for (const std::string_view pattern :
         {"[]", "*/[]", "*/[]/x", "a[", "*/a[", "a*[", "a*/["}) {
        CAPTURE(pattern);
        CHECK(glob_in(scratch, pattern).empty());
    }
    CHECK(gopath::glob("[]").empty());
    CHECK(gopath::glob("nonexist/[]").empty());
    constexpr std::size_t separators = 10001;
    CHECK(gopath::glob("/*" + std::string(separators, '/')).empty());
}

// COVERS: FR-8.5 | property
//
// A pattern with no metacharacters is looked up rather than listed, with Lstat,
// so a broken symlink is still found (TestGlobSymlink) and a missing file is
// not. A glob through a plain file, and through `.`, `..` and a doubled
// separator, comes back cleaned by Join, as filepath.Glob printed it.
TEST_CASE("glob without metacharacters looks the path up") {
    const Scratch scratch;
    plant(scratch);
    CHECK(glob_in(scratch, "file") == Names{"/file"});
    CHECK(glob_in(scratch, "broken") == Names{"/broken"});
    CHECK(glob_in(scratch, "missing").empty());
    CHECK(glob_in(scratch, "file/*").empty());
    CHECK(glob_in(scratch, "./a/*.txt") == Names{"/a/y.txt"});
    CHECK(glob_in(scratch, "a//*.txt") == Names{"/a/y.txt"});
    CHECK(glob_in(scratch, "a/../a/z*") == Names{"/a/z.jsonl"});
}

// COVERS: FR-8.10 | negative
//
// Glob ignores a directory it cannot read, so an unreadable project directory
// costs its transcripts and never raises.
TEST_CASE("glob skips a directory it cannot read") {
    const Scratch scratch;
    plant(scratch);
    const std::string directory = gopath::dir(scratch.write("locked/x.jsonl", ""));
    REQUIRE(::chmod(directory.c_str(), 0) == 0);
    // Root reads a directory whatever its mode, and then nothing is measured.
    const bool readable = ::access(directory.c_str(), R_OK) == 0;
    const Names found = glob_in(scratch, "*/*.jsonl");
    // Restored before any assertion can end the case, so the scratch directory
    // can still be removed.
    constexpr ::mode_t restored = 0755;
    REQUIRE(::chmod(directory.c_str(), restored) == 0);
    REQUIRE_FALSE(readable);
    CHECK(found ==
          Names{"/a/.hidden.jsonl", "/a/z.jsonl", "/a b/c.jsonl", "/a-b/a.jsonl"});
}

// COVERS: FR-8.2 | edge
//
// The root directory is not chopped: a pattern directly under `/` lists `/`.
TEST_CASE("glob under the root directory keeps the root") {
    const std::vector<std::string> found = gopath::glob("/tm?");
    CHECK(found == std::vector<std::string>{"/tmp"});
}

// COVERS: FR-8.2 | edge
//
// A pattern with no directory lists `.`, and Join(".", name) is the bare name,
// so the result is every entry of the working directory, dot files included, in
// byte order. The expected list is read from the same directory rather than
// assumed, since the runner's working directory is not fixed.
TEST_CASE("glob with no directory lists the working directory") {
    Names want;
    std::ranges::transform(
        std::filesystem::directory_iterator(std::filesystem::current_path()),
        std::back_inserter(want),
        [](const std::filesystem::directory_entry& entry) {
            return entry.path().filename().string();
        });
    std::ranges::sort(want);
    CHECK(gopath::glob("*") == want);
}
