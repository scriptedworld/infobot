#include "files.hpp"

#include <doctest/doctest.h>
#include <fcntl.h>
#include <simdjson.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support.hpp"

namespace files = infobot::files;
using infobot::test::Scratch;

namespace {

// Every line Lines hands out from offset, and the tail left after the last.
struct Read {
    std::vector<std::string> lines;
    std::string tail;
};

Read read_lines(const std::string& path, std::size_t offset) {
    files::Lines lines(path, offset);
    Read got;
    while (const auto line = lines.next()) {
        got.lines.emplace_back(*line);
    }
    got.tail = lines.tail();
    return got;
}

using Strings = std::vector<std::string>;

// The Go port reads through a 256KB buffer, which is what a long line has to
// outrun.
constexpr std::size_t buffer_bytes = 256UZ * 1024UZ;

constexpr ::mode_t permission_bits = 0777;

// The process umask, read from /proc rather than set and restored with umask(2),
// which would change it for the whole process while it was read.
::mode_t process_umask() {
    const std::string text = files::read("/proc/self/status").value_or("");
    const std::size_t at = text.find("Umask:");
    REQUIRE(at != std::string_view::npos);
    constexpr int octal = 8;
    const std::string field = text.substr(at + std::string_view("Umask:").size());
    return static_cast<::mode_t>(std::stoul(field, nullptr, octal));
}

::mode_t mode_of(const std::string& path) {
    struct stat info{};
    REQUIRE(::stat(path.c_str(), &info) == 0);
    return info.st_mode & permission_bits;
}

}  // namespace

// COVERS: FR-8.7 | positive
//
// Complete lines come out whole, each with its newline, and nothing is left.
TEST_CASE("lines reads complete lines") {
    const Scratch scratch;
    const std::string path = scratch.write("s.jsonl", "a\nbb\n\n");
    const Read got = read_lines(path, 0);
    CHECK(got.lines == Strings{"a\n", "bb\n", "\n"});
    CHECK(got.tail.empty());
}

// COVERS: FR-8.7 | edge
//
// A transcript can be read mid-line, and ReadBytes reports a last line with no
// newline as an error, so it is not a line. It is left as the tail for a caller
// that scans the way bufio.Scanner does.
TEST_CASE("an unterminated last line is the tail, not a line") {
    const Scratch scratch;
    const std::string path = scratch.write("s.jsonl", "whole\nhalf");
    const Read got = read_lines(path, 0);
    CHECK(got.lines == Strings{"whole\n"});
    CHECK(got.tail == "half");
}

// COVERS: FR-8.7 | edge
//
// A line longer than the whole buffer grows it rather than being split or
// dropped, and the line after it still comes out.
TEST_CASE("a line longer than the buffer comes out whole") {
    const Scratch scratch;
    const std::string long_line =
        std::string(buffer_bytes + (buffer_bytes / 2), 'x') + "\n";
    const std::string path = scratch.write("s.jsonl", long_line + "after\n" + "tail");
    const Read got = read_lines(path, 0);
    REQUIRE(got.lines.size() == 2);
    CHECK(got.lines[0] == long_line);
    CHECK(got.lines[1] == "after\n");
    CHECK(got.tail == "tail");
}

// COVERS: FR-8.7 | property
//
// Lines that straddle the edge of a buffer's read are joined across it, so many
// short lines totalling several buffers come out exactly as written.
TEST_CASE("lines straddling a buffer edge come out whole") {
    const Scratch scratch;
    constexpr std::size_t count = 100000;
    std::string body;
    Strings want;
    for (std::size_t i = 0; i < count; ++i) {
        want.push_back(std::to_string(i) + "\n");
        body += want.back();
    }
    REQUIRE(body.size() > 2 * buffer_bytes);
    const Read got = read_lines(scratch.write("s.jsonl", body), 0);
    CHECK(got.lines == want);
    CHECK(got.tail.empty());
}

// COVERS: FR-8.7 | property
//
// Each line is followed in memory by simdjson's padding, so it parses where it
// lies. A line that ends exactly at the end of the file is the case with no file
// bytes after it to stand in for the padding.
TEST_CASE("a line parses in place without a copy") {
    const Scratch scratch;
    const std::string path = scratch.write("s.jsonl", "{\"a\":1}\n{\"b\":2}\n");
    files::Lines lines(path, 0);
    simdjson::dom::parser parser;
    for (const std::string_view key : {"a", "b"}) {
        const std::string_view line = lines.next().value_or(std::string_view{});
        REQUIRE_FALSE(line.empty());
        simdjson::dom::element root;
        const bool realloc_if_needed = false;
        REQUIRE(parser.parse(line.data(), line.size(), realloc_if_needed).get(root) ==
                simdjson::SUCCESS);
        CHECK(root[key].is_int64());
    }
}

// COVERS: FR-8.6 | property
//
// Reading resumes from a stored offset, including one that lands inside a line,
// which reads from there to that line's end and on.
TEST_CASE("lines starts at the offset") {
    const Scratch scratch;
    const std::string path = scratch.write("s.jsonl", "abc\ndef\n");
    constexpr std::size_t second_line = 4;
    CHECK(read_lines(path, second_line).lines == Strings{"def\n"});
    CHECK(read_lines(path, 1).lines == Strings{"bc\n", "def\n"});
}

// COVERS: FR-8.10 | negative
//
// An offset past the end, one that cannot be sought to, a missing file and a
// directory all read as no lines and no tail, which costs a re-sum and never a
// wrong figure.
TEST_CASE("an unreadable source reads as no lines") {
    const Scratch scratch;
    const std::string path = scratch.write("s.jsonl", "abc\n");
    constexpr std::size_t past_end = 100;
    constexpr auto unseekable = static_cast<std::size_t>(-1);
    for (const auto& got : {
             read_lines(path, past_end),
             read_lines(path, unseekable),
             read_lines(scratch.file("missing.jsonl"), 0),
             read_lines(scratch.path(), 0),
         }) {
        CHECK(got.lines.empty());
        CHECK(got.tail.empty());
    }
}

// COVERS: FR-8.15 | positive
//
// The rate table and the palette are read whole, bytes as they are.
TEST_CASE("read returns a file's bytes") {
    const Scratch scratch;
    const std::string body("{\"rates\":{}}\n\0tail", 18);
    CHECK(files::read(scratch.write("pricing.json", body)) == body);
    CHECK(files::read(scratch.write("empty.json", "")) == std::string{});
}

// COVERS: FR-8.16 | negative
//
// A rate table that is missing cannot be read, and the caller falls back to the
// seed on nullopt.
TEST_CASE("read of a missing file is nullopt") {
    const Scratch scratch;
    CHECK_FALSE(files::read(scratch.file("missing.json")).has_value());
}

// COVERS: FR-8.16 | negative
//
// A directory where the rate table should be is a table that cannot be read:
// os.ReadFile returns an error and pricing.Load falls back to the seed. It must
// read as nullopt rather than raise.
TEST_CASE("read of a directory is nullopt") {
    const Scratch scratch;
    CHECK_FALSE(files::read(scratch.path()).has_value());
}

// COVERS: FR-3.3 | positive
//
// A host's answer is read from its pipe to the end, however many reads that
// takes. A file descriptor stands in as the same kind of source here.
TEST_CASE("read_all reads a descriptor to its end") {
    const Scratch scratch;
    const std::string body((buffer_bytes * 2) + 3, 'r');
    const std::string path = scratch.write("reply", body);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    const auto got = files::read_all(fd);
    ::close(fd);
    CHECK(got == body);
}

// COVERS: FR-3.3 | negative
//
// A read error is not an empty answer: a host that could not be read yields
// unknown rather than a width parsed from nothing.
TEST_CASE("read_all on a bad descriptor is nullopt") {
    constexpr int bad = -1;
    CHECK_FALSE(files::read_all(bad).has_value());
}

// COVERS: FR-8.10 | positive
//
// write_all writes everything or reports that it did not.
TEST_CASE("write_all writes the whole body or reports failure") {
    const Scratch scratch;
    const std::string path = scratch.file("out");
    constexpr ::mode_t mode = 0600;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    REQUIRE(fd >= 0);
    const std::string body(buffer_bytes, 'w');
    CHECK(files::write_all(fd, body));
    CHECK(files::write_all(fd, ""));
    ::close(fd);
    CHECK(infobot::test::slurp(path) == body);

    constexpr int bad = -1;
    CHECK_FALSE(files::write_all(bad, "x"));
    CHECK(files::write_all(bad, ""));
}

// COVERS: FR-8.10 | positive
//
// os.WriteFile: created with the mode less the umask, and truncated when it is
// already there, so a shorter state file leaves nothing of the longer behind.
TEST_CASE("write creates with the mode and truncates") {
    const Scratch scratch;
    const std::string path = scratch.file("state.json");
    constexpr ::mode_t mode = 0644;
    REQUIRE(files::write(path, "a longer body", mode));
    CHECK(mode_of(path) == (mode & ~process_umask()));
    REQUIRE(files::write(path, "short", mode));
    CHECK(infobot::test::slurp(path) == "short");
}

// COVERS: FR-8.10 | negative
TEST_CASE("write into a missing directory fails") {
    const Scratch scratch;
    constexpr ::mode_t mode = 0644;
    CHECK_FALSE(files::write(scratch.path() + "/missing/state.json", "x", mode));
}

// COVERS: FR-8.10 | negative
//
// A write that opens and then cannot put the bytes down, which /dev/full does
// on every write, is a failure even though the close succeeds.
TEST_CASE("write that runs out of space fails") {
    constexpr ::mode_t mode = 0644;
    CHECK_FALSE(files::write("/dev/full", "x", mode));
}

// COVERS: FR-4.4 | positive
//
// os.MkdirAll: the state directory and every parent missing on the way to it are
// created, each with the mode less the umask.
TEST_CASE("make_directories creates every missing parent") {
    const Scratch scratch;
    const std::string path = scratch.path() + "/state/infobot/deep";
    constexpr ::mode_t mode = 0750;
    REQUIRE(files::make_directories(path, mode));
    const ::mode_t want = mode & ~process_umask();
    CHECK(mode_of(path) == want);
    CHECK(mode_of(scratch.path() + "/state") == want);
}

// COVERS: FR-4.4 | edge
//
// An existing directory is left as it is, its mode included.
TEST_CASE("make_directories leaves an existing directory untouched") {
    const Scratch scratch;
    const std::string path = scratch.path() + "/existing";
    constexpr ::mode_t kept = 0700;
    constexpr ::mode_t asked = 0755;
    REQUIRE(::mkdir(path.c_str(), kept) == 0);
    REQUIRE(::chmod(path.c_str(), kept) == 0);
    CHECK(files::make_directories(path, asked));
    CHECK(mode_of(path) == kept);
}

// COVERS: FR-8.10 | negative
//
// A path that is a file, one beneath a file, and one beneath a directory that
// cannot be written cannot be made a directory, and the caller skips writing
// the state rather than raising.
TEST_CASE("make_directories fails where a directory cannot be made") {
    const Scratch scratch;
    constexpr ::mode_t mode = 0755;
    const std::string file = scratch.write("file", "");
    CHECK_FALSE(files::make_directories(file, mode));
    CHECK_FALSE(files::make_directories(file + "/beneath", mode));

    const std::string locked = scratch.path() + "/locked";
    constexpr ::mode_t read_only = 0500;
    REQUIRE(::mkdir(locked.c_str(), read_only) == 0);
    // Root writes a directory whatever its mode, and then nothing is measured.
    const bool writable = ::access(locked.c_str(), W_OK) == 0;
    const bool made = files::make_directories(locked + "/child", mode);
    REQUIRE(::chmod(locked.c_str(), mode) == 0);
    REQUIRE_FALSE(writable);
    CHECK_FALSE(made);
}
