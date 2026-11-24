#!/usr/bin/env python3
"""Space: a partner who reached the galactic core gives Steve's gift only in the co-op galaxy (BUG-1415, audit SPC-8).

Steve's gift (the Staff of Life, the core tools) is given to every player who lacks it once a partner entered the core
(space_finale.cpp reward_tick, g_reached_by). A guest that plays a Space save of its own in the session (supported:
BUG-0709, BUG-0816) got it in that save too: its own world was given the core's reward for the co-op galaxy.

1. The usual Space session (fixture space). The guest, still connected, loads a copy of the share save under another
   planet name (own_world.load_own): another Space save than the host's (`space_other=1`).
2. The guest's restore window of the new Space world must be over first (`trade` restore_pending=0, up to 20 s after
   the load: spaceext::hold_grants() holds every grant, Steve's gift too, and reward_tick returns before it looks at a
   partner's entry). The first form sent K_REACHED at once and watched 15 s, all of it inside that window: both arms
   passed without reward_tick ever deciding (rewards=0 and reward_other_save=0 with the fix on and off, lab
   20261123-104630 and the twin 20261123-120656; BUG-1415).
3. The host tells the partners it entered the core (`coop.space finale reached`: its K_REACHED, without the trip).
4. Over 15 s the guest's own save must not get the Staff of Life (`contracts tool 72E076A7`: the game's HasTool) nor
   the core tools flag (`finale` staff=, cPlayer::HasFlag). That reward_tick did decide is a precondition: the fix
   counted the entry as another save's (reward_other_save > 0) or the gift came; neither: INCONCLUSIVE.
   0.7.16 (SPORE_FIX_OFF=finale.gift_own_save): reward_tick gives it within 3 s (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_core_own_save.py
"""
import re
import time

import own_world
from coop_space_map import no_movie, sp, wait
from twoinst import Checks, keyframe, start

STAFF = '72E076A7'


def has(con, h):
    return ': have' in sp(con, 'contracts tool', h)


def tail(text, n):
    return ' '.join(text.split())[-n:]


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
    # the restore window of the new Space world (up to 20 s, no restore comes for an own save) holds every grant
    t1 = time.time()
    clear = bool(wait(lambda: 'restore_pending=0' in sp(guest, 'trade'), 40))
    print(f'  guest restore window over: {clear} after {time.time() - t1:.0f} s ({tail(sp(guest, "trade"), 80)})', flush=True)
    print(f'  guest finale before: {sp(guest, "finale")}', flush=True)
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
    print(f'  guest finale: {f}', flush=True)
    print(f'  guest trade: {tail(sp(guest, "trade"), 80)}', flush=True)
    ck(told, 'the host\'s entry reached the guest (reached_by)')
    gift = seen != {(False, False)}
    ck(not gift, f'the guest\'s own save got no Steve\'s gift in 15 s (Staff, flag seen: {sorted(seen)})')
    m = re.search(r'reward_other_save=(\d+)', f)
    undecided = not gift and not (m and int(m.group(1)) > 0)
    keyframe('core_own_save', 'the guest in a Space save of its own after the host reached the core: no Staff of Life '
             'in its cargo, no gift notice', players={'host': host, 'guest': guest})
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    if undecided:
        return ck.inconclusive('reward_tick never decided on the partner\'s entry (reward_other_save=0, no gift): the '
                               'gift was held for another reason (finale busy= / ctx= / waiting=, trade restore_pending=)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
