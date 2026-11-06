"""Revive a founder village after lethal hut damage on disposable Clyth copies."""
import argparse
import struct

from coop_ability_bar import mem, u32
from duo_food import food
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def scalar(client, address):
    return struct.unpack('<f', mem(client, address, 4))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--victim', choices=('host', 'guest'), required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    own, other = ('067B3E4A', '0680BF38') if args.victim == 'host' else ('0680BF38', '067B3E4A')
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe not loaded', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth copies only.')
    command(host, 'coop.tribe village_orders on')
    rows = citizens(host)
    victims = [r for r in rows.values() if r['species'] == own and r['hp'] > 0 and not r['dead']]
    helper = next(r['ptr'] for r in rows.values() if r['species'] == other and r['hp'] > 0 and not r['dead'])
    enemy = next(r['ptr'] for r in rows.values() if r['species'] == '06785BC2' and not r['dead'])
    tribe = u32(host, victims[0]['ptr'] + 0x1014)
    native_id, faction = u32(host, tribe + 0x24), u32(host, tribe + 0x30)
    hut = u32(host, tribe + 0x368)
    maximum = scalar(host, hut + 0x14C)
    for victim in victims:
        move(host, victim['ptr'], tribe + 0x124)
    partner = u32(host, helper + 0x1014)
    move(host, helper, partner + 0x124)
    command(host, f'call SporeCoop::cTribe_AddFood thiscall {tribe:#x} 100')
    before = food(host)[own]
    command(host, f'call SporeCoop::cTribeHut_TakeDamage thiscall {hut + 0x120:#x} '
                  f'f:1000 0x21e0 0 {enemy + 0xC4:#x} {enemy + 0x5A8:#x}')

    def downed():
        return next((r for r in citizens(host).values() if r['species'] == own and r['timer'] > 0), False)

    representative = wait_until(downed, 'Hut destruction did not start a rescue timer', 3)
    if scalar(host, hut + 0x158) != 1 or host.call('coop.save')['ok']:
        raise RuntimeError('The pending hut rescue was not held or could be saved prematurely.')
    move(host, helper, representative['ptr'] + 0xC4)
    wait_until(lambda: scalar(host, hut + 0x158) > 1, 'Partner did not revive the village', 5)
    if abs(scalar(host, hut + 0x158) - maximum * 0.4) > 0.1 or food(host)[own] < before - 3:
        raise RuntimeError('Revival applied the wrong hut health or charged a food-loss penalty.')
    wait_until(lambda: not host.call('coop.avatar')['out'].startswith('Tribe:'), 'Revival did not refresh the world', 45)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Revival reload did not finish', 60)
        current = next(t for t, _ in objects(client, 0x18C6D19) if u32(client, t + 0x30) == faction)
        building = u32(client, current + 0x368)
        if (u32(client, current + 0x24) != native_id or u32(client, building + 0x230) != current or
                scalar(client, building + 0x158) <= 0):
            raise RuntimeError('Revival changed the settlement identity or lost the hut.')
        population = [r for r in citizens(client).values() if r['species'] == own and r['hp'] > 0 and not r['dead']]
        if len(population) != len(victims):
            raise RuntimeError('Revival lost villagers.')
        if 'total faults: 0' not in command(client, 'exc'):
            raise RuntimeError('Native faults occurred.')
    print(f'PASS hut rescue preserved faction {faction:08X}, native settlement, {len(victims)} villagers and food on both peers.', flush=True)


if __name__ == '__main__':
    main()
