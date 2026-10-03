package render

import (
	"os"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
)

// ancestorLimit bounds the walk up the process tree. The chain from here to
// the terminal is three or four processes; anything longer is a loop or a
// surprise, and neither is worth following forever on a line that renders on
// every event.
const (
	ancestorLimit = 16

	// After the closing parenthesis of the process name, /proc/<pid>/stat
	// continues with state then ppid, so two fields must be present before the
	// parent can be read.
	statFieldsAfterName = 2

	// ptsDir holds the pseudo-terminals a process's descriptors can point at.
	ptsDir = "/dev/pts"
)

// ttyWidth is the columns of the terminal an ancestor holds, or 0.
//
// This is the bare terminal case, and it is the common one. tmux and herdr
// answer when they are there; without either, every route the rest of this
// file tries is dead, because Claude Code hands the status line pipes:
//
//	fd0  socket    fd1  pipe    fd2  /dev/null
//	COLUMNS unset, /dev/tty opens but has no size
//
// The terminal has not gone anywhere though. Claude Code itself still holds
// it, so walking up the process tree finds it two hops away. In a bare kitty
// this process has no pts while `claude` holds /dev/pts/0 at the terminal's
// full width.
//
// It is tried last, after tmux and herdr, and the order is the point. Inside a
// multiplexer the pane is what the line is drawn into and the terminal behind
// it is wider, so answering with the terminal would overflow every pane, and
// this route answers only when nothing but the terminal owns the pane.
func ttyWidth() int {
	pid := os.Getpid()
	for range ancestorLimit {
		if pts := ttyOf(pid); pts != "" {
			if columns := winsizeColumns(pts); columns != 0 {
				return columns
			}
		}
		parent := parentOf(pid)
		// 1 is init and 0 means the read failed. Neither has a terminal, and
		// neither has a parent worth asking.
		if parent <= 1 {
			return 0
		}
		pid = parent
	}
	return 0
}

// ttyOf is the pts a process holds on one of its standard descriptors, named
// within ptsDir, or "".
//
// All three are checked because which one survives is not predictable: a
// process may have stdout redirected and stderr still on the terminal, which
// is exactly the shape Claude Code leaves behind.
func ttyOf(pid int) string {
	for _, fd := range []string{"0", "1", "2"} {
		link, err := os.Readlink("/proc/" + strconv.Itoa(pid) + "/fd/" + fd)
		if pts, ok := strings.CutPrefix(link, ptsDir+"/"); err == nil && ok {
			return pts
		}
	}
	return ""
}

// parentOf reads the parent pid out of /proc/<pid>/stat, or 0.
//
// It is parsed from the last ')', not by splitting. Field two is the executable
// name in parentheses and it can contain both spaces and parentheses, so a process
// named `foo bar) baz` shifts every field for anything that splits on space.
// The kernel guarantees the final ')' closes that field, so everything after
// it is positional and safe.
func parentOf(pid int) int {
	raw, err := os.ReadFile("/proc/" + strconv.Itoa(pid) + "/stat")
	if err != nil {
		return 0
	}
	closing := strings.LastIndex(string(raw), ")")
	if closing < 0 {
		return 0
	}
	// After the name: state, then ppid.
	fields := strings.Fields(string(raw)[closing+1:])
	if len(fields) < statFieldsAfterName {
		return 0
	}
	parent, err := strconv.Atoi(fields[1])
	if err != nil {
		return 0
	}
	return parent
}

// winsizeColumns asks a terminal its width with TIOCGWINSZ.
//
// An ioctl rather than `stty size`, which the rest of this file's subprocess
// machinery would also manage. This route runs whenever there is no
// multiplexer, which is the ordinary case, so it is the one that should not
// cost a fork on every render. tmux and herdr can afford one because they are
// answering only when they are present.
//
// O_NOCTTY matters: without it, opening a terminal from a process with no
// controlling terminal can ACQUIRE it as one, which is a side effect a status
// line has no business having.
//
// The pts is opened through a root on /dev/pts, so a link read out of /proc
// can name a terminal and nothing else.
func winsizeColumns(pts string) int {
	root, err := os.OpenRoot(ptsDir)
	if err != nil {
		return 0
	}
	defer func() { _ = root.Close() }()
	file, err := root.OpenFile(pts, os.O_RDONLY|syscall.O_NOCTTY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return 0
	}
	defer func() { _ = file.Close() }()

	var size struct{ Row, Col, Xpixel, Ypixel uint16 }
	_, _, errno := syscall.Syscall(
		syscall.SYS_IOCTL,
		file.Fd(),
		syscall.TIOCGWINSZ,
		uintptr(unsafe.Pointer(&size)), //nolint:gosec // the kernel writes a winsize here
	)
	if errno != 0 {
		return 0
	}
	return int(size.Col)
}
