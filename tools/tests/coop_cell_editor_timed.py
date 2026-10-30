#!/usr/bin/env python3
"""Cell stage: the own cell editor entered within the 2-3 s after a real respawn or revive (BUG-0237).

coop_cell_editor_invuln.py sets the flag with a test command (`coop.pool invulnfor 120`). This one
takes the real way and the real timing the issue (#171) describes:

1. The guest calls its mate (the mating-call button, real click) and then is knocked out
   (`coop.pool selfhit 250`): 25 m from the host (respawn, 3 s invulnerable) or, with --revive,
   right next to it (the host revives it, 2 s invulnerable).
2. The moment the knockout ends (polled every 0.1 s), the guest's cell is put onto its mate
   (`coop.pool mate tp`): the game's own collision opens the cell editor. The test prints how many
   seconds after the end of the knockout the Cell stage was left, and the avatar's +0x111 then.
3. Cancel. Back in the pool: the avatar's +0x111 (`coop.cell pool` av_invuln) is 0 within 4 s, and
   a host bite (`coop.pool hurt 1 1`) lowers the guest's hp in the guest's game.

Measured (2026-10-30, lane 3): the Cell stage is left ~10 s after the cell touches its mate (the
mating cinematic), so the 2 s / 3 s window of the issue is over before the editor opens, whatever
the player does; the test prints that time and stays as a guard of the editor return.

  SPORE_LANE=3 python3 tools/tests/coop_cell_editor_timed.py [--revive]
"""
import re
import sys
import time

from twoinst import Checks, start
from coop_editor_desync import CANCEL, HEART, mate, mode, wait


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def hp(con):
    m = re.search(r'^avatar hp=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def main():
    revive = '--revive' in sys.argv
    window = 2.0 if revive else 3.0
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    wait(lambda: 'cine=0' in guest.call('coop.pool')['out'], 30)
    ck(wait(lambda: (num(guest.call('coop.pool')['out'], 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    # 1. the mate, then the knockout
    for _ in range(3):
        guest.call('coop.pool mate button')
        time.sleep(1)
        guest.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in mate(guest):
            break
    print('  mate: ' + mate(guest).strip())
    if not ck('alive=1' in mate(guest), 'the guest\'s mate is in the pool'):
        return ck.result()
    print('  guest: ' + guest.call('coop.pool tp %s' % ('0.5 0' if revive else '25 0'))['out'])
    time.sleep(0.5)
    r = guest.call('coop.pool selfhit 250')['out']
    print('  guest: ' + r)
    if not ck(num(r, 'downed') == 1, 'the guest is knocked out'):
        return ck.result()
    if revive:
        host.call('coop.pool tp 0.5 0')
    # 2. the end of the knockout, then right onto the mate
    t0 = time.time()
    while time.time() - t0 < 20 and num(guest.call('coop.pool')['out'], 'downed') != 0:
        time.sleep(0.1)
    t_up = time.time()
    st = guest.call('coop.pool')['out']
    up_line = guest.call('coop.cell pool')['out'].strip()
    print(f'  knockout over after {t_up - t0:.1f} s ({"revive" if revive else "respawn"}): revives={num(st, "revives")} '
          f'respawns={num(st, "respawns")}; {up_line}')
    print('  mate tp: ' + guest.call('coop.pool mate tp')['out'].strip())
    left = None
    last = ''
    while time.time() - t_up < 15:
        if 'Cell' not in mode(guest):
            left = time.time() - t_up
            break
        last = guest.call('coop.cell pool')['out'].strip()
        time.sleep(0.05)
    print(f'  Cell stage left {left if left is None else round(left, 2)} s after the knockout ended '
          f'(window {window} s); last read in the pool: {last}')
    if left is None:
        ck(False, 'the guest\'s cell editor opens (the mate was touched)')
        return ck.result()
    reached = wait(lambda: 'Editor' in mode(guest), 30, 0.2)
    ck(reached, f'the guest\'s own cell editor opens ({mode(guest).strip()})')
    in_window = left < window
    # 3. back from the editor
    time.sleep(3)
    guest.call('click %d %d' % CANCEL)
    back = wait(lambda: 'Cell' in mode(guest) and 'av_ptr=00000000' not in guest.call('coop.cell pool')['out'], 30, 0.2)
    ck(back, 'the guest leaves its editor (cancel)')
    print('  back: ' + guest.call('coop.cell pool')['out'].strip())
    wait(lambda: 'cine=0' in guest.call('coop.pool')['out'], 30)
    clear = wait(lambda: 'av_invuln=0' in guest.call('coop.cell pool')['out'], 4, 0.2)
    p = guest.call('coop.cell pool')['out'].strip()
    print('  then: ' + p)
    ck(clear, f'the avatar is not invulnerable after the editor ({p})')
    h0 = hp(guest)
    host.call('coop.pool hurt 1 1')
    time.sleep(1.5)
    h1 = hp(guest)
    print(f'  host bite: guest hp {h0} -> {h1}')
    ck(h0 is not None and h1 is not None and h1 < h0, f'a host bite hurts the guest after its editor ({h0} -> {h1})')
    ck.faults(host=host, guest=guest)
    if not in_window:  # lane 3, 2026-10-30: always ~10 s (the mating cinematic): the issue's window cannot be hit
        print(f'  note: the Cell stage was left {left:.2f} s after the knockout, after the {window} s window: the timer '
              f'had cleared +0x111 already; the checks above still guard the editor return')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
