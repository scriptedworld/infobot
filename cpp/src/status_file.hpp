// The session's context state, left on disk for programs other than infobot.
//
// WHY THE FILE EXISTS. An agent has no way to measure its own context. The
// payload the status line is handed carries the numbers and nothing else in the
// session sees them, so a reader that would otherwise tail a transcript reads
// this instead.
//
// THE FORM IS A PUBLISHED INTERFACE (FR-1.11o). silo's board reads these files
// with patterns anchored on the quoted key and the single space after the
// colon, and takes the number bare. The Go port writes it through wrench, whose
// YAML codec is go.yaml.in/yaml/v3; this writes the same bytes by hand, and
// every rule below is that emitter's, read from its source.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace infobot {
struct Environment;
namespace payload {
class Map;
}  // namespace payload
}  // namespace infobot

namespace infobot::status_file {

using Instant = std::chrono::sys_time<std::chrono::nanoseconds>;

// One value: a string, an integer, or a float, which the file keeps apart.
using Value = std::variant<std::string, std::int64_t, double>;

// The file's keys and values, sorted by key as the emitter writes them.
using Fields = std::map<std::string, Value, std::less<>>;

// Where this session's state is left, beside the offsets. Empty when the state
// directory cannot be found.
[[nodiscard]] std::string path(std::string_view session, const Environment& env);

// What a render writes: session and written always; cwd, model and effort
// where the payload names them; the four context keys where the window can be
// measured. A key whose value would be empty is left out.
[[nodiscard]] Fields fields(const payload::Map& data, std::string_view session, Instant now);

// Whether the fields satisfy status.schema.json, draft 2020-12.
[[nodiscard]] bool valid(const Fields& fields);

// The canonical text, or nullopt for a value the form cannot spell, NaN or an
// infinity.
[[nodiscard]] std::optional<std::string> encode(const Fields& fields);

// A string double-quoted and escaped as yaml.v3 escapes it.
[[nodiscard]] std::string quote(std::string_view text);

// A float as wrench spells it: the shortest digits that read back as the same
// value, positional, with ".0" when there is no point. nullopt for NaN and the
// infinities.
[[nodiscard]] std::optional<std::string> float_text(double value);

// A payload's count as an integer, saturating rather than overflowing, and 0
// for NaN.
[[nodiscard]] std::int64_t tokens(double count);

// `written`: local time to the second, with the offset as +hh:mm.
[[nodiscard]] std::string timestamp(Instant now);

// Validates, encodes, and writes the file whole or not at all: a temporary
// beside the target, synced, made 0644 and renamed into place. Best effort, so
// a failure at any step leaves nothing and says nothing.
void write(const payload::Map& data, const Environment& env, Instant now);

}  // namespace infobot::status_file
