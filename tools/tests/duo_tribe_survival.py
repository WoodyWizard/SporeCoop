"""A surviving second founder prevents the host's native population rewind (BUG-9007)."""
import argparse
import math
import re
import time

from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--nofix', action='store_true', help='Expected to fail on the native rewind assertion.')
    parser.add_argument('--both-lost', action='store_true', help='Verify ordinary game over when both villages lose their people.')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'The tribal world did not finish loading.', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Use the disposable two-founder Clyth fixture.')
    wait_until(lambda: not any(r['timer'] for r in citizens(host).values()), 'An earlier rescue did not finish.')
    time.sleep(2.2)
    # Exercise actual native death, outside the rescue window.
    if 'Shared tribe orders restored.' not in command(host, 'coop.tribe village_orders off'):
        raise RuntimeError('Could not leave rescue-enabled village mode.')
    command(host, 'coop.fix tribe.founder_survival ' + ('off' if args.nofix else 'on'))
    rows = citizens(host)
    def held():
        return int(re.search(r'founder population rewinds held=(\d+)', command(host, 'coop.tribe'))[1])
    held_before = held()
    home = [i for i, r in rows.items() if r['species'] == '067B3E4A' and not r['dead']]
    other = [i for i, r in rows.items() if r['species'] == '0680BF38' and not r['dead']]
    if not home or not other:
        raise RuntimeError('Start with both founders alive.')
    def kill(member):
        body = citizens(host)[member]['ptr']
        command(host, f'call Simulator::cCreatureCitizen::TakeDamage thiscall {body + 0x5A8:#x} f:1000 0xFFFFFFFF 0 {body + 0xC4:#x} 0')
    if args.both_lost:
        for member in other:
            kill(member)
    for member in home:
        kill(member)
    wait_until(lambda: 'Bounce back in time' in host.call('coop.ui find population')['out'] or held() > held_before,
               'The native population-loss event did not arrive.', 45)
    caption = host.call('coop.ui find population')['out']
    if args.both_lost:
        if 'Bounce back in time' not in caption:
            raise RuntimeError('The ordinary all-population-lost game over was suppressed.')
    else:
        if 'Bounce back in time' in caption:
            raise RuntimeError('The surviving partner lost progress to the native rewind.')
        command(host, 'coop.tribe village_orders on')
        wait_until(lambda: 'founder worlds:' in command(guest, 'coop.tribe'), 'The guest did not receive village orders mode.')
        if f'selected: {len(other)}' not in command(guest, 'coop.tribe select_village'):
            raise RuntimeError('The guest could not select its surviving citizens.')
        start = {i: citizens(host)[i]['pos'] for i in other}
        p = list(start[other[0]])
        p[0] += 20
        p[2] -= 20
        radius = math.hypot(*start[other[0]])
        scale = radius / math.hypot(*p)
        p = [v * scale for v in p]
        command(guest, 'coop.tribe walk ' + ' '.join(map(str, p)))
        def moved(client):
            current = citizens(client)
            return all(i in current and math.dist(current[i]['pos'], start[i]) > 4 for i in other)
        for client in (host, guest):
            wait_until(lambda: moved(client),
                       'The surviving villagers stopped moving after host population loss.')
        for _ in range(20):
            command(guest, 'coop.npc list')  # BUG-9008, sync diagnostics use this same listing
            time.sleep(0.25)
    for client in (host, guest):
        if 'total faults: 0' not in command(client, 'exc'):
            raise RuntimeError('Native fault after population loss.')
    print('PASS ordinary game over with both populations lost.' if args.both_lost else
          'PASS surviving guest villagers continued moving on both games after native host population loss.')


if __name__ == '__main__':
    main()
