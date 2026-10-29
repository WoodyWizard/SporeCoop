#!/usr/bin/env python3
"""BUG-0231 (GitHub #165), the Civ part: a guest that plays another world of the host's Civ stage
applies nothing of the host's vehicle and city state to its own planet, and its own vehicles keep
their own AI.

0.7.3.0 dropped the host's Creature, Cell and Tribe state for such a guest (BUG-0130), but not
MSG_CIV_STATE / MSG_CIV_BUILD / MSG_CIV_ALARM: the guest's Civ module bound the guest's own
vehicles as mirrors of the host's and drove them, killed the ones it could not bind after a full
cycle, and founded cities up to the host's count.

  1. the usual Civ session, then the guest leaves and plays a world of its own: a copy of the
     fixture planet under another name (Otherworld), coop.worldfix autoload 0 (auto_load_world=0)
  2. the guest joins the host and stays in Otherworld for 40 s
  3. on the guest's game (coop.census): none of its vehicles becomes a mirror of a host vehicle,
     none of the vehicles alive before the join dies, and it is still in Otherworld

  coop_civ_other_world.py [--nofix]   (coop.fix core.world_gate off: must FAIL)
"""
import glob
import os
import re
import shutil
import sys
import time

import lane
from census import dump
from twoinst import Checks, start


def world_of(con):
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def main():
    nofix = '--nofix' in sys.argv
    host, guest = start('civ')
    ck = Checks()
    for con in (host, guest):
        print(' ', con.call(f'coop.fix core.world_gate {"off" if nofix else "on"}')['out'].strip())
    print(' ', guest.call('coop.leave')['out'].strip())
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Arigine.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Arigine.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        if world_of(guest) == 'Otherworld.spo' and 'Civ' in guest.call('game.mode')['out']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f'the guest plays a world of its own ({world_of(guest)})')
    guest.call('wait.frames 300 60000')
    time.sleep(8)
    c0 = dump(guest)
    alive0 = {r['id'] if r['id'] >= 0 else i: r for i, r in enumerate(c0.rows) if r['kind'] == 'v' and r['alive']}
    n0 = len(alive0)
    print(f'  guest alone: {n0} vehicles alive, census complete={c0.complete}')
    ck(n0 > 0, 'the guest\'s own planet has vehicles')

    print(' ', guest.call(lane.JOIN)['out'].strip())
    mirrors_max, alive_min = 0, n0
    t0 = time.time()
    while time.time() - t0 < 40:
        c = dump(guest)
        vs = [r for r in c.rows if r['kind'] == 'v']
        mirrors_max = max(mirrors_max, len([r for r in vs if r['own'] == 'mirror']))
        alive_min = min(alive_min, len([r for r in vs if r['alive']]))
        time.sleep(1)
    st = guest.call('coop.status')['out']
    print(f'  during 40 s in the session: guest vehicles bound as mirrors (max) {mirrors_max}, alive (min) {alive_min} of {n0}')
    print('  guest:', ' | '.join(l for l in st.splitlines() if 'elsewhere' in l)[:200])
    ck('elsewhere=1' in st, 'the guest knows the host plays another world')
    ck(mirrors_max == 0, f'none of the guest\'s own vehicles is driven as a host vehicle ({mirrors_max})')
    ck(alive_min >= n0, f'none of the guest\'s own vehicles died ({alive_min} of {n0} alive)')
    ck(world_of(guest) == 'Otherworld.spo', f'the guest is still in its own world ({world_of(guest)})')

    for con in (host, guest):
        con.call('coop.fix core.world_gate on')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
