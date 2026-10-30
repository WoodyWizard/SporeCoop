#!/usr/bin/env python3
"""Tribe: an animal that dashes on the host is not shown metres behind on the guest (BUG-0305).

A wounded animal of a Tribe hunt runs in short dashes (5-7 m/s for ~1 s, then stands). The guest's
proxy walks with the game's locomotion, which never runs faster than the animal's own top speed:
the lag at the start of a dash stayed as an offset along the path for the whole dash (2-4 m on
every guest at once in the 4-player measurement, coop_sync_multi.py --fixture tribe).

1. Two games on the tribe fixture; the guest runs at ~15 fps (frame.limit 66, the frame rate of a
   loaded guest of a 4-player session on one PC: the largest offsets came at 9-20 fps).
2. The host picks two animals of the planet's small herd species (06808ACA, the one 4 m off in
   the 4-player probes) that the guest has as proxies and makes them run side by side (1.2 m
   apart, a herd fleeing together) back and forth along a 12 m zigzag at 6.5 m/s, 0.6 s standing
   at each end (`coop.npc dash`, test command), for 40 s.
3. Every ~0.5 s the guest's game positions of those animals is compared with the host's path over the
   0.25 s before the guest's sample and the host's sample after it (coop_multi.measure, as the
   campaign does). PASS: 90 % of the samples within 1.2 m (fix off: p90 1.5-2.8 m, on: 0.7-0.95 m).

Switch: SPORE_FIX_OFF=npc.trail (the driving before the fix) must FAIL.

Creature (BUG-0281, `creature_npc_dash`): DASH_FIXTURE=creature runs the same on the creature
fixture with the nest species next to the start (065CBE70, the NPC 3-5 m from the guests' avatars
in the 4-player report) at 4 m/s (their running speed); the guest's avatar stands 1.5 m beside the
end of the path, so the proxies run up to it and stand next to it at every turn. A measurement,
not in a suite: it fails with and without npc.trail (p90 6.2 m off, 3.1-3.4 m on, lane 5,
2026-10-30; these proxies hardly walk by themselves, BUG-0281 stays open).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_npc_dash.py
  SPORE_LANE=5 DASH_FIXTURE=creature python3 tools/tests/coop_tribe_npc_dash.py
"""
import math
import os
import re
import time

import coop_multi as cm
from twoinst import Checks, start

LIMIT = 1.5     # m (printed: samples farther than this)
P90 = 1.2       # m: the 90th percentile of the errors (fix off: 1.5-2.8 m, on: 0.7-0.95 m)
FRAME_MS = int(os.environ.get('DASH_FRAME_MS', '66'))
LEN, WIDTH = float(os.environ.get('DASH_LEN', '12')), float(os.environ.get('DASH_WIDTH', '2'))
OFF = float(os.environ.get('DASH_OFF', '1.2'))  # m: the second animal runs this far beside the first
# the small herd animal of the fixture's planet (the one 4 m off in the 4-player probes, BUG-0305)
FIXTURE = os.environ.get('DASH_FIXTURE', 'tribe')
# Creature: the nest species next to the fixture's start (BUG-0281)
SPECIES = os.environ.get('DASH_SPECIES', '065CBE70' if FIXTURE == 'creature' else '06808ACA')
# m/s: Creature NPCs ran at up to 4 m/s in coop.npc list (spd=) on lane 5
SPEED = float(os.environ.get('DASH_SPEED', '4' if FIXTURE == 'creature' else '6.5'))


def host_animals(con):
    """the host's living NPCs: id -> pos"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=\S+ pos=\((\S+) (\S+) (\S+)\)', con.call('coop.npc list')['out'], re.M):
        if m.group(2) == SPECIES and float(m.group(3)) > 0:
            out[int(m.group(1))] = tuple(map(float, m.group(4, 5, 6)))
    return out


def guest_proxies(con):
    return {int(m.group(1)) for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned ', con.call('coop.npc list')['out'], re.M)}


def main():
    host, guest = start(FIXTURE)
    ck = Checks()
    print('  guest:', guest.call(f'frame.limit {FRAME_MS}')['out'].strip(), flush=True)
    time.sleep(5)
    hs, gs = host_animals(host), guest_proxies(guest)
    both = sorted(i for i in hs if i in gs)
    ck(both, f'the guest mirrors animals of the host ({len(hs)} on the host, {len(gs)} proxies, {len(both)} both)')
    if not both:
        return ck.result()
    # the animal nearest to the guest's own view: any one the guest has; the first
    nid = both[0]
    mate = both[1] if len(both) > 1 else 0
    keys = [f'n{nid}'] + ([f'n{mate}'] if mate else [])
    if FIXTURE == 'creature' and os.environ.get('DASH_GUEST', '1') == '1':  # the guest 1.5 m beside the path's end (BUG-0281)
        p = hs[nid]
        r = math.sqrt(sum(x * x for x in p))
        up = [x / r for x in p]
        a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]  # npc.cpp debug_dash's axis
        al = math.sqrt(sum(x * x for x in a))
        a = [x / al for x in a]
        b = [up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]]
        end = [p[i] + a[i] * LEN - b[i] * 1.5 for i in range(3)]
        print('  guest:', guest.call('coop.npc gopos %.1f %.1f %.1f' % tuple(end))['out'].strip(), flush=True)
        time.sleep(3)
    print('  host:', host.call(f'coop.npc dash {nid} {LEN} {SPEED} 45 {WIDTH} {mate} {OFF}')['out'].strip(), flush=True)
    time.sleep(2)
    errs = []
    per = {k: 0 for k in keys}
    t_end = time.time() + 40
    while time.time() < t_end:
        h0, h1 = cm.host_path(host)
        gsm = cm.sample(guest)
        h2 = cm.sample(host)
        for key in keys:
            if key not in gsm or key not in h1:
                continue
            e = min(cm.seg_dist(gsm[key], h0.get(key, h1[key]), h1[key]),
                    cm.seg_dist(gsm[key], h1[key], h2.get(key, h1[key])))
            errs.append(e)
            per[key] += 1
            if os.environ.get('DASH_VERBOSE'):
                gl = [l.strip() for l in guest.call('coop.npc list')['out'].splitlines() if re.match(rf'\s*#{nid}\s', l)]
                print(f'    {e:.2f} m', re.sub(r' (puppet|ptr|herd|body|species|hp|dist)=\S+', '', gl[0])[:220] if gl else '-', flush=True)
        time.sleep(0.25)
    for i in (nid, mate):
        line = [l.strip() for l in guest.call('coop.npc list')['out'].splitlines() if re.match(rf'\s*#{i}\s', l)]
        print(f'  guest proxy #{i}:', line[0][:300] if line else '-', flush=True)
    guest.call('frame.limit auto')
    ck(len(keys) == 2 and min(per.values()) >= 30, f'samples of both animals on both machines ({per})')
    if errs:
        s = sorted(errs)
        far = sum(1 for e in errs if e > LIMIT)
        print(f'  errors: median {s[len(s) // 2]:.2f} m, p90 {s[int(0.9 * len(s))]:.2f} m, max {s[-1]:.2f} m, '
              f'{far} of {len(errs)} over {LIMIT} m', flush=True)
        p90 = s[int(0.9 * len(s))]
        ck(p90 <= P90, f'the guest shows the running animals where the host has them '
           f'(90 % of {len(errs)} samples within {p90:.2f} m, limit {P90} m)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
