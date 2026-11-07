#!/usr/bin/env python3
"""Tribe: the guest's villages have the host's colours (BUG-0644).

Player report 2026-11-06 (0.7.8): "village color is desynced", "other villages have different color for guest
and host". A village's colour is its identity colour (cTribe +0x230: mIDColorID +4, a colour id 0x053DBCxx of the
game's colour list; mCachedColor +0x24). A new tribe takes the next colour of the list, so a guest that makes the
host's villages (tribe.npc_tribes) in another order or with a village of its own picks other colours.

1. Two games on the tribe fixture; the host's game forms a village (`coop.tribe npc_tribe`), the guest makes it.
2. The guest's game gives that village another colour (`coop.tribe color`: the state a different pick leaves).
3. For 15 s: every village's colour id on both games (the game's own field), matched by place.
PASS: within 8 s every village of the guest has the host's colour and keeps it.
--nofix (`coop.fix wsync.tribe_color off`): must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_colors.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def villages(con):
    """[(index, (x, y, z), colour id)] of the NPC tribes"""
    out = []
    for ln in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+) at \((-?\d+) (-?\d+) (-?\d+)\):.* color=(\w+)', ln)
        if m:
            out.append((int(m.group(1)), tuple(int(m.group(i)) for i in (2, 3, 4)), m.group(5)))
    return out


def pairs(h, g):
    """(host village, guest village) by place (within 40 m)"""
    out = []
    for hv in h:
        best = min(g, key=lambda gv: sum((a - b) ** 2 for a, b in zip(hv[1], gv[1])), default=None)
        if best and sum((a - b) ** 2 for a, b in zip(hv[1], best[1])) < 40 * 40:
            out.append((hv, best))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix wsync.tribe_color off')['out'].strip(), flush=True)
    print('  host:', host.call('coop.tribe npc_tribe 3 150')['out'].strip(), flush=True)
    t0, pr = time.time(), []
    while time.time() - t0 < 30:
        h, g = villages(host), villages(guest)
        pr = pairs(h, g)
        if len(pr) == len(h) and len(h) >= 2:
            break
        time.sleep(1)
    print(f'  host {villages(host)}\n  guest {villages(guest)}', flush=True)
    ck(len(pr) >= 2, f'the guest has the host\'s villages, the new one too ({len(pr)})')
    if len(pr) < 2:
        return ck.result()
    target = pr[-1][1][0]  # the newest village on the guest
    other = next((hv[2] for hv, gv in pr if hv[2] != pr[-1][0][2]), '053DBCFA')
    print('  guest:', guest.call(f'coop.tribe color {target} {other}')['out'].strip(), f'(village {target} -> {other})',
          flush=True)
    t0, last_diff = time.time(), None
    while time.time() - t0 < 15:
        t = time.time() - t0
        bad = [(hv[2], gv[2]) for hv, gv in pairs(villages(host), villages(guest)) if hv[2] != gv[2]]
        if bad:
            last_diff = t
            print(f'    {t:4.1f} s: host / guest colours that differ {bad}', flush=True)
        time.sleep(1)
    ck(last_diff is None or last_diff < 8,
       f'every village of the guest has the host\'s colour within 8 s and keeps it (last difference at '
       f'{"-" if last_diff is None else f"{last_diff:.0f} s"})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
