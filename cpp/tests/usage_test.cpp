#include "usage.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "environment.hpp"
#include "gojson.hpp"
#include "support.hpp"

namespace gojson = infobot::gojson;
namespace usage = infobot::usage;
using infobot::Environment;
using infobot::test::Scratch;
using infobot::usage::Totals;

namespace {

// One transcript line carrying usage.
std::string record(std::string_view fields) {
    return std::format(R"({{"message":{{"model":"m","usage":{{{}}}}}}})", fields);
}

// text as one line of a file.
std::string lf(std::string_view text) { return std::string(text) + "\n"; }

// One transcript line carrying usage, newline and all.
std::string line(std::string_view fields) { return lf(record(fields)); }

// An environment naming a home and a state directory and nothing else.
Environment environment(std::string_view home, std::string_view state) {
    Environment env;
    env.home = home;
    env.xdg_state_home = state;
    return env;
}

// A fixture projects tree and a scratch state directory, which is what the Go
// suite's tree and scratch helpers give it between them.
struct Fixture {
    Scratch tree;
    Scratch state;
    Environment env = environment("", state.path());
};

Totals sum(const Fixture& fixture, std::string_view session = "s") {
    return usage::sum(session, fixture.tree.path(), fixture.env);
}

// The counts under one model, or none when the model is absent.
gojson::Map<double> of(const Totals& totals, std::string_view model) {
    const auto fields = totals.find(model);
    return fields == totals.end() ? gojson::Map<double>{} : fields->second;
}

// The count of one field, or nullopt when there is none.
std::optional<double> count(const gojson::Map<double>& fields, std::string_view field) {
    const auto value = fields.find(field);
    if (value == fields.end()) {
        return std::nullopt;
    }
    return value->second;
}

std::optional<double> input(const Totals& totals) {
    return count(of(totals, "m"), "input_tokens");
}

std::vector<std::string> names(const std::vector<std::string>& paths) {
    std::vector<std::string> out;
    std::ranges::transform(paths, std::back_inserter(out), [](const std::string& path) {
        return std::filesystem::path(path).filename().string();
    });
    return out;
}

bool contains(const std::vector<std::string>& paths, std::string_view name) {
    const std::vector<std::string> all = names(paths);
    return std::ranges::find(all, name) != all.end();
}

void append(const std::string& path, std::string_view body) {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    stream << body;
}

}  // namespace

// COVERS: FR-8.2 | property
//
// Found by globbing for the session id, not by rebuilding the directory slug
// from the working directory: a session may have been started elsewhere.
TEST_CASE("transcripts find the session by name") {
    const Fixture fx;
    fx.tree.write("-some-other-slug/wanted.jsonl", record(R"("input_tokens":1)"));
    fx.tree.write("-some-other-slug/other.jsonl", record(R"("input_tokens":1)"));
    const auto got = usage::transcripts("wanted", fx.tree.path(), fx.env);
    CHECK(names(got) == std::vector<std::string>{"wanted.jsonl"});
}

// COVERS: FR-8.2 | positive
//
// An empty root means the real projects directory under the home, which is the
// only thing the status line passes.
TEST_CASE("transcripts default the root to the projects under home") {
    const Fixture fx;
    fx.tree.write(".claude/projects/-p/s.jsonl", record(R"("input_tokens":1)"));
    const Environment env = environment(fx.tree.path(), fx.state.path());
    CHECK(names(usage::transcripts("s", "", env)) ==
          std::vector<std::string>{"s.jsonl"});
}

// COVERS: FR-8.3 | property
//
// THE SUBAGENTS ARE NOT OPTIONAL. On one measured session they were 51% of
// output tokens and 33% of cache reads.
TEST_CASE("transcripts include subagents") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", record(R"("input_tokens":1)"));
    fx.tree.write("-p/s/subagents/agent-a.jsonl", record(R"("input_tokens":1)"));
    fx.tree.write("-p/s/subagents/agent-b.jsonl", record(R"("input_tokens":1)"));
    constexpr std::size_t session_and_both = 3;
    CHECK(usage::transcripts("s", fx.tree.path(), fx.env).size() == session_and_both);
}

// COVERS: FR-8.4 | property
//
// /clear opens a new transcript under a NEW id, and the two ids point opposite
// ways, so transcripts are grouped by ROOT: a recorded origin where there is
// one, the file's own name where there is not.
TEST_CASE("transcripts follow both sides of a clear") {
    const Fixture fx;
    fx.tree.write("-p/original.jsonl", record(R"("input_tokens":1)"));
    // The transcript a clear opened: its own id is new, and it records the
    // session it came from.
    fx.tree.write("-p/after-clear.jsonl",
                  lf(R"({"session_id":"original"})") + record(R"("input_tokens":1)"));
    fx.tree.write("-p/unrelated.jsonl", record(R"("input_tokens":1)"));
    constexpr std::size_t both_halves = 2;
    // Asked about EITHER id, both halves come back.
    for (const auto* asked : {"original", "after-clear"}) {
        CAPTURE(asked);
        CHECK(usage::transcripts(asked, fx.tree.path(), fx.env).size() == both_halves);
    }
}

// COVERS: FR-8.4 | edge
//
// bufio.Scanner hands out a last line with no newline, so a transcript holding
// nothing but an unterminated claim is still grouped under the origin it names.
TEST_CASE("the origin search reads a last line with no newline") {
    const Fixture fx;
    fx.tree.write("-p/original.jsonl", line(R"("input_tokens":1)"));
    fx.tree.write("-p/after-clear.jsonl", R"({"session_id":"original"})");
    fx.tree.write("-p/crlf.jsonl", "{\"session_id\":\"original\"}\r");
    constexpr std::size_t all_three = 3;
    for (const auto* asked : {"original", "after-clear", "crlf"}) {
        CAPTURE(asked);
        const auto got = usage::transcripts(asked, fx.tree.path(), fx.env);
        CHECK(got.size() == all_three);
        CHECK(contains(got, "after-clear.jsonl"));
    }
}

// COVERS: FR-8.4 | edge
//
// A transcript named for the origin belongs to it whatever it records itself,
// because the name is compared as well as the claim. One that neither carries
// the name nor claims the origin does not, however it is related further back.
TEST_CASE("a transcript named for the origin is grouped with it") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", lf(R"({"session_id":"other"})"));
    fx.tree.write("-p/other.jsonl", lf(R"({"session_id":"third"})"));
    fx.tree.write("-p/third.jsonl", line(R"("input_tokens":1)"));
    const auto got = usage::transcripts("s", fx.tree.path(), fx.env);
    CHECK(names(got) == std::vector<std::string>{"other.jsonl", "s.jsonl"});
}

// COVERS: FR-8.5 | property
//
// The search stays inside the project directory the session belongs to, so its
// cost is proportional to one project's sessions.
TEST_CASE("the search stays inside the project") {
    const Fixture fx;
    fx.tree.write("-project-a/s.jsonl", record(R"("input_tokens":1)"));
    fx.tree.write("-project-b/other.jsonl", R"({"session_id":"s"})");
    for (const auto& path : usage::transcripts("s", fx.tree.path(), fx.env)) {
        CHECK(std::filesystem::path(path).parent_path().filename() != "-project-b");
    }
}

// COVERS: FR-8.5 | edge
//
// Only a session id that matches no transcript by name widens the search to
// every project, where the transcripts claiming it are found wherever they are.
TEST_CASE("a session named by no transcript is searched for everywhere") {
    const Fixture fx;
    fx.tree.write("-project-a/one.jsonl", lf(R"({"session_id":"gone"})"));
    fx.tree.write("-project-b/two.jsonl", lf(R"({"session_id":"gone"})"));
    fx.tree.write("-project-b/three.jsonl", lf(R"({"session_id":"elsewhere"})"));
    const auto got = usage::transcripts("gone", fx.tree.path(), fx.env);
    CHECK(names(got) == std::vector<std::string>{"one.jsonl", "two.jsonl"});
}

// COVERS: FR-8.19, FR-8.22 | property
//
// Cache creation counts live in a nested object as well as at the top level,
// and reading only the top level counts the writes as nothing.
TEST_CASE("nested cache creation is counted") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl",
                  lf(R"({"message":{"model":"m","usage":{"input_tokens":10,)"
                     R"("cache_creation":{"ephemeral_5m_input_tokens":7,)"
                     R"("ephemeral_1h_input_tokens":3}}}})"));
    const auto got = sum(fx);
    constexpr double five_minute = 7;
    constexpr double one_hour = 3;
    constexpr double fresh = 10;
    CHECK(count(of(got, "m"), "ephemeral_5m_input_tokens") == five_minute);
    CHECK(count(of(got, "m"), "ephemeral_1h_input_tokens") == one_hour);
    CHECK(input(got) == fresh);
}

// COVERS: FR-8.21 | negative
//
// Only numeric values are added. A field carrying anything else is ignored
// rather than coerced, and does not abandon the record it appeared in.
TEST_CASE("non-numeric fields are ignored, not coerced") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":"lots","output_tokens":5)"));
    const auto got = sum(fx);
    constexpr double output = 5;
    CHECK_FALSE(input(got).has_value());
    CHECK(count(of(got, "m"), "output_tokens") == output);
}

// COVERS: FR-8.19, FR-8.21 | edge
//
// A field present at the top level is read from there even when it is null, so
// the nested object is consulted only where the top level has no such key.
TEST_CASE("a null top-level field hides the nested one") {
    const Fixture fx;
    fx.tree.write(
        "-p/s.jsonl",
        lf(R"({"message":{"model":"m","usage":{"ephemeral_5m_input_tokens":null,)"
           R"("cache_creation":{"ephemeral_5m_input_tokens":7}}}})"));
    const auto got = sum(fx);
    REQUIRE(got.contains("m"));
    CHECK_FALSE(count(of(got, "m"), "ephemeral_5m_input_tokens").has_value());
}

// COVERS: FR-8.20 | negative
TEST_CASE("a record with no model goes under a placeholder") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", lf(R"({"message":{"usage":{"input_tokens":10}}})"));
    constexpr double counted = 10;
    CHECK(count(of(sum(fx), "?"), "input_tokens") == counted);
}

// COVERS: FR-8.21 | negative
//
// A line that is not a usage record is skipped whole: text that does not parse,
// a record of the wrong shape anywhere, and a usage that is absent, null or
// empty. None of them costs the lines around it. A usage holding nothing
// counted still enters its model, which is what flags an unpriced one.
TEST_CASE("lines that are not usage are skipped") {
    const Fixture fx;
    fx.tree.write(
        "-p/s.jsonl",
        lf("not json") + lf("[1]") + lf(R"({"message":"text"})") +
            lf(R"({"message":{"model":5,"usage":{"input_tokens":100}}})") +
            lf(R"({"message":{"model":"m","usage":[1]}})") +
            lf(R"({"message":{"model":"m","usage":{"input_tokens":100},"usage":null}})") +
            lf(R"({"message":{"model":"m","usage":{}}})") +
            lf(R"({"message":{"model":"m"}})") +
            lf(R"({"message":{"model":"quiet","usage":{"unrelated":1}}})") +
            line(R"("input_tokens":10)"));
    const auto got = sum(fx);
    constexpr double only_the_record = 10;
    CHECK(input(got) == only_the_record);
    REQUIRE(got.contains("quiet"));
    CHECK(got.at("quiet").empty());
    constexpr std::size_t two_models = 2;
    CHECK(got.size() == two_models);
}

// COVERS: FR-8.21 | edge
//
// Go decodes a repeated usage key into the map the first one made, so the
// counts of both are kept. Only a null between them resets it.
TEST_CASE("a repeated usage key keeps the counts of both") {
    const Fixture fx;
    fx.tree.write(
        "-p/s.jsonl",
        lf(R"({"message":{"model":"m","usage":{"input_tokens":10},"usage":{"output_tokens":5}}})"));
    const auto got = sum(fx);
    constexpr double fresh = 10;
    constexpr double output = 5;
    CHECK(input(got) == fresh);
    CHECK(count(of(got, "m"), "output_tokens") == output);
}

// COVERS: FR-8.7 | edge
//
// A transcript is appended to by the session that is rendering and can be read
// mid-line, so the offset advances only over lines that arrived complete.
TEST_CASE("a half-written line is not counted") {
    const Fixture fx;
    const std::string path = fx.tree.write(
        "-p/s.jsonl",
        line(R"("input_tokens":10)") + record(R"("input_tokens":999)"));  // no newline
    constexpr double whole_only = 10;
    CHECK(input(sum(fx)) == whole_only);
    // The rest arrives, and the line is counted once it is whole.
    append(path, "\n");
    constexpr double both = 1009;
    CHECK(input(sum(fx)) == both);
}

// COVERS: FR-8.6 | property
//
// Only the bytes appended since the last render are parsed, and the total is
// the running one rather than a re-sum.
TEST_CASE("only appended bytes are parsed") {
    const Fixture fx;
    const std::string path = fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    constexpr double first = 10;
    constexpr double second = 15;
    REQUIRE(input(sum(fx)) == first);
    append(path, line(R"("input_tokens":5)"));
    CHECK(input(sum(fx)) == second);
}

// COVERS: FR-8.8 | edge
//
// A file that shrank was rotated or replaced, so its offset means nothing
// against the new one and it is read from the start.
TEST_CASE("a shrunk transcript is read from the start") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl",
                  line(R"("input_tokens":10)") + line(R"("input_tokens":10)"));
    constexpr double first = 20;
    constexpr double reread = 3;
    REQUIRE(input(sum(fx)) == first);
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":3)"));
    CHECK(input(sum(fx)) == reread);
}

// COVERS: FR-8.9 | property
//
// State is per file rather than one running sum, because subagent transcripts
// appear part way through a session.
TEST_CASE("a subagent appearing later is counted once") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    constexpr double main_only = 10;
    constexpr double with_subagent = 14;
    REQUIRE(input(sum(fx)) == main_only);
    fx.tree.write("-p/s/subagents/agent-a.jsonl", line(R"("input_tokens":4)"));
    CHECK(input(sum(fx)) == with_subagent);
    // And again, with nothing new: the main transcript is not re-counted.
    CHECK(input(sum(fx)) == with_subagent);
}

// COVERS: FR-8.9 | edge
//
// A transcript that has gone since the last render drops out of the total and
// out of the offsets, rather than being counted from what was remembered.
TEST_CASE("a transcript that has gone drops out of the total") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    const std::string sub =
        fx.tree.write("-p/s/subagents/agent-a.jsonl", line(R"("input_tokens":4)"));
    constexpr double both = 14;
    constexpr double main_only = 10;
    REQUIRE(input(sum(fx)) == both);
    std::filesystem::remove(sub);
    CHECK(input(sum(fx)) == main_only);
    CHECK(infobot::test::slurp(usage::offset_path("s", fx.env)).find("agent-a") ==
          std::string::npos);
}

// COVERS: FR-8.10 | negative
//
// A transcript that is listed but cannot be read, here a link to nothing, is
// passed over rather than failing the sum, and the rest are still counted.
TEST_CASE("a transcript that cannot be read is passed over") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    std::filesystem::create_directories(fx.tree.path() + "/-p/s/subagents");
    std::filesystem::create_symlink(fx.tree.path() + "/nowhere",
                                    fx.tree.path() + "/-p/s/subagents/agent-a.jsonl");
    constexpr std::size_t both_listed = 2;
    constexpr double main_only = 10;
    REQUIRE(usage::transcripts("s", fx.tree.path(), fx.env).size() == both_listed);
    CHECK(input(sum(fx)) == main_only);
}

// COVERS: FR-8.10 | negative
TEST_CASE("an unknowable session yields no totals") {
    const Fixture fx;
    fx.tree.write("-p/other.jsonl", record(R"("input_tokens":1)"));
    CHECK(sum(fx, "").empty());
    CHECK(usage::transcripts("", fx.tree.path(), fx.env).empty());
}

// COVERS: FR-8.24 | edge
//
// Looking for the session a transcript belongs to gives up after a bounded
// number of records. The field first appeared on record 18 of a transcript
// opened by a clear, so the bound is 40: a large transcript that never carries
// one costs a bounded read rather than a full scan.
TEST_CASE("the origin search gives up after forty records") {
    constexpr int claim_at_31 = 30;
    constexpr int claim_at_46 = 45;
    const auto padded = [](int records) {
        std::string body;
        for (int i = 0; i < records; ++i) {
            body += lf(R"({"type":"bookkeeping"})");
        }
        return body + lf(R"({"session_id":"origin"})");
    };
    const Fixture fx;
    fx.tree.write("-p/origin.jsonl", record(R"("input_tokens":1)"));
    fx.tree.write("-p/early.jsonl", padded(claim_at_31));
    fx.tree.write("-p/late.jsonl", padded(claim_at_46));
    const auto got = usage::transcripts("origin", fx.tree.path(), fx.env);
    CHECK(contains(got, "early.jsonl"));
    CHECK_FALSE(contains(got, "late.jsonl"));
}

// COVERS: FR-8.24 | edge
//
// The Go port's scanner stops at a line longer than 16MiB, so a claim after
// one is never read and the transcript keeps its own name.
TEST_CASE("the origin search stops at a line too long to scan") {
    constexpr std::size_t past_the_ceiling = (16UZ * 1024UZ * 1024UZ) + 1024UZ;
    const Fixture fx;
    fx.tree.write("-p/origin.jsonl", line(R"("input_tokens":1)"));
    fx.tree.write("-p/huge.jsonl",
                  R"({"pad":")" + std::string(past_the_ceiling, 'x') + "\"}\n" +
                      lf(R"({"session_id":"origin"})"));
    const auto got = usage::transcripts("origin", fx.tree.path(), fx.env);
    CHECK(names(got) == std::vector<std::string>{"origin.jsonl"});
}

// COVERS: FR-4.4 | property
//
// The transcript root is a PARAMETER and the offsets follow XDG_STATE_HOME, so
// section 8 is tested against a fixture tree with nothing patched. A test giving
// a root must move XDG_STATE_HOME too, or the offsets it writes land beside the
// real ones and a later render skips bytes it never counted.
TEST_CASE("root and state are both seams") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":42)"));
    // The root reaches the reader: a fixture tree is counted, and the real one
    // is never consulted, which is what makes the figure 42 rather than whatever
    // this machine holds.
    constexpr double fixture = 42;
    CHECK(input(sum(fx)) == fixture);
    // And the offsets landed in the scratch directory rather than beside the
    // real ones.
    const std::string expected = fx.state.path() + "/infobot/s.json";
    CHECK(usage::offset_path("s", fx.env) == expected);
    CHECK(std::filesystem::exists(expected));
    CHECK(infobot::state_dir(fx.env) == fx.state.path() + "/infobot");
}

namespace {

// Offsets for the fixture's one transcript, claiming it was read to its end
// and held total, with extra spliced into the files object after it.
std::string offsets_claiming(const Fixture& fx,
                             std::string_view total,
                             std::string_view extra) {
    const auto found = usage::transcripts("s", fx.tree.path(), fx.env);
    const std::string& path = found.at(0);
    return std::format(
        R"({{"files":{{"{}":{{"size":{},"totals":{{"m":{{"input_tokens":{}}}}}}}{}}}}})",
        path,
        std::filesystem::file_size(path),
        total,
        extra);
}

}  // namespace

// COVERS: FR-8.6 | positive
//
// Offsets that are whole are trusted: a transcript already read to its end is
// not parsed again, and the total is the one recorded. This is what makes the
// malformed cases below mean something.
TEST_CASE("offsets that are whole are trusted") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    fx.state.write("infobot/s.json", offsets_claiming(fx, "999", ""));
    constexpr double remembered = 999;
    CHECK(input(sum(fx)) == remembered);
}

// COVERS: FR-8.10 | negative
//
// Offsets that do not decode are thrown away whole and the transcripts re-summed,
// so the figure is right and slow rather than wrong. As in Go, one value of the
// wrong type anywhere rejects the file, even beside an entry that decodes.
TEST_CASE("malformed offsets cost a re-sum and never a wrong figure") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    constexpr double resummed = 10;
    for (const auto& body : {
             std::string("{not json"),
             std::string("[1]"),
             std::string(R"({"files":[]})"),
             std::string(R"({"files":null})"),
             std::string(R"({"other":1})"),
             offsets_claiming(fx, "999", R"(,"x":{"size":"big"})"),
             offsets_claiming(fx, "999", R"(,"x":{"size":1.5})"),
             offsets_claiming(fx, "999", R"(,"x":{"size":18446744073709551615})"),
             offsets_claiming(fx, "999", R"(,"x":"state")"),
             offsets_claiming(fx, "999", R"(,"x":{"totals":[]})"),
             offsets_claiming(fx, "999", R"(,"x":{"totals":{"m":[]}})"),
             offsets_claiming(fx, R"("many")", ""),
         }) {
        CAPTURE(body);
        fx.state.write("infobot/s.json", body);
        CHECK(input(sum(fx)) == resummed);
    }
}

// COVERS: FR-8.10 | edge
//
// Null is not a type error to Go's decoder. A null entry and a null totals read
// as a file never read and as nothing counted, so the first is re-read and the
// second is trusted as the empty total it says it is.
TEST_CASE("null in the offsets reads as unset") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    const auto found = usage::transcripts("s", fx.tree.path(), fx.env);
    const std::string& path = found.at(0);
    constexpr double resummed = 10;

    fx.state.write("infobot/s.json", std::format(R"({{"files":{{"{}":null}}}})", path));
    CHECK(input(sum(fx)) == resummed);

    fx.state.write("infobot/s.json",
                   std::format(R"({{"files":{{"{}":{{"size":{},"totals":null}}}}}})",
                               path,
                               std::filesystem::file_size(path)));
    CHECK(sum(fx).empty());
}

// COVERS: FR-8.10 | negative
//
// State that cannot be written costs a re-sum on every render, never a figure.
// A state directory that is a file, and no state directory at all, both still
// count the transcript.
TEST_CASE("offsets that cannot be written still yield the total") {
    const Fixture fx;
    fx.tree.write("-p/s.jsonl", line(R"("input_tokens":10)"));
    constexpr double counted = 10;

    const std::string blocked = fx.state.write("not-a-directory", "");
    const Environment unwritable = environment("", blocked);
    CHECK(input(usage::sum("s", fx.tree.path(), unwritable)) == counted);
    CHECK(input(usage::sum("s", fx.tree.path(), unwritable)) == counted);

    const Environment homeless = environment("", "");
    CHECK(usage::offset_path("s", homeless).empty());
    CHECK(input(usage::sum("s", fx.tree.path(), homeless)) == counted);
}

// COVERS: FR-8.10 | property
//
// The offsets are written so that they read back: a path, a model and a field
// carrying quotes, backslashes and control characters survive the round trip,
// and are what the next render believes. The transcript is then rewritten to
// the same size, which only a re-sum would notice, so a second figure equal to
// the first is the offsets being read rather than being thrown away.
TEST_CASE("offsets round-trip names that need escaping") {
    const Fixture fx;
    const auto transcript = [](std::string_view tokens) {
        return lf(std::format(
            R"({{"message":{{"model":"odd \"model\" \\ \u0007","usage":{{"input_tokens":{}}}}}}})",
            tokens));
    };
    const std::string directory = "-p\"q\x01";
    fx.tree.write(directory + "/s.jsonl", transcript("2.5"));
    constexpr double counted = 2.5;
    const std::string model = "odd \"model\" \\ \a";
    REQUIRE(count(of(sum(fx), model), "input_tokens") == counted);
    fx.tree.write(directory + "/s.jsonl", transcript("7.5"));
    CHECK(count(of(sum(fx), model), "input_tokens") == counted);
}
