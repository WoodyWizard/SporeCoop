#!/usr/bin/env python3
"""BUG-0531 probe: a gather walk with a hut in the way (the play_tribe stall: a member in the gather node 0x0569236E,
state 1, standing short of its walk's goal).

For each of the host's huts near a guest's member: a fruit tree 10-35 m beyond the hut; the member is put 6 m on the
other side of the hut (`coop.tribe put`), so the straight way to the tree crosses the hut, and is sent to gather that
tree (`coop.tribe order <id> 2 <tree>`, the DoAction a click gives). Every second for 30 s the host's game is read
(`coop.tribe ai`: behaviour node, node state, walk mode, goal, its radius, position). A stall: the gather node in
state 1, the member still for 8 s or more, farther from the tree than the goal's radius + 1 m. Then the same gather
order again and a walk order: does it act?

  coop_tribe_gather_block.py [--tries 4]
"""
import argparse
import re
import time

import worlddiff
from coop_tribe_gather_stall import ai, pos_of, trees
from coop_tribe_tools import dist, members
from twoinst import Checks, start

HUT = 0x01E4DAAE


def walk_of(out):
    m = re.search(r'walk mode=(\d+) flags=(\S+) goal=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) r=([\d.]+)', out)
    return (int(m.group(1)), m.group(2), tuple(float(m.group(i)) for i in (3, 4, 5)), float(m.group(6))) if m else None


def watch(host, mid, tree, secs, tag):
    """[(t, node, state, pos, walk)] for secs; True when it stalled (see the module doc)"""
    rows, still_since, last = [], None, None
    t0 = time.time()
    stalled = False
    while time.time() - t0 < secs:
        out, node, st = ai(host, mid)
        p = pos_of(host, mid)
        w = walk_of(out)
        t = time.time() - t0
        rows.append((t, node, st, p, w))
        if p and last and dist(p, last) < 0.3:
            still_since = still_since if still_since is not None else t
        else:
            still_since = None
        last = p
        far = p and w and dist(p, tree) > w[3] + 1.0
        if node == '0569236E' and st == 1 and still_since is not None and t - still_since >= 8 and far:
            stalled = True
        if int(t) % 3 == 0:
            print(f'    {tag} {t:4.1f} s: node {node} state {st} {dist(p, tree) if p else -1:.1f} m from the tree; {out[:330]}',
                  flush=True)
        time.sleep(1.0)
    return stalled, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=4)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    own = sorted(i for i, o in members(guest).items() if o != 0)
    ck(bool(own), f'the guest has members ({own})')
    if not own:
        return ck.result()
    mid = own[-1]
    at = pos_of(host, mid)
    hw = worlddiff.dump(host, 400, at)
    huts = [x['pos'] for x in hw.rows if x['kind'] == 'g' and x['noun'] == HUT]
    ts = [t[1] for t in trees(host) if t[2]]
    cases = []
    for h in huts:
        for t in ts:
            d = dist(h, t)
            if 10 <= d <= 35:
                cases.append((d, h, t))
    cases.sort()
    print(f'  member #{mid} at {at}; {len(huts)} huts, {len(ts)} fruit trees, {len(cases)} hut-tree pairs', flush=True)
    ck(bool(cases), 'a hut with a fruit tree 10-35 m from it')
    stalls = []
    used = set()
    for d, h, t in cases:
        if len(used) >= a.tries:
            break
        if h in used:
            continue
        used.add(h)
        v = [hh - tt for hh, tt in zip(h, t)]
        n = sum(x * x for x in v) ** 0.5
        start_p = tuple(hh + x / n * 6.0 for hh, x in zip(h, v))
        print(f'  case hut {worlddiff._fmt(h)}, tree {worlddiff._fmt(t)} ({d:.0f} m beyond it): '
              + host.call('coop.tribe put %d %.2f %.2f %.2f' % ((mid,) + start_p))['out'].strip(), flush=True)
        time.sleep(1.5)
        print('    host: ' + host.call('coop.tribe order %d 2 %.2f %.2f %.2f' % ((mid,) + t))['out'].strip(), flush=True)
        stalled, rows = watch(host, mid, t, 30, 'gather')
        print(f'    stalled: {stalled}', flush=True)
        if stalled:
            stalls.append((h, t))
            print('    again the same gather order: ' + host.call('coop.tribe order %d 2 %.2f %.2f %.2f' % ((mid,) + t))['out'].strip(), flush=True)
            again, _ = watch(host, mid, t, 12, 'again')
            p = pos_of(host, mid)
            goal = tuple(pp + (pp - hh) / max(1.0, dist(p, h)) * 10 for pp, hh in zip(p, h))
            print('    a walk order 10 m off: ' + host.call('coop.tribe order %d 4 %.2f %.2f %.2f' % ((mid,) + goal))['out'].strip(), flush=True)
            _, wr = watch(host, mid, goal, 12, 'walk')
            print(f'    after the walk order: {dist(pos_of(host, mid), goal):.1f} m from its goal; node {wr[-1][1]}', flush=True)
    print(f'  stalls: {len(stalls)} of {len(used)} cases', flush=True)
    ck(not stalls, f'no gather walk stalls behind a hut ({len(stalls)} of {len(used)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
