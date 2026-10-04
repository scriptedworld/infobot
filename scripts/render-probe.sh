#!/bin/sh
# Render the deployed status line once, against a payload file, without
# touching the real state directory.
#
#     scripts/render-probe.sh PAYLOAD [CONFIG_DIR]
#
# PAYLOAD is a session payload as Claude Code sends it. CONFIG_DIR, when given,
# stands in for XDG_CONFIG_HOME, so an empty directory shows the render with no
# rate table. Colour is off and no host is asked for a width, so the rows are
# plain text at the fixed bar.
#
# It runs bin/infobot, the committed shim, rather than the binary, so a missing
# build shows as the shim's message rather than as an error from this script.
set -eu

[ $# -ge 1 ] || {
    echo "usage: $0 PAYLOAD [CONFIG_DIR]" >&2
    exit 2
}
payload=$1
here=$(cd "$(dirname "$0")/.." && pwd)

state=$(mktemp -d)
trap 'rm -rf "$state"' EXIT

if [ $# -ge 2 ]; then
    XDG_CONFIG_HOME=$2
    export XDG_CONFIG_HOME
fi
NO_COLOR=1 TMUX='' HERDR_PANE_ID='' XDG_STATE_HOME=$state "$here/bin/infobot" <"$payload"
