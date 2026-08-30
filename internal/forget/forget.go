// Package forget removes what a finished session left on disk.
//
// Reads the hook payload on standard input and exits 0 whatever it is given. A
// cleanup that fails is litter; a cleanup that raises is a hook failure
// reported to somebody who was closing their terminal.
//
// It SAYS WHAT IT REMOVED, on stderr, while still exiting 0. A cleanup that
// exits 0 having removed nothing is the same shape as a gate that passes having
// checked nothing, and the difference has to be visible in a log rather than
// invisible by design.
package forget

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/scriptedworld/infobot/internal/payload"
	"github.com/scriptedworld/infobot/internal/state"
	"github.com/scriptedworld/infobot/internal/usage"
)

// Main runs the cleanup and always reports success to the hook runner.
func Main(stdin io.Reader, log io.Writer) int {
	raw, err := io.ReadAll(stdin)
	if err != nil {
		return 0
	}
	var data payload.Map
	if json.Unmarshal(raw, &data) != nil || data == nil {
		return 0
	}
	// THE THREE WRITES BELOW DISCARD THEIR ERROR DELIBERATELY. The log is
	// stderr, so a failed write has nowhere to be reported, and the exit code
	// is 0 by contract because a hook that raises interrupts somebody closing
	// their terminal. `_ =` says that was decided rather than overlooked, which
	// is what an unchecked call cannot say.
	session := data.Str("session_id")
	if !Named(session) {
		_, _ = fmt.Fprintf(log, "forget-session: refused session id %q\n", session)
		return 0
	}

	removed := Remove(session)
	if len(removed) == 0 {
		_, _ = fmt.Fprintf(log, "forget-session: %s had nothing to remove\n", session)
		return 0
	}
	_, _ = fmt.Fprintf(log, "forget-session: %s removed %s\n", session, strings.Join(removed, " "))
	return 0
}

// Named reports whether a session id names one file each.
//
// Anything else, including a path separator smuggled through the payload, is
// refused rather than joined onto a directory this walks with unlink.
func Named(session string) bool {
	if session == "" || strings.Contains(session, "..") {
		return false
	}
	return !strings.ContainsAny(session, `/\`)
}

// Remove drops both of the session's files and returns the paths that were
// actually there, so the caller can say what it did.
func Remove(session string) []string {
	var removed []string
	for _, path := range []string{state.Path(session), usage.OffsetPath(session)} {
		if path == "" {
			continue
		}
		if _, err := os.Stat(path); err != nil {
			continue
		}
		if os.Remove(path) == nil {
			removed = append(removed, path)
		}
	}
	return removed
}
