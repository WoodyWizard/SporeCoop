#!/usr/bin/env python3
"""Creature: the species relationship table on a guest is the host's (BUG-0699; class BUG-0419).

The detector's `Creature:state:species_relation / species_killed / species_only` rows: a species with another
relation or kill count on a guest than on the host, still there 1.5 s later.

1. Two games on the creature fixture. Both games' tables (`coop.social species`, the game's own records) are
   compared: every record with a relation on either game has the same relation, impressed and killed on both.
2. The host's game changes records the way its play does (`coop.social record <crt> <relation> <impressed>`
   on three species of the table: hostile, impressed some, allied); 3 s later the guest's game has them.
3. The guest's own game changes a record (a kill, as its game counts a mirror's death: the same debug write on
   the guest); 3 s later the guest's game is back at the host's values.
"""
import re
import time

from twoinst import Checks, start


def table(con):
    t = {}
    for m in re.finditer(r'species ([0-9A-F]{8})/\S+ relation=(-?\d+) x=\d+ impressed=(\d+) killed=(\d+)',
                         con.call('coop.social species')['out']):
        t[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return t


def diff(h, g):
    out = []
    for k in sorted(set(h) | set(g)):
        a, b = h.get(k), g.get(k)
        if k[:2] in ('7C', '7D'):
            continue  # a guest's key of a received creation: no partner by design
        if (a or (0, 0, 0)) != (b or (0, 0, 0)) and ((a and a[0]) or (b and b[0])):
            out.append(f'{k}: host {a} guest {b}')
    return out


def settle(host, guest, s=3.0):
    time.sleep(s)
    h, g = table(host), table(guest)
    return h, g, diff(h, g)


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    h, g, d = settle(host, guest, 0)
    print(f'  host table {len(h)} records, guest {len(g)}; differences: {d}', flush=True)
    ck(not d, f'1. the guest has the host\'s table ({d[:3]})')
    keys = [k for k in sorted(h) if k[:2] not in ('7C', '7D')][:3]
    # how long a change of the host's table takes to be in the guest's game: the detector looks again 1.5 s after
    # its first dump (worlddiff CELL_GAP), so a change must be there well within that
    delays = []
    for i, (rel, imp) in enumerate(((1, 0), (3, 1), (6, 3), (2, 0), (3, 2), (1, 1))):
        k = keys[i % len(keys)] if keys else None
        if not k:
            break
        time.sleep(0.37 * i % 1.0)  # at another phase of the host's send cadence each time
        host.call(f'coop.social record {k} {rel} {imp}')
        t0 = time.time()
        while time.time() - t0 < 5 and table(guest).get(k, (0, 0, 0))[:2] != (rel, imp):
            time.sleep(0.05)
        delays.append(round(time.time() - t0, 2))
    print(f'  the guest had the host\'s change after {delays} s', flush=True)
    ck(delays and max(delays) <= 1.0, f"2a. a change of the host's table is in the guest's game within 1 s ({delays})")
    h, g, d = settle(host, guest)
    print(f'  after the host\'s changes: {[(k, h.get(k), g.get(k)) for k in keys]}', flush=True)
    ck(keys and not d, f"2. the host's changed records are on the guest ({d[:3]})")
    if keys:
        print(' ', guest.call(f'coop.social record {keys[0]} 3 2')['out'].strip())
    h, g, d = settle(host, guest)
    print(f'  after the guest\'s own change: {keys[:1] and (h.get(keys[0]), g.get(keys[0]))}', flush=True)
    ck(not d, f"3. the guest's own change is undone ({d[:3]})")
    print('  guest:', guest.call('coop.social')['out'].strip().replace('\n', ' | ')[:400])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
