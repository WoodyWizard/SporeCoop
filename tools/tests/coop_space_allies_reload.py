#!/usr/bin/env python3
"""BUG-0038: after a reload of the galaxy the allies mirror starts over with a cEmpire of the new
galaxy, never the old galaxy's object at its old political id (where a real NPC empire of the new
galaxy may live).

 1. The host has a mirror empire ("Zed", `coop.space allies fake`, the same code path as a
    partner's colony) on a free planet: pol P, cEmpire at A in the game's empire map.
 2. The host goes to the galaxy menu and loads the fixture again while the session runs.
 3. The mirror is applied again. The game's empire map must hold a new cEmpire for it (not A), with
    its one star, and a further empire made in this galaxy (a second mirror) must get its own id.
Before the fix the old galaxy's object A went back into the new map.

  coop_space_allies_reload.py [--without-fix] [--same-address]
  (--without-fix: `allies reloadfix 0`, so it FAILS; --same-address, BUG-0225: before the first
   apply after the load `allies samegalaxy` sets the stored player cEmpire to the new one, as an
   address reuse would; SPORE_FIX_OFF=allies.reload_id then FAILS)"""
import re
import sys
import time

import fixtures
from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def mirror(con, name):
    m = re.search(r"mirror '%s' id=\d+ \w+ pol=([0-9A-F]+) applied=(\d)" % name, sp(con, 'allies list'))
    return (m.group(1), m.group(2) == '1') if m else (None, False)


def empire(con, pol):
    out = sp(con, 'allies empire', pol)
    m = re.search(r"empire \S+ '(.*)' stars=(\d+) .* at=([0-9A-F]+)", out)
    return (m.group(1), int(m.group(2)), m.group(3)) if m else None


def free_planets(con):
    out = []
    for s in [int(x) for x in re.findall(r'free (\d+)', sp(con, 'allies free'))][1:6]:
        p = re.findall(r"planet ([0-9A-F]+) '[^']*' tech=([0-3])\b", sp(con, 'allies star', s))
        if p:
            out.append(p[0][0])
    return out


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        print(' ', sp(host, 'allies reloadfix 0'))
    free = free_planets(host)
    ck(len(free) >= 2, f'free planets for two mirrors: {free}')
    print('  fake:', sp(host, 'allies fake Zed', free[0]).splitlines()[0])
    pol, applied = mirror(host, 'Zed')
    e0 = empire(host, pol) if pol else None
    print(f'  before: pol {pol} applied={applied} empire {e0} {sp(host, "allies nextpol")}')
    ck(applied and e0 and e0[0] == "Zed's empire", 'the mirror empire is in the game')

    planet = fixtures.planet_of('space')
    print('  menu:', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {" ".join(planet.split("/"))}')['out'].strip())
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), 'the host is in Space again')
    time.sleep(5)
    print(f'  after the load: {sp(host, "allies nextpol")}')
    if '--same-address' in sys.argv:  # BUG-0225: the new player cEmpire at the old one's address
        print(' ', sp(host, 'allies samegalaxy'))
    sp(host, 'allies apply')
    pol2, applied2 = wait(lambda: (lambda m: m if m[1] else None)(mirror(host, 'Zed')), 20) or (None, False)
    e1 = empire(host, pol2) if pol2 else None
    print(f'  after: pol {pol2} applied={applied2} empire {e1}')
    ck(applied2 and e1 and e1[0] == "Zed's empire" and e1[1] == 1, f'the mirror is applied again with its one star: {e1}')
    ck(e1 and e0 and e1[2] != e0[2], f'it is a cEmpire of the new galaxy, not the old one ({e0 and e0[2]} -> {e1 and e1[2]})')
    # a further empire of this galaxy gets an id of its own; the mirror's stays its own
    print('  fake 2:', sp(host, 'allies fake Yan', free[1]).splitlines()[0])
    q, _ = mirror(host, 'Yan')
    e2 = empire(host, pol2) if pol2 else None
    ck(q and q != pol2 and e2 and e2[0] == "Zed's empire",
       f'a new empire got id {q}, the mirror keeps {pol2}: {e2}')

    sp(host, 'allies drop')
    sp(host, 'allies reloadfix 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
