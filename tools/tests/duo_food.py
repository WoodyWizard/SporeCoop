"""Check gathering into the guest village and reconciliation of both native food stores on Clyth."""
import argparse
import re
import time

from duo_orders import inspect, wait_until
from duo_villages import Console, command


def food(client):
    return {key: float(total) for key, total in re.findall(r'species=(\w+) food=([\d.]+)', command(client, 'coop.tribe food'))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    home, other = '067B3E4A', '0680BF38'
    for client in (host, guest):
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar') or other not in command(client, 'coop.founders'):
            raise RuntimeError('Open the disposable Clyth fixture on both games first.')
    command(guest, f'coop.creations choose creature {other}')
    command(host, 'coop.tribe village_orders on')
    def agreed():
        local, remote = food(host), food(guest)
        if all(k in local and local[k] == remote.get(k) for k in (home, other)):
            return {home: local[home], other: local[other]}
    try:
        wait_until(lambda: 'founder worlds:' in command(guest, 'coop.tribe'),
                   'Guest never received the independent village mode.')
        start = wait_until(agreed, 'Village food did not reconcile after enabling separate stores.')
        added = command(guest, 'coop.res add food 100')
        actual = re.search(r'food=([\d.]+)', added)
        if not actual or float(actual[1]) < start[home] + 90:
            raise RuntimeError(f'The test did not inject a measurable food difference: {added}')
        time.sleep(1)
        after = wait_until(agreed, 'A local-only food change was not repaired.')
        if after[home] > start[home] + 50:
            raise RuntimeError('The guest was able to credit the host food mat.')
        _, done = inspect(host)
        command(guest, 'cheat coop tribe gather')
        wait_until(lambda: inspect(host)[1] == done + 3, 'Host did not accept three fruit gathering orders.')
        def deposited():
            totals = agreed()
            if totals and totals[other] >= after[other] + 3:
                return totals
        end = wait_until(deposited, 'No native harvest reached the guest village on both games.', 150)
        print(f'PASS: guest village {after[other]} -> {end[other]}, home {after[home]} -> {end[home]}; both games agree. A guest-only +100 was corrected.')
    finally:
        command(host, 'coop.tribe village_orders off')


if __name__ == '__main__':
    main()
