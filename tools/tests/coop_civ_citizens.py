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
with another size. SPORE_FIX_OFF=civ.citizens: about half unpaired (FAIL); SPORE_FIX_OFF=civ.citizen_scale:
the sizes differ (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_citizens.py [--rounds 5]
"""
import argparse
import math
import time

import worlddiff
from twoinst import Checks, start

CITIZEN = 0x018EB4B7


def citizens(w, c, r):
    return [x for x in w.rows if x['kind'] == 'g' and x['noun'] == CITIZEN and not worlddiff._origin(x['pos'])
            and math.dist(x['pos'], c) < r]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=8)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(25)
    n_host = n_unpaired = n_pairs = n_scale = 0
    for rnd in range(a.rounds):
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
        time.sleep(6)
    print(guest.call('coop.wobj')['out'].strip(), flush=True)
    print(guest.call('coop.fields')['out'].strip(), flush=True)
    ck(n_host >= 10, f'the host had citizens near the centre ({n_host} over {a.rounds} rounds)')
    share = n_unpaired / max(n_host, 1)
    ck(share <= 0.10, f'the guest\'s citizens are where the host\'s are: {n_unpaired} of {n_host} without a partner '
                      f'({share:.0%}, at most 10 %)')
    ss = n_scale / max(n_pairs, 1)
    ck(n_pairs >= 10, f'unambiguous pairs to compare sizes ({n_pairs})')
    ck(ss <= 0.10, f'the guest\'s citizens have the host\'s size: {n_scale} of {n_pairs} unambiguous pairs differ ({ss:.0%}, at most 10 %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
