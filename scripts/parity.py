#!/usr/bin/env python3
"""Content parity between the Go status line and another implementation.

    scripts/parity.py --other STATUSLINE --other-forget FORGET
    scripts/parity.py --self-test

Both implementations get the same input, each writing into a state directory of
its own, and what a reader of the output sees is compared:

    rows          stdout, exactly. A row is text and has no other structure.
    state files   the .status.yaml and offsets .json, decoded and compared as
                  data with their types. Bytes may differ. `written` is checked
                  by shape, since the two runs can straddle a second.
    exit status   always.

The inputs, from most synthetic to least:

    golden        testdata/parity/golden, with no host to ask
    widths        pane widths and percentages through a stub herdr, in colour
                  and with NO_COLOR, and a few widths through a stub tmux
    countdown     rate limit resets a known distance ahead, and one elapsed
    state         strings and numbers the state file has to carry
    config        golden and some widths again under this machine's
                  ~/.config/infobot, when there is one
    transcripts   recent sessions under ~/.claude/projects, copied first so a
                  live session cannot grow between the two runs
    forget        testdata/parity/forget-payloads.jsonl through the cleanup

Stops at the first difference, printing the input that produced it, and exits 1.
Working files go under .ephemera/parity/run, emptied at the start of each run.

--self-test checks the checker: Go against itself must agree, and Go against a
copy of itself that changes one character of a row, or one value in the state
file, must each be caught.
"""

import argparse
import dataclasses
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
TESTDATA = ROOT / "testdata" / "parity"
GO = ROOT / "bin" / "statusline"
GO_FORGET = ROOT / "bin" / "forget"
SESSION = "901de17c-0000-4000-8000-000000000000"
WRITTEN = re.compile(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d[+-]\d\d:\d\d$")
TRANSCRIPT_BYTES = 250_000_000

TMUX_STUB = '#!/bin/sh\nprintf "%s\\n" "$PARITY_WIDTH"\n'
HERDR_STUB = (
    "#!/bin/sh\nprintf '"
    '{"result":{"layout":{"area":{"width":%s},"focused_pane_id":"w1:p1",'
    '"zoomed":false,"panes":[{"pane_id":"w1:p1","rect":{"width":%s}}]}}}'
    "\\n' \"$PARITY_WIDTH\" \"$PARITY_WIDTH\"\n"
)


@dataclasses.dataclass
class Case:
    name: str
    stdin: str
    env: dict = dataclasses.field(default_factory=dict)
    home: str = str(pathlib.Path.home())
    config: str = ""
    renders: int = 1


@dataclasses.dataclass
class Outcome:
    exit: int
    stdout: bytes
    files: dict


class Workspace:
    """A run's directory: stub hosts, empty config, and fresh state directories."""

    def __init__(self, root):
        self.root = root
        shutil.rmtree(root, ignore_errors=True)
        self.bin = root / "bin"
        self.bin.mkdir(parents=True)
        for name, body in (("tmux", TMUX_STUB), ("herdr", HERDR_STUB)):
            (self.bin / name).write_text(body)
            (self.bin / name).chmod(0o755)
        self.empty_config = root / "config-empty"
        self.empty_config.mkdir()
        self.count = 0

    def state(self):
        self.count += 1
        path = self.root / "state" / str(self.count)
        path.mkdir(parents=True)
        return path


def environment(ws, case, state):
    env = {
        "PATH": f"{ws.bin}:/usr/bin:/bin",
        "HOME": case.home,
        "XDG_CONFIG_HOME": case.config or str(ws.empty_config),
        "XDG_STATE_HOME": str(state),
    }
    if "TZ" in os.environ:
        env["TZ"] = os.environ["TZ"]
    env.update(case.env)
    return env


def decoded(path):
    text = path.read_text(errors="replace")
    try:
        data = yaml.safe_load(text) if path.suffix == ".yaml" else json.loads(text)
    except (yaml.YAMLError, json.JSONDecodeError):
        return f"unparseable: {text!r}"
    if isinstance(data, dict) and "written" in data:
        data["written"] = "shape ok" if WRITTEN.match(str(data["written"])) else data["written"]
    return json.dumps(data, sort_keys=True)


def state_files(state):
    return {str(p.relative_to(state)): decoded(p) for p in sorted(state.rglob("*")) if p.is_file()}


def render(binary, ws, case, state):
    done = subprocess.run([str(binary)], input=case.stdin.encode(), capture_output=True,
                          env=environment(ws, case, state), timeout=30, check=False)
    return Outcome(done.returncode, done.stdout, state_files(state))


def difference(go, other):
    if go.exit != other.exit:
        return f"exit status: go {go.exit}, other {other.exit}"
    if go.stdout != other.stdout:
        at = next((i for i, (a, b) in enumerate(zip(go.stdout, other.stdout)) if a != b),
                  min(len(go.stdout), len(other.stdout)))
        return (f"rows differ at byte {at}\n  go    {go.stdout[max(0, at - 60):at + 60]!r}"
                f"\n  other {other.stdout[max(0, at - 60):at + 60]!r}")
    if go.files != other.files:
        return f"state differs\n  go    {go.files}\n  other {other.files}"
    return None


def compare(ws, other, case):
    """The first difference across the case's renders, and Go's last rows."""
    go_state, other_state = ws.state(), ws.state()
    rows = b""
    for _ in range(case.renders):
        go = render(GO, ws, case, go_state)
        found = difference(go, render(other, ws, case, other_state))
        if found:
            return found, go.stdout
        rows = go.stdout
    shutil.rmtree(go_state)
    shutil.rmtree(other_state)
    return None, rows


def payload(pct: float = 40, five: float = 20, seven: float = 30, session=SESSION, **extra):
    body = {
        "session_id": session,
        "model": {"display_name": "Opus 5 (1M context)"},
        "effort": {"level": "xhigh"},
        "workspace": {"current_dir": "/home/me/src/infobot", "project_dir": "/home/me/src"},
        "context_window": {"context_window_size": 1000000, "used_percentage": pct},
        "rate_limits": {"five_hour": {"used_percentage": five},
                        "seven_day": {"used_percentage": seven}},
    }
    body.update(extra)
    return json.dumps(body)


def golden(config=""):
    for path in sorted((TESTDATA / "golden").glob("*.txt")):
        sections, name = {}, None
        for line in path.read_text().split("\n"):
            if line.startswith("## "):
                name = line[3:].strip()
                sections[name] = []
            elif name:
                sections[name].append(line)
        env = dict(line.split("=", 1) for line in sections.get("env", []) if "=" in line)
        yield Case(f"golden {path.stem}", "\n".join(sections.get("stdin", [])).strip(), env,
                   config=config)


def widths(ws, config="", sweep=True):
    herdr = {"HERDR_PANE_ID": "w1:p1", "HERDR_BIN_PATH": str(ws.bin / "herdr")}
    columns = [40, 60, 80, 100, 120, 160, 197, 223, 300] if sweep else [80, 160]
    percents = [0, 1, 12.5, 23, 49.5, 50, 74.9, 75, 75.1, 80, 88, 89.9, 90, 95, 99.9, 100]
    for width in columns:
        for pct in percents if sweep else [0, 49.5, 90, 100]:
            for five in (0, 34, 99) if sweep else (34,):
                env = dict(herdr, PARITY_WIDTH=str(width))
                yield Case(f"herdr width {width} pct {pct} five {five}", payload(pct, five), env,
                           config=config)
    for width in (80, 160):
        for pct in (12.5, 90):
            env = dict(herdr, PARITY_WIDTH=str(width), NO_COLOR="1")
            yield Case(f"no colour width {width} pct {pct}", payload(pct), env, config=config)
    for width in (80, 160, 300):
        env = {"TMUX": "/tmp/tmux-parity,1,0", "TMUX_PANE": "%1", "PARITY_WIDTH": str(width)}
        yield Case(f"tmux width {width}", payload(33), env, config=config)


def countdown():
    now = int(time.time())
    env = {"TMUX": "/tmp/tmux-parity,1,0", "TMUX_PANE": "%1", "PARITY_WIDTH": "200"}
    for ahead in (5 * 60, 2 * 3600, 3 * 86400, -600):
        limits = {"five_hour": {"used_percentage": 30, "resets_at": now + ahead + 30},
                  "seven_day": {"used_percentage": 60, "resets_at": now + ahead * 7 + 30}}
        yield Case(f"countdown {ahead}s", payload(rate_limits=limits), env)


def state_inputs():
    texts = ["plain", "🧠 brain", "a\U0001F600b", "\ufeffbom first", "mid\ufeffdle", "tab\there",
             'quote " and \\ backslash', "é ñ 中文", "\u00a0nbsp", "x" * 300, "\u2028\u2029"]
    texts += [f"/a{chr(cp)}b" for cp in [*range(0x20), 0x7F, *range(0x80, 0xA0)]]
    for text in texts:
        yield f"text {text[:12]!r}", {"session_id": SESSION, "workspace": {"current_dir": text},
                                      "model": {"display_name": text}, "effort": {"level": text}}
    for pct in (0, 0.0, 1e-7, 12.25, 99.95, 100, 130, 1e6, 1e21, -5):
        yield f"percent {pct}", {"session_id": SESSION,
                                 "context_window": {"context_window_size": 1000, "used_percentage": pct}}
    for used in (0, 1, 999999999999, 1e19, 1e300):
        yield f"used {used}", {"session_id": SESSION, "context_window": {
            "context_window_size": 1000, "current_usage": {"input_tokens": used}}}
    yield "model id only", {"session_id": SESSION, "model": {"id": "claude-x"}}
    yield "no session", {"model": {"display_name": "x"}}


def state():
    for name, body in state_inputs():
        yield Case(f"state {name}", json.dumps(body))


def machine_config(ws):
    source = pathlib.Path.home() / ".config" / "infobot"
    if not source.is_dir():
        return
    config = ws.root / "config-machine"
    shutil.copytree(source, config / "infobot")
    yield from golden(str(config))
    yield from widths(ws, str(config), sweep=False)


def snapshot(ws, sessions):
    projects = pathlib.Path.home() / ".claude" / "projects"
    home = ws.root / "home"
    found, copied, total = [], set(), 0
    newest = sorted(projects.glob("*/*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
    for transcript in newest:
        if len(found) >= sessions:
            break
        project = transcript.parent
        if project not in copied:
            size = sum(p.stat().st_size for p in project.rglob("*") if p.is_file())
            if total + size > TRANSCRIPT_BYTES:
                continue
            shutil.copytree(project, home / ".claude" / "projects" / project.name)
            copied.add(project)
            total += size
        found.append(transcript.stem)
    return home, found


def transcripts(ws, sessions):
    if sessions <= 0 or not (pathlib.Path.home() / ".claude" / "projects").is_dir():
        return
    home, found = snapshot(ws, sessions)
    env = {"TMUX": "/tmp/tmux-parity,1,0", "TMUX_PANE": "%1", "PARITY_WIDTH": "200"}
    for session in [*found, "00000000-0000-4000-8000-00000000abcd"]:
        yield Case(f"transcript {session[:8]}", payload(session=session), env, str(home),
                   renders=2)


def forget_outcome(binary, ws, line):
    state = ws.state()
    names = ("s1.status.yaml", "s1.json", "only-offsets.json", "innocent.status.yaml")
    (state / "infobot").mkdir()
    for name in names:
        (state / "infobot" / name).write_text("")
    done = subprocess.run([str(binary)], input=line.encode(), capture_output=True, timeout=30,
                          env={"PATH": "/usr/bin:/bin", "XDG_STATE_HOME": str(state)}, check=False)
    log = done.stderr.decode(errors="replace").replace(str(state), "STATE").encode()
    left = {str(p.relative_to(state)): "" for p in sorted(state.rglob("*")) if p.is_file()}
    shutil.rmtree(state)
    return Outcome(done.returncode, log, left)


def forget(ws, other_forget):
    for line in (TESTDATA / "forget-payloads.jsonl").read_text().splitlines():
        found = difference(forget_outcome(GO_FORGET, ws, line), forget_outcome(other_forget, ws, line))
        if found:
            return f"forget {line!r}: {found}"
    return None


def check(other, other_forget, sessions, groups=None):
    ws = Workspace(ROOT / ".ephemera" / "parity" / "run")
    plan = {"golden": golden, "widths": lambda: widths(ws),
            "countdown": countdown, "state": state,
            "config": lambda: machine_config(ws), "transcripts": lambda: transcripts(ws, sessions)}
    for group, cases in plan.items():
        if groups and group not in groups:
            continue
        count = priced = 0
        for case in cases():
            found, rows = compare(ws, other, case)
            if found:
                print(f"DIFFERS  {case.name}\n  input {case.stdin[:300]!r}\n  env   {case.env}\n{found}")
                return False
            count += 1
            priced += "💵".encode() in rows
        # A cost segment is the only proof the transcript path ran at all.
        print(f"{group:12s} {count} identical, {priced} with a cost segment")
    if other_forget and (not groups or "forget" in groups):
        found = forget(ws, other_forget)
        if found:
            print(f"DIFFERS  {found}")
            return False
        print("forget       identical")
    return True


def mutant(ws, name, body):
    path = ws.root / name
    path.write_text(f"#!{sys.executable}\nimport os, re, subprocess, sys\nGO = {str(GO)!r}\n{body}")
    path.chmod(0o755)
    return path


ROW_MUTANT = """done = subprocess.run([GO], input=sys.stdin.buffer.read(), capture_output=True)
out = done.stdout.replace(b"%", b"~", 1)
sys.stdout.buffer.write(out)
sys.exit(done.returncode)
"""

STATE_MUTANT = """done = subprocess.run([GO], input=sys.stdin.buffer.read(), capture_output=True)
for path in __import__("pathlib").Path(os.environ["XDG_STATE_HOME"]).rglob("*.status.yaml"):
    path.write_text(re.sub(r'("context_percent": )', r"\\g<1>1", path.read_text()))
sys.stdout.buffer.write(done.stdout)
sys.exit(done.returncode)
"""


def self_test():
    groups = {"golden", "state"}
    holder = Workspace(ROOT / ".ephemera" / "parity" / "mutants")
    rows, values = mutant(holder, "rows", ROW_MUTANT), mutant(holder, "values", STATE_MUTANT)
    results = []
    for name, other, forget_binary, want in (("go against itself", GO, GO_FORGET, True),
                                              ("a changed row character", rows, None, False),
                                              ("a changed state value", values, None, False)):
        print(f"--- {name}, expecting {'agreement' if want else 'a difference'}")
        results.append((name, check(other, forget_binary, 0, groups), want))
    failed = [name for name, agreed, want in results if agreed != want]
    print("self-test: " + ("every result as expected" if not failed else f"wrong for {failed}"))
    return not failed


def main():
    parser = argparse.ArgumentParser(description="Content parity between Go and another build.")
    parser.add_argument("--other", help="the statusline binary to compare with Go's")
    parser.add_argument("--other-forget", help="the forget binary to compare with Go's")
    parser.add_argument("--transcripts", type=int, default=8,
                        help="recent sessions to copy and render, 0 for none")
    parser.add_argument("--self-test", action="store_true", help="check that the checker catches")
    args = parser.parse_args()
    if args.self_test:
        return 0 if self_test() else 1
    if not args.other:
        parser.error("--other is required")
    agreed = check(pathlib.Path(args.other).resolve(),
                   pathlib.Path(args.other_forget).resolve() if args.other_forget else None,
                   args.transcripts)
    return 0 if agreed else 1


if __name__ == "__main__":
    sys.exit(main())
