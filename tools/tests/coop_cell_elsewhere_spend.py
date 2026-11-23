#!/usr/bin/env python3
"""Cell: a guest that plays a Cell world of its own sends no editor spend and no mate to the host (BUG-1507, CELL-4 of
the external audit of 0.7.15.5).

Before the fix such a guest (own_world.elsewhere: its own Cell world in the session, BUG-0797) still sent what its own
cell editor spent and its mate's pose; the host took the spend off its own DNA count and kept a ghost of the mate in
its pool.

  1. shared DNA (all.shared_dna shared: a guest's spend comes off the host's count S+0x2C, the game's own field)
  2. the guest, still connected, loads a copy of the fixture planet under another name (own_world.load_own)
  3. the guest's game: "back from its own editor with 7 DNA spent" (`coop.pool spend fake 7`, the same measure a real
     editor return takes) and its mating call (the heart, as coop_cell_mate_copy.py)
  4. over 10 s the HOST's spent count (S+0x2C, `coop.pool spend`) stays as it was and the host's game has no copy of
     the guest's mate (`coop.pool mate`)
  5. key frame: the host's screen shows no mate of the guest (the guest plays its own world)
SPORE_FIX_OFF=cell.elsewhere_spend must FAIL (the host's count goes up by 7 and the mate copy is made).

  SPORE_LANE=2 python3 tools/tests/coop_cell_elsewhere_spend.py
"""
import re
import time

import own_world
import settings_lib as sl
from coop_cell_mate_copy import copy_of
from coop_editor_desync import HEART
from harness import field
from twoinst import Checks, keyframe, start


def spent(con):
    v = field(con.call('coop.pool spend')['out'], r'\bspent=(-?\d+)')
    return int(v) if v is not None else None


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(4)
    ck(sl.force(host, [guest], 'all.shared_dna', 'shared'), 'shared DNA for both')
    ok = own_world.load_own(guest, 'Cadian.spo', 'Othercell', 'Cell')
    ck(ok, 'the guest plays a Cell world of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        return ck.result()
    time.sleep(8)
    print('  guest:', [l for l in guest.call('coop.status')['out'].splitlines() if l.startswith('own world')], flush=True)
    h0 = spent(host)
    print('  guest: ' + guest.call('coop.pool spend fake 7')['out'].strip(), flush=True)
    for _ in range(4):  # the mating call in its own world
        guest.call('coop.pool mate button')
        time.sleep(1)
        guest.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in guest.call('coop.pool mate')['out']:
            break
    mate = 'alive=1' in guest.call('coop.pool mate')['out']
    print('  guest mate: ' + guest.call('coop.pool mate')['out'].strip()[:160], flush=True)
    copies, h_max = [], h0
    t0 = time.time()
    while time.time() - t0 < 10:
        copies.append(copy_of(host))
        h = spent(host)
        if h is not None and h0 is not None:
            h_max = max(h_max, h)
        time.sleep(1)
    audit = next((l for l in guest.call('coop.pool')['out'].splitlines() if l.startswith('cell_audit')), '')
    print(f'  host spent {h0} -> max {h_max}; host copies seen {[c for c in copies if c]}; guest {audit}', flush=True)
    print('  host: ' + next((l for l in host.call('coop.pool')['out'].splitlines() if l.startswith('cell_audit')), ''), flush=True)
    ck(h0 is not None and h_max == h0, f'the host\'s DNA count did not take the spend of the guest\'s own world ({h0} -> {h_max})')
    ck(mate, 'the guest\'s mating call brought its mate in its own world')
    ck(not any(copies), f'the host\'s game has no copy of the mate of a guest elsewhere ({[c for c in copies if c][:2]})')
    keyframe('elsewhere', 'host: its own Cell world without the guest or its mate; guest: its own world with its mate')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
