"""BUG-9009. A retained tribe can receive a late hut-layout call after stage cleanup."""
import argparse
import time

from coop_ability_bar import eax, sym, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--instance', type=int, required=True)
    parser.add_argument('--nofix', action='store_true', help='Expected to fail with the native null-species fault.')
    args = parser.parse_args()
    client = Console(args.instance)
    wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Open the disposable tribal fixture.', 60)
    if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
        raise RuntimeError('Use the disposable Clyth fixture.')
    layout = sym(client, 'SporeCoop::Tribe_UpdateToolLayout')
    if not layout:
        raise RuntimeError('Missing verified layout symbol.')
    if 'total faults: 0' not in command(client, 'exc'):
        raise RuntimeError('Start a clean game process.')
    command(client, 'coop.fix tribe.layout_profile ' + ('off' if args.nofix else 'on'))
    rows = citizens(client)
    body = next(iter(rows.values()))['ptr']
    tribe = u32(client, body + 0x1014)
    table = u32(client, tribe)
    retain, release = u32(client, table), u32(client, table + 4)
    if not u32(client, tribe + 0x18CC):
        raise RuntimeError('The live tribe already has no species.')
    eax(client, f'{retain:#x} thiscall {tribe:#x}')
    print('Retained the native tribe through stage cleanup.', flush=True)
    command(client, 'coop.leave')
    command(client, 'coop.menu')
    wait_until(lambda: 'GalaxyGameEntry' in command(client, 'game.mode'), 'The game did not return to the galaxy.', 60)
    time.sleep(1)
    if u32(client, tribe + 0x18CC):
        raise RuntimeError('The native cleanup did not clear the species. Regression precondition missing.')
    # The captured hut's late layout requested kind 12 in the original crash.
    command(client, f'call SporeCoop::Tribe_UpdateToolLayout thiscall {tribe:#x} 12')
    if 'total faults: 0' not in command(client, 'exc'):
        raise RuntimeError('Native layout dereferenced the cleared species after stage exit.')
    eax(client, f'{release:#x} thiscall {tribe:#x}')
    time.sleep(2)
    if 'total faults: 0' not in command(client, 'exc'):
        raise RuntimeError('Fault after releasing the retained tribe.')
    print('PASS native species cleanup followed by the late hut-layout request, zero faults.', flush=True)


if __name__ == '__main__':
    main()
