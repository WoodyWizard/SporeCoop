#!/usr/bin/env python3
"""Tribe: a village the guest conquers while the host is away is conquered in the host's game when it is back
(BUG-0879).

While the host is in the main menu (an editor, a load) the guest's own game runs its members (tribe.away): a raid
can end an NPC village there (the hut's fall, the conquered flag, the cinematic). Nothing went to the host, and when
the host was back its state stood the village up again in the guest's game too.

1. Two games on the tribe fixture (it has one NPC village). The host goes to the main menu (`coop.menu`).
2. The guest's game raids that village's hut with the guest's members (`coop.tribe order <id> 15 <hut>` on the
   guest, the raid's DoAction in its own game), the hut's health cut to 2 there once they hit it; the raid ends the
   village in the guest's game (the hut's state 2, the village's conquered flag).
3. The host loads its world again. Within 60 s of the guest seeing it back, the host's game has the village
   conquered (cTribe +0x556, `coop.realign planet village` fl bit 2) and its hut down, and the guest's game still has.
   SPORE_FIX_OFF=tribe.away_conquest: the host's village stands unconquered: FAIL.
"""
import re
import time

from coop_tribe_conquest_guest import near, planet
from coop_tribe_conquer_orders import HUT
from coop_tribe_gather_stall import pos_of
from coop_tribe_tools import dist, members
from play_tribe import frame_point
from twoinst import Checks, keyframe, lane_state, start


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)[:3]
    if not ck(bool(own), f"the guest has members ({own})"):
        return ck.result()
    npcv = [r for r in planet(host, 'village') if not r[2] & 1]
    huts_ = [r for r in planet(host, 'hut') if not r[2] & 1]
    if not ck(npcv and huts_, f"the host's world has an NPC village ({npcv}) with a hut"):
        return ck.result()
    tp = npcv[0][0]
    hp = min(huts_, key=lambda r: dist(r[0], tp))[0]
    print(f'  the NPC village at {tp}, its hut at {hp}', flush=True)
    print(' ', host.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 90 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    if not ck('host_away=1' in guest.call('coop.status')['out'], 'the guest knows the host is away'):
        return ck.result()
    for mid in own:
        print(f'  guest raid #{mid}: ' + guest.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))['out'].strip(), flush=True)
    t0, cut, fell, last_hp, last_change = time.time(), False, None, None, time.time()
    while time.time() - t0 < 200:
        time.sleep(2)
        hs = guest.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        mm = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
        h, state = (float(mm.group(1)), int(mm.group(2))) if mm else (None, None)
        nearby = [mid for mid in own if pos_of(guest, mid) and dist(pos_of(guest, mid), hp) < 14]
        print(f'  {time.time() - t0:4.0f} s (guest): {hs}; raiders near {nearby}', flush=True)
        if hs.startswith('no hut') or state == 2:
            fell = time.time() - t0
            break
        if h != last_hp:
            last_hp, last_change = h, time.time()
        elif time.time() - last_change > 20:
            for mid in own:
                guest.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))
            last_change = time.time()
        if cut and h is not None and h > 10:
            cut = False  # the village mended it: cut again when raiders are at it
        if not cut and nearby:
            print('  cut (guest): ' + guest.call('coop.tribe huthp %.2f %.2f %.2f 2' % hp)['out'].strip(), flush=True)
            cut = True
    if not ck(fell is not None, f"the guest's raid ended the hut in the guest's game ({fell and round(fell)} s)"):
        ck.faults(host=host, guest=guest)
        return ck.result()
    t0 = time.time()
    gv = None
    while time.time() - t0 < 30:
        gv = near(planet(guest, 'village'), tp, 40)
        if gv and gv[2] & 2:
            break
        time.sleep(2)
    print(f"  the guest's village {gv}; " + guest.call('coop.tribe conquests')['out'].strip(), flush=True)
    ck(gv and gv[2] & 2, f"the guest's game conquered the village ({gv})")
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Zorispe').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 150 and 'host_away=0' not in guest.call('coop.status')['out']:
        time.sleep(2)
    ck('host_away=0' in guest.call('coop.status')['out'], 'the host is back')
    hv = hh = gv = gh = None
    t0 = time.time()
    while time.time() - t0 < 60:
        hv, hh = near(planet(host, 'village'), tp, 40), near(planet(host, 'hut'), hp, 10)
        gv, gh = near(planet(guest, 'village'), tp, 40), near(planet(guest, 'hut'), hp, 10)
        print(f'  {time.time() - t0:4.0f} s back: host village {hv} hut {hh} | guest village {gv} hut {gh}', flush=True)
        if hv and hv[2] & 2 and (not hh or hh[2] & 2):
            break
        time.sleep(3)
    time.sleep(4)
    hv, hh = near(planet(host, 'village'), tp, 40), near(planet(host, 'hut'), hp, 10)
    gv, gh = near(planet(guest, 'village'), tp, 40), near(planet(guest, 'hut'), hp, 10)
    print('  host: ' + host.call('coop.tribe conquests')['out'].strip(), flush=True)
    ck(hv and hv[2] & 2, f"the host's game has the village conquered ({hv})")
    ck(not hh or hh[2] & 2, f"the host's hut of that village is down ({hh})")
    ck(gv and gv[2] & 2 and (not gh or gh[2] & 2), f"the guest's game still has it conquered ({gv}, hut {gh})")
    frame_point({'host': host, 'guest': guest}, hp)  # BUG-0981: the village in view, not the home village
    keyframe('conquered', 'the village the guest conquered: down on both screens')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
