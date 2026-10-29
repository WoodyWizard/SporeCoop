#!/usr/bin/env bash
# SporeCoop helper: install loader, deploy builds, run/kill the game in Bottles, logs.
set -euo pipefail
# env -i, containers, cron: no $USER / $HOME (BUG-0252); not exported (Bottles keeps the session's)
USER="${USER:-$(id -un 2>/dev/null || echo user)}"
HOME="${HOME:-$(getent passwd "$(id -u)" 2>/dev/null | cut -d: -f6)}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# test lanes (docs/10-dev-pipeline.md): lane 1 = the real install in the "Games" bottle; lane N>1 =
# its own bottle SporeLabN (own wineserver: a crash or quit there leaves the other lanes alone),
# its own game folder (own mods, logs, SporeCoop.package) and instances 2N-1 (host) / 2N (guest)
LANE="${SPORE_LANE:-1}"
MAIN_GAME="$(cd "$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")/.." && pwd)"
LAB="${SPORE_LAB:-$(dirname "$MAIN_GAME")/SPORE-lab}"
if [[ "$LANE" == 1 ]]; then
    GAME="${SPORE_GAME:-$MAIN_GAME}"
    BOTTLE="${SPORE_BOTTLE:-Games}"
else
    GAME="${SPORE_GAME:-$LAB/lane$LANE}"
    BOTTLE="${SPORE_BOTTLE:-SporeLab$LANE}"
fi
HOST_INST=$((2 * LANE - 1))
BIN="$GAME/SporebinEP1"
MODS="$BIN/mods"
BACKUP="$ROOT/out/backup"
BOTTLES_DIR="$HOME/.var/app/com.usebottles.bottles/data/bottles/bottles"
# fixtures are shared by every worktree: they live in the main checkout
FIXTURES="${SPORE_FIXTURES:-$MAIN_GAME/SporeCoop/out/fixtures}"

bottles() { flatpak run --command=bottles-cli com.usebottles.bottles "$@"; }

is_ours() { strings "$1" 2>/dev/null | grep -q "sporekit-loader"; }

cmd_install() {
    mkdir -p "$BACKUP"
    if [[ ! -f "$BIN/steam_api_orig.dll" ]]; then
        if is_ours "$BIN/steam_api.dll"; then
            echo "error: steam_api.dll is already the loader but steam_api_orig.dll is missing" >&2
            exit 1
        fi
        cp -n "$BIN/steam_api.dll" "$BACKUP/steam_api.dll.gog"
        mv "$BIN/steam_api.dll" "$BIN/steam_api_orig.dll"
        echo "original steam_api.dll -> steam_api_orig.dll (backup in out/backup)"
    fi
    cp "$ROOT/build/steam_api.dll" "$BIN/steam_api.dll"
    echo "loader installed"
}

cmd_uninstall() {
    if [[ -f "$BIN/steam_api_orig.dll" ]]; then
        mv -f "$BIN/steam_api_orig.dll" "$BIN/steam_api.dll"
        rm -f "$BIN/../Data/SporeCoop.package"
        echo "original steam_api.dll restored (mods folder left in place)"
    else
        echo "loader not installed"
    fi
}

cmd_deploy() {
    mkdir -p "$MODS/sporekit/symbols" "$MODS/logs"
    # a lane whose loader is gone (update_check.py restores the original files) starts the game
    # without SporeKit: tests waited 90 s for a console that never came
    if [[ "$LANE" != 1 && ! -f "$BIN/steam_api_orig.dll" ]] && ! is_ours "$BIN/steam_api.dll"; then
        cmd_install
    fi
    [[ -f "$BIN/steam_api_orig.dll" ]] && cp "$ROOT/build/steam_api.dll" "$BIN/steam_api.dll"
    cp "$ROOT/build/sporekit.dll" "$BIN/sporekit.dll" 2>/dev/null || {
        echo "sporekit.dll is locked (game running?) - skipped" >&2; }
    for f in "$ROOT"/build/*.dll; do
        n=$(basename "$f")
        case "$n" in steam_api.dll|sporekit.dll) continue ;; esac
        cp "$f" "$MODS/$n"
    done
    cp "$ROOT"/data/symbols/*.sym "$MODS/sporekit/symbols/"
    # game resources: Spore loads every .package in its Data folder
    [[ -f "$ROOT/build/SporeCoop.package" ]] && cp "$ROOT/build/SporeCoop.package" "$BIN/../Data/SporeCoop.package"
    for ini in "$ROOT"/data/config/*.ini; do
        [[ -f "$ini" ]] || continue
        [[ -f "$MODS/$(basename "$ini")" ]] || cp "$ini" "$MODS/"
    done
    echo "deployed to $MODS"
}

APPDATA_DIR="$HOME/.var/app/com.usebottles.bottles/data/bottles/bottles/$BOTTLE/drive_c/users/$USER/AppData/Roaming"

# Test profiles live in AppData/Roaming/SporeKit-<name>/Spore and start from a copy of
# the player's preferences with windowed mode and without tutorials/hints.
prepare_profile() {
    local name="$1" size="${PROFILE_SCREEN_SIZE:-12}"
    local dst="$APPDATA_DIR/SporeKit-$name/Spore/Preferences"
    mkdir -p "$dst"
    local src="$APPDATA_DIR/Spore/Preferences"
    [[ -f "$dst/login.prop" || ! -f "$src/login.prop" ]] || cp "$src/login.prop" "$dst/"
    [[ -f "$dst/Preferences.prop" ]] || cp "$src/Preferences.prop" "$dst/" 2>/dev/null || touch "$dst/Preferences.prop"
    # SPORE_TUTORIALS=1: tutorials and hints on, like a player's first game (default off)
    python3 - "$dst/Preferences.prop" "$size" "${SPORE_TUTORIALS:-0}" <<'PY'
import sys
p, size, tut = sys.argv[1], sys.argv[2], sys.argv[3]
want = {'OptionFullScreen': '0', 'OptionTutorialsEnabled': tut, 'OptionShowHints': tut,
        'OptionEdgeScroll': '0', 'OptionScreenSize': size,
        # test runs are silent (only the test profile, the player's own settings stay as they are)
        'AudioMuteAll': 'true', 'AudioMasterVolume': '0.000000'}
lines = open(p).read().splitlines()
seen = set()
out = []
for l in lines:
    k = l.split(' ', 1)[0]
    if k in want:
        l = k + ' ' + want[k]
        seen.add(k)
    out.append(l)
out += [k + ' ' + v for k, v in want.items() if k not in seen]
open(p, 'w').write('\n'.join(out) + '\n')
PY
}

# a [sporekit] setting in the lane's mods/sporekit.ini (created by deploy)
sk_ini_set() {
    local f="$MODS/sporekit.ini"
    [[ -f "$f" ]] || return 0
    if grep -q "^$1=" "$f"; then sed -i "s/^$1=.*/$1=$2/" "$f"; else sed -i "/^\[sporekit\]/a $1=$2" "$f"; fi
}

cmd_run() {
    local inst=1 profile="" extra=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -i) inst="$2"; shift 2 ;;
            -p) profile="$2"; shift 2 ;;
            -x) extra+=("$2"); shift 2 ;;
            *) inst="$1"; shift ;;
        esac
    done
    local args=("${extra[@]}")
    [[ "$inst" != "1" ]] && args+=("-sporekit-instance:$inst")
    if [[ -n "$profile" ]]; then
        prepare_profile "$profile"
        args+=("-sporekit-profile:$profile")
    elif [[ "$inst" != "1" ]]; then
        prepare_profile "inst$inst"
    fi
    mkdir -p "$ROOT/out"
    [[ -f "$BIN/SporeApp.exe" ]] || { echo "no game at $BIN (lane $LANE: spore.sh lane create $LANE)" >&2; exit 1; }
    # test games draw on a headless GNOME Shell (tools/headless.sh): nothing on the user's desktop,
    # and they keep drawing; SPORE_HEADLESS=0 shows them on the desktop instead
    local display=()
    if [[ "${SPORE_HEADLESS:-1}" == 1 ]] && command -v gnome-shell >/dev/null; then
        local hl
        hl=$("$ROOT/tools/headless.sh" up) || { echo "the headless test display did not start" >&2; exit 1; }
        # shellcheck disable=SC2206
        display=(env -u WAYLAND_DISPLAY $hl)
        sk_ini_set test_monitor grid
    fi
    nohup "${display[@]}" flatpak run --command=bottles-cli com.usebottles.bottles run -b "$BOTTLE" \
        -e "$BIN/SporeApp.exe" -- "${args[@]}" > "$ROOT/out/run.$inst.log" 2>&1 &
    # opt-in: a window on a hidden workspace gets no frame callbacks from GNOME, DXVK then waits
    # forever and the game stops at frame 1 (seen 2026-10-23); use the headless shell instead
    if [[ "${SPORE_AGENT_WS:-0}" == 1 && "${SPORE_HEADLESS:-1}" != 1 ]] && command -v agent-ws >/dev/null; then
        # the game windows go to the "sporecoop" project's own workspace (agent-ws): Bottles drops
        # the environment that agent-ws launch would mark them with, so find them by process
        sk_ini_set test_monitor off
        nohup "$ROOT/tools/assign_windows.py" sporecoop "$(basename "$GAME")" "${args[*]:-}" > /dev/null 2>&1 &
    fi
    echo "started instance $inst ${profile:+profile $profile }(bottles log: out/run.$inst.log)"
}

# the Wine process command line starts with the Windows path, e.g. F:\...\SPORE\SporebinEP1\SporeApp.exe:
# only this lane's game folder matches
GAME_RE="^[A-Z]:\\\\.*\\\\$(basename "$GAME")\\\\SporebinEP1\\\\SporeApp\\.exe"

# The processes of this lane's games, one "<kind> <pid>" per line (BUG-0025, BUG-0101, BUG-0103):
#   game     a SporeApp.exe of this lane (its Windows path names the lane's game folder); a half-dead
#            one (zombie leader + live threads) is found by a live thread's command line
#   tree     a launch sandbox (bwrap) of this lane's game: its whole tree goes
#   wrapper  a leftover launch wrapper (bottles-cli run, start.exe) of this lane's game
#   owner    lane 1 only: a game of the real install started WITHOUT a test argument, i.e. the
#            owner's own Spore. Never killed; while it runs, no Wine tree of the bottle is killed.
# Lane 1 is the real install the owner plays from: only games started for tests (a
# -sporekit-profile: or -sporekit-instance: argument, tools/spore.sh run) are ours there.
# Lanes N>1 are ours alone: every game of the lane, and anything else that runs in its bottle.
# $1: only games of that SporeKit instance (1 = no -sporekit-instance argument).
lane_procs() {
    python3 - "$(basename "$GAME")" "$BIN/SporeApp.exe" "$BOTTLE" "$LANE" "${1:-}" <<'PY'
import os, re, sys
folder, bin_exe, bottle, lane, inst = sys.argv[1:6]
game_rx = re.compile(r'^[A-Z]:\\.*\\' + re.escape(folder) + r'\\SporebinEP1\\SporeApp\.exe', re.I)
skip, p = set(), os.getpid()  # this script and the shells that called it
while p > 1 and p not in skip:
    skip.add(p)
    try:
        p = int(open(f'/proc/{p}/stat').read().rsplit(')', 1)[1].split()[1])
    except (OSError, IndexError, ValueError):
        break


def argv(pid):
    """the command line; a zombie leader has none, a live thread of its group still has it"""
    def read(path):
        try:
            return open(path, 'rb').read().decode('utf-8', 'replace').replace('\0', ' ').strip()
        except OSError:
            return ''
    c = read(f'/proc/{pid}/cmdline')
    try:
        tasks = os.listdir(f'/proc/{pid}/task') if not c else []
    except OSError:
        tasks = []
    for t in sorted(tasks):
        c = c or read(f'/proc/{pid}/task/{t}/cmdline')
    return c


def test_game(cmd):
    return '-sporekit-profile:' in cmd or '-sporekit-instance:' in cmd


def of_inst(cmd):
    if not inst:
        return True
    toks = cmd.split()
    if inst == '1':
        return not any(t.startswith('-sporekit-instance:') for t in toks)
    return f'-sporekit-instance:{inst}' in toks


for d in sorted(os.listdir('/proc')):
    if not d.isdigit() or int(d) in skip:
        continue
    pid = int(d)
    cmd = argv(pid)
    if not cmd:
        continue
    try:
        comm = open(f'/proc/{pid}/comm').read().strip()
    except OSError:
        continue
    ours = lane != '1' or test_game(cmd)
    if game_rx.match(cmd):
        if not ours:
            print('owner', pid)
        elif of_inst(cmd):
            print('game', pid)
    elif inst:
        continue
    elif comm == 'bwrap' and (bin_exe in cmd or (lane != '1' and f' -b {bottle} ' in cmd + ' ')):
        if ours:
            print('tree', pid)
    elif bin_exe in cmd and ('bottles-cli run' in cmd or 'start.exe' in cmd) and ours:
        print('wrapper', pid)
PY
}

kill_tree() {
    local c
    for c in $(pgrep -P "$1"); do kill_tree "$c"; done
    kill -9 "$1" 2>/dev/null || true
}

cmd_kill() {
    local procs owner
    procs=$(lane_procs)
    for p in $(awk '$1 == "game" {print $2}' <<< "$procs"); do kill "$p" 2>/dev/null || true; done
    sleep 1
    # a deadlocked Wine process can stay half-dead (zombie leader + live threads) and keep its
    # UDP/TCP sockets bound: SIGKILL the whole thread group (lane_procs finds it by a live thread)
    procs=$(lane_procs)
    for p in $(awk '$1 == "game" {print $2}' <<< "$procs"); do kill -9 "$p" 2>/dev/null || true; done
    owner=$(awk '$1 == "owner" {printf "%s ", $2}' <<< "$procs")
    # launch sandboxes keep a wine services tree alive after the game exits: kill the whole tree.
    # The wineserver of a bottle may sit in any sandbox of it: while the owner's own Spore runs in
    # the bottle of lane 1, leave the trees alone (a killed wineserver ends every game of the bottle)
    if [[ -n "$owner" ]]; then
        echo "the owner's own Spore runs on lane 1 (pid $owner): left alone, and the launch sandboxes too" >&2
    else
        for p in $(awk '$1 == "tree" {print $2}' <<< "$procs"); do kill_tree "$p"; done
    fi
    for p in $(awk '$1 == "wrapper" {print $2}' <<< "$procs"); do kill "$p" 2>/dev/null || true; done
    echo "killed"
}

# the PIDs of this lane's games (of instance $1 when given): what kill would end (no owner's game)
cmd_pids() {
    lane_procs "${1:-}" | awk '$1 == "game" {print $2}'
}

cmd_status() {
    pgrep -af "$GAME_RE" || echo "not running"
}

cmd_logs() {
    local inst="${1:-1}"
    local f="$MODS/logs/sporekit.log"
    [[ "$inst" != "1" ]] && f="$MODS/logs/sporekit.$inst.log"
    tail -n "${LINES:-60}" "$f"
}

cmd_fixture() {
    local op="${1:-}" name="${2:-creature}" profile="${3:-test}"
    local prefix="$APPDATA_DIR/SporeKit-$profile/Spore"
    [[ "$profile" == "-" ]] && prefix="$APPDATA_DIR/Spore"
    # "-" is the player's own profile: a fixture may be saved FROM it, never written into it
    # (restore and slot delete saves first, BUG-0102)
    if [[ "$profile" == "-" && "$op" != "save" ]]; then
        echo "fixture $op: never into the player's own profile (\"-\"), only into a test profile" >&2; exit 1
    fi
    [[ "$profile" == "-" || "$profile" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "fixture: bad profile name '$profile'" >&2; exit 1; }
    if [[ "$op" == restore || "$op" == slot ]] && [[ ! -f "$FIXTURES/$name.tgz" ]]; then
        echo "fixture $op: no fixture $FIXTURES/$name.tgz" >&2; exit 1
    fi
    mkdir -p "$FIXTURES" "$prefix"
    case "$op" in
        save) tar -C "$prefix" -czf "$FIXTURES/$name.tgz" Games && echo "saved fixture $name" ;;
        restore) rm -rf "$prefix/Games" && tar -C "$prefix" -xzf "$FIXTURES/$name.tgz" && echo "restored fixture $name" ;;
        slot)
            # warm reset: the fixture's Game0 goes into a new slot Game<slot> next to the loaded one
            # (files of the running save must not change under the game)
            local slot="${4:?slot number}" tmp
            tmp=$(mktemp -d "$prefix/.fixture.XXXXXX")
            tar -C "$tmp" -xzf "$FIXTURES/$name.tgz"
            mkdir -p "$prefix/Games"
            rm -rf "$prefix/Games/Game$slot"
            mv "$tmp/Games/Game0" "$prefix/Games/Game$slot"
            rm -rf "$tmp"
            echo "fixture $name in Game$slot" ;;
        *) echo "usage: spore.sh fixture save [name] [profile|-] | restore [name] [profile] | slot name profile N"; exit 1 ;;
    esac
}

# a lane = reflink copies (Btrfs: no extra space until files change) of the game folder and of
# the bottle, with the game paths in the bottle registry pointed to the copy
cmd_lane() {
    local op="${1:-}" n="${2:-}"
    [[ "$n" =~ ^[0-9]+$ && "$n" -ge 2 ]] || { echo "usage: spore.sh lane create|remove|info N (N >= 2)"; exit 1; }
    local dir="$LAB/lane$n" bottle="$BOTTLES_DIR/SporeLab$n"
    case "$op" in
        create)
            if [[ ! -f "$dir/SporebinEP1/SporeApp.exe" ]]; then
                mkdir -p "$dir"
                local e
                for e in "$MAIN_GAME"/*; do
                    case "$(basename "$e")" in SporeCoop) continue ;; esac
                    cp -a --reflink=always "$e" "$dir/"
                done
                rm -rf "$dir/SporebinEP1/mods/logs"
            fi
            if [[ ! -f "$bottle/bottle.yml" ]]; then
                cp -a --reflink=always "$BOTTLES_DIR/Games" "$bottle"
                sed -i "s/^Name: .*/Name: SporeLab$n/; s/^Path: .*/Path: SporeLab$n/" "$bottle/bottle.yml"
                # the game paths in the registry: Z:\home\...\SPORE\ -> the lane copy
                local from to
                from=$(printf '%s' "Z:$MAIN_GAME" | sed 's|/|\\\\|g')
                to=$(printf '%s' "Z:$dir" | sed 's|/|\\\\|g')
                sed -i "s|$(printf '%s' "$from" | sed 's|\\|\\\\|g')\\\\|$(printf '%s' "$to" | sed 's|\\|\\\\|g')\\\\|g" \
                    "$bottle/system.reg" "$bottle/user.reg"
                # test profiles only: the player's own Spore data is not copied
                rm -rf "$bottle/drive_c/users/$USER/AppData/Roaming/Spore/Games"
            fi
            echo "lane $n: game $dir, bottle SporeLab$n, instances $((2 * n - 1))/$((2 * n))"
            ;;
        remove)
            SPORE_LANE=$n "$0" kill >/dev/null || true
            rm -rf "$dir" "$bottle"
            echo "lane $n removed"
            ;;
        info)
            echo "lane $n: game $dir ($( [[ -d $dir ]] && echo ok || echo missing)), bottle SporeLab$n ($( [[ -d $bottle ]] && echo ok || echo missing))"
            ;;
        *) echo "usage: spore.sh lane create|remove|info N"; exit 1 ;;
    esac
}

case "${1:-}" in
    lane) shift; cmd_lane "$@" ;;
    install) cmd_install ;;
    uninstall) cmd_uninstall ;;
    deploy) cmd_deploy ;;
    run) shift; cmd_run "$@" ;;
    kill) cmd_kill ;;
    pids) shift; cmd_pids "${1:-}" ;;
    status) cmd_status ;;
    logs) shift; cmd_logs "${1:-1}" ;;
    fixture) shift; cmd_fixture "$@" ;;
    *) echo "usage: [SPORE_LANE=N] spore.sh lane create|remove N|install|uninstall|deploy|run [-i inst] [-p profile] [-x game-arg]|kill|pids [inst]|status|logs [inst]|fixture save|restore [name]"; exit 1 ;;
esac
