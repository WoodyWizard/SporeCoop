"""Verify a named two-species Creature checkpoint after reconnect or cold restart."""
import argparse
import re

from duo_creature_rescue import avatar, dna, finish_hatch, wait_playable
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', required=True)
    p.add_argument('--host-species', required=True)
    p.add_argument('--guest-species', required=True)
    p.add_argument('--dna', type=float, required=True)
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    wait_playable((h, g), args.planet)
    expected = [f'{int(k, 16):08X}' for k in (args.host_species, args.guest_species)]
    if expected[0] == expected[1]:
        raise RuntimeError('This acceptance check requires two distinct species.')
    for c, species in zip((h, g), expected):
        if f'key=0x{species}' not in avatar(c) or dna(c) != args.dna:
            raise RuntimeError('The checkpoint restored the wrong native species or shared DNA.')
        # The same normal movement check used after a death; no forced growth.
        finish_hatch(c, species)
    for c in (h, g):
        if dna(c) != args.dna or not re.search(r'total faults: 0\b', command(c, 'exc')):
            raise RuntimeError('Checkpoint balance changed or a native fault occurred.')
    print(f'PASS walking adults {expected[0]} / {expected[1]}, {args.dna:g} DNA on both games.', flush=True)


if __name__ == '__main__':
    main()
