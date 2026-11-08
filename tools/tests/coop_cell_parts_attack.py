#!/usr/bin/env python3
"""Cell: a guest's electric part and poison part hurt the host's creatures (BUG-0715, BUG-0716, class BUG-0428).

The electric part's chain attack (0xE71520) writes the targets' hp itself (no DamageCell): on a guest the
targets are mirrors of the host's creatures, so their hp came back with the host's next state and the
host's creatures took nothing. A poison cloud is a cell the guest's own game makes for its avatar; its
hits on a mirror (DamageCell with the cloud as the attacker) were dropped: only the avatar's counted.

The guest's avatar borrows the model of a cell in the pool with that part (`coop.pool borrow electric|poison`:
the game's own electric step 0xE71980 / poison step 0xE76880 then run for it; none in the pool: `... graft`, the
part written into the avatar's own model data as a creation with it has), next to a host creature
given hp 60 (`coop.pool npcflag`).
1. --part electric: within --seconds the host's game applies type-1 hits of the guest (`guest_attack_types=`
   of the host's `coop.pool`, the damage type of the electric animation) and the creature's hp drops in
   the HOST's game; the guest's game zapped (`zaps=`).
2. --part poison: hits of the guest's poison clouds reach the host (`product_hits=` on the guest, the host's
   guest_attacks grow) and the creature's hp drops in the host's game.
SPORE_FIX_OFF=cell.electric (--part electric) / cell.own_products (--part poison) must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_cell_parts_attack.py --part electric|poison [--seconds 40]
"""
import argparse
import re
import time

from twoinst import Checks, start


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def types(host):
    m = re.search(r'guest_attack_types=([\d/]+)', host.call('coop.pool')['out'])
    return [int(x) for x in m.group(1).split('/')] if m else [0] * 6


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', choices=('electric', 'poison'), default='electric')
    ap.add_argument('--seconds', type=float, default=40)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    got = ''
    for _ in range(20):  # a cell of the pool with that part (mirrors come in as the guest looks around)
        got = guest.call(f'coop.pool borrow {a.part}' + (' graft' if _ >= 6 else ''))['out'].strip()
        if got.startswith('borrowed'):
            break
        guest.call(f'coop.pool tp {(_ % 4 - 1.5) * 40:.0f} {(_ % 3 - 1) * 40:.0f} 0')
        time.sleep(2)
    print('  guest: ' + got, flush=True)
    ck(got.startswith('borrowed'), f'the guest\'s avatar has the game\'s {a.part} part ({got})')
    if not got.startswith('borrowed'):
        return ck.result()
    key, r = None, ''
    for _ in range(15):
        r = guest.call('coop.pool kill - hit')['out']  # names the nearest creature (one bite)
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'a host creature next to the guest ({r.strip()[:100]})')
    if not key:
        return ck.result()
    print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'].strip())
    time.sleep(2.5)
    t0_types, g0 = types(host), guest.call('coop.pool')['out']
    hp0 = num(host.call(f'coop.pool cell {key}')['out'], 'hp')
    low, t0 = hp0, time.time()
    while time.time() - t0 < a.seconds:
        guest.call(f'coop.pool swimto {key} tp')  # stays next to it (the electric step needs a target near its parts)
        if a.part == 'poison':
            time.sleep(0.3)
            guest.call('coop.pool goto 6 0')  # moving makes the clouds (the step grows with the speed)
        time.sleep(1.5)
        st = host.call(f'coop.pool cell {key}')['out']
        hp = num(st, 'hp')
        if hp is not None and low is not None:
            low = min(low, hp)
        if 'no such cell' in st or 'dying=1' in st or (low is not None and hp0 is not None and low <= hp0 - 6):
            break
    t1_types, g1 = types(host), guest.call('coop.pool')['out']
    print('  guest: ' + guest.call('coop.pool borrow status')['out'].strip(), flush=True)
    dt = [b - x for b, x in zip(t1_types, t0_types)]
    print(f'  host: hp {hp0} -> {low}, guest attack types +{dt}', flush=True)
    for k in ('zaps', 'zap_hits', 'product_hits', 'product_pairs', 'products', 'hits_sent'):
        print(f'  guest {k}: {num(g0, k)} -> {num(g1, k)}', flush=True)
    m = re.search(r'foreign_hits=([\d/]+)', g1)
    print(f'  guest foreign_hits (mirror/puppet/other): {m.group(1) if m else "?"}', flush=True)
    if a.part == 'electric':
        ck((num(g1, 'zaps') or 0) > (num(g0, 'zaps') or 0), '1. the guest\'s game ran its electric attack')
        ck(dt[1] > 0, f'1. the host\'s game applied the guest\'s electric hits (type 1: +{dt[1]})')
    else:
        ck((num(g1, 'product_hits') or 0) > (num(g0, 'product_hits') or 0), '2. the guest\'s poison clouds hit a creature')
        ck(sum(dt) - dt[4] > 0, f'2. the host\'s game applied hits of the guest\'s poison, not bites ({dt})')
    ck(hp0 is not None and low is not None and low < hp0, f'the creature lost health in the host\'s game ({hp0} -> {low})')
    guest.call('coop.pool borrow off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
