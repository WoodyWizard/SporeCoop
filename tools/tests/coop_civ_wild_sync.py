#!/usr/bin/env python3
"""Civilization: the host owns the wild creatures, the guest shows the host's (BUG-0973).

Before: every game ran its own wild creatures in Civ (herds, epics). After a chase, a flight or a kill by the host's
vehicles (the guest's vehicles are mirrors and deal no damage) the herds of the two games differed for good, and a
guest's attack of a creature it saw named a creature the host did not have there (BUG-0970's fallback then gave a
move order).

1. Two games on civ_npc (a herd of 7 and three epics). Both games' wild creatures (census rows, noun 0x18EB45E).
2. Both cameras on the herd; the host's game sets its herd 40 m aside (`coop.civ creatureshift`, as a flight would): within 8 s every live wild
   creature of the host's game has one of the guest's within 4 m and the counts agree.
3. The host spawns two military land vehicles at the player's city nearest to the herd and gives them to the guest;
   the guest's game orders them to attack the creature of its own screen nearest to them (`coop.civ attack <id> c`,
   the game's SetOrder, as a right-click). Within 60 s the creature of the host's game at that place (the same
   creature) is dead in the host's game, and the guest's census agrees again (the same live creatures within 4 m,
   a dead body where the host's is).
SPORE_FIX_OFF=npc.civ_wild: the guest's herd stays where it was (step 2 fails) and the guest's order names a creature
the host does not have there (no kill): FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_wild_sync.py
"""
import re
import time

import worlddiff
from play_civ import civ_state, dist, frame_city
from twoinst import Checks, keyframe, start

CREATURE = 0x018EB45E


def wild(con):
    """the wild creatures of this game: [(pos, alive, serial, own)] (census rows of noun 0x18EB45E; the object
    without a species at (0 0 0) is skipped by the census; own: npc = one the host's game simulates and sends)"""
    w = worlddiff.World(con.call('coop.census creatures')['out'])
    return [(r['pos'], r['alive'], r.get('ser'), r.get('own')) for r in w.rows if r['noun'] == CREATURE]


def compare(h, g, tol=4.0):
    """(worst distance between the two games' live wild creatures, both ways, live counts, dead counts). The host's
    side: the creatures its game simulates (Civ keeps herds simulated only near the cameras and pointers: one left
    out stands still in the census, out of the world's simulation, and is shown nowhere: docs/re/civ.md#civ-wild-sync);
    every live creature on the guest's screen must have a live one of the host's game at its place."""
    hall = [x[0] for x in h if x[1]]
    hl = [x[0] for x in h if x[1] and x[3] == 'npc'] or hall  # (switch off: nothing is tracked, all count)
    gl = [x[0] for x in g if x[1]]
    fwd = max((min((dist(p, q) for q in gl), default=1e9) for p in hl), default=0.0)
    rev = max((min((dist(p, q) for q in hall), default=1e9) for p in gl), default=0.0)
    return max(fwd, rev), (len(hl), len(gl)), (sum(1 for x in h if not x[1]), sum(1 for x in g if not x[1]))


def agree(host, guest, secs, label):
    t0 = time.time()
    while True:
        h, g = wild(host), wild(guest)
        worst, live, dead = compare(h, g)
        if (worst < 4.0 and live[0] == live[1]) or time.time() - t0 > secs:
            print(f'  {label}: worst {worst:.1f} m, live host/guest {live}, dead {dead} ({time.time() - t0:.0f} s)',
                  flush=True)
            return h, g, worst, live, dead
        time.sleep(1)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    h, g, worst, live, _ = agree(host, guest, 10, 'after the load')
    print('  host', host.call('coop.npc status')['out'].strip().splitlines()[1][:160], flush=True)
    print('  guest', guest.call('coop.npc status')['out'].strip().splitlines()[1][:200], flush=True)
    # bandwidth (diagnostics): what the host sends the guest per second with the wild creatures in it
    def out_bytes():
        m = re.search(r'net to 1: out_bytes=(\d+)', host.call('coop.status')['out'])
        return int(m.group(1)) if m else 0
    b0, t0 = out_bytes(), time.time()
    time.sleep(10)
    rate = (out_bytes() - b0) / (time.time() - t0)
    tracked = re.search(r'tracked=(\d+)', host.call('coop.npc status')['out'])
    print(f'  host -> guest: {rate / 1024:.1f} KB/s in all, {tracked and tracked.group(0)} wild creatures', flush=True)
    herd = [x[0] for x in h if x[1]]
    if not ck(len(herd) >= 3, f'wild creatures in the host\'s game ({len(herd)})'):
        return ck.result()
    # the herd: the live creature with the most others within 30 m (not an epic standing alone)
    c0 = max(herd, key=lambda p: sum(1 for q in herd if dist(p, q) < 30))
    # 2. the host's herd moves 40 m aside. Both cameras look at the herd first: the Civ game keeps herds simulated only
    # within ~200 m of its camera's point (the relevance sphere, docs/re/civ.md#civ-wild-sync); a herd moved out of it
    # is taken back into the pool (on both games then: the guest drops its proxies)
    frame_city({'host': host, 'guest': guest}, c0, answer=False)
    time.sleep(3)
    x, y, z = c0
    print('  host:', host.call(f'coop.civ creatureshift {x:.1f} {y:.1f} {z:.1f} 30 40')['out'].strip(), flush=True)
    time.sleep(1)
    h, g, worst, live, _ = agree(host, guest, 8, 'after the host\'s herd moved')
    ck(worst < 4.0 and live[0] == live[1],
       f'the guest shows the host\'s wild creatures where the host has them (worst {worst:.1f} m, live {live})')
    hm = [x[0] for x in h if x[1] and 20 < dist(x[0], c0) < 100]
    if not ck(hm, 'the moved herd in the host\'s game'):
        return ck.result()
    herd_at = hm[0]
    frame_city({'host': host, 'guest': guest}, herd_at, answer=False)
    keyframe('herd', 'the same herd at the same place on both screens')
    # 3. the guest's vehicles attack a creature of the guest's screen
    cities, before, _ = civ_state(host)
    mine = [i for i, c in cities.items() if c['player']]
    if not ck(mine, f'a city of the player ({mine})'):
        return ck.result()
    ci = min(mine, key=lambda i: dist(cities[i]['pos'], herd_at))
    for _ in range(2):
        print(f'  host: city {ci}:', host.call(f'coop.civ spawnveh {ci} 0 0')['out'].strip(), flush=True)
    time.sleep(2)
    _, hv, _ = civ_state(host)
    ids = sorted(set(hv) - set(before))
    for i in ids:
        host.call(f'coop.civ select {i} 1')
    print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
    time.sleep(4)
    _, gv, _ = civ_state(guest)
    if not ck(len(ids) == 2 and all(gv.get(i, {}).get('owner') == 1 for i in ids), f'the guest owns the vehicles {ids}'):
        return ck.result()
    at = gv[ids[0]]['pos']
    g = wild(guest)
    gl = [x[0] for x in g if x[1]]
    near = min(gl, key=lambda p: dist(p, at))
    h = wild(host)
    hl = [(x[0], x[2]) for x in h if x[1]]
    tp, tser = min(hl, key=lambda x: dist(x[0], near))
    print(f'  the guest\'s creature at {tuple(round(v) for v in near)} ({dist(near, at):.0f} m from the vehicles); '
          f'the host\'s nearest {dist(tp, near):.1f} m from it (serial {tser})', flush=True)
    x, y, z = near
    for i in ids:
        print(f'  guest: attack {i}:', guest.call(f'coop.civ attack {i} c{x:.1f},{y:.1f},{z:.1f}')['out'].strip(),
              flush=True)
    t0, dead, last = time.time(), False, tp
    while time.time() - t0 < 60:
        row = [(x[0], x[1]) for x in wild(host) if x[2] == tser]
        if row:
            last = row[0][0]
        if not row or not row[0][1]:
            dead = True
            break
        time.sleep(1)
    print(f'  the host\'s creature (serial {tser}): {"dead or gone" if dead else "alive"} after {time.time() - t0:.0f} s',
          flush=True)
    _, _, hst = civ_state(host)
    print('  host', hst[:240], flush=True)
    ck(dead, 'the guest\'s attack order killed the same creature in the host\'s game')
    time.sleep(3)
    h, g, worst, live, deadn = agree(host, guest, 8, 'after the kill')
    ck(worst < 4.0 and live[0] == live[1],
       f'after the kill the guest shows the host\'s live wild creatures (worst {worst:.1f} m, live {live})')
    # the killed creature on the guest's screen: a dead body there, no live creature in its place
    gbody = min((dist(x[0], last) for x in g if not x[1]), default=1e9)
    glive = min((dist(x[0], last) for x in g if x[1]), default=1e9)
    print(f'  where the host\'s creature died {tuple(round(v) for v in last)}: the guest\'s nearest body {gbody:.1f} m, '
          f'nearest live creature {glive:.1f} m', flush=True)
    ck(dead and gbody < 4.0, f'the killed creature is dead on the guest\'s screen too (a body {gbody:.1f} m from it)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
