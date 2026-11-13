#!/usr/bin/env python3
"""BUG-1064: Cell: a level change in the host's game removes more cells round a partner than round the host.

A level change runs RemoveCellsOfOldScale in each game (docs/re/cell.md#cell-levelup-view): a game alone loses much of
its screen on an up change anyway (the cells of the old tier), little on a down change. In co-op the guest's cells are
the host's, so what counts is the host's game: the area round a partner must lose about what the host's own area loses.

Two games in the Cell fixture, the guest 60 m left of and 40 m below the host (the fixture's host is near the level's
right edge: `tp 200 0` ended 5 m away). Each round the host changes the shared food by +150 / -150 (one step of the
world scale, 0.5 <-> 1.5: distances to a third and back, the areas still apart). Right before it the host's game marks
its cells in its own visible box and in the same box round the guest's cell (`coop.pool areas mark`); 5 s later it
tells how many of each are gone and which DestroyCell caller removed them (`coop.pool areas`). Checked:
* BUG-1064 (the cause found): the partner's view centre stayed within 5 m of where the host's game draws its cell
  while the world scale moved (`centre_err`; 17-37 m without the fix).
* BUG-1066: the host's tile pool held no live tile outside its tile lists after the changes (`coop.pool tilestat`
  check_warns: the host's count of the game's pool G+0x38 against the lists).
* Printed (asserted with --strict, BUG-1064 still open): pooled over the up and over the down changes, the level change
  (the callers in LEVEL_CHANGE; meals and the like only printed) removed at most 10 points more of the partner's area
  than of the host's own.

  coop_cell_levelup_area.py [--rounds 6] [--strict]   (SPORE_FIX_OFF=cell.view_centre_scale or cell.scale_food_flush: fails)
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


def level_gone(by):
    return sum(int(n) for ra, n in (x.split(':') for x in by.split()) if ra in LEVEL_CHANGE)


def tilestat(con):
    return {k: int(v) for k, v in re.findall(r'\b(\w+)=(\d+)', con.call('coop.pool tilestat')['out'])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    ap.add_argument('--strict', action='store_true', help='assert the area shares (BUG-1064 open: fails)')
    ap.add_argument('--dx', type=int, default=-60)
    ap.add_argument('--dy', type=int, default=-40)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    # far enough that the two areas stay apart after an up change too (the world scale goes 0.5 -> 1.5: distances to a
    # third), towards the level's middle (the fixture's host is near its right edge)
    guest.call(f'coop.pool tp {a.dx} {a.dy}')
    time.sleep(10)
    t0 = tilestat(host)
    err = 0.0
    pooled = {'up': {'host': [0, 0], 'partner': [0, 0]}, 'down': {'host': [0, 0], 'partner': [0, 0]}}
    for i in range(a.rounds):
        time.sleep(3)
        way = 'up' if i % 2 == 0 else 'down'
        print(f'  change {i + 1} ({way}) mark: ' + host.call('coop.pool areas mark')['out'].strip(), flush=True)
        for line in areas(host)[1].splitlines():
            if line.startswith('centre'):
                print('    before: ' + line, flush=True)
        host.call(f'coop.pool food {150 if way == "up" else -150}')
        time.sleep(5)
        r, out = areas(host)
        parts = []
        for who in ('host', 'partner'):
            if who not in r:
                parts.append(f'{who}: none marked')
                continue
            marked, gone, nodestroy, by = r[who]
            pooled[way][who][0] += level_gone(by)
            pooled[way][who][1] += marked
            parts.append(f'{who}: {gone} of {marked} gone ({gone / max(1, marked):.0%}; {nodestroy} without DestroyCell; by {by})')
        print(f'  change {i + 1} ({way}) in the host\'s game: ' + '; '.join(parts), flush=True)
        for line in out.splitlines():
            if line.startswith('centre'):
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
    for way in ('up', 'down'):
        h, p = pooled[way]['host'], pooled[way]['partner']
        ck(h[1] >= 10 and p[1] >= 10, f'{way}: cells marked in both areas (host {h[1]}, partner {p[1]})')
        hs, ps = h[0] / max(1, h[1]), p[0] / max(1, p[1])
        msg = (f'{way} changes: the host\'s game\'s level change removed {ps:.0%} of the area round the partner '
               f'({p[0]} of {p[1]}), {hs:.0%} of its own ({h[0]} of {h[1]}): at most 10 points more')
        # BUG-1064 is open: with the view centre right the down changes still remove 50-65 points more round the partner
        # in some runs (labs 20261113-123921, -123921-2); asserted with --strict only, printed otherwise
        if a.strict:
            ck(ps <= hs + 0.10, msg)
        else:
            print(('  (printed, BUG-1064 open) ' if ps > hs + 0.10 else '  (printed) ') + msg, flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
