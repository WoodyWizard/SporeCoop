#!/usr/bin/env python3
"""Civilization: the mod takes no food from a creature of the guest's game and hands it none (BUG-0684).

The world-object sync of food bundles (BUG-0548 / BUG-0617, mods/coop/worldobj.cpp, docs/re/tribe.md#tribe-carried-bundles)
was written for Tribe, where the guest's tribe members are copies that run no behaviour of their own: a guest's own
carried bundle without a host partner is removed after a few snapshots, and the host's carried bundle is put into the
hand of the nearest empty-handed creature within 8 m. In Civ the old tribe villages (and the cities' citizens) run
their own behaviour in each game: a guest's member was handed the host's food, the mod removed it from its container
while its gather node held it, and the node read the empty container every frame (132 caught faults at 0xC23469 on
a guest, play_civ, rel/0.7.10).

Two games on civ_npc, the guest's pointer over an old tribe village that both games have:
1. a creature of the guest's game near the village carries food of the guest's own (`coop.wobj bundle carry`, what
   its gather does): 10 s later the food is still in a creature's hand in the guest's game (not removed by the mod);
2. creatures of the host's village carry food (the host's game): the guest's game hands none of it to a creature of
   its own (no carried bundle near the host's carriers that the guest's game did not have), and when the host's
   members put it down (`coop.wobj bundle take` on the host) the guest's game takes nothing from its creatures;
3. neither game faulted.

SPORE_FIX_OFF=wobj.bundle_civ_carried: the guest's own food is removed (1) and the host's is handed to the guest's
creatures (2): it FAILS.

  SPORE_LANE=9 python3 tools/tests/coop_civ_village_bundle.py
"""
import math
import random
import re
import time

from coop_world_objects import objs
from play_civ import Player
from twoinst import Checks, start

TRIBE, CITIZEN = '18C6D19', '18EB4B7'


def nouns(con, n):
    out = []
    for m in re.finditer(r'^obj (\S+) (\S+) (\S+) (\S+) ', con.call(f'coop.civ nouns {n}')['out'], re.M):
        out.append((m.group(1).upper(), tuple(float(m.group(i)) for i in (2, 3, 4))))
    return out


def carried(con, at, r=60):
    return [b for b in objs(con, 'game_bundle', r, at) if b['cls'] & 0x100]


def carry(con, cands, n, amt=5):
    """creatures among cands that now carry a new bundle: [(creature, bundle)]"""
    got = []
    for p, _ in cands:
        if len(got) >= n:
            break
        out = con.call(f'coop.wobj bundle carry {p} {amt}')['out'].strip()
        m = re.match(r'bundle (\S+)', out)
        if m and int(m.group(1), 16):
            got.append((p, m.group(1).upper()))
    return got


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    ht, gt = nouns(host, TRIBE), nouns(guest, TRIBE)
    both = [(p, at) for p, at in ht if any(math.dist(at, g) < 10 for _, g in gt)]
    print(f'  old villages: host {len(ht)}, guest {len(gt)}, in both {len(both)}', flush=True)
    if not ck(both, 'an old tribe village is in both games'):
        return ck.result()
    # the village with the most of the host's creatures near it
    hc = nouns(host, CITIZEN)
    both.sort(key=lambda v: -sum(1 for _, c in hc if math.dist(c, v[1]) < 40))
    vil = both[0][1]
    me = Player('guest', guest, 1, random.Random(1), [])
    sp = me.look_at(vil)
    if sp:
        guest.call('move %d %d' % sp)
    time.sleep(2)
    print(f'  village at {vil}; the guest looks at it (screen {sp}): '
          f'{guest.call("coop.sync")["out"].splitlines()[:2]}', flush=True)
    st0 = guest.call('coop.wobj')['out']
    # 1. the guest's own creature carries its own food
    gc = sorted(((p, c) for p, c in nouns(guest, CITIZEN) if math.dist(c, vil) < 40), key=lambda x: math.dist(x[1], vil))
    own = carry(guest, gc, 2)
    print(f'  guest: own creatures carry food: {own}', flush=True)
    if ck(own, 'a creature of the guest\'s game carries food of its own'):
        time.sleep(10)
        left = {b['p'].upper() for b in carried(guest, vil, 80)}
        kept = [b for _, b in own if b in left]
        print(f'  10 s later the guest\'s carried bundles near the village: {sorted(left)}', flush=True)
        ck(kept, f'the guest\'s own creatures still carry their food 10 s later (kept {len(kept)} of {len(own)}: the game\'s '
           f'object list)')
    # 2. the host's members carry food
    hcl = sorted(((p, c) for p, c in hc if math.dist(c, vil) < 40), key=lambda x: math.dist(x[1], vil))
    before = {b['p'].upper() for b in carried(guest, vil, 80)}
    hb = carry(host, hcl, 3)
    print(f'  host: its creatures carry food: {hb}', flush=True)
    if ck(hb, 'creatures of the host\'s game carry food'):
        handed, polls = set(), []
        t0 = time.time()
        while time.time() - t0 < 10:
            hpos = [b['pos'] for b in carried(host, vil, 80) if b['p'].upper() in {x for _, x in hb}]
            new = [b for b in carried(guest, vil, 80) if b['p'].upper() not in before
                   and any(math.dist(b['pos'], h) < 10 for h in hpos)]
            handed |= {b['p'].upper() for b in new}
            polls.append(len(new))
            time.sleep(0.5)
        print(f'  guest: carried bundles of no own making near the host\'s carriers, per poll: {polls}', flush=True)
        ck(not handed, f'no creature of the guest\'s game was handed the host\'s food ({sorted(handed)})')
        listed = {b['p'].upper() for b in carried(host, vil, 80)}  # (a member that delivered its food: freed)
        for _, b in hb:
            if b in listed:
                host.call(f'coop.wobj bundle take {b}')
        time.sleep(6)
    st1 = guest.call('coop.wobj')['out']
    for line in (st0, st1):
        m = re.search(r'bundles made=\d+ carried=\d+ removed=\d+[^;]*', line)
        print('  guest wobj:', m.group(0) if m else '?', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
