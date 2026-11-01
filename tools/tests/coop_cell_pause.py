#!/usr/bin/env python3
"""Cell: a guest in its pause menu while its mirrors churn keeps the cell broadphase intact (BUG-0348).

The game's cell broadphase (SweepPrune at cCellGame+0x4104) queues each new cell's endpoints in
pending arrays of 2 x 4096 entries (AddObject 0xBBB290, no bound check) and empties them only in its
sweep, inside SimUpdate. In the pause menu SimUpdate does not run, but a guest still creates mirrors
for the host's cells (state rounds). Seen: a 4-player campaign guest sat in the pause menu for over a
minute (lab 20261031-120025 campaign#1, Bob): its broadphase then left 782 objects open per sweep
(BUG-0337's mystery) and its game found a corrupted heap when the Cell pools were freed on the way to
Creature, then froze.

1. Two players in Cell, food 300 on the host (more cells). The guest opens its pause menu (Esc).
2. While it is paused the guest's mirrors are removed --rounds times (`coop.pool zap 5000`); the
   host's next states create them again. The broadphase's pending count (`coop.pool sap` pending=,
   the game's own header) must stay within its arrays (8192 entries).
3. Resume: the broadphase's order is intact (no object left open at the end of the game's sweep
   order, `open_at_end=0`), the guest mirrors the host's cells again.
4. The guest leaves the session and then the Cell stage for the galaxy menu (`coop.menu`: the game
   frees the Cell pools and the broadphase, 0xE7F9B0): its game still answers, and no game faulted (the lab checks the logs and crash reports too).
SPORE_FIX_OFF=cell.sap_pending must FAIL (pending grows past 20000 entries, objects left open).
The churn holds the guest's mirrors on purpose (BUG-0348: no new cell while the pending list is near
full): the guest really lacks the host's cells until it resumes, a sync-check MISMATCH the test makes
itself (BUG-0379: `c: 700 missing` in 6 of 6 runs). The rounds are marked `LAB-MARK sync-force`
for the lab's gate, and after the resume a sync-check of its own must find the two games equal.

  SPORE_LANE=8 python3 tools/tests/coop_cell_pause.py [--rounds 12]
"""
import argparse
import re
import time

import uifind
from twoinst import Checks, lab_mark, start

CAP = 8192  # entries per pending array: 2 x the object capacity (4096)


def sap(con):
    out = con.call('coop.pool sap')['out']
    return {k: int(v) for k, v in re.findall(r'\b(endpoints|pending|held|removed|open_at_end|dup_min|live_objects)=(-?\d+)', out)}


def num(con, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def sync_counts(con):
    m = re.search(r'checks (\d+) ok (\d+) differs (\d+) mismatch (\d+)', con.call('coop.diag')['out'] or '')
    return tuple(int(x) for x in m.groups()) if m else None


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'] or '')
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=12)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and (num(guest, 'mirrors') or 0) < 10:
        time.sleep(1)
    host.call('coop.pool food 300')
    time.sleep(10)
    print('  guest before:', sap(guest), flush=True)
    guest.call('key esc')
    time.sleep(2)
    paused = uifind.shown(guest, 'Resume Game')
    ck(bool(paused), f'1. the guest\'s pause menu is open ({paused})')
    peak = 0
    lab_mark('sync-force', 'on')  # the churn holds the guest's mirrors: missing on purpose until the resume
    for i in range(a.rounds):
        guest.call('coop.pool zap 5000')
        time.sleep(2.5)
        s = sap(guest)
        peak = max(peak, s.get('pending', 0))
        print(f'  round {i + 1}: {s}', flush=True)
    ck(peak <= CAP, f'2. the broadphase\'s pending entries stay within its arrays while paused (peak {peak} of {CAP})')
    for _ in range(3):
        if not uifind.shown(guest, 'Resume Game'):
            break
        uifind.press(guest, 'Resume Game')
        time.sleep(1.0)
    time.sleep(6)
    s = sap(guest)
    print('  guest after resume:', s, flush=True)
    ck(s.get('pending', -1) == 0 and s.get('open_at_end', -1) == 0 and s.get('dup_min', -1) == 0,
       f'3. the broadphase\'s order is intact after the pause (pending {s.get("pending")}, left open {s.get("open_at_end")}, '
       f'duplicates {s.get("dup_min")})')
    m = num(guest, 'mirrors') or 0
    ck(m > 100, f'3. the guest mirrors the host\'s cells again ({m})')
    time.sleep(4)
    lab_mark('sync-force', 'off')
    # the host's own sync-check: the guest has the host's cells again (an OK, no difference)
    c0 = sync_counts(host)
    host.call('coop.diag sync now')
    c1 = c0
    t0 = time.time()
    while time.time() - t0 < 20 and c0 and c1 and c1[1] == c0[1] and c1[3] == c0[3]:  # an OK or a MISMATCH
        time.sleep(1)
        c1 = sync_counts(host)
    print(f'  host sync-check counts (checks ok differs mismatch): {c0} -> {c1}', flush=True)
    ck(c0 and c1 and c1[1] > c0[1] and c1[3] == c0[3],
       f'3. after the resume the host\'s sync-check finds the guest equal ({c0} -> {c1})')
    print('  guest:', guest.call('coop.leave')['out'].strip()[:100], flush=True)
    time.sleep(3)
    # out of the Cell stage (the galaxy menu): the game frees the Cell pools and the broadphase
    # (0xE7F9B0), where the campaign's guest found the corrupted heap
    print('  guest:', (guest.call('coop.menu').get('out') or '').strip()[:100], flush=True)
    t0 = time.time()
    while time.time() - t0 < 40:
        time.sleep(2)
        try:
            if 'Cell' not in (guest.call('game.mode').get('out') or ''):
                break
        except (OSError, ValueError):
            break
    try:
        mode = (guest.call('game.mode').get('out') or '').strip()
    except (OSError, ValueError) as e:  # a frozen or dead game
        mode = ''
        print(f'  guest console: {e!r}', flush=True)
    print('  guest after leaving:', mode[:100], flush=True)
    ck(bool(mode) and 'Cell' not in mode, f'4. the guest\'s game answers after leaving the Cell stage ({mode[:40]})')
    for name, con in (('host', host), ('guest', guest)):
        n = faults(con)
        ck(n == 0, f'4. {name}: no caught fault ({n})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
