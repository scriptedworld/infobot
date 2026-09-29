# The port removed the interpreter, not the file search

The Go port was done for startup cost, since the status line runs on every
Claude Code event. Two figures came out of measuring it, and only the second is
what a session pays:

    render alone          Python 25.2ms   Go  4.0ms   6.3x
    a real event          Python 40.4ms   Go 11.8ms   3.4x

Quoted alone, the first read as a sixfold win. The second adds the two things a
real render does and the corpus case leaves out: a pane width to fit to, and a
live session id, which sends the cost segment looking for the transcripts it
names under `~/.claude/projects`.

That search takes about 15 ms in either language. It was ported as it stood,
and it is roughly 60% of what a Go render spends. Most of what the port removed
was the interpreter starting; what is left is dominated by file search, which
no choice of language changes.

## What to do

Measure the event the user pays for, not the function that was rewritten. When
a speedup shrinks between the two, the difference is the part the rewrite did
not touch, and it is the next thing to work on.

The figures and how to re-measure them are in `docs/PROJECT.md`, "What it is
FOR".
