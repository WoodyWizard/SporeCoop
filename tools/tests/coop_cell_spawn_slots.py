#!/usr/bin/env python3
"""Cell, three players: the guests are placed on their own spots, not on one (BUG-0792).

A guest's cell is put next to the others after a knockout nobody came to (the respawn) and after a load
of the host's world (join_after_load). Every guest got the same offset from the nearest partner ((2, 1)
and (2.5, 1)): two guests landed on one spot, and after a load the nearest partner of a guest at the
pool's start could be the other guest at the same start, so it was not moved at all.

Alice hosts, Bob and Carol are guests (tools/tests/party.py, one lane).
1. Respawn: Bob and Carol swim 60 m from Alice, side by side, and are knocked out (the host's
   `coop.pool hurt`): nobody can help them, both respawn after the 10 s. Then, in each guest's own
   game, its own cell and the other guest's cell (as that game draws it) are more than 4 m apart,
   and both are within 15 m of Alice's cell.
2. Load: the host saves and shares its world (`coop.save`, `coop.share`), both guests load it; 25 s
   later the same in each guest's game.
SPORE_FIX_OFF=cell.spawn_slots must FAIL (the two guests within 1-2 m of each other).

  SPORE_LANE=3 python3 tools/tests/coop_cell_spawn_slots.py [--skip load]
"""
import argparse
import math
import re
import time

from party import Party
from twoinst import Checks

APART = 4.0


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def own(p):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', p.call('coop.pool'), re.M)
    return (float(m.group(1)), float(m.group(2))) if m else None


def drawn(p, pid):
    """where this game draws player pid's cell (coop.pool scalepos p<id>)"""
    m = re.search(rf'\bp{pid}=\((-?[\d.]+) (-?[\d.]+)\)', p.call('coop.pool scalepos'))
    return (float(m.group(1)), float(m.group(2))) if m else None


def check_apart(ck, label, alice, guests, ids):
    """each guest's own game: its cell vs the other guest's and the host's as it draws them"""
    for g, other in ((guests[0], guests[1]), (guests[1], guests[0])):
        me, o, h = own(g), drawn(g, ids[other.name]), drawn(g, 0)
        d = math.dist(me, o) if me and o else None
        dh = math.dist(me, h) if me and h else None
        print(f'  [{label}] {g.name}: own {me}, {other.name} {o} ({d} m), Alice {h} ({dh} m) | '
              + ' '.join(x for x in g.call('coop.pool').splitlines() if x.startswith('cell_c1')), flush=True)
        ck(d is not None and d > APART, f'[{label}] in {g.name}\'s game its cell and {other.name}\'s are apart ({d} m > {APART})')
        ck(dh is not None and dh < 15, f'[{label}] in {g.name}\'s game its cell is next to Alice\'s ({dh} m)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--skip', default='')
    a = ap.parse_args()
    ck = Checks()
    p = Party.start('cell', 3)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        alice, bob, carol = p.host, p.guests[0], p.guests[1]
        ids = p.ids()
        bid, cid = ids['Bob'], ids['Carol']
        ck(wait(lambda: (num(carol.call('coop.pool'), 'mirrors') or 0) > 5, 30), 'Carol mirrors the host pool')
        # 1. both knocked out far from Alice: both respawn next to her
        bob.call('coop.pool tp 60 0 0')
        carol.call('coop.pool tp 60 3 0')
        time.sleep(2)
        alice.call(f'coop.pool hurt {bid} 50')
        alice.call(f'coop.pool hurt {cid} 50')
        ck(wait(lambda: num(bob.call('coop.pool'), 'downed') == 1 and num(carol.call('coop.pool'), 'downed') == 1, 6),
           '[respawn] both guests are knocked out')
        up = wait(lambda: num(bob.call('coop.pool'), 'downed') == 0 and num(carol.call('coop.pool'), 'downed') == 0, 16)
        ck(up, '[respawn] both respawn after the knockout time')
        time.sleep(1.5)
        check_apart(ck, 'respawn', alice, [bob, carol], ids)
        if 'load' not in a.skip:
            # 2. a load of the host's world
            time.sleep(3)
            loads0 = [g.call('log 400').count('world: loading ') for g in (bob, carol)]
            out = alice.call('coop.save')
            ck('saved' in out, f'[load] the host saved ({out.strip()[:80]})')
            alice.call('coop.share')
            ck(wait(lambda: all(g.call('log 400').count('world: loading ') > n for g, n in zip((bob, carol), loads0)), 150, 1),
               '[load] both guests loaded the host\'s world')
            ck(p.wait_all(200), '[load] both guests are back in the host\'s world')
            time.sleep(25)
            check_apart(ck, 'load', alice, [bob, carol], ids)
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
