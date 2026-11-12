#!/usr/bin/env python3
"""O1 of 0.7.13: a guest that plays a Cell world of its own while it stays in the session (BUG-0797, BUG-0809).

  1. the usual Cell session (fixture cell): the host's bytes to the guest over 15 s (it plays the host's world)
  2. the guest, still connected, loads a copy of the fixture planet under another name (own_world.load_own)
  3. BUG-0809: the host's bytes to the guest over 15 s fall to a small part of step 1: no live Cell state (the
     cells around the host, 10 per second) goes to a guest that is not in the host's Cell world
  4. BUG-0797: the guest's own game populates its pool: the live cells of its own game
     (the game's cell pool: `coop.pool census` strays, not mirrors) are there in the last 25 s of 60 s (the game fills the pool
     10-40 s after the load); the guest's game is in its own pool
     (cell_away solo=1)

  SPORE_FIX_OFF=cell.send_here coop_own_cell.py      (step 3 FAILS)
  SPORE_FIX_OFF=cell.solo_elsewhere coop_own_cell.py (step 4 FAILS)
"""
import re
import time

import own_world
from twoinst import Checks, start


def own_npcs(con):
    """the guest's own live creature cells within 80 m (not mirrors of the host's)"""
    out = con.call('coop.pool npcs 80')['out']
    return sum(1 for m in re.finditer(r'npc \S+ mirror=(\d)', out) if m.group(1) == '0')


def own_cells(con):
    """the guest's own foreground cells (the game's pool, not mirrors, not dying: `coop.pool census` strays)"""
    m = re.search(r'strays=(\d+) \(keepalive \d+, dying/dead (\d+)\)', con.call('coop.pool census')['out'])
    return int(m.group(1)) - int(m.group(2)) if m else 0


def rate(host, secs=15):
    b0, _ = own_world.net_out(host)
    time.sleep(secs)
    b1, _ = own_world.net_out(host)
    return None if b0 is None or b1 is None else (b1 - b0) / secs


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(8)
    r_here = rate(host)
    print(f'  host -> guest in the host\'s world: {r_here} bytes/s', flush=True)
    ck(r_here, 'the host sends to the guest')
    ok = own_world.load_own(guest, 'Cadian.spo', 'Othercell', 'Cell')
    ck(ok, 'the guest plays a Cell world of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        return ck.result()
    time.sleep(8)
    st = [l for l in guest.call('coop.status')['out'].splitlines() if l.startswith('own world')]
    print('  guest:', st, flush=True)
    # 3. BUG-0809
    r_own = rate(host)
    print(f'  host -> guest while the guest plays its own world: {r_own} bytes/s (in the host\'s world {r_here})', flush=True)
    print('  host:', [l for l in host.call('coop.pool')['out'].splitlines() if 'cell_send' in l], flush=True)
    ck(r_here and r_own is not None and r_own < 0.35 * r_here,
       f'no live Cell state to a guest in another Cell world ({r_own} bytes/s, {r_here} in the host\'s world)')
    # 4. BUG-0797
    counts, cells = [], []
    t0 = time.time()
    while time.time() - t0 < 60:
        counts.append(own_npcs(guest))
        cells.append(own_cells(guest))
        time.sleep(5)
    away = [l for l in guest.call('coop.pool')['out'].splitlines() if l.startswith('cell_away')]
    print(f'  the guest\'s own creature cells within 80 m over 60 s: {counts}; its own live cells in the pool: {cells}; {away}',
          flush=True)
    # the creatures near a guest that does not swim wander off (3 of 10 left after 60 s): the pool's own cells count
    ck(len(cells) >= 8 and min(cells[-5:]) >= 10, f'the guest\'s own game populates its pool ({cells}: the last 25 s)')
    ck(away and 'solo=1' in away[0], f'the guest plays its own pool ({away})')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
