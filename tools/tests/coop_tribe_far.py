#!/usr/bin/env python3
"""Tribe: the host's game keeps the animals alive where a guest looks, far from the host (BUG-0280).

The Tribe stage runs the same relevance pass as the Creature stage (SporeCoop::Relevance_Update,
docs/re/creature.md#creature-view-centres): herds within 100 m of a sphere are enabled, creatures
outside every sphere's leave radius go back to the pool. Without the fix the only sphere is the
host's camera (at its tribe): a guest looking at another part of the planet sees no animals there
on the host's game. With it the host adds the guest's pointer (the point under its mouse,
modes.cpp rts_fill) as a sphere.

The guest's pointer is pinned (`coop.npc marker x y z`, as if its mouse pointed there) at the home
of a herd of the game 300-450 m from the host's camera sphere. Checked on the host's game for 40 s
(`coop.npc herds all`: the game's enabled flag +0x218 and members standing in the world, not in the
pool at the planet's centre): the herds within 100 m of the guest's point are enabled and have
animals in the world in most samples.

  coop_tribe_far.py   (SPORE_FIX_OFF=npc.view_centres_tribe must FAIL)"""
import math
import re
import time

from twoinst import Checks, start

HERD = re.compile(r'herd (\S+) arch=([0-9A-F]{8}) .*? en=(\d) members=(\d+) in_world=(\d+) '
                  r'home=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')
SPHERE = re.compile(r'game sphere \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) enter (\d+) m leave (\d+) m')


def herds(host):
    out = []
    for m in HERD.finditer(host.call('coop.npc herds all')['out']):
        out.append(dict(ptr=m.group(1), arch=m.group(2), en=int(m.group(3)), members=int(m.group(4)),
                        in_world=int(m.group(5)), home=tuple(float(m.group(i)) for i in (6, 7, 8))))
    return out


def sphere(host):
    m = SPHERE.search(host.call('coop.views')['out'])
    return (tuple(float(m.group(i)) for i in (1, 2, 3)), int(m.group(4)), int(m.group(5))) if m else (None, 0, 0)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(8)
    print('  game radii in memory (0x1565D5C enter, 0x1565D60 leave): '
          + host.call('mem.read 0x1565D5C 8')['out'].strip().replace('\n', ' '), flush=True)
    cam, enter, leave = sphere(host)
    ck(cam is not None, f'the host\'s own sphere (its camera) is known: {cam}, enter {enter} m, leave {leave} m')
    if cam is None:
        return ck.result()
    r = math.dist(cam, (0, 0, 0))
    hs = [h for h in herds(host) if h['arch'] != '00000000' and abs(math.dist(h['home'], (0, 0, 0)) - r) < 60]
    far = sorted((h for h in hs if 300 < math.dist(h['home'], cam) < 450), key=lambda h: math.dist(h['home'], cam))
    print(f'  {len(hs)} herds of the game on the surface, {len(far)} 300-450 m from the host\'s camera', flush=True)
    ck(far, 'a herd of the game far from the host\'s camera')
    if not far:
        return ck.result()
    pt = far[0]['home']
    print(f'  the guest points at {pt} ({math.dist(pt, cam):.0f} m from the host\'s camera); before: '
          f'en={far[0]["en"]} in_world={far[0]["in_world"]}', flush=True)
    print('  ' + guest.call('coop.npc marker %.1f %.1f %.1f' % pt)['out'].strip(), flush=True)
    time.sleep(15)  # the pass runs every 5 s, the herd spawns its members after it is enabled
    good, samples, seen = 0, 0, []
    t0 = time.time()
    while time.time() - t0 < 40:
        near = [h for h in herds(host) if math.dist(h['home'], pt) < 100 and h['arch'] != '00000000']
        en = sum(h['en'] for h in near)
        alive = sum(h['in_world'] for h in near)
        samples += 1
        seen.append((en, alive))
        good += en >= 1 and alive >= 2
        time.sleep(2)
    cam2, _, _ = sphere(host)
    print(f'  herds within 100 m of the guest\'s point (enabled, animals in the world): {seen}', flush=True)
    print('  host ' + host.call('coop.views')['out'].strip().replace('\n', '\n  '), flush=True)
    ck(cam2 is not None and math.dist(cam2, pt) > 250, 'the host\'s camera stays far from the guest\'s point')
    ck(good >= 0.8 * samples, f'the host\'s game keeps herds with animals where the guest looks ({good}/{samples})')
    guest.call('coop.npc marker off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
