#!/usr/bin/env python3
"""BUG-1475 (#217 a, the idea of PR #217 by IltonBJSilva; WP-K1 of docs/plan-0.7.17.md): a guest's own creature is its
empire's captain on its own game in Space; the host's game and every save keep the host's captain
(docs/re/space.md#space-own-captain).

  1. the guest plays an own creature of its My Creations (`coop.creations choose creature <own>`, creature.mode own);
  2. within 20 s the guest's game has that creature as its empire's captain (cEmpire +0xC4: `coop.captain`, the game's
     field), with a species profile; the host's game keeps its own captain;
  3. the guest's captain screen opens on it as the game's Space update opens it (`coop.captain open`: the portrait of
     the captain, BUG-1111's path): no fault; key frame of the screen (the guest's own creature's portrait);
  4. the guest's game saves: its newest save names the host's captain and not the own creature (the savestrip hooks
     write the world's captain for the save), and the empire has the own captain again after it.
  SPORE_FIX_OFF=space.own_captain must FAIL (step 2: the guest's empire keeps the host's captain).

  SPORE_LANE=2 python3 tools/tests/coop_space_own_captain.py
"""
import re
import time

import coop_space_finale as fz
import lane
from coop_tribe_outfit import keys_in_save
from twoinst import Checks, keyframe, save_fresh, save_taken, shot, start


def captain(con):
    out = con.call('coop.captain')['out'] or ''
    k = re.search(r'captain=([0-9A-F]{8})', out)
    return k.group(1) if k else None, out.strip()


def own_creatures(con):
    return [ln.split()[1] for ln in (con.call('coop.creations list')['out'] or '').splitlines()
            if ln.startswith('creature ') and len(ln.split()) > 1]


def main():
    host, guest = start('space', planet=fz.fixture_planet())
    ck = Checks()
    time.sleep(3)
    hc0, gc0 = captain(host)[0], captain(guest)[0]
    print(f'  captains at the start: host {hc0}, guest {gc0}', flush=True)
    ck(hc0 and hc0 == gc0, f"the guest's game starts with the host's captain ({gc0} / {hc0})")
    mine = own_creatures(guest)
    ck(mine, f"the guest has own creatures in its My Creations ({mine[:4]})")
    if not mine:
        return ck.result()
    own = mine[0]
    print('  guest:', guest.call(f'coop.creations choose creature {own}')['out'].strip()[:100], flush=True)
    t0 = time.time()
    while time.time() - t0 < 20 and captain(guest)[0] != own:
        time.sleep(1)
    gk, gline = captain(guest)
    print('  guest:', gline, '|', guest.call('coop.owncaptain')['out'].strip(), flush=True)
    ck(gk == own, f"2. the guest's empire has its own creature {own} as captain on its game (cEmpire +0xC4: {gk})")
    ck(captain(host)[0] == hc0, f"2. the host's game keeps its own captain {hc0} ({captain(host)[0]})")
    so = (guest.call(f'coop.creations speciesof {own}')['out'] or '').strip()
    ck(re.search(r'profile [0-9A-F]{8} key [0-9A-F]{8}', so) is not None,
       f"2. the guest's game has a species profile of its captain ({so[:80]})")
    # 3. the captain screen
    n0 = len([ln for ln in guest.call('exc')['out'].splitlines() if ln.startswith('0x')])
    print('  guest:', guest.call('coop.captain open')['out'].strip(), flush=True)
    time.sleep(3)
    shot(guest, 'own_captain_screen.png')
    keyframe('own_captain', "the guest's captain screen (the mission screen of `coop.captain open`): the captain's "
             f"portrait is the guest's own creature {own}, not the host's captain", {'guest': guest})
    n1 = len([ln for ln in guest.call('exc')['out'].splitlines() if ln.startswith('0x')])
    ck(n1 == n0, f'3. no fault on the guest with its own captain\'s screen ({n0} -> {n1})')
    guest.call('key escape')
    time.sleep(1)
    # 4. a save of the guest's game
    print('  guest:', guest.call('coop.owncaptain save')['out'].strip().splitlines()[0], flush=True)
    said, _, t_save = save_taken(guest, 30, 'guest save')
    print('  guest:', said[:80], flush=True)
    time.sleep(2)
    fresh = save_fresh(lane.GUEST_DIR, t_save - 1, 30)
    ck(fresh, "4. the guest's game wrote its save")
    if fresh:
        own_in, host_in = keys_in_save(lane.GUEST_DIR, int(own, 16)), keys_in_save(lane.GUEST_DIR, int(hc0, 16))
        print(f'  guest save: records naming {own}: {own_in}, naming {hc0}: {host_in}', flush=True)
        ck(host_in and host_in != ['no save'], f"4. the guest's save names the host's captain {hc0} ({host_in})")
        ck(not own_in, f"4. the guest's save does not name its own creature {own} ({own_in})")
    time.sleep(2)
    ck(captain(guest)[0] == own, f"4. after the save the guest's empire has its own captain again ({captain(guest)[0]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
