"""Measure one status line implementation: wall time per render, and peak RSS.

    python3 bench.py <label> <runs> <payload> <command> [args...]

Run ONE implementation per process. `ru_maxrss` from RUSAGE_CHILDREN is a
monotonic high-water mark across every child this process has reaped, so two
implementations measured in one process report the larger of the two twice.

Wall time is the whole `fork` + `exec` + render + exit, because that is what
Claude Code pays. A figure that excludes process startup measures the part of
the cost the port was not about.
"""

import resource
import subprocess
import sys
import time


def main() -> int:
    label, runs, payload = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    command = sys.argv[4:]

    with open(payload, "rb") as handle:
        stdin = handle.read()

    # One unmeasured run, so the page cache holds the binary and the
    # interpreter's own imports for both sides alike.
    subprocess.run(command, input=stdin, capture_output=True)

    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    timings = []
    for _ in range(runs):
        start = time.perf_counter()
        done = subprocess.run(command, input=stdin, capture_output=True)
        timings.append((time.perf_counter() - start) * 1000)
        if done.returncode != 0:
            print(f"{label}: exit {done.returncode}", file=sys.stderr)
            print(done.stderr.decode()[:400], file=sys.stderr)
            return 1
    after = resource.getrusage(resource.RUSAGE_CHILDREN)

    timings.sort()
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    print(
        f"{label:8s} "
        f"min {timings[0]:6.1f}  "
        f"median {timings[len(timings) // 2]:6.1f}  "
        f"mean {sum(timings) / len(timings):6.1f}  "
        f"max {timings[-1]:6.1f}  ms/render   "
        f"peakRSS {after.ru_maxrss / 1024:6.1f} MB   "
        f"cpu {cpu / runs * 1000:6.1f} ms/render"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
