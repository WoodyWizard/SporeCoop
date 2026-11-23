#!/usr/bin/env python3
"""BUG-1237 (WP-K1 of docs/plan-0.7.17.md): a guest's own creature is baked before the game gives it to the avatar, so
it is not drawn plain green for about 2 s after a change (key frame S/050 of lab 20261117-154911).

The guest's My Creations get a fresh copy of one of its creatures (its metadata's times changed per run: a key no game
has baked; a lane's graphics cache keeps a bake once made, docs/re/creations.md#creations-model-bake). After the start
the guest changes its own creature to that copy (`coop.creations choose creature <copy>`, the co-op window's change);
the checks read the GAME's bake state (IBakeManager IsBaked, `coop.creations status` "own prebake ... swap_bake=") at
the moment the avatar took the copy (SetSpecies), and the avatar's species is the copy.
  SPORE_FIX_OFF=creations.own_prebake must FAIL (swap_bake=0: the swap ran before the game baked the model, the
  creature is drawn in the plain base coat until the game's own bake ends).
Key frame right after the swap: the guest's creature in its own colours.

  SPORE_LANE=2 python3 tools/tests/coop_own_prebake.py
"""
import os
import re
import time

import lane
import twoinst
from coop_nest_species import read_pkg, write_pkg
from twoinst import Checks, keyframe, start

FRESH = 0x29E00000 | (int(time.time()) & 0xFFFF)
BEM, META, CRT_GROUP = 0x1A99B06B, 0x030BDEE3, 0x40626200
_orig_restore = twoinst.restore_creations


def restore_with_fresh(guest=True):
    """the lane's My Creations, then a fresh copy of the guest's first creature under FRESH (as coop_civ_own_species)"""
    _orig_restore(guest)
    p = os.path.join(lane.GUEST_DIR, 'EditorSaves.package')
    if not guest or not os.path.exists(p):
        return
    recs = [r for r in read_pkg(p) if r[2] >> 20 != FRESH >> 20]
    src = min(i for t, g, i, _ in recs if t == BEM and g == CRT_GROUP)
    add = []
    for t, g, i, d in recs:
        if i != src:
            continue
        d = bytearray(d)
        if t == META and len(d) >= 0x44:
            if int.from_bytes(d[0x14:0x18], 'big') == src:
                d[0x14:0x18] = FRESH.to_bytes(4, 'big')
            for o in (0x34, 0x3C):  # (the creation and modification times: another content, another bake)
                d[o:o + 8] = (int.from_bytes(d[o:o + 8], 'big') + (FRESH & 0xFFFF) + 1).to_bytes(8, 'big')
        add.append((t, g, FRESH, bytes(d)))
    write_pkg(p, recs + add)
    print(f'  (the guest\'s My Creations: {FRESH:08X}, a fresh copy of {src:08X}, {len(add)} records)', flush=True)


def prebake(con):
    m = re.search(r'own prebake: fix=(\d) asks=(\d+) waits=(\d+) swap_bake=(-?\d+) baked=(\d+) unbaked=(\d+)',
                  con.call('coop.creations status')['out'] or '')
    return {k: int(v) for k, v in zip(('fix', 'asks', 'waits', 'bake', 'baked', 'unbaked'), m.groups())} if m else {}


def avatar_key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'] or '')
    return m.group(1) if m else None


def main():
    twoinst.restore_creations = restore_with_fresh
    host, guest = start('creature')
    ck = Checks()
    time.sleep(6)
    fresh = f'{FRESH:08X}'
    mine = [ln.split()[1] for ln in (guest.call('coop.creations list')['out'] or '').splitlines()
            if ln.startswith('creature ')]
    t0 = time.time()
    while fresh not in mine and time.time() - t0 < 20:  # (My Creations are read again every 10 s)
        time.sleep(2)
        mine = [ln.split()[1] for ln in (guest.call('coop.creations list')['out'] or '').splitlines()
                if ln.startswith('creature ')]
    if not ck(fresh in mine, f"the guest's My Creations hold the fresh copy {fresh} (a warm start has none)"):
        return ck.result()
    before = prebake(guest)
    print('  guest before:', before, '| avatar', avatar_key(guest), flush=True)
    print('  guest:', guest.call(f'coop.creations choose creature {fresh}')['out'].strip()[:100], flush=True)
    t0 = time.time()
    while avatar_key(guest) != fresh and time.time() - t0 < 30:
        time.sleep(0.25)
    swapped = avatar_key(guest) == fresh
    after = prebake(guest)
    print(f'  guest after {time.time() - t0:.1f} s:', after, '| avatar', avatar_key(guest), flush=True)
    keyframe('own_prebake', f"the guest's creature right after it became {fresh}: painted in its own colours (not the "
             "plain green base coat)", {'guest': guest}, settle=0)
    ck(swapped, f"the guest's avatar is the fresh copy {fresh} (the game's species key)")
    ck(after.get('baked', 0) + after.get('unbaked', 0) > before.get('baked', 0) + before.get('unbaked', 0),
       'the swap ran once more (own prebake counters)')
    ck(after.get('bake') == 1, f"the game had baked the copy's model when the avatar took it (swap_bake={after.get('bake')}; "
                               "0: drawn plain green until the game's own bake ends)")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
