#!/usr/bin/env python3
"""The game's address space over a played session: a census per game every few minutes (BUG-0631, BUG-0621;
docs/06-testing.md#testing-mem-census).

Both games switch the address space tracker on (`coop.memcensus track on`: who reserves what from now on), then
the stage's play bot plays (Creature: play_creature.Player, real input, two friends). Every --sample minutes
each game prints a census (`coop.memcensus`): free MB, largest free block, the allocations by kind and by who
reserved them. At the end the growth between the first and the last census per kind and per caller.

  --solo     the guest leaves the session first: two single games of the same length (the game's own drain
             with the mod loaded but no session, the control)

  SPORE_LANE=4 python3 tools/tests/coop_mem_census.py --minutes 20 [--solo] [--sample 4] [--depth 2]
"""
import argparse
import json
import random
import re
import sys
import time

import lane
from twoinst import Checks, Monitor, start


def census(con, depth, grain=1):
    out = con.call(f'coop.memcensus {depth} {grain} 60')['out']
    c = {'cat': {}, 'who': {}}
    for line in out.splitlines():
        f = line.split()
        if not f:
            continue
        kv = dict(x.split('=', 1) for x in f[1:] if '=' in x)
        if f[0] == 'census':
            c.update({k: (int(v) if v.lstrip('-').isdigit() else v) for k, v in kv.items()})
        elif f[0] in ('cat', 'who') and len(f) > 1:
            c[f[0]][f[1]] = {k: int(v) for k, v in kv.items()}
    return c


def diff(a, b, kind, top=15):
    keys = set(a[kind]) | set(b[kind])
    z = {'n': 0, 'res_kb': 0, 'com_kb': 0}
    rows = []
    for k in keys:
        x, y = a[kind].get(k, z), b[kind].get(k, z)
        rows.append((y['res_kb'] - x['res_kb'], y['com_kb'] - x['com_kb'], y['n'] - x['n'], k, y['res_kb']))
    rows.sort(key=lambda r: -abs(r[0]))
    return rows[:top]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--minutes', type=float, default=20)
    ap.add_argument('--sample', type=float, default=4)
    ap.add_argument('--depth', type=int, default=4)
    ap.add_argument('--solo', action='store_true')
    ap.add_argument('--idle', action='store_true', help='nobody plays (the games stand)')
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  mem census: {a.fixture}, {a.minutes} min, solo={a.solo} idle={a.idle}, seed {seed}', flush=True)
    host, guest = start(a.fixture)
    ck = Checks()
    games = {'host': host, 'guest': guest}
    for who, con in games.items():
        print(f'  {who}:', con.call('coop.memcensus track on')['out'].strip(), flush=True)
    if a.solo:
        print('  solo: the guest leaves:', guest.call('coop.leave')['out'].strip()[:80], flush=True)
        time.sleep(5)
        print('  host:', host.call('coop.status')['out'].strip().splitlines()[0][:100], flush=True)
    first = {w: census(c, a.depth) for w, c in games.items()}
    t0 = time.time()
    deadline = t0 + a.minutes * 60
    players = []
    if not a.idle and a.fixture == 'creature':
        import play_creature as pc
        world, sh = pc.World(lane.HOST), pc.Shared()
        sh.exit_result = None
        players = [pc.Player('host', lane.HOST, 1, world, sh, seed, deadline, None),
                   pc.Player('guest', lane.GUEST, 0, world, sh, seed + 1, deadline, None)]
        for p in players:
            p.start()
    series = {w: [(0.0, first[w]['free_mb'], first[w]['block_mb'])] for w in games}
    last = dict(first)
    next_at = t0 + a.sample * 60
    while time.time() < deadline and not Monitor.events:
        time.sleep(5)
        if time.time() >= next_at or time.time() >= deadline:
            next_at += a.sample * 60
            for w, c in games.items():
                try:
                    last[w] = census(c, a.depth)
                except (OSError, ValueError) as e:
                    print(f'  {w}: census failed {e!r}', flush=True)
                    continue
                m = (time.time() - t0) / 60
                series[w].append((round(m, 1), last[w]['free_mb'], last[w]['block_mb']))
                top = diff(first[w], last[w], 'cat', 4)
                print(f'  {m:5.1f} min {w}: free {last[w]["free_mb"]} MB block {last[w]["block_mb"]} MB | '
                      + ', '.join(f'{r[3]} {r[0] / 1024:+.0f} MB' for r in top), flush=True)
    for p in players:
        p.join(120)
        if p.error:
            print(f'  note: {p.name} bot: {p.error}', flush=True)
    for w in games:
        try:
            last[w] = census(games[w], a.depth)
        except (OSError, ValueError):
            pass
        f0, f1 = first[w]['free_mb'], last[w]['free_mb']
        print(f'  === {w}: free {f0} -> {f1} MB ({f1 - f0:+d}), block {first[w]["block_mb"]} -> {last[w]["block_mb"]} MB',
              flush=True)
        for r in diff(first[w], last[w], 'cat', 20):
            if r[0] or r[2]:
                print(f'    cat {r[3]:28s} res {r[0] / 1024:+8.1f} MB  com {r[1] / 1024:+8.1f} MB  n {r[2]:+5d}  (now {r[4] / 1024:.0f} MB)',
                      flush=True)
        for r in diff(first[w], last[w], 'who', 25):
            if r[0] or r[2]:
                print(f'    who {r[3]:60s} res {r[0] / 1024:+8.1f} MB  com {r[1] / 1024:+8.1f} MB  n {r[2]:+5d}', flush=True)
        print('MEMCENSUS ' + json.dumps({'who': w, 'series': series[w], 'first': first[w], 'last': last[w]}), flush=True)
    ck(not Monitor.events, 'the games survived the session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
