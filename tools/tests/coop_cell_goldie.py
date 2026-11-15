#!/usr/bin/env python3
"""Cell: after the game's "Goldie's demise" tutorial each player's own input ends it and parts can be collected (BUG-0666).

The tutorial (AddFoodPoints 0xE7CE10 -> 0xE79720, food before the add 120-150) plays the cinematic goldies_demise,
whose script ends in `cell_button: wait -forButton`; its event 0x24 (G+0x5198) re-locks the avatar every frame
(0xE5F160: +0x16C, +0x170 = Goldie, +0x178, +0x17B, +0x17C) until the player clicks the button (message 0x445F729 ->
0xE7F960 -> 0xE794F0) or presses Esc (0xE7F630 -> 0xE7F5A0). The lab's tests never did either: both avatars kept the
locks and no token could be collected (docs/re/cell.md#cell-goldie).

1. Two games on the cell fixture, in a session; the host adds food (`coop.pool food 50`, the shared food) until both
   games have played the tutorial (S+0x68, `coop.pool mate` goldie_done=1).
2. 14 s later both avatars hold the locks (`coop.pool tokens` own_locks: +0x178 set).
3. Each player presses Esc in its own game (real input, `key esc`), the guest first.
PASS: within 6 s of its own Esc each game's avatar has no lock (+0x178 0) and plays (no input lock, no cinematic),
and the other game's avatar is not freed by it (each game's own tutorial). Not a mod bug: no switch.

  SPORE_LANE=4 python3 tools/tests/coop_cell_goldie.py
"""
import re
import time

from coop_cell_part_token import playing, wait
from twoinst import Checks, start


def goldie_done(con):
    m = re.search(r'goldie_done=(-?\d+)', con.call('coop.pool mate')['out'])
    return int(m.group(1)) if m else None


def locked(con):
    """+0x178 of the own avatar (own_locks: +0x112 +0x113 +0x178 +0x17F)"""
    m = re.search(r'own_locks=(\d{4})', con.call('coop.pool tokens')['out'])
    return m.group(1)[2] == '1' if m else None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    for _ in range(8):
        if goldie_done(host) == 1 and goldie_done(guest) == 1:
            break
        print('  host:', host.call('coop.pool food 50')['out'].strip()[:60], flush=True)
        time.sleep(3)
    ck(goldie_done(host) == 1 and goldie_done(guest) == 1,
       f'both games played the tutorial (goldie_done host {goldie_done(host)} guest {goldie_done(guest)})')
    time.sleep(14)
    lh, lg = locked(host), locked(guest)
    print(f'  14 s later: locked host {lh} guest {lg}; host {host.call("coop.pool tokens")["out"].splitlines()[0][:120]}',
          flush=True)
    ck(lh and lg, 'both avatars hold the tutorial\'s locks while its cinematic waits for the button')
    for con, name, other in ((guest, 'guest', host), (host, 'host', guest)):
        con.call('key esc')
        free = wait(lambda: locked(con) is False and playing(con), 6)
        print(f'  {name} pressed Esc: locked {locked(con)}, plays {bool(playing(con))}; the other locked {locked(other)}',
              flush=True)
        ck(free, f'{name}: its own Esc ends the tutorial (no lock, plays)')
        if name == 'guest':
            ck(locked(host), 'the host\'s avatar keeps its own tutorial until the host\'s player ends it')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
