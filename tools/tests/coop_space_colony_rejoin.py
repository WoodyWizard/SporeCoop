#!/usr/bin/env python3
"""Space: a guest's own colony survives its rejoin (BUG-0195).

Every player flies in a copy of the host's save: a colony the guest founds lives only in the guest's
game, and a guest that left and joined again loaded the host's world without it. The host keeps each
guest's own colonies (space_map.cpp) and gives them back when it rejoins.

1. Both play the `space` fixture. The guest makes a colony of its empire on a free planet of the home
   system (`coop.space allies colonize`: the record-level colony of docs/re/space.md 15.2; the test's
   subject is the rejoin, not the colony tool, which coop_space_map.py fires).
2. The host's roster of the guest has it as the guest's own colony (what the host's galaxy map draws).
3. The guest leaves, joins again and loads the host's world.
4. In the guest's game the planet is a colony of the guest's empire again (the game's planet record:
   tech 5, a cCivData of the guest's political id), and the host's roster of the guest has it as its own.
PASS: 1 to 4. SPORE_FIX_OFF=space.keep_colonies: the planet is no colony in the rejoined guest's game (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_space_colony_rejoin.py
"""
import re
import time

import lane
from coop_space_map import no_movie, rosters, sp, wait
from twoinst import Checks, start, wait_guest_in


def planet_line(con, planet):
    """(tech, [civ pols]) of a planet in this game's records"""
    for line in sp(con, 'allies planet', planet).splitlines():
        m = re.match(r"\s+planet %s '[^']*' tech=(-?\d+)(.*)$" % planet, line)
        if m:
            return int(m.group(1)), re.findall(r'civ\(pol=([0-9A-F]+)', m.group(2))
    return None, []


def epoch(con):
    m = re.search(r'world=(\d+) open=1', sp(con, 'world'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    out = sp(guest, 'allies colonize')
    print('  guest:', out.splitlines()[0] if out else '-', flush=True)
    m = re.match(r'colonized ([0-9A-F]+)', out)
    ck(m is not None, 'the guest made a colony of its empire in the home system')
    if not m:
        return ck.result()
    planet = m.group(1)
    tech, civs = planet_line(guest, planet)
    print(f'  guest: planet {planet} tech={tech} civ pols {civs}', flush=True)
    gpol = civs[-1] if civs else None

    def host_has():
        return [c for c in rosters(host).get(1, {}).get('colonies', []) if c[0] == planet and c[2]]
    ck(wait(host_has, 30) is not None, f'the host\'s roster of the guest has colony {planet} as the guest\'s own')
    print('  host:', sp(host, 'map kept').replace('\n', ' | '), flush=True)

    guest.call('coop.leave')
    ck(wait(lambda: 1 not in rosters(host), 15) is not None, 'the guest left: the host dropped its roster')
    time.sleep(2)
    tech0, _ = planet_line(guest, planet)
    e0 = epoch(guest)
    guest.call(lane.JOIN)
    # the rejoin loads the host's world: the guest's Space world epoch moves on (the old world answers
    # coop.avatar until the load starts, so wait_guest_in alone returned before it)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'the guest loaded the host\'s world again (Space world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back in the host\'s world')
    no_movie(guest)

    def back():
        t, c = planet_line(guest, planet)
        return (t, c) if t == 5 and gpol in c else None
    r = wait(back, 45, 2)
    tech, civs = planet_line(guest, planet)
    print(f'  rejoined guest: planet {planet} tech={tech} civ pols {civs} (before the rejoin: tech {tech0}); '
          + sp(guest, 'map kept').splitlines()[0], flush=True)
    ck(r is not None, f'in the rejoined guest\'s game {planet} is a colony of its empire again (tech {tech}, civs {civs}, '
                      f'its pol {gpol})')
    ck(wait(host_has, 20) is not None, f'the host\'s roster of the rejoined guest has {planet} as its own colony again')
    print('  host:', sp(host, 'map kept').replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
