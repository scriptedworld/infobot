// How wide the pane is, asked of whatever owns it.
//
// Every ordinary route fails under Claude Code. It captures stdout, so the
// standard descriptors have no size; COLUMNS is unset; /dev/tty has no size;
// and a library's terminal-size call returns a fabricated 80. Believing that
// would truncate a 223-column pane to 80, so it is never used.
//
// The hosts are tried INNERMOST FIRST: tmux inside a herdr pane draws this line
// in the tmux pane, the narrower of the two. The terminal itself comes last,
// reached through an ancestor that still holds it, and only when no
// multiplexer owns the pane, because the terminal behind a pane is wider than
// the pane.
//
// Widths are 64-bit because Go's int is, and Go reads a host's answer into one.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace infobot {
struct Environment;
}  // namespace infobot

namespace infobot::host {

// The columns, or 0 when they genuinely cannot be known.
[[nodiscard]] std::int64_t terminal_width(const Environment& env);

// A host's stdout, trimmed, or empty when it could not be asked. That is: not
// found on env.path, exited other than 0, still running after two seconds,
// or exited while something it started still holds its stdout or stderr a
// quarter second later. The last is Go's WaitDelay: a host is often a script,
// and the grandchild it leaves behind must not hold the render.
//
// A name with no slash is looked for on env.path, as exec.LookPath does, and
// a match in a relative directory is refused as Go refuses it.
[[nodiscard]] std::string ask(std::initializer_list<std::string_view> argv,
                              const Environment& env);

// exec.LookPath: the executable a name resolves to on env.path, or empty.
[[nodiscard]] std::string look_path(std::string_view name, const Environment& env);

// The pane columns tmux reports for the calling pane, or 0.
[[nodiscard]] std::int64_t tmux_width(const Environment& env);

// The pane columns herdr reports for the calling pane, trimmed to what can be
// drawn into, or 0.
[[nodiscard]] std::int64_t herdr_width(const Environment& env);

// herdr's reply to `pane layout --current`, reduced to a width for the pane
// env.herdr_pane_id names. Split from herdr_width so the reading is tested
// without a host.
[[nodiscard]] std::int64_t herdr_layout_width(std::string_view reply,
                                              const Environment& env);

// herdr's rectangle less what cannot be drawn into. A width read too small
// wastes a few columns; one read too large truncates on every render.
[[nodiscard]] std::int64_t usable(std::int64_t reported);

// Decimal digits and nothing else, or 0. Accumulated as Go's int accumulates,
// wrapping rather than overflowing.
[[nodiscard]] std::int64_t atoi(std::string_view text);

// The columns of the terminal an ancestor of this process holds, or 0.
[[nodiscard]] std::int64_t tty_width();

// The same walk from a given process through a given proc root, which is what
// tty_width does with this process and /proc.
[[nodiscard]] std::int64_t tty_width_from(pid_t start, std::string_view proc);

}  // namespace infobot::host
