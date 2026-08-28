# A claim phrased as a conclusion has no seam to check

2026-08-28. Two findings from this repository travelled to other sessions and
were written into other repositories' files before anyone checked them. Both
were wrong in the same way, and so was the correction I first offered for one.

## What happened

**"The shims are gated by nothing."** FACT 2026-08-28: no checker reads their
text, because all three the gate runs select by Go extension. But
`cmd/statusline/shim_test.go` copies the committed shim into a scratch directory
and executes it, five cases carrying `COVERS:` marks for FR-1.13 both ways,
FR-3.8, FR-1.9 and FR-1.11f. Unread is not untested, and I had not looked before
saying so.

It reached toolbox, who repeated it into two committed files without opening
`cmd/statusline/`, and was about to re-rank a `.ready` task on the strength of
it.

**"dotfiles has 8 bash scripts and 10 zsh fragments."** I relayed that into this
project's own task file, having measured nothing and having misattributed it to
an inbox entry that does not contain it. FACT 2026-08-28, measured:

    git ls-files | while read f; do [ -f "$f" ] || continue
      case "$(head -c 200 "$f" | head -1)" in \#!*bash*|\#!*/sh) echo "$f";; esac
    done                                                    6, not 8

    git ls-files | grep -iE '\.zsh$|zshrc|zshenv|zprofile|zlogin|/zsh/'
                                                            17, not 10

No reading reaches 8 or 10. `*.sh` by extension is 1, tracked files under `bin/`
are 23, `config/zsh/*.zsh` alone is 15, `*.zsh` anywhere is 16.

CLAIM 2026-08-28, from toolbox and not measured here: checking that figure
caught its neighbour in the same paragraph, `install.sh` recorded at 4,380 bytes
against an actual 4,376. Re-derive with `wc -c` in `agent-support` before
quoting it. **It is marked because this document would otherwise do the thing it
describes**, relaying somebody else's number as though the checking were mine.

## Why verifying did not catch it, which is the part worth keeping

Both claims arrived **already phrased as conclusions**, so there was nothing to
check them at. "Gated by nothing" has no seam. "8 bash scripts" has no seam.
Either is believed or disbelieved whole, and a busy session believes it.

    wc -c install.sh -> 4376, 2026-08-28

has three: the command, the number, and the date. Any of them failing is
visible, and re-running it costs a second.

That is a sharper rule than "verify what you are told", which nobody can act on
at scale because nothing can be re-derived exhaustively. **Carrying the command
makes checking cheap enough to actually happen.**

## What to do

**Relay findings as the command, not the conclusion.** A finding that cannot
carry its command is a lead by construction, and should be sent as one: a thing
to ask its owner about, never a thing to design against.

**The owner is not the preferred source, it is the only source.** silo relayed
the shim finding accurately and could not carry its consequence, because the
consequence depended on knowing what `bin/infobot` is for. That could not have
been summarised at all, only re-derived at the far end.

**An example arriving pre-shaped is what makes the mistake easy, not what makes
it excusable.** agent-support used this project's own Sonnet 5 note to argue
that rates move unpredictably and want frequent checking. It argues the
opposite: the cancelled rise was announced beside the table rather than in it,
so a fetch reading only the table cannot tell a cancelled increase from an
unapplied one at any interval. It argues for a person, never for a shorter
window. I offered that as my wording having misled them; they declined the
excuse, and they were right to.

## What it cost, and did not

Nothing shipped wrong. Every instance was caught by somebody else's arithmetic
within hours, which is the system working rather than a near miss. What it cost
was two repositories' files being corrected after the fact, and one task being
sized against a claim that had been wrong the whole time it was open.
