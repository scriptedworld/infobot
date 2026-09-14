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
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "environment.hpp"

namespace infobot::host {

// The columns, or 0 when they genuinely cannot be known.
[[nodiscard]] int terminal_width(const Environment& env);

// A host's stdout, trimmed, or empty when it could not be asked: not found,
// exited non-zero, or still running after two seconds. The two seconds bound
// the wait on a grandchild holding the pipe as well, because a host is often
// a script.
[[nodiscard]] std::string ask(std::initializer_list<std::string_view> argv);

// The pane columns tmux reports for the calling pane, or 0.
[[nodiscard]] int tmux_width(const Environment& env);

// The pane columns herdr reports for the calling pane, trimmed to what can be
// drawn into, or 0.
[[nodiscard]] int herdr_width(const Environment& env);

// herdr's reply to `pane layout --current`, reduced to a width for the pane
// named by pane_id. Split from herdr_width so the reading is tested without a
// host.
[[nodiscard]] int herdr_layout_width(std::string_view reply, std::string_view pane_id);

// herdr's rectangle less what cannot be drawn into. A width read too small
// wastes a few columns; one read too large truncates on every render.
[[nodiscard]] int usable(int reported);

// Decimal digits and nothing else, or 0.
[[nodiscard]] int atoi(std::string_view text);

// The columns of the terminal an ancestor of this process holds, or 0.
[[nodiscard]] int tty_width();

}  // namespace infobot::host
