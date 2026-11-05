#!/usr/bin/env python3
"""BUG-0550 / BUG-0425: an animal off its ground on the host is drawn off the ground on the guest too, at the host's
height over the host's terrain (Creature, and Tribe with --fixture tribe).

The sync-check found an NPC 9-14 m over the ground on the host (a flier, a jump) drawn on the ground on the guest
(`n apart 9.3`, the guest's `want_agl=10.56 made_agl=0`): the proxy was walked and the guest's game put the grounded
proxy back onto its terrain.

1. Two games on the fixture, both games' time running. The host's NPC nearest the guest's view that the guest
   mirrors (not the host's family or pack).
2. 5 s on the ground: the host's and the guest's `coop.npctrack` rows of that NPC (the position the game drew,
   before the mod's frame, and its height over that game's own terrain, GetHeightAt).
3. The host's game holds the NPC LIFT m over its ground (`coop.npc lift`: placed there every frame, not grounded,
   +0x137 = 0, as a flier or a thrown animal) for SECS s.
4. Checks on what each game drew:
   * the host's NPC was in the air (its own rows: median height over its terrain over LIFT - 1 m);
   * the guest drew the proxy in the air in at least AIR_SHARE of its rows of the air phase (over 1 m);
   * the median drawn height on the guest within HEIGHT_TOL m of the host's;
   * on the ground before and after (the landing): the proxy on the guest's ground (median under 0.3 m).
SPORE_FIX_OFF=npc.air must FAIL (the proxy drawn on the guest's ground).

Tribe (--fixture tribe) is not a test of this: the host's Tribe game puts a lifted animal back onto its terrain in
its own update (the host's rows 0.00 m with the animal lifted 4 m at every mod frame, lab 20261105-112236), so the
check "the host's NPC was in the air" fails there (docs/re/tribe.md#tribe-animal-height).

  SPORE_LANE=4 python3 tools/tests/coop_npc_air.py [--fixture creature|tribe] [--lift 4] [--secs 15]
"""
import argparse
import math
import re
import statistics
import time

from coop_npc_walk import family, host_npcs
from coop_sync import wait_running
from twoinst import Checks, start

AIR_SHARE = 0.8
HEIGHT_TOL = 0.75  # m


def rows_of(con, nid):
    out, i = [], 0
    while True:
        lines = [ln.split() for ln in con.call(f'coop.npctrack dump {i} 4000')['out'].splitlines() if ln.strip()]
        i += len(lines)
        out += [(float(f[0]), int(f[2]), float(f[28])) for f in lines if len(f) >= 30 and int(f[1]) == nid]
        if len(lines) < 4000:
            return out


def med(v):
    return statistics.median(v) if v else float('nan')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--lift', type=float, default=4.0)
    ap.add_argument('--secs', type=float, default=15)
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    w = wait_running(host, guest, 120)
    ck(w is not None, f'both games\' time runs ({w})')
    for c in (host, guest):
        c.call('coop.team off')
    m = re.search(r'centre=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.npctrack count')['out'])
    centre = tuple(float(m.group(i)) for i in (1, 2, 3)) if m else (0, 0, 0)
    mirrored = {int(x) for x in re.findall(r'^\s*#(\d+) species=\S+ spawned', guest.call('coop.npc list')['out'], re.M)}
    hn = host_npcs(host)
    fam = family(host)
    cands = sorted((i for i in hn if i in mirrored and i not in fam and math.dist(hn[i][0], centre) < 100),
                   key=lambda i: math.dist(hn[i][0], centre))
    print(f'  candidates {[(i, round(math.dist(hn[i][0], centre))) for i in cands[:6]]}', flush=True)
    ck(bool(cands), f'a host NPC the guest mirrors within 100 m of its view ({len(hn)} host NPCs, {len(mirrored)} proxies, centre {centre})')
    if not cands:
        return ck.result()
    nid = cands[0]
    for c in (host, guest):
        c.call('coop.npctrack start 120')
    t0 = time.time()
    time.sleep(5)
    t_lift = time.time() - t0
    print('  host: ' + host.call(f'coop.npc lift {nid} {a.lift:.2f} {a.secs:.1f}')['out'].strip(), flush=True)
    time.sleep(a.secs)
    t_land = time.time() - t0
    time.sleep(6)
    for c in (host, guest):
        c.call('coop.npctrack stop')
    hr, gr = rows_of(host, nid), rows_of(guest, nid)
    print(f'  rows: host {len(hr)}, guest {len(gr)}; guest in the air {sum(1 for r in gr if r[1] == 5)}', flush=True)
    # the two tracks started within a command of each other: windows with a second of margin
    def win(rows, lo, hi):
        return [r[2] for r in rows if lo <= r[0] <= hi]
    h_air, g_air = win(hr, t_lift + 1.5, t_land - 0.5), win(gr, t_lift + 1.5, t_land - 0.5)
    g_before, g_after = win(gr, 1.0, t_lift - 0.5), win(gr, t_land + 3.0, t_land + 6.0)
    share = sum(1 for v in g_air if v > 1.0) / max(len(g_air), 1)
    print(f'  air: host median {med(h_air):.2f} m ({len(h_air)} rows), guest median {med(g_air):.2f} m '
          f'({len(g_air)} rows, {share:.0%} over 1 m); guest before {med(g_before):.2f} m, after {med(g_after):.2f} m',
          flush=True)
    ck(len(h_air) >= 30 and med(h_air) > a.lift - 1.0, f'the host\'s NPC #{nid} was in the air ({med(h_air):.2f} m)')
    ck(len(g_air) >= 30, f'the guest drew the proxy in the air phase ({len(g_air)} rows)')
    ck(share >= AIR_SHARE, f'the guest drew it in the air ({share:.0%} of the rows over 1 m, want {AIR_SHARE:.0%})')
    ck(abs(med(g_air) - med(h_air)) <= HEIGHT_TOL,
       f'at the host\'s height over the ground (guest {med(g_air):.2f} m, host {med(h_air):.2f} m)')
    ck(abs(med(g_before)) < 0.3, f'on the guest\'s ground before ({med(g_before):.2f} m)')
    ck(abs(med(g_after)) < 0.3, f'on the guest\'s ground after the landing ({med(g_after):.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
