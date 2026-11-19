#!/usr/bin/env python3
"""Lab self-check (BUG-1327, docs/06-testing.md#testing-lane-pid-namespace): a guest's game killed and started again
on a lane, several times, never touches the host's game.

Every `spore.sh run` is a flatpak sandbox (bwrap) with a PID namespace of its own, and the lane's wineserver lives in
the namespace of the launch that started it (the guest's first game: the tests start the guest first). The wineserver
keeps each game's own pids, numbers of other namespaces; the second exit of a guest restarted in a sandbox of its own
made the wineserver send SIGKILL to that number in its own namespace, its own pid (kill(47, 9), gdb on lane 3,
2026-11-19): the lane's wineserver was gone, the host's game ran on without it (log still, console refused).
spore.sh run now starts a game of a lane whose wineserver runs inside that wineserver's PID namespace.

  1. the guest, then the host start (as twoinst.start); every game of the lane is in the PID namespace of its
     wineserver.
  2. three times: the guest's game is SIGKILLed (coop_creature_desync.kill_game, as restart_guest): the host's console
     answers, the lane's wineserver is the one it was; the guest starts again: in the wineserver's namespace, the
     host's console still answers.
SPORE_SHARE_PIDS=0 (suite lane_guest_restart_off, the twin): a sandbox of its own per launch, as before the fix: must
FAIL (the games are not in the wineserver's namespace; after the second kill the host's console refuses, the
wineserver is gone).

  SPORE_LANE=3 python3 tools/tests/lane_guest_restart.py
"""
import os
import subprocess
import time

import lane
from coop_creature_desync import kill_game
from twoinst import Checks, Console, lane_down, wait_up

CYCLES = 3


def sh_out(*args):
    return subprocess.run([lane.SPORE, *args], capture_output=True, text=True, timeout=30).stdout.split()


def wineserver():
    p = sh_out('wineserver')
    return int(p[0]) if p else 0


def pidns(pid):
    try:
        return os.readlink(f'/proc/{pid}/ns/pid')
    except OSError:
        return '?'


def game_ns(inst):
    pids = sh_out('pids', str(inst))
    return pidns(pids[0]) if pids else '-'


def answers(inst):
    try:
        return bool(Console(inst, timeout=5).call('ping')['ok'])
    except OSError:
        return False


def start_guest(last):
    time.sleep(max(0.0, 21 - (time.time() - last)))  # spore.sh run refuses a start under 20 s after the last (BUG-1075)
    lane.run_guest()
    return time.time()


def main():
    ck = Checks()
    print('  share pids:', os.environ.get('SPORE_SHARE_PIDS', '1'), flush=True)
    lane.sh('kill')
    lane_down(10)
    t_guest = start_guest(0)
    if not ck(wait_up(lane.GUEST, 150), 'the guest\'s game started'):
        return ck.result()
    lane.run_host()
    if not ck(wait_up(lane.HOST, 150), 'the host\'s game started'):
        return ck.result()
    ws = wineserver()
    ns = pidns(ws) if ws else '?'
    print(f'  wineserver {ws} in {ns}; host game in {game_ns(lane.HOST)}, guest game in {game_ns(lane.GUEST)}',
          flush=True)
    ck(ws and game_ns(lane.HOST) == ns and game_ns(lane.GUEST) == ns,
       'both games are in the PID namespace of the lane\'s wineserver')
    for c in range(1, CYCLES + 1):
        kill_game(lane.GUEST)
        time.sleep(10)  # the lost wineserver went within a few seconds of the kill (lane 3, 2026-11-19)
        now = wineserver()
        print(f'  kill {c}: wineserver {now}, host console {answers(lane.HOST)}', flush=True)
        ck(answers(lane.HOST), f'kill {c}: the host\'s console answers after the guest\'s game was killed')
        ck(now == ws, f'kill {c}: the lane\'s wineserver is the one it was ({now}, was {ws})')
        t_guest = start_guest(t_guest)
        up = wait_up(lane.GUEST, 150)
        ck(up, f'restart {c}: the guest\'s game started again')
        print(f'  restart {c}: guest game in {game_ns(lane.GUEST)}, wineserver {wineserver()}', flush=True)
        ck(game_ns(lane.GUEST) == ns, f'restart {c}: the guest\'s game is in the wineserver\'s PID namespace')
        ck(answers(lane.HOST), f'restart {c}: the host\'s console answers')
        if not up:
            break
    lane.sh('kill', check=False)
    lane_down(10)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
