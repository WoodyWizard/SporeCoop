"""Compare native city part ownership after either Lineages founder captures a rival."""
import argparse
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_turret_combat import turret_list
from duo_orders import wait_until
from duo_villages import Console, command


def ownership(peer, index):
    town, faction = next((town, pol) for i, town, pol, _ in cities(peer) if i == index)
    first, end = struct.unpack('<II', mem(peer, town + 0x340, 8))
    assert 0 <= end-first <= 64*4 and (end-first) % 4 == 0
    buildings = [u32(peer, u32(peer, slot) + 0x30) for slot in range(first, end, 4)]
    turrets = [u32(peer, turret + 0x30) for turret in turret_list(peer, town)]
    return faction, buildings, turrets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--founder', type=int, choices=(0,1), required=True)
    parser.add_argument('--method', choices=('military','religious','economic'), default='military')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for peer in (host, guest):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    rows = cities(host)
    founder = next(pol for _, _, pol, owner in rows if owner == args.founder)
    index, city, rival, _ = next(row for row in rows if row[3] == -1)
    native, local = ownership(host,index), ownership(guest,index)
    assert native == local and len(native[1]) >= 1 and set(native[1]+native[2]) == {rival}
    # Seed a real defense so the guard covers turret and building politics.
    if not native[2]:
        command(host, f'call SporeCoop::CivCity_PlaceTurretSlot thiscall {city:#x} 0')
        wait_until(lambda: ownership(host,index)[2] == ownership(guest,index)[2] == [rival],
                   'Rival turret fixture did not synchronize', 12)
    if args.method == 'economic':
        def money(peer):
            return float(re.search(r'shared money=([-\d.]+)',command(peer,'coop.res'))[1])
        cost = 2500
        assert money(host) >= cost, 'Use the funded disposable fixture'
        before = money(host)
        command(host, f'call Simulator::cCity::BuyByCiv thiscall {city:#x} {founder:#x} f:{cost}')
        wait_until(lambda: money(host) <= before-cost+80, 'Purchase did not charge the shared treasury', 5)
        wait_until(lambda: abs(money(host)-money(guest)) < 1, 'Purchase cost differed on guest', 5)
        print('PASS native purchase charged the shared treasury',before,money(host),flush=True)
    elif args.method == 'religious':
        # Native preaching repeats across ticks. The displayed conversion percentage
        # is recomputed after each follower change, before the next conversion check.
        def preach():
            if ownership(host,index)[0] == founder:
                return True
            command(host, f'call Simulator::cCity::ConvertCitizens thiscall {city:#x} {founder:#x} 1000')
        wait_until(preach, 'Native preaching did not transfer the city', 20)
    else:
        command(host, f'call Simulator::cCity::Capture thiscall {city:#x} {founder:#x} 0 0 0 0')

    def transferred():
        native, local = ownership(host,index), ownership(guest,index)
        if native == local and native[0] == founder and native[1] and native[2] and set(native[1]+native[2]) == {founder}:
            return native

    try:
        result = wait_until(transferred, 'Captured city part owners still differ', 45)
    except RuntimeError:
        print('Host and guest after capture',ownership(host,index),ownership(guest,index),flush=True)
        raise
    assert all('total faults: 0' in command(peer,'exc') for peer in (host,guest))
    print(f'PASS founder {args.founder} {args.method} capture, building and turret owners agree',result,flush=True)


if __name__ == '__main__':
    main()
