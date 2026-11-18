"""Place the five extra rivals for a 100% duo on a disposable normal-size planet."""
import argparse
import json
import math
import re
from pathlib import Path

from coop_ability_bar import u32
from duo_orders import wait_until
from duo_tribe_sixth_site import objective_count, villages
from duo_villages import Console, command


def check_village(peer, species, position):
    rows = command(peer, 'coop.tribe villages')
    factions = re.findall(rf'village ([0-9A-F]+) species={species} members=(\d+)', rows)
    if len(factions) != 1 or int(factions[0][1]) < 3:
        return False
    village, actual = villages(peer)[int(factions[0][0], 16)]
    radius = math.hypot(*actual) / math.hypot(*position)
    return (math.dist(position, actual) < 12 and math.dist([v*radius for v in position], actual) < .75
            and u32(peer, village+0x368) and u32(peer, village+0x11C))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--record', required=True, type=Path)
    parser.add_argument('--restore', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer,'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(peer,'coop.founders')
    if args.restore:
        sites = json.loads(args.record.read_text())
    else:
        assert not args.record.exists() and len(villages(h)) == len(villages(g)) == 7
        sites = {}
        for species in ('067C684A', '067B3E4A', '0680BF38', '066B8278', '067B3AFA'):
            before = villages(h)
            reply = command(h, 'coop.tribe npc_tribe 3 clear ' + species)
            print('Adding', species, reply, flush=True)
            assert reply.startswith('npc tribe '), reply
            after = villages(h)
            faction, = after.keys()-before.keys()
            assert before.keys() <= after.keys()
            ptr, position = after[faction]
            assert min(math.dist(position,p) for _,p in before.values()) >= 69
            assert u32(h, ptr+0x384) > u32(h, ptr+0x380)
            sites[species] = position
            wait_until(lambda: check_village(g, species, position), 'New rival did not appear on guest', 35)
            print('PASS', len(after)-2, 'rivals plus two founders', flush=True)
    for peer in (h,g):
        assert len(villages(peer)) == 12 and objective_count(peer) == 3
        assert all(check_village(peer, species, pos) for species,pos in sites.items())
        assert 'total faults: 0' in command(peer,'exc')
    if not args.restore:
        args.record.write_text(json.dumps(sites), encoding='utf-8')
    print('PASS two founders, ten distinct rivals, unchanged earned progress on both games.')


if __name__ == '__main__':
    main()
