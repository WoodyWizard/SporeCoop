#!/usr/bin/env python3
"""Cell stage sync, bug wave 2 (on a plain link; coop_cell_net.py runs the lossy-link cases):

  BUG-0088  a mirror the game removed is recreated: the guest's local -> host index map keeps one
            entry per mirror (`coop.pool zap` removes mirror cells outside the mod's path).
  BUG-0128  the guest's food total differs from the host's (a total it missed while loading): it
            gets the host's within 3 s although the host's total does not change.
  BUG-0041  after a small respawn a meal over a level line: the guest's cell gets the game's size
            for the new level (the host's cell grows by the same ratio), not up to twice that.
            BUG-1381: sizes are compared as target (+0xB4) x world scale (G+0x514C), read once the world
            scale reached its target: a level line with a world rescale (food 100: 0.5 -> 1.5) divides every
            cell's target by the ratio, so the raw target shrank (0.82 -> 0.49) although the cell grew.
  BUG-0070  the guest leaves right after a revive (still invulnerable) and during a knockout
            (translucent, 1 hp): its cell is vulnerable and opaque afterwards.
  BUG-0061  the guest left while knocked out and joins again with the same id: the host draws it
            opaque, not as knocked out.

SPORE_CELL_FIX=0 switches the fixes off on both games (`coop.pool fix all 0`): the old behaviour,
for the run that must FAIL.
"""
import os
import re
import time

from harness import pool, waiter
import lane
from twoinst import Checks, start, wait_guest_in


def num(out, key, cast=int):
    m = re.search(r'\b' + key + r'=(-?[\d.]+)', out)
    return cast(m.group(1)) if m else None


def food(con):
    return num(pool(con), 'food')


def size(con):
    """BUG-1381: the avatar's target size in units that a world rescale keeps (docs/re/cell.md#cell-world-scale:
    0xE6DB70 divides every cell's +0xB4 by the scale ratio, so target x G+0x514C is invariant), from one read, and
    whether the world scale has reached its target G+0x5150 (a level change eases it over ~3.5 s)"""
    out = pool(con)
    t = num(out, 'target', float)  # the avatar line's target= comes first
    m = re.search(r'world_scale=([\d.]+) target=([\d.]+)', out)
    if t is None or not m:
        return None, False
    ws, wt = float(m.group(1)), float(m.group(2))
    return t * ws, wt > 0 and abs(ws / wt - 1) < 0.01


wait = waiter(step=0.5)


def set_fixes(ck, host, guest, on):
    for con in (host, guest):
        out = con.call(f'coop.pool fix all {on}')['out']
        ck(f'purge={on}' in out and f'cellat={on}' in out, f'fixes set to {on}: {out.strip()}')


def ko_guest(host, guest):
    """the guest away from the host, then a lethal bite: knocked out"""
    guest.call('coop.pool tp 9 0')
    time.sleep(1)
    host.call('coop.pool hurt 1 50')
    return wait(lambda: num(pool(guest), 'downed') == 1, 5)


def rejoin(host, guest):
    guest.call(lane.JOIN)
    # the guest loads the host's world again (via the galaxy menu): its old cell counts as "in" until then
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ok = wait_guest_in(host, guest)
    # in the pool together: mirrors on the guest, each player's cell drawn on the other machine
    return ok and wait(lambda: (num(pool(guest), 'mirrors') or 0) and (num(pool(guest), 'partner_dist', float) or -1) >= 0
                       and re.search(r'^puppet 1 ', pool(host), re.MULTILINE), 60, 1)


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    host, guest = start('cell')
    ck = Checks()
    try:
        set_fixes(ck, host, guest, fix)
        ck(wait(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')

        # ---- BUG-0088: mirrors recreated after the game removed their cells ----
        g = pool(guest)
        print(f'  before: mirrors={num(g, "mirrors")} host_of={num(g, "host_of")}')
        print(' ', guest.call('coop.pool zap 4')['out'].strip())
        time.sleep(2)
        g = pool(guest)
        m, h = num(g, 'mirrors'), num(g, 'host_of')
        print(f'  after:  mirrors={m} host_of={h} created={num(g, "created")}')
        ck(m and h == m, f'BUG-0088: one map entry per mirror after the recreation (host_of {h}, mirrors {m})')

        # ---- BUG-0128: a total the guest missed comes again ----
        f = food(host)
        step = -1 if f % 50 >= 1 else 1  # never over a level line
        guest.call(f'coop.pool foodlocal {step}')
        time.sleep(0.3)
        print(f'  host food {f}, guest food after the missed total {food(guest)}')
        same = wait(lambda: food(guest) == food(host), 4)
        ck(food(host) == f, f'the host total did not change meanwhile ({f} -> {food(host)})')
        ck(same, f'BUG-0128: the guest gets the host total again without a change (guest {food(guest)}, host {food(host)})')

        # ---- BUG-0041: regrowth over a level line ----
        f = food(host)
        d = (10 - f % 50) % 50  # 10 points over a level line: 41 more cross the next one
        if d:
            guest.call(f'coop.pool food {d}')
        f1 = f + d
        # both totals there and both world scales settled (BUG-1381: a state, not a sleep)
        wait(lambda: (food(host) or 0) >= f1 and food(guest) == food(host) and size(host)[1] and size(guest)[1], 10)
        (h0, hs0), (g0, gs0), lv0 = size(host), size(guest), food(host) // 50  # level line every 50
        print(f'  food {food(host)} level {lv0}: host size {h0:.3f}, guest size {g0:.3f} (target x world scale; '
              f'settled {hs0}/{gs0}; {num(pool(host), "target", float)} raw)')
        print(' ', guest.call('coop.pool small')['out'].strip(), 'body', num(pool(guest), 'body', float))
        guest.call('coop.pool food 41')
        wait(lambda: (food(host) or 0) >= f1 + 41 and food(guest) == food(host) and size(host)[1] and size(guest)[1], 15)
        (h1, hs1), (g1, gs1), lv1 = size(host), size(guest), food(host) // 50
        print(f'  food {food(host)} level {lv1}: host size {h1:.3f}, guest size {g1:.3f} body {num(pool(guest), "body", float)} '
              f'(settled {hs1}/{gs1}; {num(pool(host), "target", float)} raw)')
        ck(lv1 == lv0 + 1 and h0 and h1 > h0 * 1.1, f'over a level line ({lv0} -> {lv1}): the host cell grew ({h0:.3f} -> {h1:.3f})')
        want = g0 * h1 / h0 if h0 else 0
        ck(want and g1 and abs(g1 / want - 1) < 0.05,
           f'BUG-0041: the guest cell has the size of its new level (size {g1:.3f}, {want:.3f} from the host ratio)')

        # ---- BUG-0070 case A: leave within 2 s after a revive ----
        time.sleep(4)  # regen, no invulnerability left
        ck(ko_guest(host, guest), 'the guest is knocked out')
        revived = None
        for _ in range(12):
            host.call('coop.pool tp 1 0')
            time.sleep(0.25)
            g = pool(guest)
            if num(g, 'downed') == 0:
                revived = g
                break
        ck(revived and num(revived, 'invuln') == 1, 'revived by the host (invulnerable for 2 s)')
        guest.call('coop.leave')
        time.sleep(4)
        g = pool(guest)
        print(' ', g.splitlines()[0])
        ck(num(g, 'invuln') == 0, f'BUG-0070: after the leave the cell is vulnerable again (invuln={num(g, "invuln")})')

        # ---- BUG-0070 case B and BUG-0061: leave during a knockout, join again ----
        ck(rejoin(host, guest), 'the guest joined again')
        guest.call(f'coop.pool fix all {fix}')
        time.sleep(4)
        guest.call('coop.pool invuln 0')  # left over by case A without the fix: the knockout below needs a bite
        ck(ko_guest(host, guest), 'the guest is knocked out again')
        time.sleep(1)
        print(' ', [l for l in pool(host).splitlines() if l.startswith(('ko[', 'puppet '))])
        guest.call('coop.leave')
        time.sleep(2)
        g = pool(guest)
        print(' ', g.splitlines()[0], '|', g.splitlines()[1][:40])
        ck(num(g, 'avatar_opacity', float) == 1.0 and (num(g, 'hp') or 0) >= 1 and num(g, 'invuln') == 0,
           f'BUG-0070: the knockout ended with the session (opacity {num(g, "avatar_opacity", float)}, '
           f'invuln {num(g, "invuln")}, hp {num(g, "hp")})')
        ck(rejoin(host, guest), 'the guest joined again (same id)')
        time.sleep(4)
        hp = pool(host)
        rows = [l for l in hp.splitlines() if l.startswith(('ko[', 'puppet '))]
        print(' ', rows)
        op = re.search(r'^puppet 1 op=([\d.]+)', hp, re.MULTILINE)
        ck(op and float(op.group(1)) == 1.0 and not re.search(r'^ko\[1\]=1', hp, re.MULTILINE),
           f'BUG-0061: the host draws the guest up (puppet opacity {op.group(1) if op else None})')
    finally:
        for con in (host, guest):
            con.call('coop.pool fix all 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
