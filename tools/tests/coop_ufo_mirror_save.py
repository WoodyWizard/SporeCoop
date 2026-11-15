#!/usr/bin/env python3
"""BUG-0817: the guest's mirror of the host's drama UFO (BUG-0558) never goes into the guest's save.

1. The host starts its UFO event the game's own way (`coop.drama start 2`); the guest's game shows the mirror ship.
2. The guest saves (`coop.save`, the game's SaveGame) while the mirror is over the players.
3. The guest leaves the session and loads that save alone (`coop.load <its slot> <planet>`).
4. The guest's game object list (`coop.rules ufo`: UFO nouns) must hold no ship: its own Drama Manager made none,
   so a ship there came from the save. Not reproduced (2026-11-12): with SPORE_FIX_OFF=creature.ufo_save_strip the
   save references the mirror too, but the solo load brings no UFO noun back (lab 20261112-015723); a guard.

  SPORE_LANE=N python3 tools/tests/coop_ufo_mirror_save.py
"""
import os
import re
import sys
import time

import lane
from coop_creature_ufo_mirror import nearest, ships
from twoinst import Checks, lane_state, start


def guest_slot(since, planet):
    """the guest's save slot whose planet file was written after `since` (lastSave.pld is the host's copy)"""
    games = os.path.join(lane.GUEST_DIR, 'Games')
    best = None
    for d in os.listdir(games):
        p = os.path.join(games, d, planet + '.spo')
        if os.path.isfile(p) and os.path.getmtime(p) >= since - 2 and (not best or os.path.getmtime(p) > best[1]):
            best = d, os.path.getmtime(p)
    return best[0] if best else None


def ufo_nouns(con):
    """every listed UFO noun of the game (alive or not), from the census's noun counts"""
    m = re.search(r'018EBADC=(\d+)', con.call('coop.census all 100000')['out'])
    return int(m.group(1)) if m else 0


def main():
    host, guest = start(fixture='creature')
    ck = Checks()
    st = lane_state()
    planet = (st.get('planet') or 'Game0/Scalinus').split('/')[1]
    time.sleep(3)
    ck(not ships(guest), f'the guest\'s game has no ship before ({ships(guest)})')
    print('  host: ' + host.call('coop.drama start 2')['out'].strip().replace('\n', ' | '), flush=True)
    t0 = time.time()
    hs = gs = []
    while time.time() - t0 < 5:
        hs, gs = ships(host), ships(guest)
        if hs and gs and nearest(hs[0], gs) < 3:
            break
        time.sleep(0.25)
    ck(bool(hs) and bool(gs), f'the host\'s UFO is up and the guest shows its mirror (host {hs}, guest {gs})')
    if not (hs and gs):
        return ck.result()
    print(f'  guest UFO nouns before the save: {ufo_nouns(guest)}', flush=True)
    t_save = time.time()
    out = guest.call('coop.save')['out']
    ck('saved' in out, f'the guest saves with the mirror over the players ({out.strip()})')
    t1 = time.time()
    seen = False
    while time.time() - t1 < 15:  # the save is written, its box comes and is confirmed by the mod
        up = 'saved_box=1' in guest.call('coop.save box')['out']
        seen |= up
        if seen and not up:
            break
        time.sleep(0.3)
    time.sleep(2)
    print('  guest: ' + guest.call('coop.rules ufo')['out'].strip().replace('\n', ' | '), flush=True)
    slot = guest_slot(t_save, planet)
    ck(slot is not None, f'the guest\'s save slot was written ({slot})')
    if not slot:
        return ck.result()
    print('  guest: ' + guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(4)
    out = guest.call(f'coop.load {slot} {planet}')['out']
    print(f'  guest: coop.load {slot} {planet}: {out.strip()}', flush=True)
    t2 = time.time()
    while time.time() - t2 < 180 and not ('Creature' in guest.call('game.mode')['out'] and guest.call('coop.avatar')['ok']
                                          and time.time() - t2 > 10):
        time.sleep(2)
    time.sleep(6)
    loaded = 'Creature' in guest.call('game.mode')['out'] and guest.call('coop.avatar')['ok']
    ck(loaded, 'the guest loaded its save alone')
    left = ships(guest)
    dg = guest.call('coop.drama')['out']
    m = re.search(r'ufo +started=(\d+)', dg)
    print(f'  after the solo load: ships {left}, UFO nouns {ufo_nouns(guest)}; guest drama {m and m.group(0)}', flush=True)
    ck(loaded and not left, f'the loaded save holds no ship of the session ({left})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
