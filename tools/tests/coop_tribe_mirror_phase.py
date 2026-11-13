#!/usr/bin/env python3
"""Tribe: a guest's copy of a member is at the same moment of a gesture as the host's member (BUG-0988).

0.7.13 final check stills: the host's chief held its staff upright on one game and diagonal on the other, the same
item, the same animation id and the same hold on both (tribe_mirror_hold 02_hold, same_view_tribe 04_view4); Bob's
chief leaned forward on one game and stood on the other in the same animation. The relay starts a member's behaviour
animation on the guest's mirror when the start reaches it (one network delay and up to a cycle later) and nothing
brings the two back to the same moment of it.

Two games on the tribe fixture, 4 more members, both games' `coop.tribe anims` sampled together (two threads, ~8 Hz)
for SECS s: each member's current animation, whether it loops, and the game's time in it (the main track's time, the
game's own state). For every pair of samples where the host's member and its mirror play the same animation that does
not loop, at least 1.2 s into it on the host (the host's time news comes once a second), the difference of the two
games' times in it (each time taken back to the same wall clock moment). Checked: at least 40 such samples; the median
difference is under 0.08 s and 80 % of them under 0.15 s. SPORE_FIX_OFF=tribe.mirror_phase: the relay's delay stays
(median 0.1-0.3 s, FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_mirror_phase.py [--secs 120]
"""
import argparse
import re
import threading
import time

from twoinst import Checks, game_runs, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console

ROW = re.compile(r'cur (\d+) anim=(\w+) bhv=\w+ pos=\([^)]*\) idle=\w+ hold=\d t=([-\d.]+) loop=(-?\d)')


def sample(inst, secs, rows):
    c = Console(inst)
    t0 = time.time()
    while time.time() - t0 < secs:
        out = c.call('coop.tribe anims')['out']
        w = time.time()
        rows.append((w, {m.group(1): (m.group(2), float(m.group(3)), m.group(4) == '1') for m in ROW.finditer(out)}))
        time.sleep(0.12)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=120)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip()[:80], flush=True)
    time.sleep(5)
    rows = {'host': [], 'guest': []}
    ths = [threading.Thread(target=sample, args=(i, a.secs, rows[r])) for r, i in (('host', lane.HOST), ('guest', lane.GUEST))]
    for t in ths:
        t.start()
    for t in ths:
        t.join()
    hs, gs = rows['host'], rows['guest']
    diffs, per = [], {}
    gi = 0
    for wh, hm in hs:
        while gi + 1 < len(gs) and abs(gs[gi + 1][0] - wh) < abs(gs[gi][0] - wh):
            gi += 1
        if not gs or abs(gs[gi][0] - wh) > 0.1:
            continue
        wg, gm = gs[gi]
        for k, (an, th, lp) in hm.items():
            if k not in gm or lp or th < 1.2:
                continue
            ga, tg, glp = gm[k]
            if ga != an or glp or tg < 0:
                continue
            d = (tg - (wg - wh)) - th
            if abs(d) > 5:
                continue
            diffs.append(abs(d))
            per.setdefault(an, []).append(round(d, 2))
    diffs.sort()
    med = diffs[len(diffs) // 2] if diffs else 9.9
    q = sum(1 for d in diffs if d < 0.15) / len(diffs) if diffs else 0.0
    print(f'  {len(diffs)} samples; median {med:.3f} s, under 0.15 s {100 * q:.0f} %', flush=True)
    for an, v in sorted(per.items(), key=lambda kv: -len(kv[1]))[:8]:
        v2 = sorted(abs(x) for x in v)
        print(f'    {an}: {len(v)} samples, median {v2[len(v2) // 2]:.2f} s, e.g. {v[:8]}', flush=True)
    print('  guest: ' + next((ln.strip() for ln in guest.call('coop.tribe anims')['out'].splitlines() if ln.startswith('phase fix')), ''),
          flush=True)
    ck(len(diffs) >= 40, f'{len(diffs)} samples of a member in the same animation on both games (>= 40)')
    ck(med < 0.08, f'the guest\'s copy is at the host\'s moment of it: median difference {med:.3f} s < 0.08 s')
    ck(q >= 0.8, f'{100 * q:.0f} % of the samples within 0.15 s (>= 80 %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
