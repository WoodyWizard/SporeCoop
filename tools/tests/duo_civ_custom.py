"""Bind a real guest design on disposable Anisa and inspect it with or without its creator online."""
import argparse
import re

from duo_content import model
from duo_civ_species import nations, native, view_citizens
from duo_orders import wait_until
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, help='the guest with the custom C&C fixture for the initial binding')
    p.add_argument('--offline', action='store_true', help='audit a fresh host with only the saved planet and no guest')
    args = p.parse_args()
    host = Console(args.host)
    key = '7C93828D'
    if "world 'Anisa.spo'" not in command(host, 'coop.avatar') or not re.search('duoqaciv(custom|archive)', command(host, 'coop.savedb'), re.I):
        raise RuntimeError('Expected the isolated custom-model copy of Anisa.')
    if args.offline:
        if 'offline' not in command(host, 'coop.status').lower().splitlines()[0]:
            raise RuntimeError('Disconnect all peers before testing the saved asset.')
        bound = [pol for pol, row in nations(host).items() if row[:2] == ('0', key)]
        if len(bound) != 1 or 'guest ' + key not in command(host, 'coop.founders'):
            raise RuntimeError('The offline country or roster is missing.')
        counts = model(host, key)
        if counts != (48, 1, 'FF398DD4 9173F17E F28D965B', 2):
            raise RuntimeError(f'The restored creature data differs: {counts}')
        view_citizens(host, bound[0], key)
        print('PASS: the saved world alone restored the guest nation, native citizens, pack part and paints.')
        return
    if not args.guest:
        p.error('--guest is required for a new binding')
    guest = Console(args.guest)
    if 'duoqaparts' not in command(guest, 'coop.savedb').lower():
        raise RuntimeError('Expected the disposable guest that owns the custom fixture.')
    command(guest, 'coop.creations choose creature 5D00C0C1')
    wait_until(lambda: f'player 1 creature: {key}/' in command(host, 'coop.creations'), 'Missing custom creation.')
    if 'distinct non-player' not in command(host, 'coop.civ bind_guest 800021CA'):
        raise RuntimeError('The host nation could be overwritten.')
    def candidates():
        return [pol for pol, row in nations(host).items() if row == ('0', key, '00000000')] or [pol for pol, row in nations(host).items() if row == ('0', '00000000', '00000000')]
    if not candidates():
        sim = native(host, 'Simulator::cCivExpansion::Get cdecl')
        command(host, f'call Simulator::cCivExpansion::SpawnNextCity thiscall 0x{sim:X} 1 0')
    target = wait_until(candidates, 'No unclaimed native nation available.')[0]
    old_slot = command(guest, 'coop.savedb').split('location=')[-1].splitlines()[0]
    command(host, f'cheat coop civ bind_guest {target}')
    wait_until(lambda: nations(host).get(target, ())[:2] == ('0', key), 'The host did not bind the guest species.')
    def received():
        avatar = guest.call('coop.avatar')
        if not avatar['ok'] or "world 'Anisa.spo'" not in avatar['out']: return False
        return (command(guest, 'coop.savedb').split('location=')[-1].splitlines()[0] != old_slot and
                nations(guest).get(target, ())[:2] == ('0', key))
    wait_until(received, 'The new native nation did not arrive on the guest.', 100)
    for client in (host, guest):
        if 'guest ' + key not in command(client, 'coop.founders'): raise RuntimeError('Missing founding roster.')
    print(f'PASS: bound custom species {key} to native nation {target} from the in-game console and transferred it.')


if __name__ == '__main__':
    main()
