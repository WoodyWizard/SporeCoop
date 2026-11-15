#!/usr/bin/env python3
"""The city planner's editor is not trusted after the Space leaving frame (BUG-0682).

The city input strategy's editor (cCommunityEditor, docs/re/civ.md#civ-planner-lifetime) runs its update every
frame in Tribe, Civ and Space; the mod remembers the editor of the last update and trusted it for 3 frames.
The quit to the galaxy menu frees it with the strategy: its memory stays readable and +0x40 holds garbage, and
the next caller of planner_city() (wsync's city rows on a Space planet, a guest's Civ messages) cast it: a
guest's caught fault at game::cast (coop.dll+0x1071C8) in campaign_space, 0.3 s after the leaving frame.

Space fixture. The guest calls planner_city() once a frame (`coop.civ planner_probe on`: what those callers do,
every frame instead of now and then) and leaves the world for the galaxy menu (`coop.menu`) and gets the host's
world back, several times. Checks: the editor's update was seen (the probe had something to trust), planner_city()
never read an editor of another mode or of a leaving stage (`stale_reads=0`), and neither game faulted.

  coop_planner_leave.py [--rounds 4]   (SPORE_FIX_OFF=civ.planner_stale: the probe reads the freed editor in the
                                        leaving frames, it FAILS)"""
import argparse
import re
import time

from twoinst import Checks, start


def probe(con, arg=''):
    out = con.call(f'coop.civ planner_probe {arg}'.strip())['out']
    m = re.search(r'frames=(\d+) editor_seen=(\d+) stale_reads=(\d+) stale_cities=(\d+) refused=(\d+)', out)
    return dict(zip(('frames', 'seen', 'stale', 'cities', 'refused'), (int(x) for x in m.groups()))) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=4)
    a = ap.parse_args()
    host, guest = start('space')
    ck = Checks()
    print(' ', guest.call('coop.civ planner_probe on')['out'].strip())
    time.sleep(3)
    p0 = probe(guest)
    ck(p0 is not None and p0['seen'] > 0, f'the guest\'s game runs the planner editor\'s update in Space ({p0})')
    back_all = True
    for n in range(a.rounds):
        print(f'  round {n + 1}:', guest.call('coop.menu')['out'].strip(), flush=True)
        t0 = time.time()
        left = back = False
        while time.time() - t0 < 150:
            mode = guest.call('game.mode')['out']
            if 'Space' not in mode:
                left = True
            if left and 'Space' in mode and 'together 1 1' in host.call('coop.status')['out']:
                back = True
                break
            time.sleep(0.5)
        print(f'    back after {time.time() - t0:.0f} s: {probe(guest)}', flush=True)
        back_all &= back
        if not back:
            break
        time.sleep(4)
    ck(back_all, f'the guest came back into the host\'s world {a.rounds} times')
    p = probe(guest, 'off')
    print(f'  probe: {p}')
    ck(p is not None and p['stale'] == 0,
       f'planner_city() never read the editor of another mode or of a leaving stage ({p and p["stale"]} frames, '
       f'{p and p["cities"]} with a "city")')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
