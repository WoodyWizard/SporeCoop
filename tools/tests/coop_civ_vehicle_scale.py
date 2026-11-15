#!/usr/bin/env python3
"""Civilization: a guest's mirror vehicle has the size of the host's vehicle (BUG-0549).

play_civ with the detector: `Civilization:scale:vehicle`, one vehicle at 0.81 on the host and 10.00 on the
guest. The guest's own game sets the 10 (the vehicle's model-ready handler 0xCA40B0, SetScale(10.0) from
0xCA41DA, seen on guests only).

1. Two games on civ_npc.
2. The guest's game gives one mirror vehicle the scale 10 (`coop.civ vscale <id> 10`: the state the
   game's own handler leaves).
3. For 15 s: that vehicle's scale in both games (`coop.civ` veh lines, the game's GetScale).
PASS: within 6 s the guest's vehicle has the host's scale again (2 %) and keeps it.
--nofix (`coop.fix civ.vehicle_scale off`): must FAIL (the 10 stays).

  SPORE_LANE=4 python3 tools/tests/coop_civ_vehicle_scale.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def scales(con):
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) .* scale=([-\d.]+)', ln)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = float(m.group(2))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix civ.vehicle_scale off')['out'].strip(), flush=True)
    t0 = time.time()
    both = []
    while time.time() - t0 < 30 and not both:
        hs, gs = scales(host), scales(guest)
        both = sorted(i for i in hs if i in gs)
        time.sleep(1)
    print(f'  host {hs}\n  guest {gs}', flush=True)
    ck(both, f'a vehicle of the host mirrored on the guest ({both})')
    if not both:
        return ck.result()
    vid = both[0]
    ck(abs(hs[vid] - 10) > 0.5, f'the host\'s vehicle {vid} is not at scale 10 ({hs[vid]:.2f})')
    print('  guest:', guest.call(f'coop.civ vscale {vid} 10')['out'].strip(), flush=True)
    t0, last_diff = time.time(), None
    while time.time() - t0 < 15:
        t = time.time() - t0
        h, g = scales(host).get(vid), scales(guest).get(vid)
        if h is None or g is None or abs(h - g) > 0.02 * max(h, 0.01):
            last_diff = t
            print(f'    {t:4.1f} s: host {h} guest {g}', flush=True)
        time.sleep(1)
    ck(last_diff is not None and last_diff < 6 or last_diff is None,
       f'the guest\'s vehicle {vid} has the host\'s scale within 6 s and keeps it (last difference at '
       f'{"-" if last_diff is None else f"{last_diff:.0f} s"})')
    print('  guest fields:', guest.call('coop.fields')['out'].strip()[:400], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
