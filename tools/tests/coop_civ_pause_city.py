#!/usr/bin/env python3
"""Civilization: a city the host founds while a guest has its pause menu open reaches that guest
when it resumes (BUG-0331, BUG-0319).

The campaign's census found a new NPC city's two vehicles missing on guest Bob while Bob's pause menu
was open: the guest replays the host's founding (SpawnNextCity), but the city appears a few seconds
later through a timed callback of the game, and a guest's pause menu stops its game time.

1. Two games on civ_npc. The guest opens its pause menu (Esc, "Resume Game" shown).
2. The host founds a city (the hooked SpawnNextCity, as its city manager does: broadcast).
3. 12 s later, still paused: the guest's city count (printed: what a paused guest shows).
4. The guest resumes; within 25 s its game has the host's city count and the host's vehicles.
PASS: step 4. (A regression scenario: the delay itself is the game's pause.)

  SPORE_LANE=7 python3 tools/tests/coop_civ_pause_city.py
"""
import re
import time

import uifind
from play_civ import civ_state
from twoinst import Checks, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    guest.call('key esc')
    time.sleep(2)
    paused = bool(uifind.shown(guest, 'Resume Game'))
    ck(paused, 'the guest\'s pause menu is open')
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    print('  host founds a city:', host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')['out'].strip(), flush=True)
    time.sleep(12)
    hc, hv, _ = civ_state(host)
    gc, gv, _ = civ_state(guest)
    print(f'  while paused: host {len(hc)} cities {len(hv)} vehicles, guest {len(gc)} cities {len(gv)} vehicles', flush=True)
    for _ in range(3):
        if uifind.shown(guest, 'Resume Game'):
            uifind.press(guest, 'Resume Game')
            time.sleep(1)
    t0, ok = time.time(), False
    while time.time() - t0 < 25 and not ok:
        hc, hv, _ = civ_state(host)
        gc, gv, _ = civ_state(guest)
        ok = len(gc) == len(hc) and set(hv) <= set(gv)
        time.sleep(1)
    print(f'  after resume ({time.time() - t0:.0f} s): host {len(hc)} cities, guest {len(gc)}; vehicles the guest lacks {sorted(set(hv) - set(gv))}', flush=True)
    ck(ok, 'after the pause the guest has the host\'s cities and vehicles within 25 s')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
