#!/usr/bin/env python3
"""Tribe: a guest's gather order on a fruit the host's game does not have there still gathers (BUG-0744).

play_tribe (fix/w46-k4-bodies out/lab/20261108-020506-2): the guest's bot right-clicked a fruit (cFruit, noun
0x2C9CC91) in its game; the host's game had no fruit within 8 m of it (each game's fruit roll and are recycled
their own way, BUG-0472 / BUG-0497) and rejected the order ("rejected: no such target here"): the guest's member
stood.

1. Two games on the tribe fixture. The guest selects one of its own members; it picks a fruit of its own game
   near the village.
2. The host's game loses every fruit within 8.5 m of it (`coop.tribe unfruit`: the fruit is on the guest only),
   and right after the guest orders the gather on it (`coop.tribe fruit`: the DoActionForObjects a right-click
   gives, which the guest sends to the host).
3. The host's game did not reject it, its member has a Gather order (CitizenAction 2) with a target in the
   host's game (`coop.tribe ai`: the member's order record), and the member walks there (the guest sees its
   mirror 2 m nearer the fruit's place within 12 s).

SPORE_FIX_OFF=tribe.fruit_order must FAIL (rejected: no such target here).

  SPORE_LANE=7 python3 tools/tests/coop_tribe_fruit_order.py
"""
import re
import time

from coop_tribe_gather_stall import ai, pos_of
from coop_tribe_tools import dist, members
from twoinst import Checks, start

FRUIT = '2C9CC91'


def fruit(con):
    r = []
    for line in con.call(f'coop.tribe objs {FRUIT}')['out'].splitlines():
        m = re.match(r'(\S+) alive=(\d) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) dist=([\d.]+)', line)
        if m and m.group(2) == '1':
            r.append((tuple(float(m.group(i)) for i in (3, 4, 5)), float(m.group(6))))
    return r


def last_order(con):
    m = re.search(r'tribe last order: (.*)', con.call('coop.tribe')['out'])
    return m.group(1) if m else ''


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    own = sorted(i for i, o in members(guest).items() if o != 0)
    if not ck(own, f'the guest has members ({own})'):
        return ck.result()
    mid = own[0]
    for i in own:
        guest.call(f'coop.tribe select {i} {1 if i == mid else 0}')
    me = pos_of(guest, mid)
    gf = [f for f in fruit(guest) if f[1] < 80]
    hf = fruit(host)
    # a fruit of the guest's game the host's game has too (bound, BUG-0472), nearest the member
    both = sorted((dist(p, me), p) for p, _ in gf if any(dist(p, q) < 3 for q, _ in hf)) if me else []
    print(f'  member #{mid} at {me}; guest fruit near the village {len(gf)}, host fruit {len(hf)}, on both {len(both)}',
          flush=True)
    if not ck(both, 'a fruit near the village on both games'):
        return ck.result()
    at = both[0][1]
    x, y, z = at
    print('  host: ' + host.call(f'coop.tribe unfruit {x} {y} {z} 8.5')['out'].strip(), flush=True)
    print('  guest: ' + guest.call(f'coop.tribe fruit {x} {y} {z}')['out'].strip(), flush=True)
    g0 = pos_of(guest, mid)
    time.sleep(1.5)
    lo = last_order(host)
    print(f'  host: last order {lo}', flush=True)
    left = [p for p, _ in fruit(host) if dist(p, at) < 8]
    ck(not left, f'the host\'s game has no fruit within 8 m of the guest\'s ({len(left)})')
    ck('action=2' in lo and 'rejected' not in lo, f'the host\'s game took the guest\'s gather order ({lo[-120:]})')
    rec, out = None, ''
    t0 = time.time()
    while time.time() - t0 < 5:
        out, _, _ = ai(host, mid)
        m = re.match(r'rec=(-?\d+) target=(\S+)', out)
        rec = (int(m.group(1)), m.group(2)) if m else None
        if rec and rec[0] == 2 and rec[1] not in ('00000000', '(nil)', '0'):
            break
        time.sleep(0.5)
    print(f'  host #{mid}: {out[:300]}', flush=True)
    ck(rec and rec[0] == 2 and rec[1] not in ('00000000', '(nil)', '0'),
       f'the host\'s member #{mid} has a Gather order with a target in the host\'s game ({rec})')
    # the member's walk to its target, as the guest's game shows the host's member (its mirror): 2 m nearer the fruit's
    # place within 12 s (the target the host took is within 15 m of it)
    d0 = dist(g0, at) if g0 else None
    closer, t0 = 0.0, time.time()
    while time.time() - t0 < 12 and closer < 2 and d0 is not None:
        p = pos_of(guest, mid)
        closer = d0 - dist(p, at) if p else 0.0
        time.sleep(1)
    ck(closer >= 2, f'the guest sees its member walk toward the fruit ({closer:.1f} m nearer in 12 s, from {d0})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
