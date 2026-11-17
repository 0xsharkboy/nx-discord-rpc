#!/usr/bin/env bash
# Renders the app screens from the desktop build, one 1280x720 PNG per scenario, into build/screenshots.
# Run it in the desktop shell: nix develop .#desktop -c make screenshots
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
bin=$root/build/app-desktop/nx-discord-rpc
out=$root/build/screenshots
mkdir -p "$out"

[ -x "$bin" ] || { echo "missing $bin: run 'make app-desktop' first" >&2; exit 1; }

# Keys: Enter is A, the arrows are the D-pad.
# Xvfb has no GPU, and a hardware driver renders black frames into it.
export LIBGL_ALWAYS_SOFTWARE=1
# GLFW prefers Wayland when it is available: keep the app on the Xvfb display.
unset WAYLAND_DISPLAY
export XDG_SESSION_TYPE=x11

pid=
cleanup() { [ -n "$pid" ] && kill "$pid" 2>/dev/null; true; }
trap cleanup EXIT

key() { xdotool keydown "$1"; sleep 0.15; xdotool keyup "$1"; sleep 0.5; }

# shoot <name> <scenario> <key>...
# Starts the app on <scenario>, sends the keys, then saves the window as <name>.png.
shoot() {
    local name=$1 scenario=$2
    shift 2
    # Borealis loads ./resources, so the app runs from its build directory.
    (cd "$(dirname "$bin")" && exec "$bin" --fake "$scenario") >/dev/null 2>&1 &
    pid=$!
    # Wait for the window, then give the first frames time to render.
    timeout 30 xdotool search --sync --onlyvisible --name "" >/dev/null
    sleep 2
    for k in "$@"; do key "$k"; done
    sleep 1.5
    kill -0 "$pid" 2>/dev/null || { echo "$name: the app exited early" >&2; exit 1; }
    xwd -root -silent | magick xwd:- -strip -colors 128 -define png:compression-level=9 "$out/$name.png"
    cleanup
    wait "$pid" 2>/dev/null || true
    pid=
}

run() {
    shoot status linked-game
    shoot settings linked-game Down
    shoot account linked-game Down Down
    shoot link link-flow Down Down Right Return
}

if [ -z "${DRPC_SHOTS_INNER:-}" ]; then
    export DRPC_SHOTS_INNER=1
    exec xvfb-run -a -s '-screen 0 1280x720x24' "$0" "$@"
fi
run
