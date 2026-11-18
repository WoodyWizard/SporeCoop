"""Verify that a cold, terrain-height-shifted rival still accepts host inventory updates."""
import argparse
import math
import re

from coop_ability_bar import u32
from duo_food import food
from duo_orders import wait_until
from duo_tribe_sixth_site import villages
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    species = '067C684A'
    sites = []
    for peer in (h, g):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer, 'coop.avatar')
        match, = re.findall(rf'village ([0-9A-F]+) species={species} ', command(peer, 'coop.tribe villages'))
        village, pos = villages(peer)[int(match, 16)]
        assert u32(peer, village + 0x368)
        text = command(peer, 'coop.ground ' + ' '.join(map(str, pos)))
        assert abs(float(re.search(r'above ([-\d.]+)', text)[1])) < .15
        sites.append((village, pos))
    host, guest = (s[1] for s in sites)
    radial = math.hypot(*guest) / math.hypot(*host)
    assert 3 < math.dist(host, guest) < 12
    assert math.dist([x*radial for x in host], guest) < .75
    old = food(g)[species]
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {sites[0][0]:#x} 71')
    def agrees():
        local, remote = food(h)[species], food(g)[species]
        return local if abs(local-remote) < .01 and remote > old+40 else None
    current = wait_until(agrees, 'Grounded sixth village did not receive its food', 12)
    print('PASS distinct terrain radii', math.hypot(*host), math.hypot(*guest),
          'matched village food', current, flush=True)


if __name__ == '__main__':
    main()
