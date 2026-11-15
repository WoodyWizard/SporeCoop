#!/usr/bin/env python3
"""Creature stage, a player alone with the mod loaded (no session): the own creature dies through
the game's damage path (`coop.team die`), the death caption ("You were killed ...") and the respawn
cutscene (a new creature hatches at the nest) are skipped the ways players do it: Esc during the
caption, clicks on the cutscene, Esc after OK, Enter spam. Each round: the game answers, no caught
fault, and the creature respawns (a Reddit report, 2026-10-24: "every time I die and skip the
cutscene my game crashes"; not reproduced on GOG 2024 under Wine, with frames from Present and from
the game loop).

  SPORE_LANE=2 python3 tools/tests/coop_creature_death.py
  SPORE_LANE=9 python3 tools/tests/coop_creature_death.py --coop   # in a session: the host, then the guest dies

--coop (BUG-0026, 0.7.11 N1): the report's player was in a session (0.7.2), so the same skips with the session
kept: each variant first on the host while the guest watches, then on the guest; both games must keep answering
without a fault, and the dead player must respawn (its own health) while its partner's game goes on.
"""
import argparse
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
    ap = argparse.ArgumentParser()
    ap.add_argument('--coop', action='store_true', help='keep the session: the host dies, then the guest')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    if not a.coop:
        guest.call('coop.leave')
        time.sleep(1)
        host.call('coop.leave')  # alone, the mod loaded
        time.sleep(2)
    variants = [('Esc during the caption', ['esc'] * 6, False), ('clicks on the cutscene', ['click'] * 8, True),
                ('Esc after OK', ['esc'] * 3, True), ('Enter spam', ['enter'] * 8, False)]
    players = [('host', host, guest), ('guest', guest, host)] if a.coop else [('host', host, None)]
    for who, con, other in players:
        for name, keys, ok_first in variants:
            name = f'{who}: {name}' if a.coop else name
            ck('lethal' in con.call('coop.team die')['out'], f'{name}: the creature takes a lethal hit')
            time.sleep(1.2)
            if ok_first:
                con.call('key enter')  # the caption's OK
                time.sleep(1.2)
            for k in keys:
                con.call('click 720 540' if k == 'click' else f'key {k}')
                time.sleep(0.4)
            alive = 0
            for _ in range(30):
                time.sleep(1)
                close_pause(con)
                if hp(con) > 0:
                    alive = hp(con)
                    break
                con.call('key enter')  # a caption still waiting for OK
            ck(alive > 0, f'{name}: the creature respawned (hp {alive})')
            ck(faults(con) == 0, f'{name}: the game runs, no fault')
            if other is not None:
                ck(faults(other) == 0 and hp(other) > 0, f'{name}: the partner\'s game runs, its creature alive, no fault')
            time.sleep(3)
            close_pause(con)
    if a.coop:
        ck.faults(host=host, guest=guest)
    else:
        ck.faults(host=host)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
