#!/usr/bin/env python3
"""The guest's Creature HUD portrait follows its creature (BUG-0937).

Key-frame review of the 0.7.12 final check: after the guest changed its creature (appeared_once) or became the host's
new creature (one_creature, creature.mode host) the square at the bottom right of its HUD still showed the first
species. The game sets that picture (window 0x03D99151) in its Creature HUD refresh from the avatar's species; the
mod's body swaps never ran it.

1. Two games on the creature fixture. The guest's game draws its portrait: a shot, the portrait's rectangle
   (`coop.ui where 03D99151`) cut out.
2. The guest takes another creature of its own (`coop.creations next creature`): the avatar's species changes
   (`coop.portrait`: the png key of the avatar's species, cCreatureBase +0xB20).
3. Within 8 s the window draws the image of the new species' png (`coop.portrait` same=1: the window's own image
   object is the one the game's image cache gives for that key) and the portrait in a new shot differs from the one
   of step 1 (mean difference of the pixels over 12 of 255).
PASS: both. SPORE_FIX_OFF=creature.hud_portrait must FAIL (the window keeps the first picture).

  SPORE_LANE=4 python3 tools/tests/coop_hud_portrait.py
"""
import os
import re
import time

import lane
import twoinst
from twoinst import Checks, start


def portrait(con):
    out = con.call('coop.portrait').get('out') or ''
    m = re.search(r'avatar=1 key=(\S+) same=(-?\d)', out)
    return (m.group(1), int(m.group(2))) if m else (None, None)


def crop(con, name):
    """the portrait as the guest's screen shows it (RGB bytes of its rectangle), None without Pillow or a window"""
    m = re.search(r'03D99151 (-?\d+) (-?\d+) (-?\d+) (-?\d+)', con.call('coop.ui where 03D99151').get('out') or '')
    if not m:
        return None
    path = os.path.join(lane.out_dir('shots'), name + '.png')
    if not con.call('shot ' + path).get('ok'):
        return None
    try:
        from PIL import Image
        with Image.open(path) as im:
            box = tuple(int(v) for v in m.groups())
            return im.convert('RGB').crop(box).tobytes()
    except (ImportError, OSError):
        return None


def diff(a, b):
    if not a or not b or len(a) != len(b):
        return None
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main():
    host, guest = start('creature')
    ck = Checks()
    twoinst.game_runs(guest)
    key0, same0 = None, None
    t0 = time.time()
    while time.time() - t0 < 15:
        key0, same0 = portrait(guest)
        if same0 == 1:
            break
        time.sleep(0.5)
    print(f'  the guest\'s avatar species png {key0}: the portrait draws it={same0}', flush=True)
    ck(key0 is not None, 'the guest has an avatar and a portrait window')
    before = crop(guest, 'portrait_before')
    print('  guest:', (guest.call('coop.creations next creature').get('out') or '').strip()[:120], flush=True)
    key1, same1 = key0, None
    t0 = time.time()
    while time.time() - t0 < 15 and key1 == key0:
        key1, same1 = portrait(guest)
        time.sleep(0.3)
    ck(key1 is not None and key1 != key0, f'the guest\'s avatar has another species ({key0} -> {key1})')
    t1 = time.time()
    while time.time() - t1 < 8:
        key1, same1 = portrait(guest)
        if same1 == 1:
            break
        time.sleep(0.3)
    time.sleep(0.5)
    after = crop(guest, 'portrait_after')
    d = diff(before, after)
    print(f'  {time.time() - t1:.1f} s after the change: the portrait draws the new species\' png={same1}; the portrait '
          f'in the shots changed by {d if d is None else round(d, 1)} of 255', flush=True)
    ck(same1 == 1, f'the guest\'s HUD portrait is the picture of the creature it plays now ({key1}: same={same1})')
    ck(d is not None and d > 12, f'the portrait on the guest\'s screen changed ({d if d is None else round(d, 1)})')
    twoinst.keyframe('portrait', 'the guest changed its creature: the square at the bottom right of the guest\'s HUD shows '
                                 'its new creature, not the first one')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
