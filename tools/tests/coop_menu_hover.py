#!/usr/bin/env python3
"""Galaxy menu: a received world loads while the mouse rests on a planet of the menu, with no fault (BUG-0676).

chaos_tribe_awful (lab 20261107-113600): the guest's load of the host's world from the galaxy menu faulted at 0xDF6D93
(data 0x2C). The mod's load reads the slot's stars again (cStarManager::ReadStarsFromSaveDB), which makes the menu
rebuild its planets (0xDF95A0): every planet's info panel is freed and the list is made anew, but the menu keeps its
pointer to the planet under the mouse. Leaving the menu for the load rolled that planet out and called its panel, now
null: the menu's exit stopped half-way, the menu stayed the active mode and faulted in every frame after (22939 faults).

1. Two games on the tribe fixture.
2. The guest leaves and joins again: it receives the host's world and goes to the galaxy menu to load it.
3. In the menu, before the load, the next planet rebuild first puts the mouse on a planet of the menu
   (`coop.menuhover arm <i>`, the rollover as the mouse over it).
PASS: the guest gets into the host's world again, no fault is caught on either game, the guest runs frames.
SPORE_FIX_OFF=game.menu_hover must FAIL (the fault at 0xDF6D93 and the guest stays in the menu).

  SPORE_LANE=6 python3 tools/tests/coop_menu_hover.py [--planet I]
"""
import argparse
import re
import time

import lane

from twoinst import Checks, start, wait_guest_in

MENU_MODE = 0x2CCD1D2


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def frame(con):
    try:
        m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    except OSError:
        return -1
    return int(m.group(1)) if m else -1


def menu(con):
    try:
        return con.call('coop.menuhover')['out'].strip()
    except OSError as e:
        return f'no answer {e}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--planet', type=int, default=-1, help='the menu planet to put the mouse on (default: the last one)')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    t1 = time.time()
    while time.time() - t1 < 90 and mode(guest) != MENU_MODE:
        time.sleep(0.2)
    print(f'  guest in the galaxy menu after {time.time() - t1:.1f} s', flush=True)
    time.sleep(1.0)  # the menu's own rebuild when it comes up; the load waits 3 s (kMenuSettle)
    state = menu(guest)
    print('  guest:', state, flush=True)
    m = re.search(r'planets=(\d+) \(panels (\d+)', state)
    n = int(m.group(1)) if m else 0
    ck(n > 0, f'the guest\'s galaxy menu shows planets ({n})')
    i = a.planet if a.planet >= 0 else max(n - 1, 0)
    print('  guest:', guest.call(f'coop.menuhover arm {i}')['out'].strip(), flush=True)
    together = wait_guest_in(host, guest, 150)
    ck(together, f'the guest is in the host\'s world again ({time.time() - t1:.1f} s)')
    print('  guest:', menu(guest), flush=True)
    f0 = frame(guest)
    time.sleep(5)
    f1 = frame(guest)
    ck(f1 > f0 + 30, f'the guest runs frames 5 s later (frame {f0} -> {f1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
