#!/usr/bin/env python3
"""BUG-0815 probe: the mate the game picked near the nest is 11 m from it when the player calls and clicks it.

Fork reports (Lineages 37..40): the mate's reply score 0xD7DAB0 needs the mate within ~9.7 m of the nest; a mate that
wandered beyond it scored 0 for good, the avatar's mating node waited and no egg came. The game picks the mate within
15 m of the nest (the Drama Manager, docs/re/creature.md#creature-mating-call), so it can be out of that radius.

1. Two games on the creature fixture. The host's game names a mate (`coop.drama mate`).
2. The mate is put DIST m (default 11) from the nest (`coop.npc place`, the game's teleport), the host's creature
   next to the nest.
3. The host presses Backspace (the call); the mate is put DIST m away again (the game walks it back to the nest) and
   the host left-clicks it where its game draws it (as coop_creature_mating.py).
PASS: within 60 s the mating goes on (the egg cinematic or the game's mating editor request: the vote, or the editor).
FAIL: the avatar's mating node stays without an egg. (Probe: the host's mating logic is the game's own, no hook of the
mod is in it; a FAIL is the game's behaviour.)

  coop_creature_mating_far.py [--dist 11]
"""
import argparse
import math
import re
import time

from coop_creature_mating import MATE_NODE, mate, mode, npc_ids, screen_of, W, H
from coop_migration import cinematic
from coop_nest_follow import herd_nest_pos
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dist', type=float, default=11.0)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    m0 = mode(host)
    mt = None
    t0 = time.time()
    while time.time() - t0 < 30:
        mt = mate(host)
        if mt and mt['ptr'] and mt['flags'] & 0x10:
            break
        time.sleep(1)
    ck(mt and mt['ptr'] and mt['flags'] & 0x10, "the host's game picked a mate near the nest")
    ids = npc_ids(host)
    me = ids.get(mt['ptr']) if mt else None
    nest = herd_nest_pos(host)
    ck(me is not None and nest is not None, f'the mate is one of the host\'s NPCs ({me}), the nest at {nest}')
    if not (me and nest):
        ck.faults(host=host, guest=guest)
        return ck.result()
    nid = me[0]
    out = host.call('coop.drama mate')['out']
    mp = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    p = tuple(float(v) for v in mp.groups())
    r = math.dist(p, (0, 0, 0))
    d = [p[i] - nest[i] for i in range(3)]
    n = math.dist(d, (0, 0, 0)) or 1.0
    at = [nest[i] + d[i] / n * a.dist for i in range(3)]
    k = r / (math.dist(at, (0, 0, 0)) or 1.0)
    at = [v * k for v in at]
    print(f'  mate #{nid} {math.dist(p, nest):.1f} m from the nest; put {a.dist} m away:',
          host.call('coop.npc place %s %.2f %.2f %.2f' % (nid, *at))['out'].strip(), flush=True)
    host.call('coop.npc gopos %.2f %.2f %.2f' % tuple(nest))
    time.sleep(2)
    out = host.call('coop.drama mate')['out']
    mp = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    print(f'  mate now {math.dist(tuple(float(v) for v in mp.groups()), nest):.1f} m from the nest: {out.strip()}', flush=True)
    host.call('key 0x08')  # Backspace: the mating call
    time.sleep(3)
    # the game walks the picked mate back to the nest (3 m after 2 s, lab 20261112-075547): put it out again now,
    # while the call waits, as the fork's mate that had wandered off
    host.call('coop.npc place %s %.2f %.2f %.2f' % (nid, *at))
    out = host.call('coop.drama mate')['out']
    mp = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    far = mp and math.dist(tuple(float(v) for v in mp.groups()), nest)
    print(f'  after the call the mate is {far and round(far, 1)} m from the nest: {out.strip()}', flush=True)
    ck(far and far > 9.7, f'the mate is beyond the reply radius of ~9.7 m when it is clicked ({far and round(far, 1)} m)')
    going = False
    for i in range(6):
        xy = screen_of(host, nid)
        if not xy:
            host.call(f'drag {W // 2} {H // 2} {W // 2 + 350} {H // 2} right')
            time.sleep(0.5)
            continue
        host.call(f'click {xy[0]} {xy[1]}')
        t1 = time.time()
        while time.time() - t1 < 3 and not going:
            m = mate(host)
            going = bool(m and m['av_node'] == MATE_NODE)
            time.sleep(0.3)
        if going:
            break
    ck(going, f'a left click on the mate: the host\'s creature goes to mate (node {MATE_NODE})')
    egg = False
    cines = set()
    t0 = time.time()
    while going and time.time() - t0 < 60:
        c = cinematic(host)
        if c:
            cines.add(c)
        v = host.call('coop.vote')['out']
        if 'vote active' in v or mode(host) != m0 or 'B6A1DBE1' in cines:
            egg = True
            break
        time.sleep(0.5)
    print(f'  {time.time() - t0:.0f} s: cinematics {sorted(cines)}, mate {host.call("coop.drama mate")["out"].strip()}',
          flush=True)
    ck(egg, 'the mating goes on: the egg cinematic or the mating editor request within 60 s')
    if 'vote active' in host.call('coop.vote')['out']:
        host.call('coop.vote cancel')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
