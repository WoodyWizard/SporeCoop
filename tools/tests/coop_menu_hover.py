#!/usr/bin/env python3
"""Galaxy menu: a received world loads while the mouse rests on a planet of the menu, with no fault (BUG-0676).

chaos_tribe_awful (lab 20261107-113600): the guest's load of the host's world from the galaxy menu faulted at 0xDF6D93
(data 0x2C). The mod's load reads the slot's stars again (cStarManager::ReadStarsFromSaveDB), which makes the menu
rebuild its planets (0xDF95A0): every planet's info panel is freed and the list is made anew, but the menu keeps its
pointer to the planet under the mouse. Leaving the menu for the load rolled that planet out and called its panel, now
null: the menu's exit stopped half-way, the menu stayed the active mode and faulted in every frame after (22939 faults).

1. A freshly started guest (its galaxy menu shows the planets of its empty slot Game0: 6, room for 6) joins the host
   on the tribe fixture once its menu has made its planets (`join_at`, `before_join`). Right before the join the next
   planet rebuild of its menu is armed to put the mouse on the menu's last planet first (`coop.menuhover arm <last>`, the rollover as the
   mouse over it, as the camera of the menu passed the mouse in chaos_tribe_awful).
2. The guest receives the host's world (7 planets in its slot): the load's re-read of the stars rebuilds the menu's
   planets (a new, larger list), then the load leaves the menu.
PASS: the guest gets into the host's world, no fault is caught on either game, the guest runs frames.
SPORE_FIX_OFF=game.menu_hover must FAIL: the fault at 0xDF6D93 (data 0x2C), the guest stays in the menu with a fault
in every frame (0xE01323).

  SPORE_LANE=6 python3 tools/tests/coop_menu_hover.py [--planet I]
"""
import argparse
import re
import time

from twoinst import Checks, start

found = {}


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

    def arm(guest):
        t = time.time()
        state = menu(guest)
        while time.time() - t < 40 and 'rebuilds 0 ' in state:  # the menu makes its planets some seconds after it came up
            time.sleep(0.5)
            state = menu(guest)
        print(f'  guest: the menu has its planets {time.time() - t:.1f} s after the join was due', flush=True)
        print('  guest (before the join):', state, flush=True)
        m = re.search(r'planets=(\d+) \(panels (\d+), room (\d+)', state)
        found['n'] = int(m.group(1)) if m else 0
        found['room'] = int(m.group(3)) if m else 0
        i = a.planet if a.planet >= 0 else max(found['n'] - 1, 0)
        print('  guest:', guest.call(f'coop.menuhover arm {i}')['out'].strip(), flush=True)

    host, guest = start('tribe', join_at=3.0, before_join=arm)
    ck = Checks()
    ck(found.get('n', 0) > 0, f'the guest\'s galaxy menu showed planets before the join ({found.get("n")}, room {found.get("room")})')
    ck(guest.call('coop.avatar')['ok'],
       'the guest got into the host\'s world')
    print('  guest:', menu(guest), flush=True)
    f0 = frame(guest)
    time.sleep(5)
    f1 = frame(guest)
    ck(f1 > f0 + 30, f'the guest runs frames 5 s later (frame {f0} -> {f1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
