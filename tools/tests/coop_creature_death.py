#!/usr/bin/env python3
"""Creature stage, a player alone with the mod loaded (no session): the own creature dies through
the game's damage path (`coop.team die`), the death caption ("You were killed ...") and the respawn
cutscene (a new creature hatches at the nest) are skipped the ways players do it: Esc during the
caption, clicks on the cutscene, Esc after OK, Enter spam. Each round: the game answers, no caught
fault, and the creature respawns (a Reddit report, 2026-10-24: "every time I die and skip the
cutscene my game crashes"; not reproduced on GOG 2024 under Wine, with frames from Present and from
the game loop).

  SPORE_LANE=2 python3 tools/tests/coop_creature_death.py
"""
import re
import time

import uifind
from twoinst import Checks, start


def hp(con):
    m = re.search(r'hp=([-\d.]+)/', con.call('coop.avatar')['out'])
    return float(m.group(1)) if m else -1


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def close_pause(con):
    """a spare Esc after the cutscene opens the pause menu ("Options": the game stops): close it"""
    for _ in range(3):
        if not uifind.shown(con, 'Resume Game'):
            return
        con.call('key esc')
        time.sleep(1)


def main():
    host, guest = start('creature')
    ck = Checks()
    guest.call('coop.leave')
    time.sleep(1)
    host.call('coop.leave')  # alone, the mod loaded
    time.sleep(2)
    variants = [('Esc during the caption', ['esc'] * 6, False), ('clicks on the cutscene', ['click'] * 8, True),
                ('Esc after OK', ['esc'] * 3, True), ('Enter spam', ['enter'] * 8, False)]
    for name, keys, ok_first in variants:
        ck('lethal' in host.call('coop.team die')['out'], f'{name}: the creature takes a lethal hit')
        time.sleep(1.2)
        if ok_first:
            host.call('key enter')  # the caption's OK
            time.sleep(1.2)
        for k in keys:
            host.call('click 720 540' if k == 'click' else f'key {k}')
            time.sleep(0.4)
        alive = 0
        for _ in range(30):
            time.sleep(1)
            close_pause(host)
            if hp(host) > 0:
                alive = hp(host)
                break
            host.call('key enter')  # a caption still waiting for OK
        ck(alive > 0, f'{name}: the creature respawned (hp {alive})')
        ck(faults(host) == 0, f'{name}: the game runs, no fault')
        time.sleep(3)
        close_pause(host)
    ck.faults(host=host)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
