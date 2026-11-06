"""Check last-villager rescue and paid respawn on a disposable two-founder Clyth world."""
import argparse
import math
import re
import time

from coop_ability_bar import eax, u32
from duo_food import food
from duo_orders import wait_until
from duo_villages import Console, command


def citizens(client):
    result = {}
    species = None
    for line in command(client, 'coop.tribe').splitlines():
        header = re.match(r'tribe \d+.* species=([0-9A-F]+)', line)
        if header:
            species = header[1]
        row = re.match(r'  #(\d+) ([0-9A-F]+) owner=.* hp=([.\d]+) pos=\(([-.\d ]+)\).* dead=(\d+).* revive=([.\d]+)', line)
        if row:
            result[int(row[1])] = dict(ptr=int(row[2], 16), species=species, hp=float(row[3]),
                                     pos=tuple(map(float, row[4].split())), dead=int(row[5]), timer=float(row[6]))
    return result


def move(client, body, position_pointer):
    """Test placement through Spore's own SetTransform and Stop methods."""
    spatial = body + 0xC0
    table = u32(client, spatial)
    eax(client, f'{u32(client, table + 0xEC):#x} thiscall {spatial:#x}')
    eax(client, f'{u32(client, table + 0x44):#x} thiscall {spatial:#x} {position_pointer:#x} {body + 0xD0:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--victim', choices=('host', 'guest'), required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    own = '067B3E4A' if args.victim == 'host' else '0680BF38'
    other = '0680BF38' if args.victim == 'host' else '067B3E4A'
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'The tribal world did not finish loading.', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Open disposable Clyth copies on both instances first.')
        if own not in command(client, 'coop.founders'):
            raise RuntimeError('Missing founding species.')
    command(host, 'coop.tribe village_orders on')
    rows = citizens(host)
    victims = [i for i, r in rows.items() if r['species'] == own and not r['dead']]
    helpers = [i for i, r in rows.items() if r['species'] == other and not r['dead']]
    if not victims or not helpers:
        raise RuntimeError('Both villages must have a living member.')
    victim, helper = victims[0], helpers[0]
    victim_ptr, helper_ptr = rows[victim]['ptr'], rows[helper]['ptr']
    own_tribe = u32(host, victim_ptr + 0x1014)
    helper_tribe = u32(host, helper_ptr + 0x1014)
    command(host, f'call SporeCoop::cTribe_AddFood thiscall {own_tribe:#x} 100')
    command(host, f'call SporeCoop::cTribe_AddFood thiscall {helper_tribe:#x} 100')
    for member in victims[1:]:
        command(host, f'coop.tribe pincer {member} {helper} 1000')
    def others_gone():
        current = citizens(host)
        return all(i not in current or current[i]['dead'] for i in victims[1:])
    wait_until(others_gone, 'Other members survived the test setup.')
    wait_until(lambda: not citizens(host)[victim]['timer'], 'An earlier knockout did not finish.')
    time.sleep(2.2)
    move(host, helper_ptr, victim_ptr + 0xC4)
    before = food(host)[own]
    command(host, f'coop.tribe pincer {victim} {helper} 1000')
    wait_until(lambda: citizens(host)[victim]['timer'] > 0 and citizens(guest)[victim]['timer'] > 0,
               'Both peers must show the rescue timer.', 2)
    if host.call('coop.save')['ok']:
        raise RuntimeError('Saved a pending rescue.')
    wait_until(lambda: citizens(host)[victim]['hp'] > 1 and citizens(host)[victim]['timer'] == 0,
               'The partner did not revive the last member.', 5)
    if food(host)[own] < before - 1:
        raise RuntimeError('Revival debited the village food.')
    print(f'{args.victim} last villager revived on both games.', flush=True)

    time.sleep(2.2)
    for member in helpers:
        move(host, rows[member]['ptr'], helper_tribe + 0x124)
    # Return the victim to its own food mat so the partner can stay well away.
    mat = u32(host, own_tribe + 0x260)
    move(host, victim_ptr, mat + 0x38)
    wait_until(lambda: all(math.dist(citizens(host)[victim]['pos'], citizens(host)[i]['pos']) > 25 for i in helpers),
               'Could not separate the partner from the next knockout.')
    command(host, f'coop.tribe pincer {victim} {helper} 1000')
    wait_until(lambda: citizens(host)[victim]['timer'] > 0, 'No second knockout.')
    before = food(host)[own]
    expected = before - min(before, math.ceil(before * 0.10))
    time.sleep(3)
    for client in (host, guest):
        row = citizens(client)[victim]
        if row['hp'] != 1 or row['dead'] or row['timer'] <= 0:
            raise RuntimeError('A pending rescue healed, died or lost its timer.')
    wait_until(lambda: citizens(host)[victim]['hp'] > 1 and citizens(host)[victim]['timer'] == 0,
               'No timed respawn.', 10)
    wait_until(lambda: food(host)[own] == food(guest)[own], 'The native food stores diverged.')
    after = food(host)[own]
    if abs(after - expected) > 1:
        raise RuntimeError(f'Wrong food debit, expected {expected}, got {after}.')
    time.sleep(2)
    for client in (host, guest):
        row = citizens(client)[victim]
        if row['species'] != own or row['dead'] or row['hp'] <= 1 or row['timer']:
            raise RuntimeError('Respawn changed species or did not remain alive.')
        if abs(food(client)[own] - after) > 2:
            raise RuntimeError('Possible repeated respawn debit.')
        if 'total faults: 0' not in command(client, 'exc'):
            raise RuntimeError('Native game fault.')
    print(f'{args.victim} timed respawn kept species {own}, village food {before} -> {after}, both games fault-free.')


if __name__ == '__main__':
    main()
