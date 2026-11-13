#!/usr/bin/env python3
"""BUG-1064: Cell: a level change in the host's game removes more cells round a partner than round the host.

A level change runs RemoveCellsOfOldScale in each game (docs/re/cell.md#cell-levelup-view): a game alone loses much of
its screen on an up change anyway (the cells of the old tier), little on a down change. In co-op the guest's cells are
the host's, so what counts is the host's game: the area round a partner must lose about what the host's own area loses.

Two games in the Cell fixture, the guest 200 m from the host (20 m after an up change: the areas stay apart). Each round the host changes the shared food by +150 /
-150 (three levels: one change of the world scale). Right before it the host's game marks its cells in its own visible
box and in the same box round the guest's cell (`coop.pool areas mark`); 5 s later it tells how many of each are gone
and which DestroyCell caller removed them (`coop.pool areas`). Checked, pooled over the up changes and over the down
changes: the partner's area lost at most 10 points more than the host's own (BUG-1064); and the host's tile pool held
no live tile outside its tile lists after the changes (BUG-1066: `coop.pool tilestat` check_warns, the host's own count
of the game's pool G+0x38 against the lists).

  coop_cell_levelup_area.py [--rounds 6]   (SPORE_FIX_OFF=cell.view_centre_scale or cell.scale_food_flush: fails)
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


def tilestat(con):
    return {k: int(v) for k, v in re.findall(r'\b(\w+)=(\d+)', con.call('coop.pool tilestat')['out'])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    ap.add_argument('--dist', type=int, default=200)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    # far enough that the two areas stay apart after an up change too (the world shrinks about tenfold: 30 m became 3 m
    # and the partner's area was the host's)
    ap_dist = a.dist
    guest.call(f'coop.pool tp {ap_dist} 0')
    time.sleep(10)
    t0 = tilestat(host)
    pooled = {'up': {'host': [0, 0], 'partner': [0, 0]}, 'down': {'host': [0, 0], 'partner': [0, 0]}}
    for i in range(a.rounds):
        time.sleep(3)
        way = 'up' if i % 2 == 0 else 'down'
        print(f'  change {i + 1} ({way}) mark: ' + host.call('coop.pool areas mark')['out'].strip(), flush=True)
        host.call(f'coop.pool food {150 if way == "up" else -150}')
        time.sleep(5)
        r, out = areas(host)
        parts = []
        for who in ('host', 'partner'):
            if who not in r:
                parts.append(f'{who}: none marked')
                continue
            marked, gone, nodestroy, by = r[who]
            pooled[way][who][0] += gone
            pooled[way][who][1] += marked
            parts.append(f'{who}: {gone} of {marked} gone ({gone / max(1, marked):.0%}; {nodestroy} without DestroyCell; by {by})')
        print(f'  change {i + 1} ({way}) in the host\'s game: ' + '; '.join(parts), flush=True)
        print('    ' + host.call('coop.pool tilestat')['out'].strip(), flush=True)
    t1 = tilestat(host)
    # BUG-1066: every live tile object of the host's tile pool G+0x38 is in one list (the host's or a partner's): the
    # game's own pool count against the lists, checked by the host after each step (`check_warns`)
    ck(t1.get('check_warns', 0) - t0.get('check_warns', 0) == 0,
       f'the host\'s tile pool held no tile outside its lists after the level changes ({t1.get("check_warns", 0) - t0.get("check_warns", 0)} '
       f'mismatches, {t1.get("unshared", 0) - t0.get("unshared", 0)} of our tiles taken out of the host\'s list)')
    for way in ('up', 'down'):
        h, p = pooled[way]['host'], pooled[way]['partner']
        ck(h[1] >= 10 and p[1] >= 10, f'{way}: cells marked in both areas (host {h[1]}, partner {p[1]})')
        hs, ps = h[0] / max(1, h[1]), p[0] / max(1, p[1])
        ck(ps <= hs + 0.10, f'{way} changes: the host\'s game lost {ps:.0%} of the area round the partner ({p[0]} of {p[1]}), '
                            f'{hs:.0%} of its own ({h[0]} of {h[1]}): at most 10 points more')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
