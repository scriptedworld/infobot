# The state file is emitted by wrench's pack

`internal/state/state.go` writes each session's `.status.yaml` through wrench's
Go pack (`github.com/scriptedworld/wrench/go`). infobot keeps no emitter of its
own.

## Why

The form is a published interface whose readers match its exact layout
(FR-1.11o). wrench's pack emits canonical YAML to that shape, and
it is the emitter every other tool here uses.

Before the Go port, infobot emitted the form by hand, kept in step with wrench
by a fixture at each end. That duplicate was deliberate: the Python pack's
import cost and an interpreter that might not resolve were the reasons against
linking. A statically linked Go pack has neither, so the port removed the
reason, and infobot `1b96c85` took the link and deleted the hand emitter. wrench's
`docs/DECISIONS/infobots-hand-emitted-yaml-is-a-considered-duplicate.md` is the
decision this supersedes.

FR-1.11p is the guarantee that remains: one emitter and a conformance check.
infobot asserts the exact bytes it emits, so a change in wrench's canonical form
fails a test here instead of changing the file silently.

## infobot builds against the wrench that is pushed

`go.mod` names the pack at a pseudo-version fetched from GitHub, with no
`replace`, so the checkout of wrench beside this tree is invisible to the build.
That is the right default: infobot is verified against the pack a fresh clone
gets. The consequence is ordering. A wrench change reaches infobot only once
wrench has pushed and infobot has taken the new version, and every green run in
between is a run against the older pack. `NEXT_STEPS.md` carries the upgrade
steps and the current pin.

## What it costs

The pack validates against the schema on every write, and linking it brings a
JSON schema validator into the render. Its package init is 1.8 to 3.2 ms per
event, measured with `GODEBUG=inittrace=1`. That contradicts the shape
`published-form/10` settled, that nothing is added to the render path, and
whether to keep it is open in that task.
