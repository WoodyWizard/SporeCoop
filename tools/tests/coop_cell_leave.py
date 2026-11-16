#!/usr/bin/env python3
"""Leaving the Cell stage with mirrors on the guest, then the session ends (player report
2026-10-25, crash 2: the guest's game faulted in modes::cell_at <- cellsync::destroy_local <-
cellsync::on_session_end, in the Creature stage, 30 s after the host's game had crashed).

The guest mirrors the host's tide pool (cellsync); the host finishes the Cell stage the players'
way (food bar full, the cinematic, History, its check: the Creature stage) and the guest follows
with the host's world. The mirrors' pool indices meant nothing any more there, but the old code kept
them and destroyed them when the session ended (the host's BYE or a timeout): `cell_at` read a pool
the game had torn down (cCellGame stays allocated in the Creature stage, its pool does not).

  1. Cell: the guest mirrors the host's cells.
  2. The host enters the Creature stage, the guest follows.
  3. The guest forgot the mirrors (`coop.status`: `cell(guest): ... mirrors=0`).
  4. The host closes the session: the guest's game keeps running, no fault.
  5. The cell console commands outside the Cell stage answer "not in the cell stage" (no fault).

The report's guest got there through a timeout (the host's game had crashed); the host's BYE runs
the same disconnected callback (quitting one game would kill both games of the lane's bottle).
"""
import re
import time

from harness import mode, waiter
import lane
from twoinst import Checks, Monitor, alive, start

STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)


wait = waiter(step=1.0)


def mirrors(con):
    m = re.search(r'cell\(guest\):.*?mirrors=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else None


def faults(con):
    try:
        return con.call('exc')['out'].splitlines()[0]
    except (OSError, ValueError, IndexError):
        return 'no answer'


def main():
    host, guest = start('cell')
    ck = Checks()

    # 1. the shared pool: the guest shows the host's cells as its mirrors
    n = wait(lambda: mirrors(guest), 30)
    ck(n, f'Cell: the guest mirrors the host pool ({n} cells)')

    # 2. the end of the Cell stage, the players' way (coop_editor_desync.py step 5)
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 120 and 'Creature' not in mode(host):
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120),
       f'the guest follows into the Creature stage ({mode(guest).strip()})')
    time.sleep(5)

    # 3. the mirrors' indices pointed into the Cell stage's pool: forgotten once it was left
    n = mirrors(guest)
    ck(n == 0, f'the guest forgot the Cell mirrors in the Creature stage (mirrors={n})')
    fg = faults(guest)
    ck('total faults: 0' in fg, f'no fault on the guest so far ("{fg}")')

    # 4. the session ends on the guest (the disconnected callback: cellsync::on_session_end)
    host.call('coop.leave')
    time.sleep(8)
    up = alive(lane.GUEST)
    ck(up, 'the guest game runs on after the host closed the session')
    fg = faults(guest) if up else 'no answer'
    ck('total faults: 0' in fg, f'and has no fault ("{fg}")')
    print('  guest session:', (guest.call('coop.status')['out'].splitlines() or ['?'])[0] if up else '-')

    # 5. the cell commands outside the Cell stage (they read the Cell stage's game object)
    if up:
        for cmd in ('coop.pool', 'coop.pool why', 'coop.status', 'coop.creations'):
            out = guest.call(cmd)['out']
            if cmd == 'coop.pool':
                ck('not in the cell stage' in out, f'{cmd} in the Creature stage: "{out.strip()[:60]}"')
        fg = faults(guest)
        ck('total faults: 0' in fg, f'no fault after the cell commands ("{fg}")')
    if up and alive(lane.HOST):
        ck.faults(host=host, guest=guest)
    elif up:  # (the host's console can be busy right after its leave: only the guest is checked)
        ck.faults(guest=guest)
    else:
        for e in Monitor.events:
            print('  (monitor)', e)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
