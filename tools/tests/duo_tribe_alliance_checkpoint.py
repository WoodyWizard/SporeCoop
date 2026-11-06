"""Verify an earned Tribal alliance after cold restart and fresh guest transfer.

Use the saved Clyth result of duo_tribe_performance.py --alliance. This test
does not create citizens, change relationships or grant campaign rewards.
"""
import argparse
import json
import struct

from coop_ability_bar import eax, mem, u32
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def state(c, expected=(0, 1), species_pair=('067B3E4A', '0680BF38')):
    rows = citizens(c)
    tribes = {}
    for species in species_pair:
        members = [r for r in rows.values() if r['species'] == species and not r['dead']]
        if not members:
            return None
        owners = {u32(c, r['ptr'] + 0x1014) for r in members}
        if len(owners) != 1:
            raise RuntimeError('A founder species has inconsistent native village ownership')
        tribes[species] = owners.pop()
    host, guest = tribes.values()
    home_id, guest_id = u32(c, host + 0x30), u32(c, guest + 0x30)
    if home_id == guest_id:
        raise RuntimeError('Founding villages lost their separate political identities')
    totem = u32(c, host + 0x19D0)
    if not totem or u32(c, totem + 0x2C) != host:
        raise RuntimeError('Campaign totem or its native owner was lost')
    history = {}
    for offset, name, width in ((0x11C, 'segments', 4), (0x130, 'colors', 12)):
        begin, end = u32(c, totem + offset), u32(c, totem + offset + 4)
        if end - begin != len(expected) * width:
            raise RuntimeError(f'Expected {len(expected)} earned victories in native {name}')
        history[name] = mem(c, begin, end - begin).hex()
    if struct.unpack('<'+'I'*len(expected), bytes.fromhex(history['segments'])) != tuple(expected):
        raise RuntimeError('Native history does not distinguish conquest from alliance')
    allies = []
    for tribe, _ in objects(c, 0x18C6D19):
        faction = u32(c, tribe + 0x30)
        if faction in (home_id, guest_id):
            continue
        feelings = [float(command(c, f'coop.tribe relation {faction:X} {founder:X}').split('=')[-1])
                    for founder in (home_id, guest_id)]
        if min(feelings) >= 7:
            # Political IDs may differ on the guest. Identify the NPC whose
            # native opinion we read by its shared citizen (chief) identity.
            chief = u32(c, tribe + 0x11C)
            leaders = [identity for identity, row in rows.items() if row['ptr'] == chief]
            if len(leaders) != 1:
                raise RuntimeError('Cannot identify the allied NPC chief on this peer')
            allies.append(leaders[0])
    if len(allies) != expected.count(1):
        raise RuntimeError('Earned NPC allies do not match the shared campaign history')
    game = eax(c, 'SporeCoop::TribalGame_Get cdecl')
    unlocked = [kind for kind in range(1, 10)
                if eax(c, f'SporeCoop::TribalGame_IsToolUnlocked thiscall {game:#x} {kind}') & 0xFF]
    return dict(history=history, level=u32(c, host + 0x38), unlocks=unlocked,
                allied_chiefs=sorted(allies), home=home_id, guest=guest_id,
                species=sorted(tribes), roster=command(c, 'coop.founders').strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--planet', default='Clyth.spo')
    parser.add_argument('--species', nargs=2, default=['067B3E4A','0680BF38'], metavar=('HOST','GUEST'))
    parser.add_argument('--history', type=int, choices=(0, 1), nargs='+', default=[0, 1],
                        help='Expected native victory sequence: conquest=0, alliance=1.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if f"world '{args.planet}'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Wrong earned-alliance campaign')
    expected = state(h, args.history, args.species)
    if not expected:
        raise RuntimeError('Host founders are absent')
    wait_until(lambda: state(g, args.history, args.species) == expected, 'Native campaign history or unlocks diverged', 30)
    for c in (h, g):
        if 'total faults: 0' not in command(c, 'exc'):
            raise RuntimeError('Native fault after alliance reload')
    print('PASS both peers agree on native conquest/alliance history, colors, level, unlocks, '
          'both species and NPC alliance relationships.', json.dumps(expected, sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
