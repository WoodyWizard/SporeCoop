#!/usr/bin/env python3
"""Space: a disabled partner rescues nobody (BUG-0131).

A lethal hit disables a ship instead of destroying it only when a partner who can repair it is in the
same place. A partner whose own ship is disabled counted too (and one whose game froze, for up to the
30 s session timeout): two disabled ships next to each other repaired each other.

1. The guest's ship takes a lethal hit next to the host (`coop.space hit`): disabled (down=1).
2. The host's ship takes a lethal hit: its only partner is disabled, so the ship is destroyed
   (the game's UFO state 2), not disabled.

SPORE_SPACE_FIX=0 switches the fix off on both games (`coop.space helperfix 0`): the run that must FAIL.
"""
import os
import re
import time

from harness import sp, waiter
from twoinst import Checks, start


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'(-?[\d.]+)', text)
    return float(m.group(1)) if m else -1


wait = waiter(step=0.5)


def main():
    fix = os.environ.get('SPORE_SPACE_FIX', '1')
    host, guest = start('space')
    ck = Checks()
    try:
        for con in (host, guest):
            ck(sp(con, 'helperfix', fix) == f'helperfix {fix}', f'helperfix {fix}')
        ck(wait(lambda: 'partners_here=1' in sp(host) and 'partners_here=1' in sp(guest), 60),
           'both ships in the same place')
        g = sp(guest, 'hit', field(sp(guest, 'hp'), 'hp') + 50)
        print('  guest:', g)
        ck(wait(lambda: field(sp(guest, 'hp'), 'down') == 1, 5), f'the guest\'s ship is disabled ({sp(guest, "hp")})')
        ck(wait(lambda: 'partners_down=1' in sp(host), 5), 'the host is told')
        h = sp(host, 'hit', field(sp(host, 'hp'), 'hp') + 50)
        print('  host:', h)
        time.sleep(1.5)
        st = sp(host, 'hp')
        print('  host after:', st)
        ck(field(h, 'down') == 0 and field(st, 'state') == 2,
           f'the host\'s lethal hit destroys its ship, its only partner is disabled ({st})')
    finally:
        for con in (host, guest):
            sp(con, 'helperfix', 1)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
