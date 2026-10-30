#!/usr/bin/env python3
"""Cell stage: the guest plays its own pool while the host is away, and the host's again after (BUG-0205).

GitHub #130 (BUG-0151 / BUG-0205, Cell part). While the host is in the main menu (or in an editor or
on a loading screen) its pool is not simulated. The guest was told "you play on alone", but every
creature on its screen was a mirror the mod keeps AI-locked (cell +0x16C), its game spawned nothing,
and the food it ate went to a host that dropped it.

1. The host leaves for the main menu (`coop.menu`); the guest counts it as away (host_away=1).
2. In the guest's game the creatures around it swim by their own AI: at least two of them moved
   more than 1 m within 6 s (`coop.pool npcs`, the positions in the guest's own pool).
3. The guest eats 10 points through the game's hooked AddFoodPoints (`coop.pool food 10`).
4. The host loads its world again: within 30 s every creature around the guest is a mirror of a live
   host cell again (the guest's own cells are gone) and there are more than 5 mirrors, and the host's
   game food total S+0x1C grew by the 10 points, the guest's the same, and the guest's total never
   fell below its own on the way (read every 0.2-0.5 s).

--editor: the host is in its own cell editor instead (its mating call by a real click, then onto the
mate with `coop.pool mate tp`; its editor request waits for the vote: `coop.vote go`; the guest
stays in its pool; cancel after the checks). It counts as away after 4 s (in Cell since
BUG-0205; BUG-0233 had left Cell out because nothing played on there).

SPORE_FIX_OFF=pool.away must FAIL (the frozen pool of 0.7.3.1: no creature moves, the 10 points are lost).

  SPORE_LANE=3 python3 tools/tests/coop_cell_away.py [--editor]
"""
import re
import sys
import time

from twoinst import Checks, lane_state, start
from coop_editor_desync import CANCEL, HEART, mode


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def food(con):
    m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def npcs(con):
    """local index -> (mirror, ailock, (x, y)) of the creatures around the own cell"""
    out = {}
    for m in re.finditer(r'^npc ([0-9A-F]{8}) mirror=(\d) ailock=(\d+) pos=\((-?[\d.]+) (-?[\d.]+)\)',
                         con.call('coop.pool npcs 80')['out'], re.MULTILINE):
        out[m.group(1)] = (int(m.group(2)), int(m.group(3)), (float(m.group(4)), float(m.group(5))))
    return out


def dist(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def away_line(con):
    m = re.search(r'^cell_away: .*$', con.call('coop.pool')['out'], re.MULTILINE)
    return m.group(0) if m else '(no cell_away line)'


def main():
    editor = '--editor' in sys.argv
    host, guest = start('cell', guest_creations=False) if editor else start('cell')
    ck = Checks()
    ck(wait(lambda: (num(guest.call('coop.pool')['out'], 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    ck(wait(lambda: food(host) is not None and food(host) == food(guest), 10), 'the same food total')
    h0, g0 = food(host), food(guest)
    before = npcs(guest)
    print(f'  food host {h0} guest {g0}; guest creatures {len(before)}, mirrors {sum(v[0] for v in before.values())}')

    # 1. the host leaves for the main menu (or opens its own cell editor)
    if editor:
        # the mating call (real click), then onto the mate (`coop.pool mate tp`: a click-swim to a mate
        # off the screen did not reach it); the mating cinematic takes ~10 s before the editor
        for _ in range(3):
            host.call('coop.pool mate button')
            time.sleep(1)
            host.call('click %d %d' % HEART)
            time.sleep(3)
            if 'alive=1' in host.call('coop.pool mate')['out']:
                break
        print('  host mate: ' + host.call('coop.pool mate tp')['out'].strip())
        # the host's editor request waits for the evolution vote (every player ready or 60 s): "Start now"
        reached = wait(lambda: 'Editor' in mode(host) or (host.call('coop.vote go') and False), 60, 2)
        if not ck(reached, f'the host\'s own cell editor opens ({mode(host).strip()})'):
            return ck.result()
    else:
        print(' ', host.call('coop.menu')['out'].strip())
    away = wait(lambda: 'host_away=1' in guest.call('coop.status')['out'], 90, 1)
    ck(away, 'the guest knows the host is away')
    time.sleep(3)
    # 2. the guest's creatures swim by their own AI
    n1 = npcs(guest)
    time.sleep(6)
    n2 = npcs(guest)
    both = [k for k in n1 if k in n2]
    moved = [k for k in both if dist(n1[k][2], n2[k][2]) > 1.0]
    locked = [k for k in n2 if n2[k][1]]
    print(f'  away: creatures {len(n1)} -> {len(n2)}, in both {len(both)}, moved >1 m {len(moved)}, '
          f'AI-locked {len(locked)}; {away_line(guest)}')
    # +0x16C is printed, not checked: between two frames it read 0 on the mirrors too (lane 3, pool.away
    # off), the mod sets it again every frame (guest_tick); the movement is what the player sees
    ck(len(n2) > 0, f'creatures around the guest while the host is away ({len(n2)})')
    ck(len(moved) >= 2, f'the guest\'s creatures swim by their own AI ({len(moved)} moved more than 1 m in 6 s)')
    # 3. the guest eats
    print(' ', guest.call('coop.pool food 10')['out'].strip())
    time.sleep(1)
    g1 = food(guest)
    print(f'  guest food {g0} -> {g1}; {away_line(guest)}')
    ck(g1 == g0 + 10, f'the guest\'s game counts its meal while the host is away ({g0} -> {g1})')

    # 4. the host is back
    if editor:
        host.call('click %d %d' % CANCEL)
    else:
        lst = lane_state()
        slot, name = lst.get('planet', 'Game0/Cadian').split('/')
        if lst.get('slot', 9) != 9:
            slot = f'Game{lst["slot"]}'
        print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    lows = []  # the guest's total while the host comes back: no dip below its own (BUG-0239's dip)

    def sample():
        f = food(guest)
        if f is not None:
            lows.append(f)
        return f
    back = wait(lambda: sample() is not None and 'host_away=0' in guest.call('coop.status')['out'] and
                'not in the cell stage' not in host.call('coop.pool')['out'], 150, 0.5)
    ck(back, 'the host is back in its pool')

    def host_pool_again():
        sample()
        mm = [int(h) for h in re.findall(r'^map (\d+) ', guest.call('coop.pool mirrormap')['out'], re.MULTILINE)]
        live = set(int(x, 16) for x in re.findall(r'idx=([0-9A-F]{8})', host.call('coop.cell list')['out']))
        dead = [h for h in mm if h not in live and 'no such cell' in host.call(f'coop.pool cell c{h}')['out']]
        own = [k for k, v in npcs(guest).items() if not v[0]]
        return (len(mm), dead, own)
    res = wait(lambda: (lambda r: r if r[0] > 5 and not r[1] and not r[2] else None)(host_pool_again()), 30, 1)
    n, dead, own = res or host_pool_again()
    print(f'  back: guest mirrors {n}, not live on the host {len(dead)}, own creatures left {len(own)}; {away_line(guest)}')
    ck(res is not None, f'the guest shows the host\'s pool again ({n} mirrors, {len(dead)} dead, {len(own)} own creatures)')
    ok = wait(lambda: food(host) == h0 + 10 and sample() == food(host), 15, 0.2)
    h2, g2 = food(host), food(guest)
    print(f'  food host {h0} -> {h2}, guest {g1} -> {g2}')
    ck(ok, f'the 10 points eaten while the host was away are in the host\'s total, the guest the same ({h0} -> {h2}, guest {g2})')
    low = min(lows) if lows else None
    print(f'  guest total while the host came back: low {low} ({len(lows)} reads)')
    ck(low is not None and low >= g1, f'the guest\'s total did not dip while the host came back (low {low}, its own {g1})')
    time.sleep(5)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
