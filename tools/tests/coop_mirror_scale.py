#!/usr/bin/env python3
"""BUG-0461 / BUG-0475 / BUG-0479 (class G2, BUG-0419): a mirrored object has the size of the original.

The game picks a size per individual (creatures and tribe members: a random size per member, young
ones small) and per view (UFOs: a player ship bigger than an NPC ship, both by the camera distance). The
mirrors on the other game got their own sizes: NPC proxies (NpcSpawn.scale sent and never applied,
babies shown as adults), tribe member mirrors, and the partner's ship (a Defender NPC UFO here: drawn at
an NPC ship's size on both games). Scales are read from the game on each machine (cSpatialObject
GetScale), never from the mod's bookkeeping:

  --kind npc           Creature: every host NPC near the guest vs the guest's proxy of its id
                       (`coop.census all`: c rows own=npc / own=proxy, same id)
  --kind tribe_member  Tribe: every host member vs the guest's mirror of its id (`coop.fields list`:
                       the host's GetScale, the guest mirror's GetScale)
  --kind ufo           Space: each player's own ship on its game vs its puppet on the other game
                       (`coop.census all`: the ufo row at the player's position on both games)

PASS when, after the mirrors settled, at most 5 % of the pairs differ by more than 12 % (the
detector's tolerance) and there were enough pairs. The divergence detector's signature
(`<stage>:scale:<kind>`, tools/tests/worlddiff.py) is printed for the record.

SPORE_FIX_OFF=npc.scale (--kind npc), tribe.scale (tribe_member), space.puppet_scale (ufo) fail.

  coop_mirror_scale.py --kind npc|tribe_member|ufo [--fixture f] [--rounds 4]"""
import argparse
import math
import re
import time

import worlddiff
from twoinst import Checks, shot, start

TOL = 0.12
FIXTURE = {'npc': 'creature', 'tribe_member': 'tribe', 'ufo': 'space'}
STAGE = {'npc': 'Creature', 'tribe_member': 'Tribe', 'ufo': 'Space'}
SIG = {'npc': 'scale:creature', 'tribe_member': 'scale:citizen', 'ufo': 'scale:ufo'}
LINE = re.compile(r'obj (\S+) (\d+) scale=(-?[\d.]+)(?: host=(-?[\d.]+))?')


def rel(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1e-6)


def pairs_fields(host, guest, kind):
    h = {int(m.group(2)): float(m.group(3)) for m in LINE.finditer(host.call(f'coop.fields list {kind}')['out'])}
    g = {int(m.group(2)): float(m.group(3)) for m in LINE.finditer(guest.call(f'coop.fields list {kind}')['out'])
         if float(m.group(3)) > 0}  # -1: no mirror of that id (yet)
    return [(i, h[i], g[i]) for i in sorted(set(h) & set(g))]


def pairs_census(hw, gw, kind):
    if kind == 'npc':
        c = gw.me
        h = {x['id']: x for x in hw.rows if x['kind'] == 'c' and x['own'] == 'npc'}
        g = {x['id']: x for x in gw.rows if x['kind'] == 'c' and x['own'] == 'proxy'}
        return [(i, h[i]['scale'], g[i]['scale']) for i in sorted(set(h) & set(g))
                if c and math.dist(h[i]['pos'], c) < 70 and math.isfinite(h[i]['scale'])]
    # ufo: each game's own ship (at its own position) and the other game's ship nearest to it
    out = []
    ufo = [n for n, v in worlddiff.NOUNS.items() if v == 'ufo'][0]
    for a, b, who in ((hw, gw, 'host'), (gw, hw, 'guest')):
        if not a.me:
            continue
        own = [x for x in a.rows if x['kind'] == 'g' and x['noun'] == ufo and math.dist(x['pos'], a.me) < 0.5]
        other = sorted((math.dist(x['pos'], a.me), x) for x in b.rows if x['kind'] == 'g' and x['noun'] == ufo)
        if own and other and other[0][0] < 3.0:
            out.append((who, own[0]['scale'], other[0][1]['scale']))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kind', choices=sorted(FIXTURE), required=True)
    ap.add_argument('--fixture')
    ap.add_argument('--rounds', type=int, default=4)
    a = ap.parse_args()
    fixture = a.fixture or FIXTURE[a.kind]
    print(f'mirror scale: {a.kind} on {fixture}, {a.rounds} rounds', flush=True)
    host, guest = start(fixture)
    ck = Checks()
    time.sleep(20)  # the mirrors are made and their sizes applied (fields: every 0.25 s)
    last, total, bad_total, sigs = [], 0, 0, 0
    for k in range(a.rounds):
        hw, gw = worlddiff.pair_dump(host, guest)
        pairs = pairs_fields(host, guest, a.kind) if a.kind == 'tribe_member' else pairs_census(hw, gw, a.kind)
        bad = [p for p in pairs if rel(p[1], p[2]) > TOL]
        found = [f for f in worlddiff.diff(hw, gw) if f['sig'].endswith(SIG[a.kind])]
        sigs += len(found)
        print(f'  round {k + 1}: {len(pairs)} pairs, {len(bad)} with another size'
              + (f' (e.g. {bad[0][0]}: {bad[0][1]:.3f} on the original\'s game, {bad[0][2]:.3f} on the mirror\'s)' if bad else '')
              + (f'; detector: {found[0]["text"]}' if found else '; detector: no scale finding'), flush=True)
        total += len(pairs)
        bad_total += len(bad)
        last = pairs
        time.sleep(5)
    shot(host, f'mirror_scale_{a.kind}_host.png')
    shot(guest, f'mirror_scale_{a.kind}_guest.png')
    for who, con in (('host', host), ('guest', guest)):
        if a.kind == 'ufo':
            lines = [ln for ln in con.call('coop.status')['out'].splitlines() if ln.startswith('space puppet scale')]
        else:
            lines = [ln for ln in con.call('coop.fields')['out'].splitlines() if ln.startswith(f'fields {a.kind} ')]
        print(f'  {who} ' + (lines[0] if lines else '?'), flush=True)
    need = 2 if a.kind == 'ufo' else 3
    ck(len(last) >= need, f'enough pairs to compare in the last round: {len(last)} (at least {need})')
    share = bad_total / total if total else 1.0
    ck(total > 0 and share <= 0.05,
       f'mirrors at the original\'s size: {bad_total} of {total} pairs over all rounds differ by more than '
       f'{TOL:.0%} ({share:.0%}, at most 5 %)')
    print(f'  detector {STAGE[a.kind]}:{SIG[a.kind]}: in {sigs} of {a.rounds} comparisons', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
