#!/usr/bin/env python3
"""Cell: a knocked-out cell stays at its place in the world through a level change (BUG-0798).

The knockout holds the cell at the spot of its knockout every frame (downed_tick -> stop_at). A level
change in the 10 s rescales each game's world over ~3.5 s (every position times old / new world scale
G+0x514C about the origin, docs/re/cell.md#cell-world-scale), but the held spot was kept in the metres
of the knockout's moment: after the change the cell lay 3x (level 1 -> 2) farther from the centre than
the world around it, far from the partner and the food, and nobody could revive it.

1. Both games play; the guest is 6 m from the host's cell (both idle, nobody helps: the revive needs
   ~3 m).
2. The guest is knocked out (`coop.pool selfhit 50`: the lethal bite becomes a knockout, its game says
   downed=1).
3. The host's food goes up until its level changes (the shared total: the guest's world rescales too).
4. In the GUEST's own game, 4 s after the change, while it is still knocked out: its cell's position
   times its world scale (the place in the world, unchanged by a rescale) is within 1.5 m (in its
   metres) of where it was at the knockout, and the host's cell (the puppet the guest's game draws)
   is still about 6 m away (not tens).
5. The host swims right next to the guest (`coop.pool tp 1 0 <guest id>`, next to the guest's cell as
   the host's game draws it): the guest's own game revives it (downed=0, revives +1) before its 10 s end.

SPORE_FIX_OFF=cell.down_scale must FAIL at 4 (the cell held at the old metres: 2-3x the place).

  SPORE_LANE=2 python3 tools/tests/coop_cell_ko_scale.py
"""
import math
import re
import time

from coop_cell_ko_food import pool, wait_downed
from coop_cell_puppet_scale import level, playing, scalepos, wait
from twoinst import Checks, start


def revives(con):
    m = re.search(r'\brevives=(\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'
    guest.call('coop.pool tp 6 0')  # 6 m from the host's cell, both still
    time.sleep(3)
    g0 = scalepos(guest)
    print(f'  guest before: {g0}', flush=True)
    r0 = revives(guest)
    print('  guest: ' + guest.call('coop.pool selfhit 50')['out'].strip(), flush=True)
    ok = wait_downed(guest, 1)
    ck(ok, 'the guest is knocked out (its own game: downed=1)')
    if not ok or not g0:
        return ck.result()
    k0 = scalepos(guest)  # the spot it is held at, its scale then
    l0 = level(host)
    t0 = time.time()
    for _ in range(8):  # food until the level changes (the shared total)
        host.call('coop.pool food 30')
        time.sleep(0.4)
        if level(host) != l0:
            break
    l1 = level(host)
    ck(l0 is not None and l1 is not None and l1 > l0, f'the level changed ({l0} -> {l1}) {time.time() - t0:.1f} s after the knockout')
    trail = []
    while time.time() - t0 < 6.0:  # the rescale (~3.5 s on each game)
        trail.append(scalepos(guest))
        time.sleep(0.25)
    k1 = scalepos(guest)
    still = pool(guest)['downed'] == 1
    print(f'  guest held at: {k0} -> {k1} (still knocked out: {still})', flush=True)
    for x in trail[::4]:
        print(f'    {x}', flush=True)
    ck(still, 'the guest is still knocked out after the level change')
    if k0 and k1:
        s0, (x0, y0), _ = k0
        s1, (x1, y1), p1 = k1
        ck(abs(s1 - s0) > 0.1 * s0, f'the guest\'s world was rescaled ({s0:.3f} -> {s1:.3f})')
        # the place in the world (pos x scale) in the guest's metres now
        off = math.dist((x0 * s0, y0 * s0), (x1 * s1, y1 * s1)) / s1
        ck(off < 1.5, f'the knocked-out cell is where it lay in the world ({off:.2f} m from it after the rescale, '
           f'pos ({x0:.1f} {y0:.1f}) at scale {s0:.3f} -> ({x1:.1f} {y1:.1f}) at {s1:.3f})')
        if 0 in p1:
            d = math.dist((x1, y1), p1[0])
            ck(d < 12, f'the host\'s cell is still next to it in the guest\'s game ({d:.1f} m)')
    print('  host: ' + host.call(f'coop.pool tp 1 0 {gid}')['out'].strip(), flush=True)
    revived = wait(lambda: pool(guest)['downed'] == 0 and (revives(guest) or 0) > (r0 or 0), 6, 0.25)
    print(f'  guest revives {r0} -> {revives(guest)}, downed={pool(guest)["downed"]}', flush=True)
    ck(revived, 'the host next to the guest\'s cell revives it in the guest\'s own game')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
