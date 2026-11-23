#!/usr/bin/env python3
"""A stop while the game window is minimized or unfocused is logged as the game's own pause, not a hitch (BUG-1529,
the tearing report).

Spore stops drawing and pauses itself while its window is minimized or not in the foreground. Every such stop was
logged as `perf: hitch N ms` and counted in the minute's min fps and longest frame like a freeze (the player's report:
"freezes up to 28 s" that were Alt+Tab / Explorer over a full-screen game, out/research/dc-report-tearing-20261116.md).
Now a poll thread notes the window's state every 100 ms and such a gap is a `perf: pause`, kept out of min fps and the
longest frame; the keepalive's BUG-0687 line names the window state.

The lab plays in background mode (the game counts as in the foreground), so the test tells the poll that the window is
unfocused (`coop.diag pausemark 1`) while the main thread stops for 3 s (`coop.stalltest 3`, a message loop of its own:
no frames, the keepalive runs):
  PASS: the host's log has `perf: pause` of about 3000 ms and no `perf: hitch` over 2 s for it, the keepalive line
  names "foreground 0"; `coop.diag` shows the longest frame of the minute under 2 s. Then without the mark a 3 s stop is
  a hitch as before.
  SPORE_FIX_OFF=diag.hitch_pause: the unfocused stop is a `perf: hitch 3000 ms`: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_perf_pause.py
"""
import os
import re
import time

import lane
from twoinst import Checks, start

LOGS = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')


def log_path(inst):
    return os.path.join(LOGS, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def log_size(inst):
    try:
        return os.path.getsize(log_path(inst))
    except OSError:
        return 0


def log_from(inst, offset):
    try:
        with open(log_path(inst), errors='replace') as f:
            f.seek(offset)
            return f.read()
    except OSError:
        return ''


def stop(host, secs):
    h0 = log_size(lane.HOST)
    print('  host:', host.call(f'coop.stalltest {secs}')['out'].strip(), flush=True)
    time.sleep(2)
    return log_from(lane.HOST, h0)


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(3)
        host.call('coop.diag detail 1')
        print(' ', host.call('coop.diag pausemark 1')['out'].strip(), flush=True)
        time.sleep(0.5)
        log = stop(host, 3)
        host.call('coop.diag pausemark 0')
        pauses = [int(x) for x in re.findall(r'perf: pause (\d+) ms', log)]
        hitches = [int(x) for x in re.findall(r'perf: hitch (\d+) ms', log)]
        keep = [ln for ln in log.splitlines() if 'keepalive: no frame' in ln]
        print(f'  unfocused stop: pauses {pauses}, hitches {hitches}; {keep[:1]}', flush=True)
        ck(any(p >= 2500 for p in pauses), f'the 3 s stop while unfocused is logged as the game\'s own pause ({pauses})')
        ck(not any(h >= 2000 for h in hitches), f'and not as a hitch over 2 s ({hitches})')
        ck(any('foreground 0' in ln for ln in keep), 'the keepalive line names the window state (foreground 0)')
        st = host.call('coop.diag')['out']
        m = re.search(r'longest frame (\d+) ms, hitches (\d+), pauses (\d+)', st)
        print('  host', m.group(0) if m else st.splitlines()[:1], flush=True)
        ck(m and int(m.group(1)) < 2000 and int(m.group(3)) >= 1,
           'the minute\'s longest frame leaves the pause out, the pause is counted')
        log = stop(host, 3)
        hitches = [int(x) for x in re.findall(r'perf: hitch (\d+) ms', log)]
        print(f'  focused stop: hitches {hitches}', flush=True)
        ck(any(h >= 2500 for h in hitches), f'a 3 s stop with the window in front is a hitch as before ({hitches})')
    finally:
        host.call('coop.diag pausemark 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
