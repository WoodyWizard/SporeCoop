"""Look for sustained NPC village differences between two local instances on the same test planet."""
import argparse
import re
import time

from duo_villages import Console, command


def roster(client):
    rows = command(client, 'coop.tribe villages')
    return sorted(re.findall(r'species=([0-9A-F]{8}) members=\d+ native_player=\d', rows))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--seconds', type=int, default=240)
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    if not all("world 'Clyth.spo'" in command(c, 'coop.avatar') for c in (host, guest)):
        raise RuntimeError('Open the same disposable Clyth save in both games.')
    start = time.monotonic()
    previous = since = None
    while time.monotonic() - start < args.seconds:
        current = (roster(host), roster(guest))
        if current != previous:
            print(f'{time.monotonic() - start:.1f}s host={current[0]} guest={current[1]}', flush=True)
            previous = current
        if not current[0] or not current[1]:
            raise RuntimeError('One of the games left Tribe.')
        if current[0] == current[1]:
            since = None
        elif since is None:
            since = time.monotonic()
        elif time.monotonic() - since > 6:
            raise RuntimeError('Village species differ for over six seconds. Inspect guest village creation log.')
        time.sleep(1)
    print(f'PASS: matched village species over {args.seconds}s of observation.')


if __name__ == '__main__':
    main()
