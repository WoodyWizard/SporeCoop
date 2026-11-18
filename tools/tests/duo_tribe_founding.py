"""Automatic native village founding, shared checkpoint and original-species control."""
import argparse
import math
import re
import time

from duo_orders import inspect, wait_until
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--control', action='store_true')
    p.add_argument('--restored', action='store_true')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Vantiegan.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Vantiegan transition profiles')
    expected = re.search(r'player 1 creature: ([0-9A-F]+)/', command(h, 'coop.creations'))[1]
    if args.control:
        if h.call('coop.founders')['ok'] or expected in inspect(h)[0]:
            raise RuntimeError('Old missing automatic founding did not reproduce')
        print('FAIL control reproduced: no guest-species village or founder roster after native Tribal entry.', flush=True)
        command(h, 'coop.fix tribe.auto_founders on')

    def ready():
        for c in (h, g):
            result = c.call('coop.founders')
            if not result['ok'] or expected not in result['out']: return False
            rows = inspect(c)[0].get(expected, {})
            if len(rows) != 3 or any(r[0] != 1 for r in rows.values()): return False
            if 'busy: -' not in command(c, 'coop.status'): return False
        return True

    wait_until(ready, 'Automatic founding/checkpoint did not produce playable guest citizens', 120)
    home = re.search(r'host ([0-9A-F]+)', command(h, 'coop.founders'))[1]
    for c in (h, g):
        groups = inspect(c)[0]
        if home == expected or not groups[home] or any(r[0] != 0 for r in groups[home].values()):
            raise RuntimeError('Automatic founding lost the host species or ownership')
        villages = command(c, 'coop.tribe villages')
        if len(re.findall(rf'species={expected}\b', villages)) != 1:
            raise RuntimeError('Guest species has duplicate native villages')
    command(g, 'coop.tribe select_village')
    groups, done = inspect(h)
    start = {i: row[1] for i, row in groups[expected].items()}
    center = [sum(v[j] for v in start.values()) / len(start) for j in range(3)]
    tangent = [-center[2], 0, center[0]]
    length = math.hypot(*tangent)
    if length < 1: raise RuntimeError('Fixture is too close to the pole for this walking test')
    dest = [center[i] + 40*tangent[i]/length for i in range(3)]
    factor = math.hypot(*center)/math.hypot(*dest)
    dest = [v*factor for v in dest]
    print('Walking from', start, 'toward', dest, flush=True)
    command(g, 'coop.tribe walk ' + ' '.join(f'{v:.2f}' for v in dest))
    wait_until(lambda: inspect(h)[1] >= done+3, 'Automatically founded guest cannot issue native orders')
    for c in (h, g):
        def moved():
            current = inspect(c)[0].get(expected, {})
            return all(i in current and math.dist(pos, dest)-math.dist(current[i][1], dest)>3
                       for i,pos in start.items())
        try:
            wait_until(moved, 'Automatically founded citizens did not walk on both peers', 30)
        except RuntimeError:
            print('Movement failure positions:', inspect(c)[0].get(expected, {}), flush=True)
            raise
        if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native fault occurred')
    time.sleep(6) # at least one further automatic-found tick, without duplicate creation
    for c in (h, g):
        if len(re.findall(rf'species={expected}\b', command(c, 'coop.tribe villages'))) != 1:
            raise RuntimeError('Automatic retry duplicated the guest village')
    print('PASS '+('cold restored' if args.restored else 'automatic')+
          f' native founders {home}/{expected}, one village each, ownership and walking on both peers.', flush=True)


if __name__ == '__main__': main()
