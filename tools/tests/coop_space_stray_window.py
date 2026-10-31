#!/usr/bin/env python3
"""Space bots: a window that pauses the game without a caption is closed (BUG-0336).

In the campaign 20261031-024151 the host's Space bot had left Spore's My Collections / Mission Log
window open (the 4th small HUD button at the bottom left opens it). It pauses the game time
(finale busy=1, no caption, no cinematic) and takes no mouse wheel and no Enter, so the finale bot
never reached the galaxy view and the whole finale stalled for 25 minutes. The Sporepedia (the 3rd
button) does the same. Here the host opens each with a real click, then the bot tools must still
get it to the galaxy view (coop_space_finale.to_galaxy) and settle it: the game's own context
(galaxy view) and its game time running again are checked.

  SPORE_LANE=9 python3 tools/tests/coop_space_stray_window.py

OLD_FINALE_TOOL=1: the tools without the close (the BUG-0303 switch of these tools): FAIL.
"""
import time

import coop_space_finale as fz
from twoinst import Checks, shot, start

BUTTONS = (('My Collections', 114, 1054), ('Sporepedia', 84, 1054))  # 1440x1080 window


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    fz.get_drive(host, 'host', ck)  # (the fixture's ship has no interstellar drive: no galaxy view)
    fz.settle(host, 'host', timeout=60)
    for name, x, y in BUTTONS:
        host.call(f'click {x} {y}')
        time.sleep(2.5)
        f = fz.fin(host)
        print(f'  host: {name} clicked: busy={f.get("busy")} ctx={f.get("ctx")}')
        shot(host, f'stray_{name.replace(" ", "_")}.png')
        ck(f.get('busy') == '1', f'host: {name} is open (the game time is paused)')
        fz.to_galaxy(host, 'host', ck)
        f = fz.fin(host)
        ck(f.get('ctx') == '2' and f.get('busy') == '0',
           f'host: in the galaxy view with the game running after {name} ({f["line"][:90]})')
        # back into a system for the next window (the wheel in, as a player does)
        for _ in range(10):
            if fz.fin(host).get('ctx') == '1':
                break
            host.call('wheel 720 540 5')
            time.sleep(1.5)
        fz.settle(host, 'host', timeout=60)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
