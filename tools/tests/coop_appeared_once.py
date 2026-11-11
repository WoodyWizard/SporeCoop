#!/usr/bin/env python3
"""The host is told once that a guest appeared in its world (BUG-0923).

Key-frame review of the 0.7.12 final check: the host's notification list showed "Bob appeared in the world" two
times, one under the other, at every join. The guest's creation record reaches the host 0.3-0.4 s after its first
state: the puppet made from the first state is taken away and built again with the guest's look, and each making
told the player.

1. Two games on the creature fixture; the guest leaves; the host's notification panels run out.
2. The guest joins again. From the join command on, the host's game is asked every 0.2 s for the panels it shows
   (`coop.status`: the `toast` lines, the game's own windows) for 15 s.
PASS: at no moment the host shows more than one panel "<guest> appeared in the world", its log has that line once
since the join, and the puppet was in fact rebuilt in that time (the case under test happened). The guest's side is
printed.

  SPORE_LANE=7 python3 tools/tests/coop_appeared_once.py   (SPORE_FIX_OFF=notify.appeared_once must FAIL)
"""
import os
import re
import time

import lane
import twoinst
from twoinst import Checks, start

TEXT = 'appeared in the world'


def toasts(con, who):
    out = con.call('coop.status')['out']
    return [m for m in re.findall(r"^toast \S+ \([^)]*\) '([^']*)'", out, re.M) if m == f'{who} {TEXT}']


def counters(con):
    m = re.search(r'appeared notices: told (\d+), puppet rebuilds not told (\d+)', con.call('coop.status')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def log_lines(inst, offset, who):
    with open(lane.game_log(inst), 'rb') as f:
        f.seek(offset)
        text = f.read().decode('utf-8', 'replace')
    return [ln for ln in text.splitlines() if f'coop: {who} {TEXT}' in ln]


def main():
    host, guest = start('creature')
    ck = Checks()
    guest.call('coop.leave')
    t0 = time.time()
    while time.time() - t0 < 20 and (toasts(host, 'Bob') or time.time() - t0 < 8):
        time.sleep(0.5)
    ck(not toasts(host, 'Bob'), 'the host shows no "Bob appeared" panel before the join')
    told0, quiet0 = counters(host)
    off_h = os.path.getsize(lane.game_log(lane.HOST))
    off_g = os.path.getsize(lane.game_log(lane.GUEST))
    guest.call(lane.JOIN)
    most, most_g, first = 0, 0, None
    t0 = time.time()
    while time.time() - t0 < 60:
        n = len(toasts(host, 'Bob'))
        most = max(most, n)
        most_g = max(most_g, len(toasts(guest, 'Alice')))
        if n and first is None:
            first = time.time()
            print(f'  {first - t0:.1f} s after the join command the host shows "Bob {TEXT}"', flush=True)
        if first is not None and time.time() - first > 15:
            break
        time.sleep(0.2)
    ck(first is not None, 'the host is told that the guest appeared in the world')
    ck(twoinst.wait_guest_in(host, guest, 60), 'the guest is in the host\'s world again')
    told1, quiet1 = counters(host)
    lines = log_lines(lane.HOST, off_h, 'Bob')
    for ln in lines:
        print('  host log:', ln.strip()[:120], flush=True)
    print(f'  host: panels "Bob {TEXT}" shown at once: {most}; log lines since the join: {len(lines)}; '
          f'notices told {told0} -> {told1}, puppet rebuilds not told {quiet0} -> {quiet1}', flush=True)
    print(f'  guest: panels "Alice {TEXT}" shown at once: {most_g}; log lines since the join: '
          f'{len(log_lines(lane.GUEST, off_g, "Alice"))}', flush=True)
    ck(most == 1, f'the host never shows two panels "Bob {TEXT}" at once (most {most})')
    ck(len(lines) == 1, f'the host\'s log has the line once since the join ({len(lines)})')
    rebuilt = told1 is not None and (told1 - told0) + (quiet1 - quiet0) >= 2
    ck(rebuilt, 'the guest\'s puppet was rebuilt with its look after the join (the case under test: '
       f'{(told1 or 0) - (told0 or 0)} told + {(quiet1 or 0) - (quiet0 or 0)} not told)')
    twoinst.keyframe('joined', 'the host\'s notification list shows "Bob appeared in the world" once, not twice')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
