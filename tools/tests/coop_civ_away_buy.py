#!/usr/bin/env python3
"""Civilization: a guest's purchase made while the host is away is made when the host is back (BUG-0693; class
BUG-0423: credits and effects dropped or undone).

1. Two games on the civ fixture. The host's game has n0 vehicles of the player's civilization (`coop.civ`).
2. The host goes to the main menu (`coop.menu`); once the guest knows (host_away=1), the guest buys a land vehicle
   at its city (`coop.civ buy <city> 0`: the city menu's purchase, request_buy -> MSG_CIV_CMD CMD_BUY). The guest's
   game says that it waits for the host.
3. The host loads its world again (the same save). 20 s later the host's game has n0 + 1 vehicles of the player's
   civilization: the purchase was held and made. 0.7.10 (SPORE_FIX_OFF=civ.cmd_hold): the host dropped the command
   (n0, nothing said on the guest).
"""
import time

from coop_civ import civ
from twoinst import Checks, lane_state, start


def player_vehicles(con):
    cities, veh = civ(con)
    pols = {c['pol'] for c in cities.values() if c['player']}
    return cities, len([v for v in veh.values() if v['pol'] in pols])


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    hc, n0 = player_vehicles(host)
    pc = [k for k, v in hc.items() if v['player']]
    if not ck(pc, 'a player city'):
        return ck.result()
    print(f"  the host's game: {n0} vehicles of the player's civilization", flush=True)
    print(' ', host.call('coop.menu')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    if not ck('host_away=1' in guest.call('coop.status')['out'], 'the guest knows the host is away'):
        return ck.result()
    print(' ', guest.call(f'coop.civ buy {pc[0]} 0')['out'].strip())
    time.sleep(3)
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150 and 'host_away=0' not in guest.call('coop.status')['out']:
        time.sleep(2)
    n1 = n0
    t0 = time.time()
    while time.time() - t0 < 20:
        _, n1 = player_vehicles(host)
        if n1 > n0:
            break
        time.sleep(2)
    print(f"  back: the host's game has {n1} vehicles of the player's civilization (before {n0})", flush=True)
    ck(n1 == n0 + 1, f"the guest's purchase made while the host was away is in the host's game ({n0} -> {n1})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
