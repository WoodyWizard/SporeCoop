#!/usr/bin/env python3
"""BUG-0653: one long frame (a hitch over 2 s) is not taken for "Present lost".

Lab log sweep (32 runs: team, chat_pause, select, frames_loop, ...): right after a 2.5 s hitch at the game saved
box, `frames: Present has not reached SporeKit for 2 s (another overlay took it over?); frames come from the game
loop ... on-screen labels are off`, and 1 ms later `Present reaches SporeKit again`. The fallback (sporekit
d3d9hook.cpp frame_from_game_loop) looked only at the time since the last Present: the first game loop pass after
one long frame came 2.5 s after that frame's Present. An overlay that took Present over leaves many loop passes
without a Present; one long frame leaves one.

1. Creature fixture (two games). The host's frame source is Present.
2. Three times: `kit.test.hang 2600` (the main thread sleeps 2.6 s in its next frame, as in the saved box hitch),
   then frames go on.
PASS: the host's SporeKit log has no "Present has not reached SporeKit" line since the start of the test and its
frame source is still Present. `SPORE_FIX_OFF=kit.presentlost`: the time alone, as before: FAIL.
--loop (a check that the fallback still works): `frames.source loop` (Present ignored, as with an overlay that took
it over) gives frames from the game loop, `frames.source auto` gives Present back.

  SPORE_LANE=4 python3 tools/tests/kit_present_hitch.py
"""
import os
import re
import time

import lane
from twoinst import Checks, start

LOGDIR = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')
LOST = 'Present has not reached SporeKit'


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def log_size():
    try:
        return os.path.getsize(logname())
    except OSError:
        return 0


def log_since(off):
    with open(logname(), 'rb') as f:
        f.seek(off)
        return f.read().decode('utf-8', 'replace')


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame')['out'])
    return int(m.group(1)) if m else -1


def source(con):
    return con.call('frames.source')['out'].strip()


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    print('  host: ' + source(host), flush=True)
    ck('Present' in source(host) and 'loop' not in source(host), 'the host\'s frames come from Present before')
    off = log_size()
    for i in range(3):
        f0 = frame(host)
        print('  host: ' + host.call('kit.test.hang 2600')['out'].strip(), flush=True)
        time.sleep(4)
        host.call('wait.frames 30 20000')
        f1 = frame(host)
        print(f'  hitch {i + 1}: frames {f0} -> {f1}, {source(host)}', flush=True)
        ck(f1 > f0, f'hitch {i + 1}: the host\'s frames go on')
    text = log_since(off)
    lost = [l for l in text.splitlines() if LOST in l]
    for l in lost[:3]:
        print('  log: ' + l.strip(), flush=True)
    hitches = [l for l in text.splitlines() if 'hitch' in l and 'perf' in l]
    print(f'  {len(hitches)} hitch lines, {len(lost)} "{LOST}" lines in the host\'s SporeKit log', flush=True)
    ck(not lost, f'no "{LOST}" after a 2.6 s frame ({len(lost)} lines)')
    ck('Present' in source(host) and 'loop' not in source(host), 'the host\'s frames still come from Present')
    ck(frame(guest) > 0, 'the guest\'s game answers')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
