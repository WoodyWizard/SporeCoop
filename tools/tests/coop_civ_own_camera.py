#!/usr/bin/env python3
"""BUG-9294: a guest with its own nation starts its Civilization stage with the camera on its OWN city.

civ_npc (the host's capital, NPC nations, free city sites left); the guest plays its own creature, so the host founds
its own nation (BUG-9274). The game's Civ start points every game's camera at the player civ of the loaded world, the
host's; the fix sends the guest's camera to its own nation's first city the way the game's "next city" button does
(SimCamera_GoToPoint). Checks, on the guest's game (its camera, `coop.view`, against the two cities' positions):
  1. once its own nation is founded, its camera goes to that nation's city: the viewer nearer its own city than the
     host's, along the ground, and still above the ground
  2. a new evening (twoinst.session_restart: both games start cold, the host loads the planet, the guest joins): the
     same, the camera keeps the load's height, and the arrow keys still move it
SPORE_FIX_OFF=civ.own_camera fails checks 1 and 2 (the camera stays at the host's capital). The manual runs on
Windows (lane B, 2026-11-21, Epina and Kealeneon) are in the ticket.
"""
import math
import re
import time

import creations_state
import twoinst
from twoinst import Checks, shot, start, wait_guest_in

from coop_civ_seats import out, own_nation, seats, wait

VEC = r'\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)'


def vec(text, key):
    m = re.search(key + '=' + VEC, text)
    return [float(x) for x in m.groups()] if m else None


def ground(a, b):
    """the distance along the planet's surface (at b's radius) between the directions of a and b"""
    la, lb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    if la < 1 or lb < 1:
        return -1.0
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b)) / la / lb))
    return math.acos(d) * lb


def camera(guest):
    """(viewer position, own city, host city) on the guest's game"""
    n = out(guest, 'coop.civ nations')
    return vec(out(guest, 'coop.view'), 'pos'), vec(n, 'own_city'), vec(n, 'host_city')


def at_own_city(guest, seen):
    pos, oc, hc = camera(guest)
    if not (pos and oc and hc) or not any(oc):
        return False
    seen['own'], seen['host'] = ground(pos, oc), ground(pos, hc)
    seen['height'] = math.sqrt(sum(x * x for x in pos)) - math.sqrt(sum(x * x for x in oc))
    return 0 <= seen['own'] < seen['host'] and seen['own'] < 250 and seen['height'] > 20


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    st0 = creations_state.save(guest)
    own = creations_state.choose_own(guest)
    ck(own is not None, f'the guest has a creature of its own ({own})')
    gid = int((re.search(r'client id=(\d+)', out(guest, 'coop.status')) or [None, '1'])[1])
    ck(wait(lambda: any(s >= 1 and v[0] == gid and v[1] != '00000000' for s, v in seats(host).items()), 240, 3),
       f'host: the guest\'s seat has a nation it commands ({seats(host)})')
    wait_guest_in(host, guest)
    ck(wait(lambda: own_nation(guest), 240, 4), f'guest: its game knows its own nation ({own_nation(guest)})')

    # 1. its camera goes to its own city
    seen = {}
    ck(wait(lambda: at_own_city(guest, seen), 60, 2), f'guest: its camera at its own city, not the host\'s ({seen})')
    shot(guest, 'civ_own_camera_founded.png')

    # 2. a new evening: the camera of the guest's load goes to its own city
    host, guest, _info = twoinst.session_restart(host, guest)
    ck(wait(lambda: own_nation(guest), 240, 4), f'guest: its own nation after the restart ({own_nation(guest)})')
    seen = {}
    ck(wait(lambda: at_own_city(guest, seen), 90, 2), f'guest: after its load the camera at its own city ({seen})')
    shot(guest, 'civ_own_camera_load.png')
    p0 = camera(guest)[0]
    for k in (37, 37, 37, 38, 38):  # the arrow keys
        out(guest, f'key {k}')
        time.sleep(0.5)
    time.sleep(2)
    p1 = camera(guest)[0]
    moved = math.dist(p0, p1) if p0 and p1 else 0
    ck(moved > 3, f'guest: the arrow keys still move its camera ({moved:.1f} m)')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
