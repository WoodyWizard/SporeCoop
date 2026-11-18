"""Exercise automatic host reconstruction refresh on disposable two-founder Clyth copies.

The native defeat setup is a fixture. The mod must save, transfer and reload
without test-issued save/load/share commands. Capture/helper gameplay is separate.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

from coop_ability_bar import eax, mem, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--record', type=Path, required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe not loaded', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth test copies only.')
    command(host, 'coop.tribe village_orders off')
    people = [r for r in citizens(host).values() if r['species'] == '067B3E4A']
    if not people:
        raise RuntimeError('Missing fixture host population.')
    old = u32(host, people[0]['ptr'] + 0x1014)
    if u32(host, old + 0x30) != 0x800021CA:
        raise RuntimeError('Wrong host faction.')
    record = dict(old_pos=struct.unpack('<3f', mem(host, old + 0x124, 12)),
                  old_native_id=u32(host, old + 0x24),
                  conqueror_faction=0x21E0, founder_faction=0x800021CA)
    for person in people:
        pointer = person['ptr']
        command(host, f'call Simulator::cCreatureCitizen::TakeDamage thiscall {pointer + 0x5A8:#x} '
                      f'f:1000 0x21e0 0 {pointer + 0xC4:#x} 0')
    wait_until(lambda: all(r['hp'] <= 0 or r['dead'] for r in citizens(host).values()
                           if r['species'] == '067B3E4A'), 'Fixture defeat did not complete', 5)
    command(host, 'coop.tribe refound_home_probe 21E0')

    def completed():
        value = command(host, 'coop.tribe refound_home_status').strip()
        return value if value and value != 'pending' else False

    result = wait_until(completed, 'Reconstruction did not complete', 15)
    if not result.startswith('Host replacement ') or 'failed' in result:
        raise RuntimeError(result)

    def native_player():
        manager = eax(host, 'Simulator::cGameNounManager::Get cdecl')
        return eax(host, f'Simulator::cGameNounManager::GetPlayerTribe thiscall {manager:#x}') if manager else None

    replacement = native_player()
    if not replacement or replacement == old:
        raise RuntimeError('No fresh native replacement.')
    record.update(new_pos=struct.unpack('<3f', mem(host, replacement + 0x124, 12)),
                  new_native_id=u32(host, replacement + 0x24))
    args.record.write_text(json.dumps(record, indent=2), encoding='utf-8')
    # Capture the load boundary, then verify the native saved state and controls.
    wait_until(lambda: not host.call('coop.avatar')['out'].startswith('Tribe:') or
                       native_player() != replacement, 'The mod did not reload the host', 45)
    tests = Path(__file__).parent
    common = ['--host', str(args.host), '--guest', str(args.guest), '--planet', 'Clyth.spo', '--species', '067B3E4A']
    subprocess.run([sys.executable, str(tests / 'duo_tribe_rebuild_checkpoint.py'), *common,
                    '--record', str(args.record)], check=True)
    subprocess.run([sys.executable, str(tests / 'duo_tribe_host_orders.py'), *common], check=True)
    print('PASS automatic save, host reload, guest world refresh and native walking; no manual refresh commands.', flush=True)


if __name__ == '__main__':
    main()
