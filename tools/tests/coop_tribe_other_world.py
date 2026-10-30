#!/usr/bin/env python3
"""BUG-0231 (GitHub #165), the Tribe part: a guest that plays another world of the host's Tribe
stage plays its own tribe there (its own AI and orders).

0.7.3.0 dropped the host's tribe state for such a guest (BUG-0130) but still treated it as a
mirror guest: the AI of every citizen was skipped and its orders went to the host with ids of an
empty table, so its own tribe stood still.

  1. the usual Tribe session, then the guest leaves and plays a world of its own: a copy of the
     fixture planet under another name (Otherworld), coop.worldfix autoload 0
  2. the guest joins the host and stays in Otherworld (coop.status: elsewhere=1)
  3. the guest selects one of its own members and orders it to walk 20-80 m (coop.tribe select_own / walk:
     the game's WalkSelectedTo, as a right-click): the member's position in the guest's game moves
     by more than 8 m

  coop_tribe_other_world.py [--nofix]   (coop.fix core.world_gate off: must FAIL)
"""
import glob
import os
import re
import shutil
import sys
import time

import lane
from coop_tribe import dist
from twoinst import Checks, start


def world_of(con):
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def tribes(con):
    """[(is_player, [(x, y, z), ...])] in the game's order (ids are 0 in a world of the guest's own)"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        if re.match(r'tribe \d+', line):
            out.append(('(player)' in line, []))
            continue
        m = re.match(r'\s+#\d+ \S+ owner=-?\d+ age=\d+ sel=\d+ hp=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and out:
            out[-1][1].append(tuple(float(m.group(i)) for i in (1, 2, 3)))
    return out


def own(con):
    return next((ms for pl, ms in tribes(con) if pl), [])


def main():
    nofix = '--nofix' in sys.argv
    host, guest = start('tribe')
    ck = Checks()
    for con in (host, guest):
        print(' ', con.call(f'coop.fix core.world_gate {"off" if nofix else "on"}')['out'].strip())
    print(' ', guest.call('coop.leave')['out'].strip())
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Zorispe.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Zorispe.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        if world_of(guest) == 'Otherworld.spo' and 'Tribe' in guest.call('game.mode')['out']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f'the guest plays a world of its own ({world_of(guest)})')
    guest.call('wait.frames 300 60000')
    time.sleep(6)

    print(' ', guest.call(lane.JOIN)['out'].strip())
    t0 = time.time()
    st = ''
    while time.time() - t0 < 30:
        st = guest.call('coop.status')['out']
        if 'elsewhere=1' in st:
            break
        time.sleep(1)
    ck('elsewhere=1' in st, 'the guest knows the host plays another world')
    time.sleep(4)
    mine = own(guest)
    if not ck(mine, f'the guest has members of its own tribe ({len(mine)})'):
        return ck.result()
    p0 = mine[0]
    others = [p for pl, ms in tribes(guest) if not pl for p in ms]
    cands = sorted((dist(p0, p), p) for p in others)
    tgt = next((p for d, p in cands if 20 < d < 80), cands[0][1] if cands else None)
    if not ck(tgt is not None, 'a target to walk to'):
        return ck.result()
    for i in range(len(mine)):
        guest.call(f'coop.tribe select_own {i} 0')
    print(' ', guest.call('coop.tribe select_own 0 1')['out'].strip())
    print(' ', guest.call('coop.tribe walk %.1f %.1f %.1f' % tgt)['out'].strip(), f'({dist(p0, tgt):.0f} m)')
    time.sleep(10)
    now = own(guest)
    p1 = now[0] if now else p0
    moved = dist(p0, p1)
    print(f'  own member 0 moved {moved:.1f} m in the guest\'s own world')
    ck(moved > 8, f'the guest\'s own member walks while it plays its own world ({moved:.1f} m)')
    ck(world_of(guest) == 'Otherworld.spo', f'the guest is still in its own world ({world_of(guest)})')

    for con in (host, guest):
        con.call('coop.fix core.world_gate on')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
