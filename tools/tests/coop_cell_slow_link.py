#!/usr/bin/env python3
"""BUG-1309: Cell over a slow link: the guest's game has the host's cells around it, within seconds and for good.

Run with SPORE_NET=lowband (the suite entry does: the guest's link capped at 512 kbit/s with a 400 ms queue that drops
what does not fit, tools/tests/netem.py). Before the fix the host sent its whole Cell state (~400 cells at level 1,
~260 KB/s) ten times a second in pool order; the link kept the same first ~7 chunks of every round and the guest
removed the other mirrors a second after they came: ~100 of the ~400 cells, spread over the pool (sync-check
"c: 247 missing on the guest" in every evo_screens_switch_slow run since 2026-11-18).

Cell fixture (level 1, both players next to each other). For --secs s, every 2 s: the guest's own cell's position,
the host's live cells within --r cell units of it (`coop.pool near <r> x y` on the host: its pool) and the guest's own
game's cells within --r + 2 of it that mirror a host cell (`coop.pool near` on the guest: its pool, by the host's
index). Coverage = the share of the host's cells near the guest that the guest's game has, of the host's cells that
were near it in the samples before and after too (2 s each way: a cell the host's population pass made a moment ago
may still be on its way, the link carries a round every ~0.4 s; one the host's cull takes may leave the guest a
moment before the host's list is read). One view box is 20 units wide at every level
(docs/re/cell.md#cell-kin-test-units): --r 20 is a box around the guest's screen (its screen showed 2-7 cells),
~150 of the host's cells at level 1. (A round at 512 kbit/s carries the nearest ~320 cells: --r 40 held 270-490.)

  1. from --after s after start() returned (the guest arrived and settled) on, the coverage of every sample is at
     least --min (one sample below is allowed: a cell that swims over the border between the two reads), and the
     median at least --median;
  2. the host sent the state paced (`coop.pool pace`: rate limited, cuts >= 1) - printed, not checked: a link that
     carries everything needs no pace;
  3. key frame `slow_link_cells`: the guest's screen shows the host's cells around it (not empty water).

  SPORE_NET=lowband coop_cell_slow_link.py [--secs 40] [--r 20] [--after 8] [--min 0.8] [--median 0.9]
FAIL twin: SPORE_FIX_OFF=cell.slow_link (coverage ~0.25: the guest keeps the cells of the first chunks only).
"""
import argparse
import os
import re
import statistics
import time

from harness import cell_pos
from twoinst import Checks, keyframe, start


def near(con, r, at=None):
    cmd = f'coop.pool near {r}' + (f' {at[0]:.2f} {at[1]:.2f}' if at else '')
    out = con.call(cmd)['out']
    m = re.search(r'^keys ?(.*)$', out, re.M)
    if not m:
        return None
    return set(int(k) for k in m.group(1).split())


def on_screen(con):
    return sum(1 for ln in con.call('coop.pool see 400')['out'].splitlines() if ln.startswith('see '))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=40)
    ap.add_argument('--r', type=float, default=20)
    ap.add_argument('--after', type=float, default=8)
    ap.add_argument('--min', type=float, default=0.8)
    ap.add_argument('--median', type=float, default=0.9)
    a = ap.parse_args()
    print(f"  SPORE_NET={os.environ.get('SPORE_NET', '') or '(none: a clean link, nothing to pace)'}", flush=True)
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    t0 = time.time()
    rows = []
    shot_done = False
    samples = []  # (t, host set, guest set)
    while time.time() - t0 < a.after + a.secs:
        t = time.time() - t0
        gp = cell_pos(guest)
        if gp is None:
            print(f'  {t:5.1f} s: the guest has no cell', flush=True)
            time.sleep(2)
            continue
        hs = near(host, a.r, gp)
        gs = near(guest, a.r + 2)
        if hs is None or gs is None:
            print(f'  {t:5.1f} s: no answer (host {hs is not None}, guest {gs is not None})', flush=True)
            time.sleep(2)
            continue
        samples.append((t, hs, gs))
        print(f'  {t:5.1f} s: guest at ({gp[0]:.1f} {gp[1]:.1f}): host {len(hs)} cells within {a.r:g}, the guest\'s game '
              f'{len(hs & gs)} of them, {len(gs)} mirrors near it, {on_screen(guest)} on its screen', flush=True)
        if not shot_done and t >= a.after + a.secs / 2:
            shot_done = True
            keyframe('slow_link_cells', "Cell over a 512 kbit/s link: the guest's screen shows the host's cells all "
                     "around its own cell as the host's screen shows them around the guest's cell (not empty water, "
                     "no cells popping in late)", settle=5)
        time.sleep(2)
    pace = host.call('coop.pool pace')['out'].strip()
    print('  host:', pace.replace('\n', ' | '), flush=True)
    # a sample counts the host's cells near the guest in the samples before and after it too (2 s each way): a cell
    # the host's population pass made a moment ago may still be on its way, one its cull took may be gone on the
    # guest a moment before the host's list was read (20261119-133159: 111 of 137, the host 21 fewer 2 s later)
    for i in range(1, len(samples) - 1):
        t, hs, gs = samples[i]
        ref = hs & samples[i - 1][1] & samples[i + 1][1]
        cov = len(ref & gs) / len(ref) if ref else 1.0
        rows.append((t, len(ref), len(gs), cov))
        print(f'  {t:5.1f} s: {len(ref)} host cells near the guest from 2 s before to 2 s after, the guest\'s game has '
              f'{len(ref & gs)} of them ({cov:.2f})', flush=True)
    steady = [r for r in rows if r[0] >= a.after]
    counted = [r for r in steady if r[1] >= 5]
    ck(len(counted) >= max(5, len(steady) // 2), f'enough samples with the host\'s cells near the guest '
       f'({len(counted)} of {len(steady)} with at least 5)')
    if counted:
        covs = [r[3] for r in counted]
        low = [f'{r[0]:.0f}s:{r[3]:.2f}' for r in counted if r[3] < a.min]
        med = statistics.median(covs)
        ck(len(low) <= 1, f"from {a.after:g} s after the start on, the guest's game has at least {a.min:.0%} of the host's "
           f"cells within {a.r:g} units of it in every sample but one (below: {low or 'none'}; min {min(covs):.2f})")
        ck(med >= a.median, f"the median share of the host's cells near the guest that the guest's game has is at least "
           f"{a.median:.0%} ({med:.2f} over {len(covs)} samples)")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
