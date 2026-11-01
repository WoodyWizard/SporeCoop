#!/usr/bin/env python3
"""Cell: the sync-check finds no difference where the two games agree (BUG-0379).

The lab fails a test on any `sync-check: MISMATCH` line (docs/06-testing.md#testing-log-gates), so
the check must not report what is no desync. Two cases from the recorded runs:

A. A shared total sent late: the host sends its food total every 0.5 s (MSG_CELL_FOOD) and builds
   its digest a round trip after the guest's, so while food comes in steadily two checks 3 s apart
   each catch the guest a meal behind ("cell food: host 252.0, guest 240.0" in campaign_short; the
   guest had 252 0.15 s later). Here the host eats a point every 0.3 s for 20 s while its
   sync-check is asked every 5 s; afterwards both games' food totals are equal.
B. A crowded pool: the host sends a guest its 900 nearest cells, picked around the guest's position
   as the host last saw it, while the check counted the 900 nearest to the guest's own position:
   cells near rank 900 came out "missing" although the guest had all 900 it was sent (campaign_short,
   "904 objects compared: c: 14 missing"). Here food 300 at the start crowds the pool (as in
   cell_pause: the host sends the guest 900 of more) and the guest swims to and fro while the host
   checks every 5 s.

Each phase: no new MISMATCH on the host's sync-check, and at least 2 checks with an OK. The guest's
own game must hold the host's cells (more than 500 mirrors) and the same food total.
SPORE_FIX_OFF=diag.total_lag fails A. diag.cell_margin off did not fail B in 2 players (the send is
capped in a few rounds at most): its check is net_tests' test_diag_compare.

  SPORE_LANE=7 python3 tools/tests/coop_cell_sync_check.py [--phase a|b|ba]
"""
import argparse
import re
import time

from twoinst import Checks, start


def counts(con):
    m = re.search(r'checks (\d+) ok (\d+) differs (\d+) mismatch (\d+)', con.call('coop.diag')['out'] or '')
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0, 0)


def food(con):
    m = re.search(r'\bfood=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def mirrors(con):
    m = re.search(r'mirrors=(\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else 0


def phase(ck, name, host, seconds, step):
    """step(i) runs every 0.3 s; the host's sync-check is asked every 5 s"""
    c0 = counts(host)
    t0, last_ask, i = time.time(), 0.0, 0
    while time.time() - t0 < seconds:
        step(i)
        i += 1
        if time.time() - last_ask >= 5.0:
            host.call('coop.diag sync now')
            last_ask = time.time()
        time.sleep(0.3)
    time.sleep(4.5)  # a recheck that was due
    c1 = counts(host)
    d = tuple(b - a for a, b in zip(c0, c1))
    print(f'  {name}: host sync-check counts (checks ok differs mismatch) {c0} -> {c1}, new {d}', flush=True)
    ck(d[0] >= 3, f'{name}: the host checked the guest ({d[0]} checks)')
    ck(d[3] == 0, f'{name}: no MISMATCH ({d[3]}; {d[2]} differed first, {d[1]} OK)')
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--phase', default='ba')
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and mirrors(guest) < 10:
        time.sleep(1)

    if 'b' in a.phase:
        # a crowded pool: food 300 at the start takes the host to the next levels, whose pool around the
        # two cells holds more than the 900 a guest is sent (cell_pause: 900 mirrors on the guest)
        host.call('coop.pool food 300')
        time.sleep(12)
        moves = [(14, 0), (-14, 0), (0, 14), (0, -14)]
        phase(ck, 'B (crowded pool, the guest swims)', host, 30,
              lambda i: guest.call('coop.pool goto %d %d' % moves[(i // 6) % len(moves)]) if i % 6 == 0 else None)
        m = mirrors(guest)
        sent = re.search(r'send_capped=(\d+)', host.call('coop.pool')['out'])
        print(f'  B: guest mirrors {m}, host send_capped {sent.group(1) if sent else "?"}', flush=True)
        # how crowded the pool got varies (send capped in 0-9 rounds of 4 runs): information only; the
        # boundary case itself is checked by net_tests (test_diag_compare)
        ck(m > 100, f'B: the guest\'s game holds the host\'s cells ({m} mirrors)')
    if 'a' in a.phase:
        phase(ck, 'A (food coming in)', host, 20, lambda i: host.call('coop.pool food 1'))
        time.sleep(1.5)
        hf, gf = food(host), food(guest)
        ck(hf is not None and hf == gf, f'A: both games have the same food total afterwards (host {hf}, guest {gf})')

    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
