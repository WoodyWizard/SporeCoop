#!/usr/bin/env python3
"""Tribe: the marker the host's game makes for a creature attack on a place far from a village (an ornament of model
0x356EB8A with a minimap blip, on a new spot record) is shown in the guest's game too (BUG-0740).

The game keeps spot records of planet cells (SpotRecord_Table); SpotRecord_CellId makes a record with its marker
when no record is in the cells round the place it is asked for. The villages' records are made by both games; the
creature attack's start (0xD73930, the Drama Manager's kind 7) asks for the attacked place in the host's game only,
so the marker it made there was the host's only (the detector's Tribe:host_only:ornament, BUG-0528 triage).
docs/re/tribe.md#tribe-attack-marker.

1. Tribe fixture, 2 players. The host's game is asked for the spot record of places --dist m (60) and further from
   the player village (`coop.wsync spot`: SpotRecord_CellId as the attack's start calls it with the attacked place;
   a record is made only at a shore cell with none near) until one is made (the fixture's own attacks hit the
   village, whose record exists, lab 20261108-052511 / -052727).
   (The game's own attack on the fixture makes none: it hits the player village, whose record exists, and a tribe
   formed now lies inland, where no record can be made: lab 20261108-061613.)
2. --rejoin: then the guest leaves and joins again (its game loads the host's world without the record).
PASS: the host's game made a new record with a marker of model 0x356EB8A; within 20 s the guest's game has the same
cell's record with a marker of that model at the same place (its own game objects, `coop.wsync spots`), and an
ornament object within 3 m of it in the guest's world (`coop.census`).

  SPORE_LANE=9 python3 tools/tests/coop_tribe_attack_marker.py [--rejoin]   (SPORE_FIX_OFF=wsync.tribe_marker fails)
"""
import math
import re
import sys
import time

from twoinst import Checks, start

MODEL = 0x0356EB8A
SPOT = re.compile(r'^spot cell=([0-9A-F]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) marker=(\S+) model=([0-9A-F]+)', re.M)


def spots(con):
    out = {}
    for m in SPOT.finditer(con.call('coop.wsync spots')['out']):
        out[m.group(1)] = dict(pos=tuple(float(m.group(i)) for i in (2, 3, 4)), marker=m.group(5),
                               model=int(m.group(6), 16))
    return out


def has_marker(s):
    return s and s['model'] == MODEL and s['marker'] not in ('00000000', '0x0', '(nil)', 'nil')


def village(con):
    m = re.search(r'^tribe i=\d+ player=1 pol=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.wstate')['out'], re.M)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def away(p, d, ang=0.0):
    """a point d m from p along the planet's surface (same radius), in the direction ang (radians)"""
    r = math.sqrt(sum(c * c for c in p))
    n = [c / r for c in p]
    a = [0.0, 0.0, 1.0] if abs(n[2]) < 0.9 else [1.0, 0.0, 0.0]
    t = [n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0]]
    tl = math.sqrt(sum(c * c for c in t))
    t = [c / tl for c in t]
    b = [n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]]
    q = [p[i] + (t[i] * math.cos(ang) + b[i] * math.sin(ang)) * d for i in range(3)]
    ql = math.sqrt(sum(c * c for c in q))
    return tuple(c / ql * r for c in q)


def ornaments_near(con, pos, r=3.0):
    out = con.call(f'coop.census all 30 {pos[0]:.1f} {pos[1]:.1f} {pos[2]:.1f}')['out']
    n = 0
    for line in out.splitlines():
        if 'noun=018C88E4' not in line:
            continue
        m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and math.dist(pos, tuple(float(m.group(i)) for i in (1, 2, 3))) <= r:
            n += 1
    return n


def main():
    rejoin = '--rejoin' in sys.argv
    dist = 60.0
    for i, a in enumerate(sys.argv):
        if a == '--dist' and i + 1 < len(sys.argv):
            dist = float(sys.argv[i + 1])
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    v = village(host)
    ck(v is not None, f'the host has a player village ({v})')
    if v is None:
        return ck.result()
    h0, g0 = spots(host), spots(guest)
    print(f'  before: host {len(h0)} spot records ({sum(has_marker(s) for s in h0.values())} with a marker), guest {len(g0)} '
          f'({sum(has_marker(s) for s in g0.values())})', flush=True)
    # a record is made only at a shore cell (shallow water, kind 3 of 0xB83110) with no record within +-4 cells:
    # places round the village are asked until one is
    new, asked = [], 0
    for d in (dist, dist * 1.5, dist * 2, dist * 2.5, dist * 3):
        for k in range(12):
            p = away(v, d, k * math.pi / 6)
            host.call(f'coop.wsync spot {p[0]:.2f} {p[1]:.2f} {p[2]:.2f}')
            asked += 1
            time.sleep(0.4)
            h1 = spots(host)
            new = [c for c in h1 if c not in h0 and has_marker(h1[c])]
            if new:
                break
        if new:
            break
    print(f'  host: {asked} places asked, the last ({p[0]:.1f} {p[1]:.1f} {p[2]:.1f}) {d:.0f} m from the village', flush=True)
    ck(len(new) == 1, f'the host\'s game made one new spot record with its marker of model {MODEL:08X} ({new})')
    if len(new) != 1:
        return ck.result()
    cell, hs = new[0], h1[new[0]]
    print(f'  host record {cell} at {hs["pos"]} ({math.dist(hs["pos"], v):.0f} m from the village)', flush=True)
    if rejoin:
        import lane
        guest.call('coop.leave')
        time.sleep(4)
        print('  rejoin: ' + guest.call(lane.JOIN)['out'].strip()[:60], flush=True)
        t1, left = time.time(), False
        while time.time() - t1 < 60 and not left:  # the host's world is loaded again: the old copy goes
            left = 'Tribe' not in guest.call('game.mode')['out']
            time.sleep(0.3)
        ck(left, 'the guest\'s game left its old copy to load the host\'s world')
        time.sleep(3)
        t1 = time.time()
        while time.time() - t1 < 120 and not ('Tribe' in guest.call('game.mode')['out'] and village(guest)):
            time.sleep(0.5)
        ck(village(guest) is not None, 'the guest plays the host\'s Tribe again after its rejoin')
        time.sleep(5)
    t0, gs = time.time(), None
    while time.time() - t0 < 20:
        gs = spots(guest).get(cell)
        if has_marker(gs):
            break
        time.sleep(1)
    print(f'  guest record {cell}: {gs} after {time.time() - t0:.0f} s', flush=True)
    ck(has_marker(gs), f'the guest\'s game has the host\'s spot record {cell} with a marker of model {MODEL:08X}')
    if gs:
        d = math.dist(gs['pos'], hs['pos'])
        ck(d < 1.0, f'at the host\'s place ({d:.2f} m apart)')
    n = ornaments_near(guest, hs['pos'])
    ck(n >= 1, f'the guest\'s world has an ornament at the host\'s marker ({n} within 3 m)')
    print('  guest: ' + guest.call('coop.wsync')['out'].strip().splitlines()[-1], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
