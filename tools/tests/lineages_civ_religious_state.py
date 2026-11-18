"""BUG-9084: compare native followers and conversion progress on isolated peers.

Use a checkpoint with partial conversion. --cycle seeds a bounded native
conversion on a fresh checkpoint and exercises changed/stale map entries.
This tests replication, not the vehicle AI's ability to finish a siege.
"""
import argparse
import re
import struct

from coop_ability_bar import mem
from duo_civ_founder_control import cities
from duo_civ_takeover import followers
from duo_orders import wait_until
from duo_villages import Console, command


def state(peer):
    return {index: (pol, followers(peer, city),
                   struct.unpack('<fI', mem(peer, city+0x750, 8)))
            for index, city, pol, _ in cities(peer)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--cycle', action='store_true')
    parser.add_argument('--rejoin-port', type=int)
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h, g):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
        assert 'Civ' in command(peer, 'game.mode')
    if args.rejoin_port:
        def slot():
            found = re.search(r' location=(.*?) access=', command(g, 'coop.savedb'))
            return found[1].casefold() if found else ''
        old = slot()
        assert old
        founders = command(g, 'coop.founders')
        command(g, 'coop.leave')
        wait_until(lambda: 'players=1/4' in command(h, 'coop.status'), 'Guest did not leave', 15)
        command(g, f'coop.join 127.0.0.1:{args.rejoin_port} DuoTribalGuest')
        wait_until(lambda: 'players=2/4' in command(h, 'coop.status') and
                   'Civ' in command(g, 'game.mode') and slot() and slot() != old,
                   'Guest did not load a fresh checkpoint', 100)
        assert command(g, 'coop.founders') == founders
        print('PASS returning founder loaded a fresh native slot', flush=True)
    rows = cities(h)
    rival = next(row for row in rows if row[3] == -1)
    partner = next(row for row in rows if row[3] == 1)
    if args.cycle:
        command(h, f'call Simulator::cCity::ConvertCitizens thiscall {rival[1]:#x} {partner[2]:#x} 2')
    initial = state(h)
    print('HOST', initial, 'GUEST', state(g), flush=True)
    assert initial[rival[0]][1].get(partner[2], 0) > 0, 'Requires partial native conversion'

    def equal():
        host, guest = state(h), state(g)
        return host.keys() == guest.keys() and all(
            host[k][:2] == guest[k][:2] and host[k][2][1] == guest[k][2][1] and
            abs(host[k][2][0]-guest[k][2][0]) < .1 for k in host)

    wait_until(equal, 'Guest native follower map or conversion meter differs', 12)
    print('PASS native followers, converter and meter agree', flush=True)
    if args.cycle:
        before = followers(h, rival[1]).get(partner[2], 0)
        command(h, f'call Simulator::cCity::ConvertCitizens thiscall {rival[1]:#x} {partner[2]:#x} 2')
        assert followers(h, rival[1]).get(partner[2], 0) > before
        wait_until(equal, 'Changed followers did not reach guest', 12)
        print('PASS changed native followers reach guest', flush=True)
        # Insert the high-bit host faction into the guest's unsigned-key map.
        # It is a stale zero entry, not a gameplay conversion or capture.
        host_pol = next(row[2] for row in rows if row[3] == 0)
        local = next(row[1] for row in cities(g) if row[0] == rival[0])
        host_city = next(row[1] for row in cities(g) if row[2] == host_pol)
        assert host_pol not in followers(h, rival[1])
        command(g, 'coop.fix civ.religious_state off')
        try:
            command(g, f'call SporeCoop::CityFollowersMap_Index thiscall {local+0x5A0:#x} {host_city+0x30:#x}')
            assert host_pol in followers(g, local)
        finally:
            command(g, 'coop.fix civ.religious_state on')
        wait_until(equal, 'Stale guest follower entry was not removed', 12)
        print('PASS stale unsigned-key follower entry removed', flush=True)
    for peer in (h, g):
        assert 'total faults: 0' in command(peer, 'exc')


if __name__ == '__main__':
    main()
