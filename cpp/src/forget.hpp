// What a finished session left on disk, removed.
//
// Claude Code runs the status line once per event and never again once a
// session ends, so nothing else would remove what the last render left behind:
// one status file and one offsets file per session, forever.
//
// It exits 0 whatever it is given, because a hook that raises interrupts
// somebody closing their terminal. It SAYS WHAT IT REMOVED on its log, because
// a cleanup that exits 0 having removed nothing is the same shape as a gate that
// passes having checked nothing.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace infobot {
struct Environment;
}  // namespace infobot

namespace infobot::forget {

// Whether a session id names one file each. Empty, `..` anywhere, or a slash
// or backslash is refused rather than joined onto a directory this unlinks in.
[[nodiscard]] bool named(std::string_view session);

// Removes the session's status file and offsets, and returns the paths that
// were there and went.
[[nodiscard]] std::vector<std::string> remove(std::string_view session,
                                              const Environment& env);

struct Streams {
    int input;
    int log;
};

// The SessionEnd hook: the payload read to its end, one line of log saying what
// happened. Returns 0 whatever it is given.
int run(Streams streams, const Environment& env);

}  // namespace infobot::forget
