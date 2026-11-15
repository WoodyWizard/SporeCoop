#!/usr/bin/env python3
"""Creature: the eggs in the players' own nest after a respawn are the same on both games (BUG-0660).

The detector's `Creature:noun_only_host:egg` / `host_only:egg` rows in play_creature (2-9 a run) were eggs at the
players' nest, in one game only, for minutes after that game's player had died and hatched again.

1. Two games on the creature fixture; the eggs within 80 m of each avatar (`coop.nest objs`).
2. The players' family herd (`coop.nest herds`, avatar=1) misses members on the host (`coop.nest target <h> <n+2>`,
   `respawn <h> 2000`): a family member lays eggs in the host's game, as after a family member's death in play
   (--die instead: the host's avatar dies and respawns at the nest, `coop.team die`).
3. For 60 s: the eggs at the nest on both games.
PASS: every egg the host's game has at the nest the guest's has within 2 m too, and the other way (one to one).

  SPORE_LANE=4 python3 tools/tests/coop_creature_family_eggs.py [--guest: the guest dies instead]
"""
import math
import re
import sys
import time

from coop_creature_respawn import av, respawn
from twoinst import Checks, start

GUEST = '--guest' in sys.argv
DIE = '--die' in sys.argv or GUEST


def eggs(con, r=80):
    out = []
    for ln in con.call(f'coop.nest objs {r}')['out'].splitlines():
        if not ln.startswith('egg '):
            continue
        p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln)
        st = re.search(r'state=(-?\d+)', ln)
        fam = re.search(r'family=(\d+)', ln)
        mir = re.search(r'mirror_of=(\d+)', ln)
        out.append({'pos': tuple(float(x) for x in p.groups()), 'state': int(st.group(1)) if st else None,
                    'family': int(fam.group(1)) if fam else None, 'mirror': int(mir.group(1)) if mir else 0,
                    'line': ln[:200]})
    return out


def unpaired(a, b, tol=2.0):
    """the eggs of a and of b without a partner within tol m (one to one, nearest pairs first)"""
    pairs = sorted((math.dist(x['pos'], y['pos']), i, j) for i, x in enumerate(a) for j, y in enumerate(b))
    ua, ub = set(range(len(a))), set(range(len(b)))
    for d, i, j in pairs:
        if d <= tol and i in ua and j in ub:
            ua.discard(i)
            ub.discard(j)
    return [a[i] for i in sorted(ua)], [b[j] for j in sorted(ub)]


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    nest = av(host)['pos']
    for con, name in ((host, 'host'), (guest, 'guest')):
        print(f'  {name} eggs before:', [(e['state'], e['family'], e['mirror']) for e in eggs(con)], flush=True)
    if DIE:
        dier, name = (guest, 'guest') if GUEST else (host, 'host')
        full = av(dier)['max']
        ck('lethal' in dier.call('coop.team die')['out'], f'{name}: a lethal hit (coop.team die)')
        respawn(ck, dier, name, full)
        nest = av(dier)['pos'] or nest
    else:
        name = 'family herd\'s lay'
        fam = [ln.split()[1] for ln in host.call('coop.nest herds')['out'].splitlines()
               if ln.startswith('herd ') and ' avatar=1 ' in ln]
        ck(fam, f'the host has the players\' family herd ({fam})')
        if not fam:
            return ck.result()
        h = fam[0]
        info = host.call(f'coop.nest herd {h}')['out']
        m = re.search(r'members=(\d+)', info)
        n = int(m.group(1)) if m else 4
        print('  host:', info.strip().splitlines()[0][:200], flush=True)
        print('  host:', host.call(f'coop.nest target {h} {n + 2}')['out'].strip(),
              host.call(f'coop.nest respawn {h} 2000')['out'].strip(), flush=True)
        e0 = len([e for e in eggs(host) if math.dist(e['pos'], nest) < 40])
        t1 = time.time()
        while time.time() - t1 < 90 and len([e for e in eggs(host) if math.dist(e['pos'], nest) < 40]) <= e0:
            time.sleep(2)
        laid = len([e for e in eggs(host) if math.dist(e['pos'], nest) < 40]) - e0
        print(f'  host: {laid} new eggs after {time.time() - t1:.0f} s', flush=True)
        ck(laid > 0, f'a family member laid eggs in the host\'s game ({laid})')
        time.sleep(8)  # (a laid egg falls and rolls for a few seconds: docs/re/creature.md#creature-nest-eggs)
    t0, bad, samples = time.time(), [], 0
    while time.time() - t0 < 60:
        he = [e for e in eggs(host, 150) if math.dist(e['pos'], nest) < 70]
        ge = [e for e in eggs(guest, 150) if math.dist(e['pos'], nest) < 70]
        ho, go = unpaired(he, ge)
        samples += 1
        print(f'    {time.time() - t0:4.1f} s: host {len(he)} eggs {[(e["state"], e["family"]) for e in he]}, guest '
              f'{len(ge)} {[(e["state"], e["family"], e["mirror"]) for e in ge]}; host only {len(ho)}, guest only {len(go)}',
              flush=True)
        if ho or go:
            bad.append(round(time.time() - t0, 1))
            if len(bad) == 1:
                for e in ho + go:
                    print('      ' + e['line'], flush=True)
        time.sleep(6)
    ck(samples > 5, f'the eggs were read ({samples} samples)')
    ck(not bad, f'the nest has the same eggs on both games after the {name}\'s respawn ({len(bad)} samples not: {bad[:6]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
