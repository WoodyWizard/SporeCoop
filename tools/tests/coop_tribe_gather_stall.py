#!/usr/bin/env python3
"""Tribe: a member sent to gather far off acts on a new order (BUG-0531, not reproduced: a regression check).

play_tribe: member #29 had the guest's new order (walk, then hunt) but stayed in the gather node 0x0569236E,
state 1, its walk goal an old tree 100 m off, not moving. This test sends a member of the guest to gather a
fruit tree 90-120 m from it in the host's game (`coop.tribe order <id> 2 <tree>`: the DoAction a click and
host_command give), watches its behaviour node (`coop.tribe ai`: node, the node's state, the walk, the food it
carries), then gives it a walk order 15 m from where it stands (the DoAction of a guest's walk order on the
host). The member must leave the gather node and walk: in the host's game it is within 6 m of the walk's goal
within 40 s, and the guest's screen shows its mirror there too. The walk order is the guest's own (select +
walk on the guest, run by host_command), given while the member carries the food home (gather state 19, more
than 35 m from the village) or in --states. 0.7.6: passed in every run (the stall of play_tribe was not
reproduced this way: docs/re/tribe.md#tribe-gather-orders).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_gather_stall.py [--far 90] [--wait 40]
"""
import argparse
import re
import time

from coop_tribe_tools import dist, members
from twoinst import Checks, start


def trees(con, n=400):
    r = []
    for line in con.call(f'coop.tribe trees {n}')['out'].splitlines():
        m = re.match(r'\S+ d=(\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) .* 48=([0-9A-F]+)', line)
        if m:
            r.append((int(m.group(1)), tuple(float(m.group(i)) for i in (2, 3, 4)), int(m.group(5), 16)))
    return r


def pos_of(con, mid):
    for line in con.call('coop.tribe')['out'].splitlines():
        if re.match(rf'\s+#{mid} ', line):
            m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
            if m:
                return tuple(float(m.group(i)) for i in (1, 2, 3))
    return None


def ai(con, mid):
    out = con.call(f'coop.tribe ai {mid}')['out'].strip()
    m = re.search(r'node=([0-9A-F]+) depth=\d+ mem=(-?\d+)', out)
    return out, (m.group(1) if m else None), (int(m.group(2)) if m else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--far', type=float, default=90.0)
    ap.add_argument('--wait', type=float, default=40.0)
    ap.add_argument('--states', type=lambda v: [int(x) for x in v.split(',')], default=[19],
                    help='the gather node states to give the walk order in (19: carrying the food home)')
    ap.add_argument('--after', type=float, default=8.0, help='s after the gather order at the earliest')
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
    ts = [t for t in trees(host) if t[2]]
    far = sorted((t for t in ts if at and a.far <= dist(t[1], at) <= a.far + 40), key=lambda t: dist(t[1], at))
    ck(bool(far), f'a fruit tree {a.far:.0f}-{a.far + 40:.0f} m from member #{mid} ({len(ts)} trees with fruit)')
    if not far:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # control: the same walk order on the member before the gather (the order path works on an idle member)
    here = pos_of(host, mid)
    cgoal = tuple(h + (t - h) / max(1.0, dist(here, far[0][1])) * 12 for h, t in zip(here, far[0][1]))
    print('  host (control):', host.call(f'coop.tribe order {mid} 4 {cgoal[0]} {cgoal[1]} {cgoal[2]}')['out'].strip())
    moved = 0.0
    for i in range(8):
        time.sleep(2)
        out, node, st = ai(host, mid)
        moved = dist(pos_of(host, mid), here)
        print(f'  control {2 * i + 2} s: moved {moved:.1f} m, {dist(pos_of(host, mid), cgoal):.1f} m from the goal; {out}')
        if moved >= 3:
            break
    # (the game's walk ends early at an obstacle by the village: the order is acted on when the member walks)
    ck(moved >= 3, f'control: an idle member #{mid} walks on a walk order (moved {moved:.1f} m)')
    tree = far[0][1]
    print(f'  member #{mid} at {at}, tree {tree} {dist(tree, at):.0f} m off')
    print('  host:', host.call(f'coop.tribe order {mid} 2 {tree[0]} {tree[1]} {tree[2]}')['out'].strip())
    # wait until the member is busy with the gather far from the village (at the tree, or carrying the food home)
    t0 = time.time()
    seen_gather = False
    vil = at
    while time.time() - t0 < 45:
        time.sleep(2)
        out, node, st = ai(host, mid)
        seen_gather |= node == '0569236E'
        here = pos_of(host, mid)
        print(f'  {time.time() - t0:.0f} s: {dist(here, vil):.0f} m from the village; {out}')
        if node == '0569236E' and st in a.states and dist(here, vil) > 35 and time.time() - t0 > a.after:
            break
    ck(seen_gather, f'member #{mid} went into the gather node 0x0569236E')
    here = pos_of(host, mid)
    # 15 m back toward the tree it came from (walkable ground, not along its way home)
    d = [t - h for h, t in zip(here, tree)]
    l = sum(x * x for x in d) ** 0.5 or 1.0
    goal = tuple(h + x / l * 15 for h, x in zip(here, d))
    k = dist(here, (0, 0, 0)) / dist(goal, (0, 0, 0))  # on the sphere of the member's height
    goal = tuple(g * k for g in goal)
    out, node, st = ai(host, mid)
    print(f'  before the walk order: {out}')
    stalled = node == '0569236E'
    # the guest's own order, as a player gives it: select its member, walk (sent to the host, host_command)
    print('  guest:', guest.call(f'coop.tribe select {mid} 1')['out'].strip())
    print('  guest:', guest.call(f'coop.tribe walk {goal[0]:.1f} {goal[1]:.1f} {goal[2]:.1f}')['out'].strip())
    t0 = time.time()
    hd = gd = None
    d0 = dist(here, goal)
    left = None
    while time.time() - t0 < a.wait:
        time.sleep(3)
        out, node, st = ai(host, mid)
        hp, gp = pos_of(host, mid), pos_of(guest, mid)
        hd = dist(hp, goal) if hp else None
        gd = dist(gp, goal) if gp else None
        if node != '0569236E' and left is None:
            left = time.time() - t0
        print(f'  walk {time.time() - t0:.0f} s: host {hd and round(hd, 1)} m, guest {gd and round(gd, 1)} m from the goal; {out}')
        if hd is not None and hd < 6 and gd is not None and gd < 6:
            break
    # (the game's own walk can end some metres short of a goal off the paths: acted on = left the gather and went
    # at least 5 m of the 15 toward the goal)
    print('  host order:', re.search(r'tribe last order: (.*)', host.call('coop.status')['out']).group(1))
    print(f'  the member was {"in the gather node" if stalled else "NOT in the gather node"} at the walk order')
    ck(stalled, 'the walk order came while the member was gathering')
    ck(left is not None and left < 7, f'member #{mid} left the gather node after the order ({left} s)')
    ck(hd is not None and hd <= d0 - 5, f'member #{mid} walked toward the new order\'s goal in the host\'s game ({d0:.1f} -> {hd and round(hd, 1)} m)')
    ck(gd is not None and gd <= d0 - 5, f'the guest sees member #{mid} walk toward the goal ({d0:.1f} -> {gd and round(gd, 1)} m)')
    print('  host', host.call('coop.tribe tools')['out'].strip().splitlines()[-1])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
