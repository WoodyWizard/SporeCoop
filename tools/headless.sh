#!/usr/bin/env bash
# A headless GNOME Shell for the test games (docs/06-testing.md): nothing appears on the user's
# desktop and the games keep drawing. It has its own runtime, config and state folders and its own
# D-Bus session: sharing the real runtime folder lets it unmount the Flatpak document portal
# (/run/user/$UID/doc).
#
#   headless.sh up      start it (once) and print DISPLAY=... XAUTHORITY=...
#   headless.sh env     print the variables if it runs (exit 1 if not)
#   headless.sh down    stop it and everything it started (its process group)
#   headless.sh name    print the shell's name (its runtime folder under $XDG_RUNTIME_DIR)
#   headless.sh list    every lab shell that runs: "<name> DISPLAY=... XAUTHORITY=..."
#
# A16 (BUG-1113, docs/06-testing.md#testing-two-displays): the lab has SPORE_HEADLESS_DISPLAYS shells (default 2),
# each with its own Xwayland, runtime folder, config and D-Bus session; lane N draws on shell (N - 1) % displays:
# index 0 is "sporecoop-headless" (lanes 1, 3, 5, 7, 9), index k > 0 "sporecoop-headless-<k+1>" (lanes 2, 4, ...).
# One Xwayland for every lane saturated at 10-14 games (BUG-0663). SPORE_HEADLESS_NAME names one shell outright;
# SPORE_HEADLESS_DISPLAYS=1: one shared shell (the lab before 0.7.15). tools/labbudget.py has the same rule.
#
# The display comes from the live Xwayland of this shell (its -auth file is in our runtime folder),
# never from a log: a stale value once sent test games to the user's desktop.
set -euo pipefail
RTROOT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
DISPLAYS="${SPORE_HEADLESS_DISPLAYS:-2}"
[[ "$DISPLAYS" =~ ^[1-9]$ ]] || DISPLAYS=1
lane_name() {
    local lane="${SPORE_LANE:-1}" k
    [[ "$lane" =~ ^[0-9]+$ && "$lane" -ge 1 ]] || lane=1
    k=$(( (lane - 1) % DISPLAYS ))
    if [[ "$k" == 0 ]]; then echo sporecoop-headless; else echo "sporecoop-headless-$((k + 1))"; fi
}
NAME="${SPORE_HEADLESS_NAME:-$(lane_name)}"
RT="$RTROOT/$NAME"
# two virtual monitors side by side; SporeKit puts game windows in a grid of 1440x1080 cells (8x3:
# 24 games, lanes 1-8 need 16). Keep 5760x3240 per monitor: Wine offers 1440x1080 there (5760x4320
# does not). SPORE_HEADLESS_NAME starts a second shell beside the shared one (trials).
SIZE="${SPORE_HEADLESS_SIZE:-5760x3240}"
MONITORS="${SPORE_HEADLESS_MONITORS:-2}"

# "DISPLAY=:N XAUTHORITY=..." of the Xwayland that serves our shell (or the one of runtime folder $1), or nothing
envvars() {
    local p args disp auth rt="${1:-$RT}"
    for p in $(pgrep -x Xwayland || true); do
        args=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null || true)
        [[ "$args" == *"-auth $rt/"* ]] || continue
        disp=$(awk '{print $2}' <<< "$args")
        auth=$(grep -oP -- '-auth \K\S+' <<< "$args")
        [[ "$disp" == :* && -S "/tmp/.X11-unix/X${disp#:}" && -f "$auth" ]] || continue
        echo "DISPLAY=$disp XAUTHORITY=$auth"
        return 0
    done
    return 1
}

group() { [[ -f "$RT/pgid" ]] && cat "$RT/pgid"; }

# the group is ours only while its leader still runs with our runtime folder: after the shell died,
# its number can belong to any process group of the user (a stale pgid file must not kill that)
running() {
    local g
    g=$(group) && [[ "$g" =~ ^[0-9]+$ ]] || return 1
    [[ "$(ps -o pgid= -p "$g" 2>/dev/null | tr -d ' ')" == "$g" ]] || return 1
    tr '\0' '\n' < "/proc/$g/environ" 2>/dev/null | grep -qxF "XDG_RUNTIME_DIR=$RT"
}

case "${1:-}" in
    up)
        if envvars; then
            exit 0
        fi
        "$0" down >/dev/null 2>&1 || true
        mkdir -p "$RT"/{config,state,cache}
        chmod 700 "$RT"
        rm -f "$RT"/.mutter-Xwaylandauth.* "$RT/shell.log"
        vm=()
        for _ in $(seq 1 "$MONITORS"); do vm+=(--virtual-monitor "$SIZE"); done
        # setsid: its own session and process group, so "down" stops all of it
        # the environment goes in before dbus-run-session: services the bus starts (a document
        # portal among them) must see our runtime folder too, or stopping them unmounts the real
        # /run/user/$UID/doc and Bottles can no longer start
        setsid env -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY \
            XDG_RUNTIME_DIR="$RT" XDG_CONFIG_HOME="$RT/config" XDG_STATE_HOME="$RT/state" \
            XDG_CACHE_HOME="$RT/cache" GSETTINGS_BACKEND=keyfile \
            dbus-run-session -- gnome-shell --headless --wayland --wayland-display=sporecoop-0 "${vm[@]}" \
            > "$RT/shell.log" 2>&1 < /dev/null &
        echo $! > "$RT/pgid"
        for _ in $(seq 1 60); do
            sleep 0.5
            envvars && exit 0
            # mutter starts Xwayland on the first X client: knock on its display
            d=$(grep -oP 'Using public X11 display :\K[0-9]+' "$RT/shell.log" 2>/dev/null | tail -1 || true)
            [[ -n "$d" ]] && python3 -c "import socket,sys
for a in ('\0/tmp/.X11-unix/X' + sys.argv[1], '/tmp/.X11-unix/X' + sys.argv[1]):
    try:
        socket.socket(socket.AF_UNIX).connect(a); break
    except OSError: pass" "$d" 2>/dev/null || true
        done
        echo "headless shell did not start: $RT/shell.log" >&2
        exit 1
        ;;
    env) envvars ;;
    name) echo "$NAME" ;;
    list)
        for d in "$RTROOT"/sporecoop-headless "$RTROOT"/sporecoop-headless-*; do
            [[ -d "$d" ]] || continue
            v=$(envvars "$d") && echo "$(basename "$d") $v"
        done
        ;;
    down)
        if running; then
            kill -- "-$(group)" 2>/dev/null || true
            sleep 1
            kill -9 -- "-$(group)" 2>/dev/null || true
        fi
        rm -f "$RT/pgid"
        ;;
    *) echo "usage: [SPORE_LANE=N] headless.sh up|env|down|name|list"; exit 1 ;;
esac
