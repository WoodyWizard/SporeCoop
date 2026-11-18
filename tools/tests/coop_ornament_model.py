#!/usr/bin/env python3
"""BUG-1251: a fossil (an interactive ornament with a list of models: dug up, it shows its next model) one player digs
must show the same dug model in the other player's game.

The players' video: a fossil one player dug kept its model and glow in the other's game. The game's own dig (the
interact node 0xD869B0) calls cInteractiveOrnament_NextModel 0xC40360 (+0x248: the next model of the 12-byte list
+0x24C..+0x250, set on the model, +0xA4 = 0), clears +0xA3 and posts the reward message 0xD3353638; worldobj.cpp's copy
of a use (orn_use) only marked the reward used and cleared +0xA3.

No ornament of the fixtures has a model list (Creature fixture: all 12 have none, lab 20261118-122432; its bones keep
their glow after the game's own dig too, docs/re/creature.md#creature-ornament-models), so the test gives two of the
fixture's bones (sort 2, an unused part reward, the same objects on both games) a list of two bone models on both games
(`coop.wobj ornlist`: the shown one, then the other bone model) and ends its games with a kill (the list is the mod's).
1. both avatars next to bone A; the host digs it the game's way (`coop.wobj use`: NextModel, +0xA3, the reward
   message): the host's bone shows model 2 of its list; within 8 s the guest's copy shows model 2 too (+0x248, the
   spatial's model key +0xC4), its reward is used up and it is not usable (+0xA3 0);
2. the other way round with bone B: the guest digs, the host's copy follows.
Key frames at each bone before and after (both games: the same bone model). SPORE_FIX_OFF=wobj.ornament_model must FAIL.

  SPORE_LANE=N python3 tools/tests/coop_ornament_model.py
"""
import math
import re
import time

import lane
from twoinst import Checks, keyframe, start

ROW = re.compile(r'orn (\S+) id=(\d+) pos=\((\S+) (\S+) (\S+)\) dist=\S+ sort=(-?\d+) model=(-?\d+)/(-?\d+) a3=(\d+) '
                 r'a4=(\d+) reward=(\S+) used=(-?\d+) key=([0-9A-F]{8})')
BONES = ('61C0F798', '7AC5C47A')  # the fixture's two bone models (sort 2)


def orns(con, r=5000):
    out = []
    for m in ROW.finditer(con.call(f'coop.wobj orns {r}')['out']):
        out.append({'p': m.group(1), 'id': int(m.group(2)), 'pos': tuple(map(float, m.group(3, 4, 5))),
                    'sort': int(m.group(6)), 'idx': int(m.group(7)), 'n': int(m.group(8)), 'a3': int(m.group(9)),
                    'a4': int(m.group(10)), 'reward': m.group(11), 'used': int(m.group(12)), 'key': m.group(13)})
    return out


def at(con, pos):
    near = [o for o in orns(con, 5000) if math.dist(o['pos'], pos) < 1.5]
    return min(near, key=lambda o: math.dist(o['pos'], pos)) if near else None


def wait(fn, t=10.0):
    t0 = time.time()
    while True:
        v = fn()
        if v or time.time() - t0 > t:
            return v
        time.sleep(1)


def go_near(con, pos, side):
    """the avatar 3 m beside a point (along a tangent of the planet)"""
    r = math.sqrt(sum(v * v for v in pos))
    up = [v / r for v in pos]
    t = [0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0, up[0]]
    tl = math.sqrt(sum(v * v for v in t))
    p = [pos[i] + side * 3 * t[i] / tl + up[i] * 1.0 for i in range(3)]
    return con.call('coop.npc gopos %.2f %.2f %.2f' % tuple(p))['out'].strip()


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    ho, go = orns(host), orns(guest)
    print(f'  interactive ornaments: host {len(ho)} ({sum(o["n"] > 0 for o in ho)} with a model list), guest {len(go)}',
          flush=True)
    pairs = []
    for h in ho:
        g = [x for x in go if math.dist(x['pos'], h['pos']) < 1.5]
        if g and h['sort'] == 2 and h['used'] == 0 and g[0]['used'] == 0 and h['a3'] and g[0]['a3'] \
                and h['key'] in BONES and g[0]['key'] == h['key']:
            pairs.append((h, g[0]))
    print(f'  bones with an unused part reward on both games: {[(h["id"], h["key"], h["pos"]) for h, _ in pairs]}',
          flush=True)
    if not ck(len(pairs) >= 2, f'two bones with an unused part reward on both games ({len(pairs)})'):
        return ck.result()
    for step, (actor, other, an, on, (h, g)) in enumerate((
            (host, guest, 'host', 'guest', pairs[0]), (guest, host, 'guest', 'host', pairs[1])), 1):
        mine, theirs = (h, g) if actor is host else (g, h)
        dug = BONES[1] if mine['key'] == BONES[0] else BONES[0]
        for con, o in ((actor, mine), (other, theirs)):
            print(f'  {step}. ' + con.call(f'coop.wobj ornlist {o["p"]} {o["key"]} {dug}')['out'].strip(), flush=True)
        print(f'  {step}. {an}: ' + go_near(actor, mine['pos'], 1) + f'; {on}: ' + go_near(other, theirs['pos'], 1),
              flush=True)
        time.sleep(4)
        keyframe(f'bone{step}_before', f'bone {step} (model {mine["key"]}) before the {an} digs it, the same on both games',
                 settle=3)
        print(f'  {step}. {an}: ' + actor.call(f'coop.wobj use {mine["p"]}')['out'].strip(), flush=True)
        a = at(actor, mine['pos'])
        print(f'  {step}. the {an}\'s bone after its dig: model {a and a["idx"]} key {a and a["key"]} a3 {a and a["a3"]} '
              f'used {a and a["used"]}', flush=True)
        ck(a and a['idx'] == 1 and a['key'] == dug, f'{step}. the {an}\'s game shows the next model of the bone it dug')
        ok = wait(lambda: (lambda b: b and b['used'] == 1 and b['idx'] == 1 and b['key'] == dug and b['a3'] == 0)(
            at(other, theirs['pos'])), 8)
        b = at(other, theirs['pos'])
        print(f'  {step}. the {on}\'s copy: model {b and b["idx"]} key {b and b["key"]} a3 {b and b["a3"]} '
              f'used {b and b["used"]}', flush=True)
        ck(b and b['used'] == 1 and b['a3'] == 0, f'{step}. the {on}\'s copy of the bone the {an} dug is used up and not usable')
        ck(ok, f'{step}. the {on}\'s copy shows the dug model like the {an}\'s (model {b and b["idx"]} {b and b["key"]}, '
               f'the {an}\'s {a and a["idx"]} {a and a["key"]})')
        time.sleep(1)
        keyframe(f'bone{step}_after', f'bone {step} after the {an} dug it: the other bone model ({dug}) on both games',
                 settle=3)
    ck.faults(host=host, guest=guest)
    lane.sh('kill')  # the model lists are the mod's buffers: the games never free them
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
