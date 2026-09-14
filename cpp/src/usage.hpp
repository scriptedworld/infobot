// Token totals for the running session, by tailing its transcripts.
//
// The payload carries no cumulative usage, but Claude Code writes a usage
// record per assistant message into the session transcript, and the payload
// carries the session id that names it.
//
// THIS IS THE ONE PLACE INFOBOT READS A FILE IT WAS NOT HANDED, and it is
// bounded: a stat on every render, and a parse only of what has been appended
// since the last one.
//
// Every read is guarded. A missing transcript, an unreadable offsets file, a
// half-written line: each yields no totals rather than an error.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "gojson.hpp"

namespace infobot::usage {

// Token counts keyed by model, then by field.
using Totals = gojson::Map<gojson::Map<double>>;

// Every transcript the session bills for: its own, its subagents', and those a
// /clear left behind under another id, grouped by recorded origin.
//
// root is where the projects live. Empty means env.projects(), which is the
// only thing the status line passes; a test passes a fixture tree.
[[nodiscard]] std::vector<std::string> transcripts(std::string_view session,
                                                   std::string_view root,
                                                   const Environment& env);

// The session's token counts, keyed by model. Empty when unknowable.
//
// Offsets are kept per file under env.state_dir(), so only bytes appended since
// the last call are parsed. A test giving a fixture root gives a scratch state
// directory too, or the offsets it records land beside the real ones.
[[nodiscard]] Totals sum(std::string_view session,
                         std::string_view root,
                         const Environment& env);

// Where this session's offsets live. Empty when the state directory is.
[[nodiscard]] std::string offset_path(std::string_view session, const Environment& env);

}  // namespace infobot::usage
