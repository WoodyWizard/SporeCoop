#!/usr/bin/env python3
"""Cell stage: a cell that is invulnerable when its own editor opens is vulnerable after it (BUG-0237).

A revive (2 s) or a respawn (3 s) sets the avatar's +0x111 and a timer that clears it. Leaving the
Cell stage resets the timer (BUG-0070); if the avatar object came back from the editor with the flag
still set, nothing would clear it and the cell would take no damage for the rest of the stage.

1. Guest: `coop.pool invulnfor 120` (the flag and the timer, as a respawn sets them).
2. The guest's own mating call, its editor opens (real clicks, coop_editor_desync.py's way), cancel.
3. On the first reads back in the pool: the avatar's +0x111 (`coop.cell pool`: av_invuln) and
   the pool header, printed for docs/re/cell.md#cell-rebuilds-in-session.
4. 4 s after the return a host bite (`coop.pool hurt 1 1`) lowers the guest's hp in the guest's game.
"""
import re
import time

from twoinst import Checks, start
from coop_editor_desync import CANCEL, mating_call, mode, wait


def hp(con):
    m = re.search(r'^avatar hp=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def main():
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    print('  before: ' + guest.call('coop.cell pool')['out'].strip())
    print('  guest: ' + guest.call('coop.pool invulnfor 120')['out'])
    print('  set: ' + guest.call('coop.cell pool')['out'].strip())
    alive, reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(reached, f"the guest's own cell editor opens ({mode(guest)})")
    if reached:
        time.sleep(3)
        guest.call('click %d %d' % CANCEL)
        back = wait(lambda: 'Cell' in mode(guest) and 'av_ptr=00000000' not in guest.call('coop.cell pool')['out'], 30, 0.2)
        ck(back, 'the guest leaves its editor (cancel)')
        print('  back: ' + guest.call('coop.cell pool')['out'].strip())
        wait(lambda: 'cine=0' in guest.call('coop.pool')['out'], 30)
        time.sleep(4)
        p = guest.call('coop.cell pool')['out'].strip()
        print('  4 s later: ' + p)
        h0 = hp(guest)
        host.call('coop.pool hurt 1 1')
        time.sleep(1.5)
        h1 = hp(guest)
        print(f'  host bite: guest hp {h0} -> {h1}')
        ck(h0 is not None and h1 is not None and h1 < h0, f'a host bite hurts the guest after its editor ({h0} -> {h1}; {p})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
