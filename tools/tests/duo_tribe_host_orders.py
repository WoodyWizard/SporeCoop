"""Check native selection and walking of a rebuilt host village on a loaded disposable pair."""
import argparse
import math
import re

from coop_ability_bar import eax, u32
from duo_orders import inspect, wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--planet', required=True)
    parser.add_argument('--species', required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    species = f'{int(args.species, 16):08X}'
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe did not load.', 60)
        if f"world '{args.planet}'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Wrong disposable planet.')
    command(host, 'coop.tribe village_orders on')
    try:
        def owned():
            groups = [inspect(client)[0].get(species, {}) for client in (host, guest)]
            return all(len(group) == 3 and all(owner == 0 for owner, _ in group.values()) for group in groups)
        wait_until(owned, 'The rebuilt host citizens were not owned by the host on both peers.')
        manager = eax(host, 'Simulator::cGameNounManager::Get cdecl')
        home = eax(host, f'Simulator::cGameNounManager::GetPlayerTribe thiscall {manager:#x}')
        if not home or u32(host, home + 0x30) != 0x800021CA:
            raise RuntimeError('The native player-tribe reference was not preserved.')
        for index in range(3):
            if command(host, f'coop.tribe select_own {index} 1').strip() != 'selected=1':
                raise RuntimeError('Native host selection failed.')
        start = {i: row[1] for i, row in inspect(host)[0][species].items()}
        center = [sum(pos[j] for pos in start.values()) / len(start) for j in range(3)]
        target = [center[0] + 26, center[1], center[2] - 20]
        scale = math.hypot(*center) / math.hypot(*target)
        target = [value * scale for value in target]
        command(host, 'coop.tribe walk ' + ' '.join(f'{value:.2f}' for value in target))
        for role, client in (('host', host), ('guest', guest)):
            def moved():
                now = inspect(client)[0].get(species, {})
                return all(i in now and math.dist(position, target) - math.dist(now[i][1], target) > 4
                           for i, position in start.items())
            wait_until(moved, f'The rebuilt host citizens did not walk on {role}.', 30)
            if not re.search(r'total faults: 0\b', command(client, 'exc')):
                raise RuntimeError('Native faults occurred.')
        print('PASS the rebuilt native host village retained its player reference; all three selected citizens walked on both peers.', flush=True)
    finally:
        for index in range(3):
            host.call(f'coop.tribe select_own {index} 0')
        command(host, 'coop.tribe village_orders off')


if __name__ == '__main__':
    main()
