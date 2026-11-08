#!/usr/bin/env python3
"""Fixed game pools under co-op object counts, per stage (BUG-0719, class BUG-0432).

The game keeps some of its objects in pools of a fixed size (docs/05-re-notes.md#re-notes-fixed-pools): in every
stage the physics world's body pool (4096) and contact pool (2048), in Cell the cell pool (4096), its broadphase
and its graphics. Co-op adds objects to the same world (puppets, NPC proxies and mirrors, herds, vehicles, ships).
This is the stress run of the class: --players players (as many as one lane holds: four, tools/tests/party.py) in
one stage (in Cell every guest swimming about: new mirrors near each), the last guest leaving and joining again
(its puppet and proxies made anew on every game), then the guests in their pause menus while the host plays on
(objects made while a guest's simulation stands), --seconds s each.

Checked on every game, sampled every few seconds (`coop.pools`, the pools' own count and capacity; in Cell also
`coop.pool` cells / peak): each pool's high water stays under 75 % of its capacity, and no game caught a fault
(the lab checks the logs too, and fails a game that died). The numbers go into the ticket's inventory.

  SPORE_LANE=6 python3 tools/tests/coop_pool_stress.py --fixture creature [--players 4] [--seconds 40]
"""
import argparse
import re
import time

from party import Party
from twoinst import Checks

POOL = re.compile(r'pool (\w+): count=(\d+) high=(\d+) cap=(\d+)')


def sample(p, peaks):
    for x in p.all:
        out = x.call('coop.pools')
        for m in POOL.finditer(out):
            name, cnt, high, cap = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
            k = (x.name, name)
            old = peaks.get(k, (0, cap, 0))
            peaks[k] = (max(old[0], high), cap, max(old[2], cnt))
        m = re.search(r'objects: all=(\d+) creatures=(\d+)', out)
        if m:
            k = (x.name, 'objects')
            old = peaks.get(k, (0, 0, 0))
            peaks[k] = (max(old[0], int(m.group(1))), 0, max(old[2], int(m.group(2))))
        if p.fixture == 'cell':
            c = x.call('coop.pool')
            m = re.search(r'\bpeak=(\d+)', c)
            if m:
                k = (x.name, 'cells')
                peaks[k] = (max(peaks.get(k, (0,))[0], int(m.group(1))), 4096, 0)


def move(p, k, fixture):
    """Cell: every guest swims (new tiles and mirrors at the edges of its screen); the other stages: the players
    stay (the world's own NPCs come and go around them, the joins and the rejoin make the objects)"""
    if fixture != 'cell':
        return
    for i, g in enumerate(p.guests):
        g.call(f'coop.pool goto {(-1) ** (k + i) * 20} {(-1) ** (k // 2 + i) * 20}')


def phase(p, label, seconds, peaks, k0=0):
    t0, k = time.time(), k0
    while time.time() - t0 < seconds:
        move(p, k, p.fixture)
        k += 1
        time.sleep(4)
        sample(p, peaks)
    print(f'  [{label}] ' + ', '.join(f'{n}/{pool} {v[0]}/{v[1]}' for (n, pool), v in sorted(peaks.items()) if pool != 'objects'), flush=True)
    return k


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--seconds', type=float, default=40)
    a = ap.parse_args()
    ck = Checks()
    p = Party.start(a.fixture, a.players)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        for x in p.all:
            x.call('coop.team hp 1000')
        peaks = {}
        sample(p, peaks)
        k = phase(p, 'play', a.seconds, peaks)
        d = p.guests[-1]
        ck(p.leave(d) and p.join(d), f'{d.name} left and joined again')
        k = phase(p, 'rejoin', a.seconds / 2, peaks, k)
        for g in p.guests:  # the guests' games stand (pause menu) while the host's world goes on
            g.call('key escape')
        time.sleep(1)
        t0 = time.time()
        while time.time() - t0 < a.seconds / 2:
            time.sleep(4)
            sample(p, peaks)
        for g in p.guests:
            g.call('key escape')
        phase(p, 'after pause', a.seconds / 4, peaks, k)
        for (name, pool), (high, cap, extra) in sorted(peaks.items()):
            if pool == 'objects':
                print(f'  {name}: at most {high} world objects, {extra} creatures', flush=True)
                continue
            ck(cap > 0 and high < 0.75 * cap, f'{name}: the game\'s {pool} pool stays under 75 % ({high} of {cap})')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
