#!/usr/bin/env python3
"""Cell: re-creating the guest's mirrors again and again does not drain its address space (BUG-0307).

Seen in the 4-player campaign (lab 20261030-104149 campaign#2): a guest whose own cell went NaN 26
times had its mirrors replaced over and over (900 -> 300 -> 40 -> 900) and its free address space
fell from 3502 to 1427 MB in 6 minutes while the other games stayed near 3500 MB.

The guest's game removes all its mirror cells (`coop.pool zap 5000`), the host's next state creates
them again; this is done `--rounds` times (default 40). `vm_free_mb` of `coop.pool` (the game's
free address space) is read before and after.

1. Each round the mirrors come back (the guest mirrors the host's cells).
2. The free address space after the rounds is within `--budget` MB (default 150) of before.

  SPORE_LANE=2 python3 tools/tests/coop_cell_churn.py [--rounds 40] [--budget 150]
"""
import argparse
import re
import time

from twoinst import Checks, lab_mark, start


def num(con, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=40)
    ap.add_argument('--budget', type=int, default=150)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and (num(guest, 'mirrors') or 0) < 10:
        time.sleep(1)
    host.call('coop.pool food 300')  # a few levels up: more and bigger cells, as at level 3-4
    time.sleep(10)
    m0, v0 = num(guest, 'mirrors'), num(guest, 'vm_free_mb')
    print(f'  guest: {m0} mirrors, {v0} MB free', flush=True)
    low = []
    lab_mark('sync-force', 'on')  # the rounds remove the guest's mirrors on purpose (BUG-0379)
    for i in range(a.rounds):
        guest.call('coop.pool zap 5000')
        time.sleep(2.5)
        m = num(guest, 'mirrors')
        v = num(guest, 'vm_free_mb')
        if i % 5 == 4:
            print(f'  round {i + 1}: {m} mirrors, {v} MB free, created={num(guest, "created")}', flush=True)
        if (m or 0) < 10:
            low.append(i)
    lab_mark('sync-force', 'off')
    time.sleep(5)
    v1 = num(guest, 'vm_free_mb')
    ck(len(low) <= a.rounds // 10, f'1. the mirrors came back after the rounds ({a.rounds - len(low)} of {a.rounds})')
    ck(v0 is not None and v1 is not None and v0 - v1 <= a.budget,
       f'2. the guest\'s free address space stays within {a.budget} MB ({v0} -> {v1} MB after {a.rounds} rounds)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
