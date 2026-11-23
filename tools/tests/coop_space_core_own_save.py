#!/usr/bin/env python3
"""Space: a partner who reached the galactic core gives Steve's gift only in the co-op galaxy (BUG-1415, audit SPC-8).

Steve's gift (the Staff of Life, the core tools) is given to every player who lacks it once a partner entered the core
(space_finale.cpp reward_tick, g_reached_by). A guest that plays a Space save of its own in the session (supported:
BUG-0709, BUG-0816) got it in that save too: its own world was given the core's reward for the co-op galaxy.

1. The usual Space session (fixture space). The guest, still connected, loads a copy of the share save under another
   planet name (own_world.load_own): another Space save than the host's (`space_other=1`).
2. The host tells the partners it entered the core (`coop.space finale reached`: its K_REACHED, without the trip).
3. Over 15 s the guest's own save must not get the Staff of Life (`contracts tool 72E076A7`: the game's HasTool) nor
   the core tools flag (`finale` staff=, cPlayer::HasFlag).
   0.7.16 (SPORE_FIX_OFF=finale.gift_own_save): reward_tick gives it within 3 s (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_core_own_save.py
"""
import re
import time

import own_world
from coop_space_map import no_movie, sp
from twoinst import Checks, keyframe, start

STAFF = '72E076A7'


def has(con, h):
    return ': have' in sp(con, 'contracts tool', h)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    before = has(guest, STAFF) or 'staff=1' in sp(guest, 'finale')
    ck(not before, 'the fixture has no Steve\'s gift yet')
    if before:
        return ck.result()
    ok = own_world.load_own(guest, 'Madas.spo', 'Coreown', 'Space')
    ck(ok, 'the guest plays a Space save of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        guest.call('coop.worldfix autoload 1')
        return ck.result()
    no_movie(guest)
    ck(not has(guest, STAFF), 'the guest\'s own save has no Staff of Life')
    print('  host: ' + sp(host, 'finale reached'), flush=True)
    seen = set()
    told = False
    t0 = time.time()
    while time.time() - t0 < 15:
        f = sp(guest, 'finale')
        told |= bool(re.search(r'reached_by=\S', f))
        seen.add((has(guest, STAFF), 'staff=1' in f))
        time.sleep(1)
    f = sp(guest, 'finale')
    print(f'  guest finale: {f[-220:]}', flush=True)
    ck(told, 'the host\'s entry reached the guest (reached_by)')
    ck(seen == {(False, False)}, f'the guest\'s own save got no Steve\'s gift in 15 s (Staff, flag seen: {sorted(seen)})')
    keyframe('core_own_save', 'the guest in a Space save of its own after the host reached the core: no Staff of Life '
             'in its cargo, no gift notice', players={'host': host, 'guest': guest})
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
