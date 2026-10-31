#!/usr/bin/env python3
"""Tribe: the players' chiefs do not stand on each other (BUG-0340).

Players reported their characters in Tribe (each player's chief) appearing at the same spot,
standing in each other, and sometimes stuck that way: the members a tribe gets (its hut, the co-op
split of new members, a rejoin) appear at one spot.

1. Two games on the tribe fixture; every player has a chief (`coop.tribe`: chiefs=).
2. `coop.tribe stack` on the host (test command): every chief is put onto the host's chief, as the
   members the game spawns appear at one spot.
3. 6 s later, on each game: the chiefs' own positions there (each machine's `coop.tribe`): no two
   chiefs within 1.5 m.
4. They can move: each player orders its own chief (select + walk, the guest's order goes to the
   host) 8 m away; after 8 s each chief has moved at least 3 m on both games.
PASS: 3 and 4. --nofix (`coop.fix tribe.spread_chiefs off` on the host): must FAIL at 3.

  SPORE_LANE=8 python3 tools/tests/coop_tribe_chiefs_apart.py [--nofix]
"""
import argparse
import math
import re
import time

from twoinst import Checks, start


def chiefs(con):
    """owner -> (member id, position) on this game"""
    out = con.call('coop.tribe')['out']
    m = re.search(r'chiefs=([^\n]*?)(?: spread=\d+.*)?\n', out)
    ids = dict((int(a), int(b)) for a, b in re.findall(r'(\d+):#(\d+)', m.group(1))) if m else {}
    pos = {int(x.group(1)): tuple(map(float, x.group(2, 3, 4)))
           for x in re.finditer(r'#(\d+) \S+ owner=-?\d+ .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)}
    return {o: (i, pos.get(i)) for o, i in ids.items() if pos.get(i)}


def closest(ch):
    ps = [p for _, p in ch.values()]
    return min((math.dist(a, b) for i, a in enumerate(ps) for b in ps[i + 1:]), default=99.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.fix tribe.spread_chiefs off')['out'].strip(), flush=True)
    time.sleep(5)
    hc = chiefs(host)
    ck(len(hc) >= 2, f'every player has a chief on the host ({hc})')
    if len(hc) < 2:
        return ck.result()
    print('  host:', host.call('coop.tribe stack')['out'].strip(), flush=True)
    time.sleep(0.5)
    print(f'  right after: closest chiefs host {closest(chiefs(host)):.2f} m', flush=True)
    time.sleep(6)
    for name, con in (('host', host), ('guest', guest)):
        ch = chiefs(con)
        d = closest(ch)
        print(f'  {name}: chiefs {ch}, closest {d:.2f} m', flush=True)
        ck(len(ch) >= 2 and d >= 1.5, f'{name}: no two chiefs within 1.5 m ({d:.2f} m)')
    # each player walks its own chief 8 m away
    before = {name: chiefs(con) for name, con in (('host', host), ('guest', guest))}
    for me, con in ((0, host), (1, guest)):
        ch = before['host'].get(me)
        if not ch:
            continue
        mid, p = ch
        con.call(f'coop.tribe select {mid} 1')
        r = math.dist(p, (0, 0, 0))
        t = (p[0] + 8, p[1], p[2])
        k = r / math.dist(t, (0, 0, 0))
        con.call('coop.tribe walk %.1f %.1f %.1f' % tuple(c * k for c in t))
    time.sleep(8)
    for name, con in (('host', host), ('guest', guest)):
        after = chiefs(con)
        for o, (mid, p) in before[name].items():
            if o in (0, 1) and o in after:
                moved = math.dist(p, after[o][1])
                ck(moved >= 3, f'{name}: player {o}\'s chief walked ({moved:.1f} m)')
    print('  host:', re.search(r'tribe co-op: .*', host.call('coop.tribe')['out']).group(0)[-60:], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
