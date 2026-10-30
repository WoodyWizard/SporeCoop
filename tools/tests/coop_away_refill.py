#!/usr/bin/env python3
"""The host leaves to the main menu: the guest's planet keeps its animals (BUG-0180).

While the host plays, a guest shows the host's NPCs as proxies and its own game's NPCs are removed
(the update filter). When the host leaves to the main menu the guest plays on alone: the proxies go
and its own game runs the planet again. Before the fix the nests brought their members back one at
a time: 0-1 own NPCs near the guest 30 s later (coop_exit_menu.py, docs/known-issues.md).

1. Two games on the creature fixture; the guest counts the host's NPCs it shows within 70 m of its
   avatar (census: proxies, alive).
2. The host leaves through its pause menu ("Exit to Main Menu", "Don't Save"), as a player does.
3. Every second for 30 s: the guest's own NPCs within 70 m (census: `local`, alive, AI on: the
   game's creatures, not ours). PASS: 10 s after the guest learned the host is away it has at least
   60 % of the count it showed before (and at least 2).

  coop_away_refill.py   (SPORE_FIX_OFF=npc.away_locals must FAIL)"""
import math
import time

import census
from twoinst import Checks, start
from uifind import press, shown

NEAR = 70.0


def near(rows, own):
    me = next((r['pos'] for r in rows if r['own'] == 'avatar'), None)
    if me is None:
        return None
    return sum(1 for r in rows if r['kind'] == 'c' and r['own'] == own and r['alive'] > 0
               and math.dist(r['pos'], me) < NEAR and (own != 'local' or r['ai'] == 1))


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    before = [near(census.dump(guest).rows, 'proxy') for _ in range(5) if not time.sleep(0.5)]
    n0 = sorted(x for x in before if x is not None)[len(before) // 2]
    print(f'  the guest shows {n0} NPCs of the host within {NEAR:.0f} m ({before})', flush=True)
    for _ in range(3):
        host.call('key esc')
        time.sleep(1.5)
        if shown(host, 'Resume Game'):
            break
    ok = press(host, 'Exit to Main Menu')
    for _ in range(2):
        time.sleep(1.5)
        if shown(host, "Don't Save"):
            press(host, "Don't Save")
    ck(ok, 'the host leaves through the pause menu ("Exit to Main Menu")')
    t0 = time.time()
    away = None
    while time.time() - t0 < 60:
        if 'host_away=1' in guest.call('coop.status')['out']:
            away = time.time()
            break
        time.sleep(0.5)
    ck(away is not None, 'the guest knows the host is away')
    if away is None:
        return ck.result()
    counts = []
    while time.time() - away < 30:
        counts.append((round(time.time() - away), near(census.dump(guest).rows, 'local')))
        time.sleep(1)
    print(f'  the guest\'s own NPCs within {NEAR:.0f} m (s after the host left: count): {counts}', flush=True)
    print(' ', guest.call('coop.npc')['out'].strip().splitlines()[0], flush=True)
    at10 = [n for t, n in counts if 8 <= t <= 12 and n is not None]
    need = max(2, math.ceil(0.6 * n0))
    ck(n0 >= 3, f'the guest showed NPCs of the host around it ({n0})')
    ck(at10 and max(at10) >= need, f'10 s after the host left the guest\'s game has its own NPCs around it '
       f'({at10}, need {need} of {n0})')
    ck(guest.call('coop.avatar')['ok'], 'the guest plays on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
