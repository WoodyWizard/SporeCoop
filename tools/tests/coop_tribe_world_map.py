#!/usr/bin/env python3
"""Tribe: right after a world load the guest builds no tool in a tribe of the old world, and never freezes (BUG-0672).

tribe_play (lab 20261107-073512, warm reset): 0.5 s after the guest's Loading -> Tribe its wobj tool kind made the
host's tool type 6 in host tribe 1 before the new world's first tribe state had mapped the tribes again: the mapping
(host tribe index -> this game's tribe) still named the old world's freed tribe; the tool's SetPosition re-laid its
spots and read its released species: a fault at 0xCE6AE7 (data 0x554) the game's handler caught inside SporeKit's
frame. Then a task for outside the frame posted WM_SK_PUMP_OUT while the frame flag was still set: the window procedure
posted it again at once, App::Canvas::Tick's PeekMessage loop never ran dry, no frame ran again (watchdog 30 s later).

1. Two games on the tribe fixture; the guest's mapping names an NPC tribe.
2. The guest ignores the host's tribe states (`coop.tribe statehold 1`: the mapping stays as before the first state of
   a new world), leaves and joins again: it receives and loads the host's world.
3. In Tribe again the guest makes the host's tool in that NPC tribe from its frame's tick, as the wobj snapshot does
   (`coop.tribe frametool <index> 6`), with a task for outside the frame queued just before.
PASS: no fault caught on the guest, the guest runs frames 5 s and 15 s later.
SPORE_FIX_OFF=tribe.world_map must FAIL (the stale tribe is used: a fault); SPORE_FIX_OFF=tribe.world_map,kit.outpump:
the fault and the freeze (no frames).

  SPORE_LANE=9 python3 tools/tests/coop_tribe_world_map.py
"""
import re
import time

import lane

from twoinst import Checks, start, wait_guest_in

TRIBE_MODE = 0x1654C02


def guest_mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def frame(con):
    try:
        m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    except OSError:
        return -1
    return int(m.group(1)) if m else -1


def mapping(con):
    out = con.call('coop.tribe statehold 0')['out']
    return out, re.findall(r'(\d+):([0-9A-Fa-fx]+)(\(p\))? ', out.split('map=', 1)[-1] + ' ')


def main():
    host, guest = start('tribe')
    ck = Checks()
    t0, npc = time.time(), None
    while time.time() - t0 < 90 and npc is None:
        out, m = mapping(guest)
        npc = next((int(i) for i, p, pl in m if not pl and int(p, 16) != 0), None)
        if npc is None:
            time.sleep(3)
    print('  guest:', out.strip(), flush=True)
    ck(npc is not None, f'the guest maps an NPC tribe of the host (index {npc})')
    if npc is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    print('  guest:', guest.call('coop.tribe statehold 1')['out'].strip(), flush=True)
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    t1 = time.time()
    while time.time() - t1 < 60 and guest_mode(guest) == TRIBE_MODE:
        time.sleep(0.3)
    print(f'  guest left its world after {time.time() - t1:.1f} s', flush=True)
    together = wait_guest_in(host, guest, 150)
    ck(together, f'the guest is in the host\'s world again ({time.time() - t1:.1f} s)')
    time.sleep(2)
    held = guest.call('coop.tribe statehold 1')['out'].strip()
    print('  guest (states held):', held, flush=True)
    print('  guest:', guest.call(f'coop.tribe frametool {npc} 6 10')['out'].strip(), flush=True)
    time.sleep(3)
    try:
        print('  guest:', guest.call('coop.tribe statehold 0')['out'].strip(), flush=True)
    except OSError as e:
        print('  guest: no answer', e, flush=True)
    f0 = frame(guest)
    time.sleep(5)
    f1 = frame(guest)
    ck(f1 > f0 + 30, f'the guest runs frames 5 s later (frame {f0} -> {f1})')
    time.sleep(10)
    f2 = frame(guest)
    ck(f2 > f1 + 60, f'the guest runs frames 15 s later (frame {f1} -> {f2})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
