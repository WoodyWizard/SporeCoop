#!/usr/bin/env python3
"""BUG-0838: after a reload of the world the allies mirror never puts the old world's cities into a planet of the new
one (Space, host).

A mirror unmirrored before the reload (a save on the way to the galaxy menu; here `allies remove`, the same unapply)
kept its snapshots: the old world's cPlanetRecord and the cCivData it had added there. The first apply in the new
world released the old record (our reference was its last: it went, with its cities) and added the kept cCivData
pointers into the new world's record of that planet.

The host on the space fixture mirrors an empire ("Zed", `allies fake`, the code path of a partner's colony) on a free
planet, unmirrors it (`allies remove`), goes to the galaxy menu and loads the fixture again, then applies the mirror
(`allies apply`). The new world's record of that planet must hold none of the cCivData of the old world's record
(`allies civ <planet>`), and no game faults (now or 20 s later).
SPORE_FIX_OFF=allies.snap_world must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_space_allies_snap.py"""
import re
import time

import fixtures
from coop_space_allies_reload import free_planets, mirror, sp, wait
from twoinst import Checks, start


def civ(con, planet):
    out = sp(con, 'allies civ', planet)
    m = re.search(r'rec=([0-9A-F]+) civ=(\S*)', out)
    return (m.group(1), [c for c in m.group(2).split(',') if c]) if m else (None, []), out


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    free = free_planets(host)
    ck(free, f'a free planet for the mirror: {free}')
    if not free:
        return ck.result()
    p = free[0]
    print('  fake:', sp(host, 'allies fake Zed', p).splitlines()[0], flush=True)
    pol, applied = mirror(host, 'Zed')
    (rec0, civ0), out = civ(host, p)
    print(f'  applied={applied}: {out.strip()}', flush=True)
    ck(applied and civ0, 'the mirror\'s cities are in the planet\'s record')
    print('  remove:', sp(host, 'allies remove').splitlines()[0], flush=True)
    print('  ' + civ(host, p)[1].strip(), flush=True)
    planet = fixtures.planet_of('space')
    print('  menu:', host.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {" ".join(planet.split("/"))}')['out'].strip(), flush=True)
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), 'the host is in Space again')
    time.sleep(5)
    (rec1, civ1), out = civ(host, p)
    print(f'  the new world before the apply: {out.strip()}', flush=True)
    sp(host, 'allies apply')
    wait(lambda: mirror(host, 'Zed')[1], 20)
    (rec2, civ2), out = civ(host, p)
    print(f'  after the apply: {out.strip()}', flush=True)
    old = [c for c in civ2 if c in civ0 and c not in civ1]
    ck(rec2 and rec2 != rec0, f'the new world has a record of its own for the planet ({rec0} -> {rec2})')
    ck(not old, f'the new world\'s record holds none of the old world\'s cities ({old})')
    time.sleep(20)
    sp(host, 'allies drop')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
