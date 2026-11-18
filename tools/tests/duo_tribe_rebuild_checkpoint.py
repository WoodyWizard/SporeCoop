"""Read native captured-site ownership and rebuilt citizens after a checkpoint load.

This checks a disposable native prototype; it does not perform capture or rebuilding.
The record contains old_pos, new_pos, conqueror_faction and founder_faction.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_orders import wait_until
from duo_villages import Console, command


def objects(client, noun):
    result = []
    for line in command(client, f'coop.tribe objs {noun:X}').splitlines():
        match = re.match(r'([0-9A-F]+) alive=1 pos=\(([-.\d ]+)\)', line)
        if match:
            result.append((int(match[1], 16), tuple(map(float, match[2].split()))))
    return result


def nearest(rows, position):
    if not rows:
        raise RuntimeError('The native building list is empty.')
    pointer, actual = min(rows, key=lambda row: math.dist(row[1], position))
    if math.dist(actual, position) >= 30:
        raise RuntimeError('No native building exists at the recorded settlement site.')
    return pointer


def members(client, tribe):
    begin, end = u32(client, tribe + 0x340), u32(client, tribe + 0x344)
    if not begin or not 0 < end - begin <= 1024 or (end - begin) % 4:
        raise RuntimeError('The rebuilt tribe has an empty or invalid member vector.')
    return [u32(client, at) for at in range(begin, end, 4)]


def verify(client, record, args):
    wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'),
               'The Tribal checkpoint did not load.', 60)
    if f"world '{args.planet}'" not in command(client, 'coop.avatar'):
        raise RuntimeError('Wrong checkpoint planet.')
    huts, mats = objects(client, 0x1E4DAAE), objects(client, 0x629BAFE)
    old_hut, new_hut = [nearest(huts, record[key]) for key in ('old_pos', 'new_pos')]
    old_mat, new_mat = [nearest(mats, record[key]) for key in ('old_pos', 'new_pos')]
    if old_hut == new_hut or old_mat == new_mat:
        raise RuntimeError('The occupied and rebuilt settlements must be separate.')
    conqueror, founder = u32(client, old_hut + 0x230), u32(client, new_hut + 0x230)
    campaign = record.get('campaign')
    if campaign:
        totem = u32(client,founder+0x19D0) if founder else 0
        if not totem or u32(client,totem+0x2C) != founder:
            raise RuntimeError('Reload lost the rebuilt campaign totem or native owner')
        for offset, key in ((0x11C,'segments'),(0x130,'colors')):
            begin, end = u32(client,totem+offset), u32(client,totem+offset+4)
            if end-begin != len(campaign[key]) or list(mem(client,begin,end-begin)) != campaign[key]:
                raise RuntimeError(f'Reload changed earned totem {key}')
        if u32(client,founder+0x38) != campaign['upgrade'] or u32(client,new_hut+0x14C) != campaign['hut_max_health']:
            raise RuntimeError('Reload changed earned tribe level or upgraded native hut health')
        if math.dist(struct.unpack('<3f',mem(client,totem+0x38,12)),record['new_pos']) > 60:
            raise RuntimeError('Rebuilt totem is not at the new village')
        print('PASS native totem segments/colors, owner, position and upgraded hut survive reload.',flush=True)
    if not conqueror or not founder or conqueror == founder:
        raise RuntimeError('Missing or shared native building owners.')
    checks = (
        u32(client, conqueror + 0x30) == record['conqueror_faction'],
        u32(client, founder + 0x30) == record['founder_faction'],
        u32(client, old_hut + 0x30) == record['conqueror_faction'],
        u32(client, old_mat + 0x30) == record['conqueror_faction'],
        u32(client, old_mat + 0x10C) == conqueror,
        u32(client, new_hut + 0x30) == record['founder_faction'],
        u32(client, new_mat + 0x30) == record['founder_faction'],
        u32(client, new_mat + 0x10C) == founder,
        u32(client, founder + 0x368) == new_hut,
        u32(client, founder + 0x260) == new_mat,
        u32(client, conqueror + 0x368) != old_hut,
    )
    if not all(checks):
        raise RuntimeError('Native ownership or primary-building references changed on load.')
    if 'new_native_id' in record and u32(client, founder + 0x24) != record['new_native_id']:
        raise RuntimeError('The rebuilt native object identity changed on load.')
    if 'old_native_id' in record and any(u32(client, tribe + 0x24) == record['old_native_id']
                                       for tribe, _ in objects(client, 0x18C6D19)):
        raise RuntimeError('The retired native tribe object remains in the world.')
    people = members(client, founder)
    chief = u32(client, founder + 0x11C)
    if len(people) != args.population or chief not in people:
        raise RuntimeError(f'Rebuilt native population={len(people)} expected={args.population}; '
                           f'chieftain={chief:08X} belongs_to_population={chief in people}.')
    for person in people:
        health = struct.unpack('<f', mem(client, person + 0x5E0, 4))[0]
        if (u32(client, person + 0xB28) != args.species or
                u32(client, person + 0x30) != record['founder_faction'] or
                u32(client, person + 0x1014) != founder or
                not math.isfinite(health) or health <= 0 or mem(client, person + 0xB5E, 1) != b'\0'):
            raise RuntimeError('A rebuilt citizen has the wrong species, owner or native life state.')
    if args.retired_faction is not None:
        if any(u32(client, tribe + 0x30) == args.retired_faction
               for tribe, _ in objects(client, 0x18C6D19)):
            raise RuntimeError('The retired settlement remains in the native live-object list.')
    if not re.search(r'total faults: 0\b', command(client, 'exc')):
        raise RuntimeError('The game reported a native fault.')
    print(f'PASS old site owner {record["conqueror_faction"]:08X}; '
          f'new faction {record["founder_faction"]:08X}, {len(people)} living '
          f'{args.species:08X} citizens and a native chieftain.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--planet', required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--species', type=lambda value: int(value, 16), required=True)
    parser.add_argument('--population', type=int, default=3)
    parser.add_argument('--retired-faction', type=lambda value: int(value, 16))
    args = parser.parse_args()
    record = json.loads(args.record.read_text())
    for instance in (args.host, args.guest):
        verify(Console(instance), record, args)


if __name__ == '__main__':
    main()
