#!/usr/bin/env python3
"""Space: an unvisited NPC star gets the same planets in both games (BUG-0185).

RequirePlanetsForStar loads the star's species profile before it generates the planets; a first
load draws from the game's random generator. Those draws used to land on the private, star-seeded
generator of the planet generation, so the planets depended on whether that game had loaded the
profile before (the galaxy view, an empire's colour, a contact do): 6 or 10 planets of 0046101B in
`coop_space_jump.py` runs (docs/re/space.md#space-seed-profile). Forced here: the host loads the
profile of an unvisited NPC star (`coop.space profile`), the guest does not; then the guest jumps
there and the host follows. Both games must have the same planets (the PlanetIDs the game loaded).

  SPORE_LANE=9 python3 tools/tests/coop_space_seed_profile.py [--without-fix]

--without-fix (or SPORE_FIX_OFF=space.seed_profile): `coop.fix space.seed_profile off` on both
games: the planets differ (FAIL).
"""
import re
import sys
import time

from coop_space_jump import settle, sp
from coop_space_seed_thread import jump, planets
from twoinst import Checks, start


def fresh_npc(con, n=8):
    """unvisited stars of NPC empires whose planets were not generated yet"""
    out = sp(con, 'empire', 'near', n)
    return [int(m.group(1), 16) for m in re.finditer(
        r'^npc ([0-9A-F]{8}) pol=\S+ d=\S+ contact=0 flags=\S+ visited=0 planets=0', out, re.M)]


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:  # (SPORE_FIX_OFF=space.seed_profile does the same through twoinst.start)
        for con in (host, guest):
            print(' ', con.call('coop.fix space.seed_profile off')['out'].strip())
    stars = [s for s in fresh_npc(host) if s in fresh_npc(guest)]
    print('  fresh NPC stars:', [f'{s:08X}' for s in stars])
    if not ck(len(stars) >= 1, 'an NPC star near home whose planets nobody generated'):
        return ck.result()
    # 0046101B (an empire's home star, 6 or 10 planets) shows it; a star with one planet may not
    star = 0x0046101B if 0x0046101B in stars else stars[0]
    print('  host:', sp(host, 'profile', f'{star:08X}'))  # only the host's game has it loaded
    ck(jump(guest, 'guest', star), f'the guest jumped to {star:08X}')
    ck(jump(host, 'host', star), 'the host followed')
    settle(host, 'host')
    settle(guest, 'guest')
    hp, gp = planets(host, star), planets(guest, star)
    print(f'  host {[f"{i:08X}" for i in hp]}\n  guest {[f"{i:08X}" for i in gp]}')
    ck(hp and hp == gp, f'both games made the same planets of {star:08X} ({len(hp)} and {len(gp)})')
    ck.faults(host=host, guest=guest)
    if without:
        for con in (host, guest):
            con.call('coop.fix space.seed_profile on')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
