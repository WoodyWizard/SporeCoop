"""Verify native chief identity and staff on an already connected disposable Tribe pair.

--exercise also changes the host's chief through its native setter and restores it.
For the negative control, disable tribe.native_chiefs on the guest before joining.
"""
import argparse
import re
import time

from coop_ability_bar import u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def verify(host, guest, species):
    host_rows, guest_rows = citizens(host), citizens(guest)
    host_people = {i: row for i, row in host_rows.items() if row['species'] == species}
    if len(host_people) < 2:
        raise RuntimeError('The fixture needs at least two citizens of the requested species.')
    tribe = u32(host, next(iter(host_people.values()))['ptr'] + 0x1014)
    chief = u32(host, tribe + 0x11C)
    chief_id = next((i for i, row in host_people.items() if row['ptr'] == chief), None)
    if chief_id is None:
        raise RuntimeError('The host has no native chief in its current member vector.')
    if chief_id not in guest_rows:
        return False
    other = guest_rows[chief_id]['ptr']
    other_tribe = u32(guest, other + 0x1014)
    if u32(guest, other_tribe + 0x11C) != other or u32(guest, other + 0x102C) != 11:
        return False
    return tribe, chief, chief_id, host_people


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--planet', required=True)
    parser.add_argument('--species', required=True)
    parser.add_argument('--exercise', action='store_true')
    parser.add_argument('--knockout', action='store_true', help='Kill the native chief on this disposable fixture and check the mirror death state.')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    species = f'{int(args.species, 16):08X}'
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe did not load.', 60)
        if f"world '{args.planet}'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Wrong disposable planet.')
    state = wait_until(lambda: verify(host, guest, species),
                       'Guest native chief or staff does not match the host.', 12)
    tribe, original, chief_id, people = state
    print(f'PASS native chief #{chief_id} and staff agree after joining.', flush=True)
    if args.exercise:
        replacement = next(row['ptr'] for row in people.values()
                           if row['ptr'] != original and row['hp'] > 0 and not row['dead'])
        try:
            command(host, f'call SporeCoop::Community_SetLeader thiscall {tribe:#x} 0')
            def cleared():
                if u32(host, tribe + 0x11C):
                    return False
                remote = citizens(guest).get(chief_id)
                return remote and not u32(guest, u32(guest, remote['ptr'] + 0x1014) + 0x11C)
            wait_until(cleared, 'The guest elected a chief after the host explicitly cleared its leader.')
            print('PASS an absent host chief clears the guest leader without electing another.', flush=True)
            command(host, f'call SporeCoop::Tribe_SetChieftain thiscall {tribe:#x} {replacement:#x}')
            def changed():
                now = verify(host, guest, species)
                return now and now[1] == replacement and now[2] != chief_id
            wait_until(changed, 'The guest did not adopt the host\'s changed native chief.')
            time.sleep(1)
            if not changed():
                raise RuntimeError('The mapped native chief reverted.')
            print('PASS guest adopted a different host chief and retained the native staff.', flush=True)
        finally:
            if original in [row['ptr'] for row in citizens(host).values()]:
                command(host, f'call SporeCoop::Tribe_SetChieftain thiscall {tribe:#x} {original:#x}')
        wait_until(lambda: verify(host, guest, species), 'The original native chief did not restore.')
    if args.knockout:
        rows = citizens(host)
        attacker = next(i for i, row in rows.items()
                        if row['species'] != species and row['hp'] > 0 and not row['dead'])
        command(host, f'coop.tribe pincer {chief_id} {attacker} 1000')
        def down_on_both():
            for client in (host, guest):
                row = citizens(client).get(chief_id)
                if not row or row['hp'] > 0 or not row['dead']:
                    return False
            return True
        wait_until(down_on_both, 'Native-chief synchronization left the dead chief alive on a peer.', 4)
        print('PASS the native chief is dead with zero health on both games after a lethal hit.', flush=True)
    for client in (host, guest):
        if not re.search(r'total faults: 0\b', command(client, 'exc')):
            raise RuntimeError('Native faults occurred.')
    print('PASS both games report zero native faults.', flush=True)


if __name__ == '__main__':
    main()
