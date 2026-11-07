#!/usr/bin/env python3
"""BUG-0634: Cell: does the guest's cell swim through the other cells?

The guest's NPC cells are mirrors of the host's (placed at the host's pose every frame, AI-locked); its own
cell collides with them in its own game (HandleCollisionPair, the contact solver). Each player in turn
swims its own cell into an NPC creature of about its size (`coop.pool swimto <key>`: the move target on
the creature's centre, as a held click) for 3 s and the smallest centre distance is measured against the
two sizes (`coop.pool gap`). A cell that swims through the other gets to ~0; a pushed one stays near the
sum of the two radii. The host's own game is the reference (a single player's collisions); the guest's
overlap must be about the host's.

  coop_cell_push.py [--rounds 6]
"""
import argparse
import re
import time

from twoinst import Checks, start


def gap(con, key):
    m = re.search(r'd=([\d.]+) own=([\d.]+) other=([\d.]+) own_pairs=(\d+) particles=(-?\d+)/(-?\d+)', con.call(f'coop.pool gap {key}')['out'])
    return (float(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4)), m.group(5) + '/' + m.group(6)) if m else None


def nearest_creature(con, ratio=(0.6, 1.8)):
    out = con.call('coop.pool see 400')['out']
    for l in out.splitlines():
        f = l.split()
        if len(f) > 3 and f[0] == 'see' and f[2] == 'creature' and ratio[0] <= float(f[3]) <= ratio[1]:
            return f[1]
    return None


def push(con, key):
    """swim into <key> for 3 s: the smallest centre distance / (own + other size)"""
    best, pairs0, info = 9e9, None, ''
    t0 = time.time()
    while time.time() - t0 < 3.0:
        con.call(f'coop.pool swimto {key}')
        g = gap(con, key)
        if not g:
            break
        d, a, b, pairs, parts = g
        pairs0 = pairs if pairs0 is None else pairs0
        if a + b > 0 and d / (a + b) < best:
            best, info = d / (a + b), f'd={d:.2f} sizes {a:.2f}+{b:.2f} particles {parts}'
        time.sleep(0.05)
    g = gap(con, key)
    return best, info, (g[3] - pairs0) if g and pairs0 is not None else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    res = {'host': [], 'guest': []}
    for i in range(a.rounds * 2):
        who, con = ('host', host) if i % 2 == 0 else ('guest', guest)
        key = nearest_creature(con)
        if not key:
            print(f'  {who}: no creature of its size on screen ({con.call("coop.pool see 400")["out"].count("creature")} creatures)')
            con.call('coop.pool tp 25 0' if who == 'guest' else 'coop.pool tp -25 0')
            time.sleep(3)
            continue
        con.call(f'coop.pool swimto {key} tp')
        time.sleep(0.3)
        r, info, pairs = push(con, key)
        res[who].append(r)
        print(f'  {who} into {key}: closest {r:.2f} of the two sizes ({info}), own pairs +{pairs}', flush=True)
        con.call('coop.pool tp 20 0' if who == 'guest' else 'coop.pool tp -20 0')
        time.sleep(1.5)
    for who in ('host', 'guest'):
        ck(len(res[who]) >= a.rounds // 2, f'{who}: {len(res[who])} pushes')
    h = sorted(res['host'])[len(res['host']) // 2] if res['host'] else 0
    g = sorted(res['guest'])[len(res['guest']) // 2] if res['guest'] else 0
    ck(g >= h * 0.6, f'the guest\'s cell is pushed by the cells as the host\'s is (median closest: guest {g:.2f}, host {h:.2f} of the sizes)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
