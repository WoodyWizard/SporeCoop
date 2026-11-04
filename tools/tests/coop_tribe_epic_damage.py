#!/usr/bin/env python3
"""Tribe: the co-op epic hits as hard as the game's own epic (BUG-0515, players: "giants only dmg 8").

1. Tribe fixture (its planet has an epic of the game's own). The host spawns the co-op epic (`coop.tribe epic`).
2. `coop.tribe epic stats` on the host: every live epic with its herd's tuning (archetype, generation, scale /
   HP / damage multipliers) and the blows it dealt to tribe members (the game's citizen TakeDamage).
3. The co-op epic is put next to a member of the player tribe; its blows are read for up to 60 s.
Checks: the co-op epic's herd has the game's epic's damage multiplier (the fixture's epic: x15), and its blows on
the members are hard (> --min-blow: without the fix the co-op epic hit for 1.5-8, an animal's blows).
--nofix (`coop.fix tribe.epic_tuning off`) must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_tribe_epic_damage.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def epics(con):
    out = con.call('coop.tribe epic stats')['out']
    rows = []
    for line in out.splitlines():
        m = re.match(r'epic (\S+) ours=(\d) hp=([-\d.]+)/([-\d.]+) scale=([-\d.]+) herd=\S+ arch=(\w+) gen=(-?\d+) '
                     r'scale_mult=([-\d.]+) hp_override=([-\d.]+) dmg_mult=([-\d.]+) blows=(\d+) last=([-\d.]+) max=([-\d.]+)', line)
        if m:
            rows.append(dict(ptr=m.group(1), ours=m.group(2) == '1', hp=float(m.group(3)), maxhp=float(m.group(4)),
                             scale=float(m.group(5)), arch=m.group(6), gen=int(m.group(7)), scale_mult=float(m.group(8)),
                             hp_override=float(m.group(9)), dmg_mult=float(m.group(10)), blows=int(m.group(11)),
                             last=float(m.group(12)), max=float(m.group(13))))
    return rows, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--min-blow', type=float, default=30.0)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.fix tribe.epic_tuning off')['out'].strip())
    time.sleep(4)
    print('  host:', host.call('coop.tribe epic')['out'].strip())
    time.sleep(2)
    rows, out = epics(host)
    print('  ' + out.replace('\n', '\n  '))
    ours = [r for r in rows if r['ours']]
    game = [r for r in rows if not r['ours']]
    ck(bool(ours), 'the co-op epic is alive')
    if not ours:
        return ck.result()
    own = None
    for line in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(\d+) age=1 sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0' and float(m.group(3)) > 0:
            own = m.group(1)
            break
    t0 = time.time()
    blows = None
    while time.time() - t0 < 60:
        print('  host:', host.call(f'coop.tribe epic near {own}')['out'].strip())
        time.sleep(5)
        rows, out = epics(host)
        o = [r for r in rows if r['ours']]
        if o and o[0]['blows'] >= 2:
            blows = o[0]
            break
    print('  ' + epics(host)[1].replace('\n', '\n  '))
    if game:
        g = game[0]
        ck(abs(ours[0]['dmg_mult'] - g['dmg_mult']) < 0.01,
           f'the co-op epic\'s herd hits as the game\'s epic (damage x{ours[0]["dmg_mult"]:.2f} vs x{g["dmg_mult"]:.2f}; '
           f'size {ours[0]["scale"]:.2f} vs {g["scale"]:.2f})')
    ck(blows is not None and blows['max'] > a.min_blow,
       f'the co-op epic hits members hard (> {a.min_blow:g}): {blows["blows"] if blows else 0} blows, max {blows["max"] if blows else 0:.1f}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
