#!/usr/bin/env python3
"""The host is told once that a guest appeared in its world (BUG-0923).

Key-frame review of the 0.7.12 final check: the host's notification list showed "Bob appeared in the world" two
times, one under the other, at every first join (and a third one after the guest changed its creature). The guest's
creation record reaches the host 0.3-0.4 s after its first state: the puppet made from the first state is taken
away and built again with the guest's look, and each making told the player.

1. Two games on the creature fixture (a cold start: the guest's first join of the host's session). The host's log
   since its game started: the lines "Bob appeared in the world" within 3 s of the first one (the join).
2. The panels of the join run out. The guest takes another creature of its own (`coop.creations next creature`):
   its new record reaches the host, the puppet is rebuilt. The host's game is asked every 0.2 s for the panels it
   shows (`coop.status`: the `toast` lines, the game's own windows) for 12 s.
PASS: one line at the join; no panel "Bob appeared in the world" after the creature change although the puppet was
rebuilt (the host's species of the puppet changed) and is shown.

  SPORE_LANE=8 python3 tools/tests/coop_appeared_once.py   (SPORE_FIX_OFF=notify.appeared_once must FAIL)
"""
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
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def log_times(inst, who):
    """[seconds of the day] of the game's log lines '<who> appeared in the world'"""
    with open(lane.game_log(inst), 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    out = []
    for ln in text.splitlines():
        m = re.match(r'(\d+):(\d+):(\d+)\.(\d+) .*coop: ' + re.escape(f'{who} {TEXT}'), ln)
        if m:
            out.append(int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000.0)
    return out


def puppet_species(con):
    m = re.search(r"remote 1 '[^']*' puppet=(\S+) state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{8})",
                  con.call('coop.status')['out'])
    return (m.group(1), m.group(2)) if m else (None, None)


def main():
    host, guest = start('creature')
    ck = Checks()
    # 1. the join (start() did it): the host's log
    times = log_times(lane.HOST, 'Bob')
    join = [t for t in times if times and t - times[0] <= 3.0]
    print(f'  host log: "Bob {TEXT}" at ' + ', '.join(f'+{t - times[0]:.3f} s' for t in times), flush=True)
    print(f'  host panels "Bob {TEXT}" right after the start: {len(toasts(host, "Bob"))}; guest log lines '
          f'"Alice {TEXT}": {len(log_times(lane.GUEST, "Alice"))}', flush=True)
    ck(len(join) == 1, f'the host is told once at the join that the guest appeared ({len(join)} lines within 3 s)')
    # 2. the guest's new look: the puppet is rebuilt, no new panel
    t0 = time.time()
    while time.time() - t0 < 20 and (toasts(host, 'Bob') or time.time() - t0 < 3):
        time.sleep(0.5)
    ck(not toasts(host, 'Bob'), 'the panels of the join ran out')
    told0, quiet0 = counters(host)
    shown0, sp0 = puppet_species(host)
    print('  guest:', guest.call('coop.creations next creature')['out'].strip()[:120], flush=True)
    most, changed = 0, None
    t0 = time.time()
    while time.time() - t0 < 30:
        most = max(most, len(toasts(host, 'Bob')))
        shown1, sp1 = puppet_species(host)
        if changed is None and sp1 and sp1 != sp0:
            changed = time.time()
            print(f'  {changed - t0:.1f} s: the host draws the guest as {sp1} (was {sp0})', flush=True)
        if changed is not None and time.time() - changed > 12:
            break
        time.sleep(0.2)
    told1, quiet1 = counters(host)
    shown1, sp1 = puppet_species(host)
    print(f'  host: panels "Bob {TEXT}" after the creature change: {most}; notices told {told0} -> {told1}, puppet '
          f'rebuilds not told {quiet0} -> {quiet1}; puppet {shown0} {sp0} -> {shown1} {sp1}', flush=True)
    ck(changed is not None, f'the host\'s game rebuilt the guest\'s puppet with its new creature ({sp0} -> {sp1})')
    ck(sp1 is not None and shown1 not in (None, '00000000'), f'the guest is shown in the host\'s world (puppet {shown1})')
    ck(most == 0, f'the host shows no new panel "Bob {TEXT}" for the rebuilt puppet (most {most} at once)')
    ck(len(log_times(lane.HOST, 'Bob')) == len(times), 'the host\'s log has no new line of it')
    twoinst.keyframe('relook', 'the host\'s notification list has no second "Bob appeared in the world" after the '
                               'guest changed its creature; the guest is shown with its new creature')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
