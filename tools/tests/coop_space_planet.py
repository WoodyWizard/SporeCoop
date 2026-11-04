#!/usr/bin/env python3
"""Life on a colony planet in Space: what the other player's game shows (BUG-0490, BUG-0537;
docs/re/space.md#space-planet-life, docs/06-testing.md#testing-space-planet).

  --case citizens  both ships land on the home planet, the host's ship over the guest's: the host's citizens
                   near the guest stand in the guest's game at the same spots (3 m) at the same size (12 %)
                   (SPORE_FIX_OFF=space.citizens fails: 15-20 % elsewhere, every size its own)
  --case vehicles  the same planet: the colony's 40 vehicles (~20 m/s) are where the host's are (15 m)
                   (SPORE_FIX_OFF=space.vehicles fails: about 60 % elsewhere)
  --case spice     the host lands, the guest leaves the star system and comes back (its game collects the
                   home colony's stored spice into its hold, as every arrival does): the host's colony cities
                   keep no spice and the host's planet shows no spice pickups (the guest's game shows none);
                   nothing for the host's hold (SPORE_FIX_OFF=space.colony_spice fails: 9 pickups, the
                   spice in the host's colony and in the guest's hold)
"""
import argparse
import math
import re
import time

import worlddiff
from twoinst import Checks, shot, start

CITIZEN, VEHICLE, ARTIFACT = 0x018EB4B7, 0x018C6DE8, 0x02DD8C42
HOME_STAR = 4587573


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def place(con):
    m = re.search(r'place ([0-9A-F]+)', sp(con, 'star'))
    return m.group(1) if m else ''


def objs(con, noun, at=(0.0, 0.0, 0.0), r=3000):
    w = worlddiff.dump(con, r, at)
    return w, [x for x in w.rows if x.get('noun') == noun]


def ground(p, ref):
    """the point under p on the sphere of the objects (ref: one of them)"""
    rp, rr = math.hypot(*p), math.hypot(*ref)
    return tuple(v * rr / rp for v in p) if rp > 1 and rr > 1 else p


def land_both(host, guest, ck):
    print('  host land:', sp(host, 'land'), '| guest land:', sp(guest, 'land'))
    for _ in range(30):
        time.sleep(1)
        if place(host).startswith('4') and place(host) == place(guest):
            break
    ck(place(host).startswith('4') and place(host) == place(guest), f'both on the home planet ({place(host)} / {place(guest)})')
    time.sleep(3)
    gw = worlddiff.dump(guest, 50)
    if gw.me:
        print('  host over the guest:', sp(host, 'over', *(f'{v:.1f}' for v in gw.me)))
    time.sleep(12)  # the host's game moves citizens to the cities near its camera


def case_citizens(host, guest, ck):
    land_both(host, guest, ck)
    gw, gc = objs(guest, CITIZEN)
    if not gw.me or not gc:
        ck(False, f'the guest has citizens ({len(gc)}) and a position ({gw.me})')
        return
    c = ground(gw.me, gc[0]['pos'])
    rounds = []
    for rnd in range(3):
        _, h = objs(host, CITIZEN, c, 400)
        _, g = objs(guest, CITIZEN, c, 400)
        h = [x for x in h if math.dist(x['pos'], c) < 400]
        g = [x for x in g if math.dist(x['pos'], c) < 400]
        far, size, used = 0, 0, set()
        for x in h:
            best = min(((math.dist(x['pos'], y['pos']), i) for i, y in enumerate(g) if i not in used), default=(1e9, -1))
            if best[0] > 3.0:
                far += 1
                continue
            used.add(best[1])
            y = g[best[1]]
            if abs(x['scale'] - y['scale']) > 0.12 * max(x['scale'], 0.01):
                size += 1
        print(f'  round {rnd}: host {len(h)} citizens near the guest, guest {len(g)}; {far} without a guest citizen within 3 m, '
              f'{size} of another size', flush=True)
        rounds.append((len(h), far, size))
        time.sleep(5)
    n = sum(r[0] for r in rounds)
    far = sum(r[1] for r in rounds)
    size = sum(r[2] for r in rounds)
    ck(n >= 15, f'the host has citizens near the guest ({n} over 3 rounds)')
    ck(n and far <= 0.05 * n, f'the guest\'s game shows the host\'s citizens where they stand ({far} of {n} elsewhere, at most 5 %)')
    ck(n and size <= 0.05 * n, f'... at the host\'s size ({size} of {n} of another size, at most 5 %)')
    print('  ', [ln for ln in guest.call('coop.wobj')['out'].splitlines() if 'space_citizen' in ln])
    shot(guest, 'space_planet_citizens.png')


def case_vehicles(host, guest, ck):
    land_both(host, guest, ck)
    rounds = []
    for rnd in range(4):
        _, h = objs(host, VEHICLE)
        _, g = objs(guest, VEHICLE)
        far = sum(1 for x in h if min((math.dist(x['pos'], y['pos']) for y in g), default=1e9) > 15.0)
        print(f'  round {rnd}: host {len(h)} vehicles, guest {len(g)}; {far} host vehicles without a guest vehicle within 15 m', flush=True)
        rounds.append((len(h), far))
        time.sleep(4)
    n = sum(r[0] for r in rounds)
    far = sum(r[1] for r in rounds)
    ck(n >= 20, f'the colony has vehicles ({n} over 4 rounds)')
    ck(n and far <= 0.1 * n, f'the guest\'s game shows the host\'s vehicles where they are ({far} of {n} more than 15 m off, at most 10 %)')
    print('  ', [ln for ln in guest.call('coop.wobj')['out'].splitlines() if 'space_vehicle' in ln or 'drives' in ln])
    shot(guest, 'space_planet_vehicles.png')


def colony_spice(con):
    m = re.search(r'colony 00460035 cities \d+:([\d. ]+)', sp(con, 'colonyspice'))
    return [float(v) for v in m.group(1).split()] if m else []


def hold(con):
    m = re.search(r'count (\d+)', sp(con, 'spice'))
    return int(m.group(1)) if m else 0


def case_spice(host, guest, ck):
    h0, g0 = colony_spice(host), colony_spice(guest)
    print('  colony spice: host', h0, '| guest', g0, '| holds: host', hold(host), 'guest', hold(guest))
    ck(sum(h0) >= 5, f'the host\'s home colony stores spice ({sum(h0):.1f})')
    print('  host land:', sp(host, 'land'))
    time.sleep(20)
    _, a0 = objs(host, ARTIFACT)
    ck(len(a0) >= 5, f'the host\'s planet shows the cities\' spice pickups ({len(a0)})')
    hh0 = hold(host)
    print('  guest away:', sp(guest, 'jumpto', HOME_STAR + 1))
    time.sleep(15)
    print('  guest back:', sp(guest, 'jump'))
    time.sleep(15)
    g1 = colony_spice(guest)
    print('  guest after its arrival: colony', g1, '| hold', hold(guest))
    ck(sum(g1) < sum(g0) - 3, f'the guest\'s game collected the colony\'s spice on its arrival ({sum(g0):.1f} -> {sum(g1):.1f})')
    time.sleep(10)  # the game puts or takes the pickups of the active planet every ~6 s
    h1 = colony_spice(host)
    _, a1 = objs(host, ARTIFACT)
    print('  host: colony', h1, '| pickups', len(a1), '| hold', hold(host))
    ck(sum(h1) <= sum(g1) + 1.0, f'the host\'s colony keeps no spice the guest collected ({sum(h1):.1f}, the guest\'s {sum(g1):.1f})')
    ck(len(a1) <= 1, f'the host\'s planet shows no spice pickups the guest collected ({len(a1)})')
    ck(hold(host) == hh0, f'the host\'s hold is not paid ({hh0} -> {hold(host)})')
    print('  guest land:', sp(guest, 'land'))
    time.sleep(20)
    _, ag = objs(guest, ARTIFACT)
    _, ah = objs(host, ARTIFACT)
    ck(abs(len(ag) - len(ah)) <= 1, f'both planets show the same pickups (host {len(ah)}, guest {len(ag)})')
    shot(host, 'space_planet_spice_host.png')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('citizens', 'vehicles', 'spice'), required=True)
    a = ap.parse_args()
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    {'citizens': case_citizens, 'vehicles': case_vehicles, 'spice': case_spice}[a.case](host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
