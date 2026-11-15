#!/usr/bin/env python3
"""BUG-0422 (class: per-player state under a reused id): when a player leaves, no module keeps state under its id.

The host gives a newcomer the lowest free player id, so the next player to join gets a departed player's id. Five
modules forgot a leaver by hand; spacecoop (its last star: Jump-to went to the departed player's star), space_map
(its roster), space_rivalry (its budget), fields (the values last sent to it), creations (designs answered to it),
... kept it. Every module with per-player state now registers in player_life.h and forgets on a leave and a join.

In Space (fixture space: the modules with the most per-player state run there):
  1. the session runs 20 s: the host holds state of the guest (player 1): `coop.players held 1` lists modules
  2. the guest leaves: within 5 s the host holds nothing of player 1
  3. the guest joins again under id 1: it is shown its partner's roster again (space_map) and the session goes on

  SPORE_FIX_OFF=core.player_forget coop_player_forget.py   (step 2 FAILS: spacecoop, space_map ... still hold id 1)
"""
import time

import lane
from twoinst import Checks, start


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(20)
    before = host.call('coop.players held 1')['out'].strip()
    print('  the host holds of player 1 in the session:', before.replace('\n', ', '))
    ck(before != 'nothing held', 'the session made per-player state of the guest on the host')
    print(' ', guest.call('coop.leave')['out'].strip())
    after = ''
    t0 = time.time()
    while time.time() - t0 < 5:
        time.sleep(1)
        after = host.call('coop.players held 1')['out'].strip()
        if after == 'nothing held':
            break
    print('  after the leave:', after.replace('\n', ', '))
    ck(after == 'nothing held', f'the host keeps nothing of the departed player 1 ({after.replace(chr(10), ", ")})')
    print(' ', guest.call(lane.JOIN)['out'].strip())
    ok = False
    t0 = time.time()
    while time.time() - t0 < 60 and not ok:
        time.sleep(2)
        ok = 'client' in guest.call('coop.status')['out'].lower()
    ck(ok, 'the guest joined again')
    time.sleep(10)
    again = host.call('coop.players held 1')['out'].strip()
    print('  after the rejoin the host holds:', again.replace('\n', ', '))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
