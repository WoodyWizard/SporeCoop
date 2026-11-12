#!/usr/bin/env python3
"""Tribe: a village the host's game conquers is conquered in the guest's game too (BUG-0777).

Discord 2026-11-09 ("their settlement is captured, the host's is not"): the conquest (the hut's fall, the village's
conquered flag, the goal mission's end) ran only in the game that saw the hut fall; the guest's realign kept the
guest's copy of the hut at 1 hp at least, so the guest's game never conquered the village.

1. Two games on the tribe fixture; the host's game forms an NPC tribe 45 m away (`coop.tribe npc_tribe`).
2. The guest's members and one of the host's raid its hut (`coop.tribe order <id> 15 <hut>`, the raid click's
   DoAction); once they hit it the hut's health is cut to 40 (`coop.tribe huthp`); the raid ends the tribe on the
   host (the game's own blows, the hut's state 2).
3. Within 30 s of the host's game conquering the village, the guest's game has the same: the village's own
   conquered flag (cTribe +0x556, `coop.realign planet village` fl bit 2) and its hut dead or gone.
   SPORE_FIX_OFF=realign.hut_fall: the guest's village stands unconquered with its hut: FAIL.
"""
import re
import time

from coop_tribe_conquer_orders import HUT, huts
from coop_tribe_gather_stall import pos_of
from coop_tribe_tools import dist, members
from twoinst import Checks, keyframe, start


def planet(con, kind):
    """[(pos, st, fl)] of this game's whole-planet list of that kind"""
    out = []
    for ln in con.call(f'coop.realign planet {kind}')['out'].splitlines():
        m = re.match(r'pl \S+ \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) st=(\d+) fl=(\d+)', ln)
        if m:
            out.append((tuple(float(m.group(i)) for i in (1, 2, 3)), int(m.group(4)), int(m.group(5))))
    return out


def near(rows, p, tol):
    best = min(rows, key=lambda r: dist(r[0], p), default=None)
    return best if best and dist(best[0], p) < tol else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)[:3]
    hown = sorted(i for i, o in mem.items() if o == 0)[-1:]
    if not ck(bool(own), f"the guest has members ({own}), the host's raider {hown}"):
        return ck.result()
    before = {h for h, _ in huts(host)}
    out = host.call('coop.tribe npc_tribe 3 45')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if not ck(bool(m), 'the host formed an NPC tribe'):
        return ck.result()
    tp = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(3)
    new = sorted((dist(p, tp), h, p) for h, p in huts(host) if h not in before)
    if not ck(bool(new), f'the new tribe has a hut ({len(new)})'):
        return ck.result()
    _, hut, hp = new[0]
    t0 = time.time()
    while time.time() - t0 < 30 and not near(planet(guest, 'hut'), hp, 10):
        time.sleep(2)
    gh = near(planet(guest, 'hut'), hp, 10)
    gv = near(planet(guest, 'village'), tp, 40)
    print(f'  hut at {hp}; the guest\'s copy: hut {gh}, village {gv}', flush=True)
    ck(gh and gv, "the guest's game has the new village and its hut")
    raiders = own + hown
    for mid in raiders:
        print(f'  raid #{mid}: ' + host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))['out'].strip(), flush=True)
    t0, cut, fell, last_hp, last_change = time.time(), False, None, None, time.time()
    while time.time() - t0 < 150:
        time.sleep(2)
        hs = host.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        mm = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
        h, state = (float(mm.group(1)), int(mm.group(2))) if mm else (None, None)
        nearby = [mid for mid in raiders if pos_of(host, mid) and dist(pos_of(host, mid), hp) < 14]
        print(f'  {time.time() - t0:4.0f} s: {hs}; raiders near {nearby}', flush=True)
        if hs.startswith('no hut') or state == 2:
            fell = time.time() - t0
            break
        if h != last_hp:
            last_hp, last_change = h, time.time()
        elif cut and time.time() - last_change > 16:
            for mid in raiders:
                host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))
            last_change = time.time()
        if cut and h is not None and h > 10:
            cut = False  # the village mended it: cut again when raiders are at it
        if not cut and len(nearby) >= max(1, len(raiders) // 2):
            print('  cut: ' + host.call('coop.tribe huthp %.2f %.2f %.2f 2' % hp)['out'].strip(), flush=True)
            cut = True
    if not ck(fell is not None, f"the raid ended the hut in the host's game ({fell and round(fell)} s)"):
        ck.faults(host=host, guest=guest)
        return ck.result()
    hvil = gvil = ghut = None
    t0 = time.time()
    while time.time() - t0 < 30:
        hvil = near(planet(host, 'village'), tp, 40)
        gvil = near(planet(guest, 'village'), tp, 40)
        ghut = near(planet(guest, 'hut'), hp, 10)
        print(f'  {time.time() - t0:4.0f} s after the fall: host village {hvil} | guest village {gvil}, guest hut {ghut}', flush=True)
        if hvil and gvil and hvil[2] & 2 and gvil[2] & 2 and (not ghut or ghut[2] & 2):
            break
        time.sleep(2)
    print('  guest: ' + guest.call('coop.realign')['out'].strip().splitlines()[0], flush=True)
    ck(hvil and hvil[2] & 2, f"the host's game conquered the village ({hvil})")
    ck(gvil and gvil[2] & 2, f"the guest's game has the village conquered too ({gvil})")
    ck(not ghut or ghut[2] & 2, f"the guest's hut of that village is down ({ghut})")
    keyframe('conquered', 'the conquered village on both screens: the hut down, no village members standing')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
