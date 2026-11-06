#!/usr/bin/env python3
"""Tribe: every member the host has near the guest is a member at that place in the guest's game (BUG-0488 probe).

The detector's `Tribe:unmatched:citizen` row in play_tribe / campaign_tribe (3-6 of 31 samples a run, lab
2026-11-05/06): "N on the host, N-1 on the guest, 1 host / 0 guest without a partner within 15 m".

1. Two games on the tribe fixture; the session runs on its own (NPC tribes, members at work) for --minutes.
2. Every 10 s: the detector's own compare (worlddiff.pair_dump, the game's object lists round the guest) and,
   for each host citizen without a partner, what the mod knows of it: the host member id at that place
   (`coop.sync` t<id>), the guest's mirror of that id and where it is, the nearest guest citizen.
PASS: no host citizen without a partner in two samples in a row (a mirror on its way is seen once).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_member_place.py [--minutes 3] [--attach]
"""
import argparse
import math
import time

import worlddiff
from coop_sync import sample
from twoinst import Checks, start


def citizens(w, c, r):
    return [x for x in w.rows if x['kind'] == 'g' and worlddiff.noun_name(x['noun']) == 'citizen'
            and not worlddiff._origin(x['pos']) and math.dist(x['pos'], c) <= r]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=3)
    ap.add_argument('--fixture', default='tribe')
    ap.add_argument('--attach', action='store_true', help='watch the games of a running test on SPORE_LANE (play_tribe)')
    a = ap.parse_args()
    if a.attach:
        import lane
        from sporectl import Console
        host, guest = Console(lane.HOST, timeout=20), Console(lane.GUEST, timeout=20)
    else:
        host, guest = start(a.fixture)
    ck = Checks()
    t0, prev, twice, samples = time.time(), set(), [], 0
    while time.time() - t0 < a.minutes * 60:
        try:
            hw, gw = worlddiff.pair_dump(host, guest)
            if 'Tribe' not in (hw.mode or '') or 'Tribe' not in (gw.mode or ''):
                raise RuntimeError(f'not both in Tribe ({hw.mode}, {gw.mode})')
            hs, gs = sample(host), sample(guest)
        except Exception as e:  # --attach: the test's games start, load or stop
            print(f'  {time.time() - t0:5.0f} s: no compare ({e})', flush=True)
            time.sleep(10)
            continue
        c = gw.me if gw.me and not worlddiff._origin(gw.me) else hw.me
        if not c:
            time.sleep(10)
            continue
        samples += 1
        r = worlddiff.AREA['Tribe'] * 0.8
        hc, gc = citizens(hw, c, r), citizens(gw, c, worlddiff.AREA['Tribe'] * 1.5)
        lone = []
        for x in hc:
            d = min((math.dist(x['pos'], y['pos']) for y in gc), default=1e9)
            if d <= worlddiff.TOL_MOVING:
                continue
            tid = min((k for k in hs if k.startswith('t')), key=lambda k: math.dist(hs[k][0], x['pos']), default=None)
            td = math.dist(hs[tid][0], x['pos']) if tid else -1
            gm = gs.get(tid)
            anyk = min(hs, key=lambda k: math.dist(hs[k][0], x['pos']), default=None)
            hme = hw.me if hw.me and not worlddiff._origin(hw.me) else None
            print(f'           key={x["key"]} noun={x["noun"]:08X}; nearest host sync entry {anyk} '
                  f'{math.dist(hs[anyk][0], x["pos"]) if anyk else -1:.1f} m; the host player at '
                  f'{worlddiff._fmt(hme) if hme else "-"} ({math.dist(hme, x["pos"]) if hme else -1:.1f} m)', flush=True)
            # what the object is: its pointer (coop.wobj list citizen), vtable, age (+0xB34: 0 baby), dead flag (+0xB5E)
            wl = host.call('coop.wobj list citizen 3 ' + ' '.join(f'{v:.2f}' for v in x['pos']))['out'].split()
            if len(wl) > 1 and wl[0] == 'citizen':
                ptr = int(wl[1], 16)
                vt = host.call(f'mem.read {ptr:#x} 4')['out'].strip()
                age = host.call(f'mem.read {ptr + 0xB34:#x} 1')['out'].strip()
                dead = host.call(f'mem.read {ptr + 0xB5E:#x} 1')['out'].strip()
                print(f'           object {ptr:#x}: vtable {vt}; age byte {age}; dead flag {dead}', flush=True)
                # BUG-0620: its tribe (+0x1014), whether that tribe lists it, its behaviour node and order
                print(f'           who: {host.call(f"coop.tribe who {ptr:x}")["out"].strip()}', flush=True)
            lone.append((tuple(round(v) for v in x['pos']), x['ser']))
            print(f'  {time.time() - t0:5.0f} s: host citizen ser={x["ser"]} at {worlddiff._fmt(x["pos"])} '
                  f'({math.dist(x["pos"], c):.0f} m from the guest): nearest guest citizen {d:.0f} m; mod id {tid} '
                  f'({td:.1f} m from it on the host); guest mirror {tid}: '
                  f'{"none" if gm is None else worlddiff._fmt(gm[0]) + f" {math.dist(gm[0], x['pos']):.1f} m off"}',
                  flush=True)
        cur = {s for _, s in lone}
        if cur & prev:
            twice.append(sorted(cur & prev))
        prev = cur
        time.sleep(5)
    print(f'  {samples} samples; host citizens without a partner in two samples in a row: {twice}', flush=True)
    ck(samples > 3, f'compared ({samples} samples)')
    ck(not twice, f'every host citizen near the guest has a partner on the guest within two samples ({len(twice)} times not)')
    if not a.attach:
        ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
