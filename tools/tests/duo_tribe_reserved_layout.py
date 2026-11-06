"""Check terrain capacity without founding a village on a fresh four-seat Tribal planet."""
import argparse
from itertools import combinations
import math
import re

from duo_villages import Console, command


def check_layout(report):
    header, *rows = report.strip().splitlines()
    assert re.fullmatch(r'terrain sites 24/24, live villages 1', header), header
    positions = []
    for row in rows:
        marker, x, y, z = row.split()
        assert marker == 'clear', row
        positions.append(tuple(map(float, (x, y, z))))
    assert len(positions) == 23
    # Coordinates print to two decimals. Allow for rounding at both ends.
    assert all(math.dist(a, b) >= 69.98 for a, b in combinations(positions, 2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=int)
    peer = Console(parser.parse_args().host)
    assert 'duoqa' in command(peer, 'coop.savedb').lower()
    assert 'Tribe' in command(peer, 'game.mode')
    status = command(peer, 'coop.status')
    assert 'players=1/4' in status and 'difficulty scaling 100%' in status
    before = command(peer, 'coop.tribe villages')
    check_layout(command(peer, 'coop.tribe planned_sites'))
    assert command(peer, 'coop.tribe villages') == before
    assert 'total faults: 0' in command(peer, 'exc')
    print('PASS one village plus 23 spaced sites. No villages created.')


if __name__ == '__main__':
    main()
