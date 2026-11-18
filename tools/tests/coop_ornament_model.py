#!/usr/bin/env python3
"""BUG-1251: a fossil (an interactive ornament with a list of models: dug up, it shows its next model and no longer
glows) one player digs must change in the other player's game the same way.

The players' video: a fossil one player dug kept its model and glow in the other's game, and the guest saw bones
the host did not have (and could not click them). The game's own dig (the interact node 0xD869B0) calls
cInteractiveOrnament_NextModel 0xC40360 (+0x248 the next model of the list +0x24C..+0x250, set on the model, +0xA4 = 0),
clears +0xA3 and posts the reward message. worldobj.cpp's copy of a use (orn_use) only marked the reward used and
cleared +0xA3.

Creature fixture (start('creature')). `coop.wobj orns` lists every interactive ornament of the planet (sort, model
index / count, +0xA3, +0xA4, reward); pairs at the same place on both games (1.5 m) that still have a next model and
an unused reward are the fossils of the test.
1. the guest's avatar and the host's are put next to fossil A; the guest digs it the game's way (`coop.wobj use`:
   NextModel, +0xA3, the reward message); within 8 s the host's copy shows the same model index as the guest's, is no
   longer usable (+0xA3 0) and has +0xA4 like the guest's;
2. the other way: the host digs fossil B; the guest's copy follows the same way.
Key frames at each fossil (both games). SPORE_FIX_OFF=wobj.ornament_model must FAIL.

  SPORE_LANE=N python3 tools/tests/coop_ornament_model.py
"""
import math
import re
import time

from twoinst import Checks, keyframe, start

ROW = re.compile(r'orn (\S+) id=(\d+) pos=\((\S+) (\S+) (\S+)\) dist=\S+ sort=(-?\d+) model=(-?\d+)/(-?\d+) a3=(\d+) '
                 r'a4=(\d+) reward=(\S+) used=(-?\d+)')


def orns(con, r=5000):
    out = []
    for m in ROW.finditer(con.call(f'coop.wobj orns {r}')['out']):
        out.append({'p': m.group(1), 'id': int(m.group(2)), 'pos': tuple(map(float, m.group(3, 4, 5))),
                    'sort': int(m.group(6)), 'idx': int(m.group(7)), 'n': int(m.group(8)), 'a3': int(m.group(9)),
                    'a4': int(m.group(10)), 'reward': m.group(11), 'used': int(m.group(12))})
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
    print(f'  interactive ornaments: host {len(ho)}, guest {len(go)}', flush=True)
    for o in ho:
        print(f'    host {o["id"]} pos={o["pos"]} sort={o["sort"]} model={o["idx"]}/{o["n"]} a3={o["a3"]} a4={o["a4"]} '
              f'reward={o["reward"]} used={o["used"]}', flush=True)
    pairs = []
    for h in ho:
        g = [x for x in go if math.dist(x['pos'], h['pos']) < 1.5]
        if g and h['n'] > 0 and h['idx'] + 1 < h['n'] and h['a3'] and g[0]['a3'] and h['idx'] == g[0]['idx']:
            pairs.append((h, g[0]))
    print(f'  fossils (a next model, usable, the same on both games): {[(h["id"], h["pos"]) for h, _ in pairs]}', flush=True)
    if not ck(len(pairs) >= 2, f'two fossils with a next model on both games ({len(pairs)})'):
        return ck.result()
    for step, (actor, other, an, on, (h, g)) in enumerate((
            (guest, host, 'guest', 'host', pairs[0]), (host, guest, 'host', 'guest', pairs[1])), 1):
        mine, theirs = (g, h) if actor is guest else (h, g)
        print(f'  {step}. {an}: ' + go_near(actor, mine['pos'], 1) + f'; {on}: ' + go_near(other, theirs['pos'], -1),
              flush=True)
        time.sleep(4)
        keyframe(f'fossil{step}_before', f'fossil {step} before the {an} digs it: glowing on both games')
        print(f'  {step}. {an}: ' + actor.call(f'coop.wobj use {mine["p"]}')['out'].strip(), flush=True)
        a = at(actor, mine['pos'])
        print(f'  {step}. {an}\'s fossil after its dig: {a}', flush=True)
        ck(a and a['idx'] == mine['idx'] + 1, f'{step}. the {an}\'s game shows the next model of the fossil it dug')
        ok = wait(lambda: (lambda b: b and a and b['idx'] == a['idx'] and b['a3'] == 0 and b['a4'] == a['a4'])(
            at(other, theirs['pos'])), 8)
        b = at(other, theirs['pos'])
        print(f'  {step}. {on}\'s copy: {b}', flush=True)
        ck(ok, f'{step}. the {on}\'s copy of the fossil the {an} dug shows the same model (index {b and b["idx"]}, '
               f'the {an}\'s {a and a["idx"]}), is not usable and does not glow like it (a4 {b and b["a4"]}, '
               f'the {an}\'s {a and a["a4"]})')
        time.sleep(2)
        keyframe(f'fossil{step}_after', f'fossil {step} after the {an} dug it: the same dug model on both games')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
