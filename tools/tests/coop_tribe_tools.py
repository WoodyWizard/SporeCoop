#!/usr/bin/env python3
"""Tribe: a guest's member takes a weapon from a rack the host built (BUG-0514).

Players: "the guest cannot pick up tools from host-built buildings, so the guest's army is much weaker".
A tool the host buys is made by its own game's palette only: the guest's game had no rack there to click,
and the guest's members, whose GrabTool runs on the host, never showed the weapon on the guest (the
mirror's own +0x102C).

1. Tribe fixture. The host builds a weapon rack (`coop.tribe build 1 <m>`: CreateTool as the palette's
   placement, test setup: the fixture's palette is locked).
2. The guest's game has that rack: a tool of type 1 in its player tribe within 3 m of the host's
   (`coop.tribe tools` on the guest: the game's own tool list).
3. The guest orders one of its own members to the rack (`coop.tribe grab <id> <tool>`: the GrabTool order a
   click on the rack gives, through DoActionForObjects, sent to the host).
4. Within 60 s the member holds the weapon in the host's game (+0x102C = 1) AND on the guest's screen (its
   mirror's +0x102C, which the game draws as the handheld tool).

--nofix (or SPORE_FIX_OFF=wobj.tribe_tool,tribe.tool_hold): must FAIL (no rack on the guest).

  SPORE_LANE=2 python3 tools/tests/coop_tribe_tools.py [--nofix] [--type 1] [--dist 9]
"""
import argparse
import re
import time

from twoinst import Checks, shot, start


def tools(con):
    """[(index, type, (x, y, z))], {member id: held tool}"""
    out = con.call('coop.tribe tools')['out']
    ts, held = [], {}
    for line in out.splitlines():
        m = re.match(r'tool (\d+) \S+ serial=\d+ type=(-?\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            ts.append((int(m.group(1)), int(m.group(2)), tuple(float(m.group(i)) for i in (3, 4, 5))))
        m = re.match(r'held (\d+) owner=(\d+) tool=(-?\d+)', line)
        if m:
            held[int(m.group(1))] = int(m.group(3))
    return ts, held


def members(con):
    """{id: owner} of the player tribe (alive)"""
    own, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+) sel=\d+ hp=([\d.]+)', line)
        if m and player and m.group(1) != '0' and float(m.group(4)) > 0 and m.group(3) == '1':
            own[int(m.group(1))] = int(m.group(2))
    return own


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--type', type=int, default=1)
    ap.add_argument('--dist', type=float, default=9.0)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for con in (host, guest):
            for name in ('wobj.tribe_tool', 'tribe.tool_hold'):
                print(' ', con.call(f'coop.fix {name} off')['out'].strip())
    time.sleep(5)
    h0, _ = tools(host)
    g0, _ = tools(guest)
    print(f'  tools before: host {[(t, p) for _, t, p in h0]}')
    print(f'  tools before: guest {[(t, p) for _, t, p in g0]}')
    out = host.call(f'coop.tribe build {a.type} {a.dist}')['out'].strip()
    print('  host:', out)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    ck(bool(m), f'the host built a tool of type {a.type}')
    if not m:
        return ck.result()
    at = tuple(float(m.group(i)) for i in (1, 2, 3))
    hb, _ = tools(host)
    ck(any(t == a.type and dist(p, at) < 1 for _, t, p in hb), 'the host\'s game lists the new rack in the player tribe')
    rack = None
    t0 = time.time()
    while time.time() - t0 < 20 and rack is None:
        time.sleep(2)
        gt, _ = tools(guest)
        near = [(i, t, p) for i, t, p in gt if t == a.type and dist(p, at) < 3]
        rack = near[0] if near else None
    print(' ', guest.call('coop.wobj')['out'].strip().splitlines()[-1])
    ck(rack is not None, f'the guest\'s game has the host\'s rack (type {a.type} within 3 m of the host\'s): '
       f'{rack[2] if rack else "none"}')
    if rack is None:
        print('  guest tools:', tools(guest)[0])
        ck.faults(host=host, guest=guest)
        return ck.result()
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)
    ck(bool(own), f'the guest has members of its own ({len(own)} of {len(mem)})')
    if not own:
        return ck.result()
    _, hh = tools(host)
    pick = next((i for i in own if hh.get(i, 0) != a.type), own[0])
    out = guest.call(f'coop.tribe grab {pick} {rack[0]}')['out'].strip()
    print('  guest:', out)
    held_h = held_g = None
    t0 = time.time()
    while time.time() - t0 < 60:
        time.sleep(3)
        _, hh = tools(host)
        _, gh = tools(guest)
        held_h, held_g = hh.get(pick), gh.get(pick)
        if held_h == a.type and held_g == a.type:
            break
    m = re.search(r'tribe last order: (.*)', host.call('coop.status')['out'])
    print('  host order:', m.group(1) if m else '')
    ck(held_h == a.type, f'the guest\'s member #{pick} took the weapon in the host\'s game (tool {held_h})')
    print('  shots:', shot(host, 'tribe_tools_host.png'), shot(guest, 'tribe_tools_guest.png'))
    ck(held_g == a.type, f'the guest sees member #{pick} hold the weapon (its game\'s tool {held_g}, '
       f'{time.time() - t0:.0f} s)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
