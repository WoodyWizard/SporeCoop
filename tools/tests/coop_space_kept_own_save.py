#!/usr/bin/env python3
"""Space: the host's list of a guest's kept colonies is not applied in a Space save of the guest's own (BUG-1488, audit
SPC-14 c).

The host keeps each guest's own colonies of the co-op galaxy (BUG-0195) and sends them when the guest is back (K_KEPT,
on its first roster). The guest applied the list in the first Space frame of any world it loaded since its join,
checked only by the world epoch: a guest that joined from the menu and then played a Space save of its own (supported:
BUG-0709, BUG-0816) made those planets its colonies in its own save (which it then saves).

1. Both play the `space` fixture. The guest makes a colony of its empire on a free planet P of the home system
   (`coop.space allies colonize`); the host's roster has it as the guest's own (the host keeps it).
2. The guest leaves, goes to the galaxy menu, joins again from there without loading the host's world
   (`coop.worldfix autoload 0`), and loads a copy of the share save under another name (own_world.load_own).
3. Over 30 s in that own save (its roster reaches the host, the host sends its K_KEPT): P must stay free there (the
   game's planet record: tech below 5, no cCivData of the guest's empire), and the guest's list waits (`map kept`
   pending > 0, own_held > 0).
   0.7.16.1 (SPORE_FIX_OFF=space.kept_own_save): P is the guest's colony in its own save (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_kept_own_save.py
"""
import re
import time

import lane
import own_world
from coop_space_colony_rejoin import planet_line
from coop_space_map import no_movie, rosters, sp, wait
from twoinst import Checks, keyframe, start


def kept(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con, 'map kept'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    out = sp(guest, 'allies colonize')
    m = re.match(r'colonized ([0-9A-F]+)', out or '')
    ck(m is not None, f'the guest made a colony of its empire in the home system ({(out or "-").splitlines()[0]})')
    if not m:
        return ck.result()
    planet = m.group(1)
    _, civs = planet_line(guest, planet)
    gpol = civs[-1] if civs else None

    def host_has():
        return [c for c in rosters(host).get(1, {}).get('colonies', []) if c[0] == planet and c[2]]
    ck(wait(host_has, 30) is not None, f'the host keeps colony {planet} as the guest\'s own')
    print('  host:', sp(host, 'map kept').replace('\n', ' | '), flush=True)

    # 2. leave, the menu, join without the host's world, the own save
    guest.call('coop.leave')
    ck(wait(lambda: 1 not in rosters(host), 15) is not None, 'the guest left')
    print('  menu:', guest.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 90, 1), 'the guest is in the galaxy menu')
    guest.call('wait.frames 240 90000')
    guest.call('coop.worldfix autoload 0')
    print('  join:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    ck(wait(lambda: own_world.connected(guest), 60, 1), 'the guest joined from the menu')
    time.sleep(5)
    ok = own_world.load_own(guest, 'Madas.spo', 'Keptown', 'Space')
    ck(ok, 'the guest plays a Space save of its own')
    if not ok:
        guest.call('coop.worldfix autoload 1')
        return ck.result()
    no_movie(guest)

    # 3. the host's list must not land in the own save
    worst = None
    t0 = time.time()
    while time.time() - t0 < 30:
        t, c = planet_line(guest, planet)
        if t == 5 and gpol in c:
            worst = (t, c)
        time.sleep(2)
    t, c = planet_line(guest, planet)
    print(f'  own save: planet {planet} tech={t} civ pols {c} | guest ' + sp(guest, 'map kept').splitlines()[0]
          + ' | host ' + sp(host, 'map kept').splitlines()[0], flush=True)
    ck(worst is None, f'{planet} stays free in the guest\'s own save (seen {worst}, now tech {t})')
    ck(kept(guest, 'own_held') > 0 and kept(guest, 'pending') > 0,
       'the host\'s list reached the guest and waits for the host\'s world')
    keyframe('kept_own_save', 'the guest in a Space save of its own: the galaxy map shows no colony of the co-op galaxy '
             f'at {planet}', players={'host': host, 'guest': guest})
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
