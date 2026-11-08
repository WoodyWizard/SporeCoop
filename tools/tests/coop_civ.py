#!/usr/bin/env python3
"""Civilization co-op ("Governors") on two instances: the host simulates vehicles and cities, the
guest mirrors them; cities and vehicles have owners; guest purchases, orders and buildings go
through the host; buildings made on the host appear on the guest."""
import math
import re
import time

from twoinst import Checks, shot, start


def civ(con):
    cities, veh = {}, {}
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (-?\d+) \S+ pol=(\w+)( \(player\))? owner=(-?\d+) buildings=(\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            cities[int(m.group(1))] = dict(pol=m.group(2), player=bool(m.group(3)), owner=int(m.group(4)),
                                           buildings=int(m.group(5)), pos=tuple(float(m.group(i)) for i in (6, 7, 8)))
        m = re.match(r'\s+veh #(\d+) \S+ pol=(\w+) owner=(-?\d+) purpose=(\d+) loco=(\d+) sel=(\d+) hp=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(1) != '0':
            veh[int(m.group(1))] = dict(pol=m.group(2), owner=int(m.group(3)), sel=int(m.group(6)),
                                        pos=tuple(float(m.group(i)) for i in (7, 8, 9)))
    return cities, veh


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def status(con, key):
    st = con.call('coop.status')['out']
    m = re.search(key + r'=(\d+)', st)
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    hc, hv = civ(host)
    gc, gv = civ(guest)
    print('  host cities', {k: (v['owner'], v['player'], v['buildings']) for k, v in hc.items()}, 'vehicles', len(hv))
    print('  guest cities', {k: (v['owner'], v['player'], v['buildings']) for k, v in gc.items()}, 'vehicles', len(gv))
    ck(len(hv) > 0 and set(hv) == set(gv), f'guest mirrors every vehicle ({len(hv)} host, {len(gv)} guest)')
    err = max((dist(hv[i]['pos'], gv[i]['pos']) for i in hv if i in gv), default=99)
    ck(err < 5, f'mirrors are where the host has them (max error {err:.2f} m)')
    pcities = [k for k, v in hc.items() if v['player']]
    ck(pcities, f'player cities on the host: {pcities}')
    if not pcities:
        return ck.result()
    city = pcities[0]

    # purchase by the guest in a city it may use
    n0 = len(gv)
    print(' ', guest.call(f'coop.civ buy {city} 0')['out'])
    time.sleep(3)
    hc, hv = civ(host)
    gc, gv = civ(guest)
    guest_owned = [i for i, v in hv.items() if v['owner'] == 1]
    ck(guest_owned, f'the host built a vehicle for the guest: {guest_owned}')
    ck(len(gv) > n0 and all(i in gv for i in guest_owned), 'the guest sees its new vehicle')
    if guest_owned:
        vid = guest_owned[0]
        host_owned = [i for i, v in hv.items() if v['owner'] == 0]
        if host_owned:
            r = guest.call(f'coop.civ select {host_owned[0]} 1')['out']
            ck('selected=0' in r, f'guest cannot select a host vehicle ({r})')
        r = guest.call(f'coop.civ select {vid} 1')['out']
        ck('selected=1' in r, f'guest selects its own vehicle ({r})')
        p0 = hv[vid]['pos']
        cpos = hc[city]['pos']
        # 40 m from the city, towards the vehicle (stays on land near the city)
        d = dist(p0, cpos) or 1
        tgt = tuple(c + (p - c) / d * (d + 40) for p, c in zip(p0, cpos))
        print(' ', guest.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'])
        time.sleep(8)
        hc, hv = civ(host)
        gc, gv = civ(guest)
        moved = dist(hv[vid]['pos'], p0)
        ck(moved > 5, f'the host drove the guest vehicle {moved:.1f} m')
        ck(dist(hv[vid]['pos'], gv[vid]['pos']) < 6, 'the guest sees it drive')

    # buildings: the guest builds a house, the host gets it; the host builds, the guest gets it
    b0h, b0g = hc[city]['buildings'], gc.get(city, {}).get('buildings', -1)
    print(' ', guest.call(f'coop.civ build {city} house')['out'])
    time.sleep(2)
    hc, _ = civ(host)
    gc, _ = civ(guest)
    ck(hc[city]['buildings'] == b0h + 1, f'host city got the guest building ({b0h} -> {hc[city]["buildings"]})')
    ck(gc[city]['buildings'] == b0g + 1, f'no duplicate on the guest ({b0g} -> {gc[city]["buildings"]})')
    b1g = gc[city]['buildings']
    # the fixture's 3000 went to the vehicle and the house: a paid build needs the money (BUG-0613: the mod's build
    # took the money below 0, and the detector found the host at -800 and the guest at 0)
    host.call('coop.res add money 5000')
    time.sleep(1)
    print(' ', host.call(f'coop.civ build {city} factory')['out'])
    time.sleep(2)
    gc, _ = civ(guest)
    ck(gc[city]['buildings'] == b1g + 1, f'guest city got the host building ({b1g} -> {gc[city]["buildings"]})')
    print(' ', host.call('coop.status')['out'].split('civ(host)')[1].splitlines()[0])
    print(' ', guest.call('coop.status')['out'].split('civ(guest)')[1].splitlines()[0])
    # roles: the guest governs as economic, everybody knows it
    guest.call('coop.role 2')
    time.sleep(2.5)
    ck(re.search(r'role 1 Bob economic', host.call('coop.role')['out']), 'the host knows the guest role (economic)')
    ck('governs as economic' in host.call('coop.status')['out'], 'everybody is told about the role')

    # a gift: the guest gives its vehicle to the host
    _, hv = civ(host)
    gifts = [i for i, v in hv.items() if v['owner'] == 1]
    if gifts:
        _, gv = civ(guest)
        for i in gv:
            guest.call(f'coop.civ select {i} 0')
        guest.call(f'coop.civ select {gifts[0]} 1')
        print(' ', guest.call('coop.give')['out'])
        time.sleep(2)
        _, hv = civ(host)
        ck(hv[gifts[0]]['owner'] == 0, f'the host owns the gifted vehicle #{gifts[0]} now')
        ck('gave 1 vehicle' in host.call('coop.status')['out'], 'everybody is told about the gift')
    shot(host, 'civ_host.png')
    shot(guest, 'civ_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
