#!/usr/bin/env python3
"""BUG-1064: Cell: a level change in the host's game removes more cells round a partner than round the host.

A level change runs RemoveCellsOfOldScale in each game (docs/re/cell.md#cell-levelup-view): a game alone loses much of
its screen on an up change anyway (the cells of the old tier), less on a down change (the cull 0xE5F8C0 of the cells
the rescale pushes out of the active range). In co-op the guest's cells are the host's, so what counts is the host's
game: the area round a partner must lose about what the host's own area loses.

Two games in the Cell fixture. Before each change the guest goes 70 m (world scale 0.5, before an up change) or 30 m
(1.5, before a down change) left of and below the host, so the two areas are apart at the mark and after the change,
and the host's camera goes onto its cell. Then the host changes the shared food by +150 / -150 (one step of the world
scale, 0.5 <-> 1.5): right before it the host's game marks its cells in its own visible box and in the same box round
the guest's cell (`coop.pool areas mark`); 5 s later it tells how many of each are gone and which DestroyCell caller
removed them (`coop.pool areas`, also per ring of the box and the cull diagnostics). Checked:
* BUG-1064: the partner's view centre stayed within 5 m of where the host's game draws its cell while the world scale
  moved (`centre_err`; 17-37 m without cell.view_centre_scale).
* BUG-1066: the host's tile pool held no live tile outside its tile lists after the changes (`coop.pool tilestat`).
* BUG-1064: pooled over the up and over the down changes (only marks with the areas apart and the host's view on its
  cell), the level change (the callers in LEVEL_CHANGE; meals and the like only printed) removed at most MARGIN more
  of the partner's area than of the host's own.

  coop_cell_levelup_area.py [--rounds 6] [--print-only]   (SPORE_FIX_OFF=cell.view_centre_scale or cell.scale_food_flush: fails)
"""
import argparse
import re
import time

from twoinst import Checks, start


def areas(con):
    out = con.call('coop.pool areas')['out']
    r = {}
    for m in re.finditer(r'^area (host|p)(\d+) marked=(\d+) gone=(\d+) nodestroy=(\d+) by(.*)$', out, re.M):
        who = 'host' if m.group(1) == 'host' else 'partner'
        r[who] = (int(m.group(3)), int(m.group(4)), m.group(5), m.group(6).strip())
    return r, out.strip()


# the DestroyCell callers of a level change (docs/re/cell.md#cell-levelup-partner-area): the old tier shrunk away
# (0xE7BFB0), the cull of a cell whose tile is not active (0xE7C080 after 0xE5F8C0), RemoveCellsOfOldScale. The others
# are play (a meal, a swallow, the room a hatching cell clears) and are only printed
LEVEL_CHANGE = {'00E7C03C', '00E7C3E6', '00E7CDA1'}

# how much more of the partner's area than of the host's own a level change may remove, pooled over a run (BUG-1064).
# With the areas apart and the host's view on its cell (20 runs, labs 20261113-170542 .. -171723) partner minus host:
# down -38 .. +9 points, mostly well below 0 (the cull 0xE5F8C0 reads the active range in whole tiles, so how much of
# a box survives a rescale depends on where its centre sits in the tile grid: one area loses 2-57 % of its cells in
# one change, the host's as much as the partner's); up -14 .. +17, mean +2.4, standard deviation 9.2 (the old tier
# fading 0xE7C03C is decided by a cell's level; the host's area holds only 34-145 cells of them). The margins are
# about the mean plus 2.5 standard deviations. Before the areas were kept apart the partner's area was the band round
# the host's box (the guest 1-8 m from the host) and lost 26-66 points more (labs 20261113-123921, -123921-2,
# -071833): the measurement this version no longer makes.
MARGIN = {'up': 0.25, 'down': 0.15}


def level_gone(by):
    return sum(int(n) for ra, n in (x.split(':') for x in by.split()) if ra in LEVEL_CHANGE)


def tilestat(con):
    return {k: int(v) for k, v in re.findall(r'\b(\w+)=(\d+)', con.call('coop.pool tilestat')['out'])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    ap.add_argument('--print-only', action='store_true', help='print the area shares, do not assert them')
    ap.add_argument('--strict', action='store_true', help='(the default now; kept for old command lines)')
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    time.sleep(5)
    t0 = tilestat(host)
    err = 0.0
    pooled = {'up': {'host': [0, 0], 'partner': [0, 0]}, 'down': {'host': [0, 0], 'partner': [0, 0]}}
    skipped = 0
    for i in range(a.rounds):
        way = 'up' if i % 2 == 0 else 'down'
        # the two areas apart at the mark and after the change: one `tp` at the start did not hold (the guest was
        # 1-7 m from the host at most marks of labs 20261113-1643xx, the partner's area only the band round the host's
        # box, the cells the rescale pushes out first). Before an up change (world scale 0.5, distances to a third
        # after it) 70 m, before a down change (1.5) 30 m left of the host: more than two box widths (20 m) both times
        dx, dy = (-70, -45) if way == 'up' else (-30, -20)
        print(f'  change {i + 1} ({way}) tp {dx} {dy}: ' + guest.call(f'coop.pool tp {dx} {dy}')['out'].strip(), flush=True)
        time.sleep(4)
        # the host's screen round its cell: a still cell's camera stays where the last rescale left it (CameraUpdate
        # moves it at 1.5 x the avatar's speed only), 7-40 m off in 2 of 8 runs (labs 20261113-170544, -170548); still
        # 6-7 m off a second later at some marks (lab 20261113-171213), so a mark with it more than 10 m off is not pooled
        host.call('coop.pool camsnap')
        time.sleep(1)
        mark = host.call('coop.pool areas mark')['out'].strip()
        print(f'  change {i + 1} ({way}) mark: ' + mark, flush=True)
        ctr, cam = {}, 0.0
        for line in areas(host)[1].splitlines():
            m = re.match(r'centre (\d) \(([-\d.]+) ([-\d.]+)\) cell \(([-\d.]+) ([-\d.]+)\)', line)
            if m:
                v = [float(m.group(k)) for k in range(2, 6)]
                ctr[int(m.group(1))] = (v[0], v[1]) if m.group(1) == '0' else (v[2], v[3])
                if m.group(1) == '0':
                    cam = ((v[0] - v[2]) ** 2 + (v[1] - v[3]) ** 2) ** 0.5
            if line.startswith('centre'):
                print('    before: ' + line, flush=True)
        bm = re.search(r'box=([\d.]+)x([\d.]+)', mark)
        bx, by_ = (float(bm.group(1)), float(bm.group(2))) if bm else (10.0, 7.5)
        apart = 0 in ctr and 1 in ctr and (abs(ctr[1][0] - ctr[0][0]) > 2 * bx or abs(ctr[1][1] - ctr[0][1]) > 2 * by_)
        why = '' if apart else ' (the areas overlap at the mark: not pooled)'
        if apart and cam > 10.0:  # the host's own cell outside its visible box (+-10 m)
            apart, why = False, f' (the host\'s view {cam:.0f} m off its cell at the mark: not pooled)'
        host.call(f'coop.pool food {150 if way == "up" else -150}')
        time.sleep(5)
        r, out = areas(host)
        parts = []
        for who in ('host', 'partner'):
            if who not in r:
                parts.append(f'{who}: none marked')
                continue
            marked, gone, nodestroy, by = r[who]
            if apart:
                pooled[way][who][0] += level_gone(by)
                pooled[way][who][1] += marked
            parts.append(f'{who}: {gone} of {marked} gone ({gone / max(1, marked):.0%}; {nodestroy} without DestroyCell; by {by})')
        print(f'  change {i + 1} ({way}) in the host\'s game: ' + '; '.join(parts) + why, flush=True)
        skipped += not apart
        for line in out.splitlines():
            if line.startswith(('centre', 'cull', 'passes', 'rings')):
                print('    ' + line, flush=True)
            m = re.match(r'centre_err settled ([\d.]+) moving ([\d.]+)', line)
            if m:
                err = max(err, float(m.group(2)))
        print('    ' + host.call('coop.pool tilestat')['out'].strip(), flush=True)
    # the cause (BUG-1064): the centre the host keeps the world round, against where the game draws the partner's cell
    ck(err <= 5.0, f'the partner\'s view centre was at most {err:.1f} m from its cell in the host\'s game while the world '
                   f'scale moved (5 m; the pose in the partner\'s own scale: 17-37 m, lab 20261113-044709)')
    t1 = tilestat(host)
    # BUG-1066: every live tile object of the host's tile pool G+0x38 is in one list (the host's or a partner's): the
    # game's own pool count against the lists, checked by the host after each step (`check_warns`)
    ck(t1.get('check_warns', 0) - t0.get('check_warns', 0) == 0,
       f'the host\'s tile pool held no tile outside its lists after the level changes ({t1.get("check_warns", 0) - t0.get("check_warns", 0)} '
       f'mismatches, {t1.get("unshared", 0) - t0.get("unshared", 0)} of our tiles taken out of the host\'s list)')
    print(f'  {skipped} of {a.rounds} changes not pooled (the areas overlapped, or the host\'s view was off its cell)', flush=True)
    for way in ('up', 'down'):
        h, p = pooled[way]['host'], pooled[way]['partner']
        ck(h[1] >= 10 and p[1] >= 10, f'{way}: cells marked in both areas (host {h[1]}, partner {p[1]})')
        hs, ps = h[0] / max(1, h[1]), p[0] / max(1, p[1])
        msg = (f'{way} changes: the host\'s game\'s level change removed {ps:.0%} of the area round the partner '
               f'({p[0]} of {p[1]}), {hs:.0%} of its own ({h[0]} of {h[1]}): at most {100 * MARGIN[way]:.0f} points more')
        if a.print_only:
            print('  (printed) ' + msg, flush=True)
        else:
            ck(ps <= hs + MARGIN[way], msg)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
