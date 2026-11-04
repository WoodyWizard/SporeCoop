#!/usr/bin/env python3
"""Space: a load starts a new game for the disable rule (BUG-0257).

A lethal hit disables a ship (a partner can repair it) instead of destroying it, but not twice within
60 s. Those 60 s were counted across a load: right after a session restart (the lab's warm reset, a
rejoin that loads the host's world) the first lethal hit destroyed the ship next to its partner.

1. The guest's ship takes a lethal hit next to the host (`coop.space hit`): disabled (down=1).
2. Within 60 s both games start again from the fixture (twoinst.warm_reset: leave, the host loads a
   fresh copy of the save, the guest joins and loads the host's world).
3. The guest's ship takes a lethal hit next to the host again: disabled (down=1, the game's UFO
   state is not 2), not destroyed.

  coop_space_down_reload.py   (SPORE_FIX_OFF=space.down_epoch: FAILS, the ship is destroyed in 3)
"""
import re
import time

import fixtures
from twoinst import Checks, start, warm_reset


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'(-?[\d.]+)', text)
    return float(m.group(1)) if m else -1


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def together(host, guest):
    return wait(lambda: 'partners_here=1' in sp(host) and 'partners_here=1' in sp(guest), 60)


def main():
    host, guest = start('space')
    ck = Checks()
    ck(together(host, guest), 'both ships in the same place')
    g = sp(guest, 'hit', field(sp(guest, 'hp'), 'hp') + 50)
    print('  guest, first hit:', g)
    ck(wait(lambda: field(sp(guest, 'hp'), 'down') == 1, 5), f'the first lethal hit disables ({sp(guest, "hp")})')
    t_down = time.time()
    ok = warm_reset(host, guest, 'space', fixtures.planet_of('space'))
    ck(ok, 'the games start again from the fixture (leave, load, join)')
    ck(together(host, guest), 'both ships in the same place after the reload')
    age = time.time() - t_down
    print(f'  {age:.0f} s since the first disable')
    if age >= 58:
        print('RESULT: SKIP (the reload took longer than the 60 s the test needs to be inside)')
        return 0
    st = sp(guest, 'hp')
    print('  guest after the reload:', st)
    ck(field(st, 'down') == 0 and field(st, 'state') != 2, 'the guest\'s ship flies (not disabled, not destroyed)')
    g = sp(guest, 'hit', field(st, 'hp') + 50)
    print('  guest, second hit:', g)
    time.sleep(1.5)
    st = sp(guest, 'hp')
    print('  guest after:', st)
    ck(field(st, 'down') == 1 and field(st, 'state') != 2,
       f'after the load the first lethal hit next to the partner disables again, the ship is not destroyed ({st})')
    ck(wait(lambda: 'partners_down=1' in sp(host), 5), 'the host sees its partner disabled')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
