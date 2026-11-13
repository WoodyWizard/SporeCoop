#!/usr/bin/env python3
"""A save loaded in game plays its cinematics: a Civ capture after the host's in-game load from Tribe (BUG-1073).

The live Creature -> Tribe change turns the game's cinematics off after CRG2TRG (cCinematicManager_SetEnabled(0)) and
the Tribe start turns them on again; the mod's own reload of the host's Tribe right after that change
(tribe.host_reload) could come first, and a load does not turn them on: the whole Tribe ran without a cinematic, and
so did every stage loaded later in that game (campaign_tribe_end lab 20261113-133357 lane 5: the flag 0 from the
CRG2TRG at 13:40:52 on, through the galaxy menu and the reload; the four jump campaigns of fix/w49-tests). In Civ a
capture's cinematic never played and the city never changed hands (its ChangeOwner waits for the cinematic's end).

1. The tribe fixture, two games. The host's cinematics are turned off as that change leaves them (`coop.cine off`, the
   game's own SetEnabled(0)).
2. The host loads the civ_npc fixture in game (`coop.load`, the campaign's jump: the galaxy menu, then the load; the
   guest follows with the host's world).
3. In Civ: the game's flag (`coop.cine`), then the capture of another nation's city by the game's cCity::Capture with
   its cinematic (instant 0), as a siege, a conversion or a purchase ends. Within 40 s the city must be the player
   civilization's in the host's game AND in the guest's game (`coop.civ`: the city's political id), and the capture
   cinematic must have played on the host.
PASS: 3. SPORE_FIX_OFF=game.cinematics_on_load (the load keeps them off, 0.7.13): no cinematic, the city stays the
other nation's (FAIL).

  SPORE_LANE=5 python3 tools/tests/coop_cine_load.py
"""
import re
import time

import fixtures
import lane
from twoinst import Checks, start


def cine(con):
    out = con.call('coop.cine')['out']
    m = re.search(r'enabled=(-?\d+) state=(\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (-1, 0), out.strip()


def cities(con):
    """(player pol, [(ptr, pol) of other nations' cities]) of this game's `coop.civ`"""
    out = con.call('coop.civ')['out']
    mine = re.search(r'city -?\d+ (\S+) pol=([0-9A-F]+) \(player\)', out)
    others = [(m.group(1), m.group(2)) for m in re.finditer(r'city -?\d+ (\S+) pol=([0-9A-F]+) owner=', out)]
    return (mine.group(2) if mine else None), others


def wait_mode(con, what, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        if what in con.call('game.mode')['out']:
            return True
        time.sleep(1)
    return False


def main():
    host, guest = start('tribe')
    ck = Checks()
    print('  host: ' + host.call('coop.cine off')['out'].strip(), flush=True)
    (en, _), line = cine(host)
    ck(en == 0, f'the host\'s cinematics are off, as the Creature -> Tribe change leaves them ({line})')

    fx, slot = 'civ_npc', 10
    planet = fixtures.planet_of(fx).split('/')[1]
    lane.sh('fixture', 'slot', fx, lane.HOST_PROFILE, str(slot))
    print('  host: ' + host.call(f'coop.load Game{slot} {planet}')['out'].strip(), flush=True)
    left = False
    t0 = time.time()
    while time.time() - t0 < 180:
        mode = host.call('game.mode')['out']
        left |= 'Tribe' not in mode
        if left and 'Civ' in mode:
            break
        time.sleep(0.5)
    ck(left and 'Civ' in host.call('game.mode')['out'], 'the host loaded the civ_npc fixture in game (Civ)')
    ck(wait_mode(guest, 'Civ', 120), 'the guest follows into Civ with the host\'s world')
    host.call('wait.frames 300 90000')
    time.sleep(8)
    (en, _), line = cine(host)
    print(f'  host after the load: {line}', flush=True)

    pol, others = cities(host)
    others = [o for o in others if o[1] != pol]
    ck(pol is not None and others, f'the host plays a player civilization ({pol}) with other nations\' cities ({len(others)})')
    if not (pol and others):
        return ck.result()
    gpol, gothers = cities(guest)
    g_before = sum(1 for o in gothers if o[1] != gpol)
    ptr = others[0][0] if others[0][0].startswith('0x') else '0x' + others[0][0]
    print(f'  host: cCity::Capture of city {ptr} (pol {others[0][1]}) for {pol}, with its cinematic', flush=True)
    host.call(f'call Simulator::cCity::Capture thiscall {ptr} 0x{pol} 0 0 0 0')
    played, took, g_took = False, False, False
    t0 = time.time()
    while time.time() - t0 < 40 and not (took and g_took):
        (_, state), _ = cine(host)
        played |= state != 0
        mine, rest = cities(host)
        took = all(o[0] != others[0][0] for o in rest if o[1] != mine)
        gp, grest = cities(guest)
        g_took = sum(1 for o in grest if o[1] != gp) < g_before
        time.sleep(1)
    (_, state), line = cine(host)
    print(f'  host: {line}', flush=True)
    ck(played, 'the capture cinematic played in the host\'s game')
    ck(took, 'the captured city is the player civilization\'s in the host\'s game')
    ck(g_took, 'the guest\'s game shows the city taken too (one city of another nation fewer)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
