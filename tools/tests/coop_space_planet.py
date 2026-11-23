#!/usr/bin/env python3
"""Life on a colony planet in Space: what the other player's game shows (BUG-0490, BUG-0537;
docs/re/space.md#space-planet-life, docs/06-testing.md#testing-space-planet).

  --case citizens  both ships land on the home planet, the host's ship over the guest's: the host's citizens
                   near the guest stand in the guest's game at the same spots (3 m) at the same size (12 %)
                   (SPORE_FIX_OFF=space.citizens fails: 15-20 % elsewhere, every size its own). BUG-1404: the misses
                   left (2.7-4.3 m, every one a bound copy) were runners whose start or stop reached the guest a
                   0.5 s snapshot late. CORRECTION (2026-11-23, BUG-1454): this case cannot show space.citizen_lead
                   (0.5 s snapshots: 2 of 9 runs over its 5 % since BUG-1210, 11 misses in 513 samples; the twin
                   passed 4 of 4 in 20261122-192851); the "about half" was before BUG-1210, a
                   different cause. --case runners measures it
  --case runners   the same landing; waits until host citizens run near the guest, then RUN_ROUNDS rounds of host,
                   guest, host dumps: each running host citizen (by serial, over RUN_SPEED m/s between the two host
                   dumps) taken at the guest's moment and its nearest guest citizen; at most RUN_SHARE of those samples
                   over RUN_OFF m (BUG-1404: what space.citizen_lead changes; SPORE_FIX_OFF=space.citizen_lead fails);
                   fewer than RUN_SAMPLES runner samples: INCONCLUSIVE (no runners, nothing measured)
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

from harness import sp
import worlddiff
from twoinst import Checks, shot, start

CITIZEN, VEHICLE, ARTIFACT = 0x018EB4B7, 0x018C6DE8, 0x02DD8C42
HOME_STAR = 4587573
# BUG-1404 (--case runners): --case citizens reads 19 mostly standing citizens 3 times; a runner turning at one of those
# moments is luck (with 0.5 s snapshots after BUG-1210: 11 misses in 513 samples on 9 runs, 2 of 9 runs over its 5 %).
# This case measures only the runners, where space.citizen_lead (0.25 s snapshots) acts
RUN_SPEED = 2.0  # m/s between the two host dumps of a round (runners ~5 m/s, docs/re/space.md#space-planet-life)
RUN_FAST = 30.0  # m/s: faster is a put, not a run
RUN_WAIT = 240  # s at most until RUN_MIN host citizens run near the guest (most stand right after the landing)
RUN_MIN = 2
RUN_ROUNDS = 60
RUN_SAMPLES = 40  # fewer runner samples: INCONCLUSIVE
RUN_OFF = 2.5  # m: a runner's copy this far from the host's citizen at the same moment is behind
RUN_SHARE = 0.10  # at most this share of the runner samples behind (provisional: the lab measurement of BUG-1404 sets it)
# BUG-1454: a console read is answered within a frame or two (~20 ms): back to back, the two host reads of a round were
# under 0.05 s apart (60 rounds in ~17 s, lab 20261123-023643) and every round was dropped: 0 samples in 4 runs.
# The reads of a round are RUN_GAP s apart (a runner moves ~1.5 m between the host reads: 0.1 m print steps are 0.3 m/s)
RUN_GAP = 0.15


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
        # BUG-1210: as many host citizens as possible each get a guest citizen of their own within 3 m (a maximum
        # matching, augmenting paths), at the host's size first; then the rest within 3 m at any size. Citizens stand
        # in groups 0.5-2 m apart: a host citizen taking the nearest guest one in host order (or nearest first) took
        # its neighbour's partner, and the neighbour counted as elsewhere or of another size although each had its own
        def match(ok, pair):
            owner = {b: a for a, b in pair.items()}

            def aug(a, seen):
                for b in range(len(g)):
                    if b not in seen and ok(h[a], g[b]):
                        seen.add(b)
                        if b not in owner or aug(owner[b], seen):
                            owner[b] = a
                            return True
                return False
            for a in range(len(h)):
                if a not in pair.keys() and a not in owner.values():
                    aug(a, set())
            return {a: b for b, a in owner.items()}

        def near(x, y):
            return math.dist(x['pos'], y['pos']) <= 3.0

        def same(x, y):
            return near(x, y) and abs(x['scale'] - y['scale']) <= 0.12 * max(x['scale'], 0.01)
        pair = match(same, {})
        pair = match(near, pair)
        far, size, miss = 0, 0, []
        for a, x in enumerate(h):
            if a not in pair:
                far += 1
                miss.append(round(min((math.dist(x['pos'], y['pos']) for y in g), default=1e9), 1))
                continue
            y = g[pair[a]]
            if not same(x, y):
                size += 1
                miss.append(f"size {x['scale']:.2f}/{y['scale']:.2f}")
        print(f'  round {rnd}: host {len(h)} citizens near the guest, guest {len(g)}; {far} without a guest citizen within 3 m, '
              f'{size} of another size' + (f' (elsewhere: nearest guest citizen in m; size: host/guest: {miss})' if miss else ''), flush=True)
        rounds.append((len(h), far, size))
        time.sleep(5)
    n = sum(r[0] for r in rounds)
    far = sum(r[1] for r in rounds)
    size = sum(r[2] for r in rounds)
    ck(n >= 15, f'the host has citizens near the guest ({n} over 3 rounds)')
    ck(n and far <= 0.05 * n, f'the guest\'s game shows the host\'s citizens where they stand ({far} of {n} elsewhere, at most 5 %)')
    ck(n and size <= 0.05 * n, f'... at the host\'s size ({size} of {n} of another size, at most 5 %)')
    print('  ', [ln for ln in guest.call('coop.wobj')['out'].splitlines() if 'space_citizen' in ln or 'space citizens' in ln])
    shot(guest, 'space_planet_citizens.png')


def timed_citizens(con, c):
    """(the moment of the read on the test's clock, the citizens within 400 m of c): one PC, one clock for both games"""
    t0 = time.monotonic()
    _, rows = objs(con, CITIZEN, c, 400)
    t1 = time.monotonic()
    return (t0 + t1) / 2, [x for x in rows if math.dist(x['pos'], c) < 400]


def runners_round(host, guest, c, st=None):
    """host, guest, host (RUN_GAP s apart): for each host citizen running between the two host reads, the distance of
    its place at the guest's read (linear between the two) to the nearest guest citizen. st: counts of what the rounds
    saw (why no runner was found)"""
    ta, ha = timed_citizens(host, c)
    time.sleep(RUN_GAP)
    tg, g = timed_citizens(guest, c)
    time.sleep(RUN_GAP)
    tb, hb = timed_citizens(host, c)
    if st is not None:
        st['rounds'] += 1
        st['dt'] = (min(st['dt'][0], tb - ta), max(st['dt'][1], tb - ta))
        st['host'] = max(st['host'], len(hb))
        st['short'] += tb - ta < 0.05
        st['no_guest'] += not g
    if tb - ta < 0.05 or not g:
        return []
    a = {x['ser']: x['pos'] for x in ha if x['ser']}
    f = (tg - ta) / (tb - ta)
    out = []
    for x in hb:
        p0 = a.get(x['ser'])
        if p0 is None:
            continue
        v = math.dist(p0, x['pos']) / (tb - ta)
        if st is not None:
            st['matched'] += 1
            st['moving'] += v >= 0.5
            st['vmax'] = max(st['vmax'], v if v <= RUN_FAST else 0.0)
        if v < RUN_SPEED or v > RUN_FAST:
            continue
        at = tuple(u + (w - u) * f for u, w in zip(p0, x['pos']))
        out.append(min(math.dist(at, y['pos']) for y in g))
    return out


def case_runners(host, guest, ck):
    land_both(host, guest, ck)
    gw, gc = objs(guest, CITIZEN)
    if not gw.me or not gc:
        ck(False, f'the guest has citizens ({len(gc)}) and a position ({gw.me})')
        return None
    c = ground(gw.me, gc[0]['pos'])
    t0, most = time.monotonic(), 0
    st = dict(rounds=0, dt=(1e9, 0.0), host=0, short=0, no_guest=0, matched=0, moving=0, vmax=0.0)
    while time.monotonic() - t0 < RUN_WAIT:
        most = max(most, len(runners_round(host, guest, c, st)))
        if most >= RUN_MIN:
            break
        time.sleep(2)
    print(f'  {most} host citizens running near the guest after {time.monotonic() - t0:.0f} s ({st["rounds"]} rounds, '
          f'host reads {st["dt"][0]:.2f}-{st["dt"][1]:.2f} s apart, {st["short"]} under 0.05 s, {st["no_guest"]} without '
          f'guest citizens; at most {st["host"]} host citizens near, {st["matched"]} matched by serial, {st["moving"]} '
          f'over 0.5 m/s, fastest {st["vmax"]:.1f} m/s)', flush=True)
    s = []
    for _ in range(RUN_ROUNDS):
        s += runners_round(host, guest, c)
        time.sleep(0.2)
    s.sort()
    behind = sum(1 for d in s if d > RUN_OFF)
    if s:
        print(f'  runner samples {len(s)}: p50 {s[len(s) // 2]:.2f} m, p90 {s[int(len(s) * 0.9)]:.2f} m, max {s[-1]:.2f} m; '
              f'{behind} over {RUN_OFF} m', flush=True)
    print('  ', [ln for ln in guest.call('coop.wobj')['out'].splitlines() if 'space_citizen' in ln or 'space citizens' in ln])
    if len(s) < RUN_SAMPLES:
        return f'{len(s)} runner samples (at least {RUN_SAMPLES}): no host citizens ran near the guest'
    ck(behind <= RUN_SHARE * len(s), f'the guest\'s game shows running citizens where the host\'s are at the same moment '
       f'({behind} of {len(s)} runner samples over {RUN_OFF} m, at most {RUN_SHARE:.0%})')
    shot(guest, 'space_planet_runners.png')
    return None


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
    ap.add_argument('--case', choices=('citizens', 'runners', 'vehicles', 'spice'), required=True)
    a = ap.parse_args()
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    why = {'citizens': case_citizens, 'runners': case_runners, 'vehicles': case_vehicles, 'spice': case_spice}[a.case](host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.inconclusive(why) if why else ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
