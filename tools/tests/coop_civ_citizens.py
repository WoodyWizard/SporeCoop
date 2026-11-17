#!/usr/bin/env python3
"""Civilization: the city citizens near the guest walk where the host's walk, at the host's size
(BUG-0476, BUG-0475's Civ half).

The divergence detector found `Civilization:unmatched:citizen` in every Civ comparison (9 of 19 citizens
without a partner within 15 m, lab 20261103-233836) and `Civilization:scale:citizen` in 88 %: each game
walks its own citizens and gives them its own size. Now the host's citizens near a guest are a world-object
kind (`citizen`, worldobj.cpp: bound by position, walked to the host's) and their size a fields kind
(`civ_citizen`).

1. Two games on civ_npc, 25 s to settle.
2. 8 rounds 6 s apart: both games' own objects around the same centre (`coop.census all`, the detector's
   dump): the host's citizens within 128 m of the centre, the guest's within 160 m, paired greedily
   within 15 m (the detector's rule); the sizes (GetScale) of the unambiguous pairs (each the other's nearest,
   within 3 m, the next citizen 2 m farther) compared at 12 % (the detector's greedy pairs cross in a crowd).
PASS: over all rounds at most 10 % of the host's citizens without a partner and at most 10 % of the unambiguous pairs
with another size.
--plant N: before the rounds (after each load) the guest drops the binds of its N citizens nearest the centre and puts
them 60 m out (`coop.wobj loose citizen N 60`): the state a load leaves now and then, every time.
--reload (civ_citizens_reload, BUG-1221): before the rounds the host loads the fixture again in the running games and the
guest follows (as a player's save load, the lab's warm reset), `--reloads N` times, the rounds after each; with
SPORE_FIX_OFF=civ.citizen_rebind (switched off after the cold start, so the loads run without it) 2-4 of the guest's
citizens stay unbound after some loads (FAIL). Each round names the misses: a host citizen without a partner (bound or
not, its copy's distance), a pair of another size (what the guest's citizen is bound to, that host citizen's distance and
size). civ.citizens off failed (retired in 0.7.15, BUG-1138: FAIL run 20261103-235237:civ_citizens): about half unpaired (FAIL); civ.citizen_scale off failed (retired in 0.7.15, BUG-1138: FAIL run 20261104-001130:civ_citizens):
the sizes differ (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_citizens.py [--rounds 5]
"""
import argparse
import math
import re
import time

import fixtures
import worlddiff
from twoinst import Checks, start, warm_reset

CITIZEN = 0x018EB4B7


def citizens(w, c, r):
    return [x for x in w.rows if x['kind'] == 'g' and x['noun'] == CITIZEN and not worlddiff._origin(x['pos'])
            and math.dist(x['pos'], c) < r]


def bound_map(con, c):
    """this game's citizens near c: serial -> (host id it is bound to or 0) (`coop.wobj list citizen`)"""
    out = con.call(f'coop.wobj list citizen 250 {c[0]:.1f} {c[1]:.1f} {c[2]:.1f}')['out']
    return {int(m.group(1)): int(m.group(2)) for m in re.finditer(r'id=(\d+) .*?bound_to=(\d+)', out)}


def misses(h, g, uh, sc, gmap):
    """BUG-1221: why a host citizen has no partner and a pair has another size (the guest's binds)"""
    hs = {x['ser']: x for x in h}
    gs = {y['ser']: y for y in g}
    copy_of = {hid: gs.get(gser) for gser, hid in gmap.items() if hid}
    for x in uh:
        y = copy_of.get(x['ser'])
        dn = min((math.dist(x['pos'], z['pos']) for z in g), default=-1)
        print(f'      unpaired host {x["ser"]}: ' + (f'its copy {y["ser"]} {math.dist(x["pos"], y["pos"]):.1f} m away'
              if y else 'no copy bound') + f', nearest guest citizen {dn:.1f} m', flush=True)
    for x, y in sc:
        hid = gmap.get(y['ser'], -1)
        z = hs.get(hid)
        mine = copy_of.get(x['ser'])
        print(f'      size pair host {x["ser"]} {x["scale"]:.2f} / guest {y["ser"]} {y["scale"]:.2f} '
              f'({math.dist(x["pos"], y["pos"]):.1f} m): the guest\'s is bound to '
              + (f'host {hid} {math.dist(y["pos"], z["pos"]):.1f} m away, size {z["scale"]:.2f}' if z else
                 'nobody' if hid == 0 else f'host {hid} (not near)')
              + ('; the host\'s copy ' + (f'{mine["ser"]} {math.dist(x["pos"], mine["pos"]):.1f} m away, size {mine["scale"]:.2f}'
                                         if mine else 'none')), flush=True)


def wobj_citizen(guest):
    out = guest.call('coop.wobj')['out']
    return ' '.join(ln.strip() for ln in out.splitlines() if ln.startswith('kind citizen') or ln.startswith('citizens '))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=8)
    ap.add_argument('--reload', action='store_true', help='the host loads the fixture again first (BUG-1221)')
    ap.add_argument('--reloads', type=int, default=1, help='with --reload: loads, the rounds after each')
    ap.add_argument('--plant', type=int, default=0, help='before the rounds the guest drops the binds of N citizens and '
                    'puts them 60 m out (`coop.wobj loose`: what a load leaves, BUG-1221)')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    n_host = n_unpaired = n_pairs = n_scale = 0
    worst = (0.0, 0, 0)  # BUG-1221: the load with the largest share of host citizens without a partner
    for load in range(a.reloads if a.reload else 1):
        if a.reload:
            time.sleep(5)
            ok = warm_reset(host, guest, 'civ_npc', fixtures.planet_of('civ_npc'))
            ck(ok, f'load {load + 1}: the games start again from the fixture (leave, load, join)')
            if not ok:
                break
        time.sleep(25)
        print(f'  after {"load " + str(load + 1) if a.reload else "the start"}: {wobj_citizen(guest)}', flush=True)
        if a.plant:
            out = guest.call(f'coop.wobj loose citizen {a.plant} 60')['out'].strip()
            print('  planted: ' + out.replace('\n', '; '), flush=True)
            ck(f'loose n={a.plant}' in out, f'the guest let {a.plant} of its citizens loose 60 m out')
            time.sleep(3)
        r = rounds(host, guest, a.rounds)
        n_host, n_unpaired, n_pairs, n_scale = (n_host + r[0], n_unpaired + r[1], n_pairs + r[2], n_scale + r[3])
        worst = max(worst, (r[1] / max(r[0], 1), r[1], r[0]))
    print(guest.call('coop.wobj')['out'].strip(), flush=True)
    print(guest.call('coop.fields')['out'].strip(), flush=True)
    n_rounds = a.rounds * (a.reloads if a.reload else 1)
    ck(n_host >= 10, f'the host had citizens near the centre ({n_host} over {n_rounds} rounds)')
    share = n_unpaired / max(n_host, 1)
    ck(share <= 0.10, f'the guest\'s citizens are where the host\'s are: {n_unpaired} of {n_host} without a partner '
                      f'({share:.0%}, at most 10 %)')
    if a.reload and a.reloads > 1:  # each load on its own (one bad load among good ones is still a player's bad load)
        ck(worst[0] <= 0.10, f'after every load: at most {worst[1]} of {worst[2]} without a partner ({worst[0]:.0%}, at most 10 %)')
    ss = n_scale / max(n_pairs, 1)
    ck(n_pairs >= 10, f'unambiguous pairs to compare sizes ({n_pairs})')
    ck(ss <= 0.10, f'the guest\'s citizens have the host\'s size: {n_scale} of {n_pairs} unambiguous pairs differ ({ss:.0%}, at most 10 %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


def rounds(host, guest, n_rounds):
    """n_rounds checks 6 s apart: (host citizens, without a partner, unambiguous pairs, of another size)"""
    n_host = n_unpaired = n_pairs = n_scale = 0
    for rnd in range(n_rounds):
        hw0, gw0 = worlddiff.pair_dump(host, guest)
        time.sleep(1.0)
        hw, gw = worlddiff.pair_dump(host, guest)
        c = next((m for m in (gw.me, hw.me) if m and not worlddiff._origin(m)), None)
        if c is None:  # no pointer on either game: the host's first citizen's place
            allh = citizens(hw, (0, 0, 0), 1e9)
            c = allh[0]['pos'] if allh else None
        if c is None:
            print(f'  round {rnd + 1}: no citizen on the host', flush=True)
            continue
        h, g = citizens(hw, c, 128.0), citizens(gw, c, 160.0)
        pairs, uh, _ = worlddiff._match(h, g, 15.0)
        # sizes: only unambiguous pairs (each the other's nearest, within 3 m, the next one 2 m farther): in a
        # crowd the detector's greedy pairs cross (a pair 0.3 m apart of two different citizens)
        clear = []
        for x in h:
            dg = sorted((math.dist(x['pos'], y['pos']), i) for i, y in enumerate(g))
            if not dg or dg[0][0] > 3.0 or (len(dg) > 1 and dg[1][0] < dg[0][0] + 2.0):
                continue
            y = g[dg[0][1]]
            dh = sorted(math.dist(y['pos'], z['pos']) for z in h)
            if len(dh) > 1 and dh[1] < dh[0] + 2.0:
                continue
            clear.append((x, y))
        pairs_all = pairs
        pairs = clear
        sc = [(x, y) for x, y in pairs if worlddiff._rel(x['scale'], y['scale']) > 0.12]
        n_host += len(h)
        n_unpaired += len(uh)
        n_pairs += len(pairs)
        n_scale += len(sc)
        ex = f', e.g. {sc[0][0]["scale"]:.2f} host / {sc[0][1]["scale"]:.2f} guest' if sc else ''
        ds = sorted(math.dist(x['pos'], y['pos']) for x, y in pairs_all)
        scd = sorted(round(math.dist(x['pos'], y['pos']), 1) for x, y in sc)
        if ds:
            print(f'    pair distances: median {ds[len(ds) // 2]:.1f} m, max {ds[-1]:.1f} m; size pairs at {scd}', flush=True)
        print(f'  round {rnd + 1}: host {len(h)} citizens near {worlddiff._fmt(c)}, guest {len(g)}; {len(uh)} of the '
              f'host\'s without a partner within 15 m; {len(sc)} of {len(pairs)} unambiguous pairs with another size{ex}', flush=True)
        gmap = bound_map(guest, c)
        hbound = set(gmap.values())
        hfree = [x for x in h if x['ser'] not in hbound]
        gfree = [y for y in g if not gmap.get(y['ser'])]
        if hfree:
            print(f'      unbound host citizens {[x["ser"] for x in hfree]}: the nearest free guest citizen '
                  + ', '.join(f'{min((math.dist(x["pos"], y["pos"]) for y in gfree), default=-1):.0f} m' for x in hfree), flush=True)
        # (diagnostic) standing pairs: both moved < 0.5 m in the second before
        h0 = {x['ser']: x['pos'] for x in hw0.rows if x['kind'] == 'g' and x['noun'] == CITIZEN}
        g0 = {y['ser']: y['pos'] for y in gw0.rows if y['kind'] == 'g' and y['noun'] == CITIZEN}
        stand = [(x, y) for x, y in pairs if x['ser'] in h0 and y['ser'] in g0 and math.dist(h0[x['ser']], x['pos']) < 0.5
                 and math.dist(g0[y['ser']], y['pos']) < 0.5]
        print(f'      standing unambiguous pairs {len(stand)}, of another size '
              f'{sum(1 for x, y in stand if worlddiff._rel(x["scale"], y["scale"]) > 0.12)}', flush=True)
        if uh or sc:
            misses(h, g, uh, sc, gmap)
        time.sleep(5)
    return n_host, n_unpaired, n_pairs, n_scale


if __name__ == '__main__':
    raise SystemExit(main())
