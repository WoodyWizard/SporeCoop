#!/usr/bin/env python3
"""Tribe: a member mirror held in the air comes down when the host's news of it stops (BUG-1493, audit TR-7).

A guest's mirror of a member the host's game has in the air is placed at the host's height and held there
(game::air_hold, BUG-0946 / BUG-1082: re-placed whenever the guest's game grounds it). guest_tick stops placing a
mirror when the host goes away (the guest's own game plays it) or no snapshot of it came for 3 s; it left the hold as it
was, so the mirror hung at the old height until the host was back or the snapshots came again.

1. Two games on the tribe fixture. The host's game holds a player member 12 m up for 30 s (`coop.tribe hang`: not
   grounded, no velocity, as the game holds one). The guest's mirror is up too (over 6 m above the guest's own ground,
   read from the guest's game: precondition, else INCONCLUSIVE).
2. The guest ignores the host's tribe states (`coop.tribe statehold 1`: the snapshots stop, as on a lost link or a
   host stand; the air list still comes). 3 s later guest_tick no longer places that mirror.
3. 7 s after the hold of states the guest's mirror is on its ground (under 2 m over the guest's ground: the guest's
   game's own position of it). A key frame of the guest's view.
4. The states come again (`statehold 0`): the mirror is up again within 6 s while the host still holds it (the air
   hold still works).
SPORE_FIX_OFF=tribe.air_release must FAIL (the mirror hangs at 12 m in step 3).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_air_release.py
"""
import re
import time

from coop_tribe_member_air import ground
from coop_tribe_member_hang import all_members
from play_tribe import frame_point
from twoinst import Checks, game_runs, keyframe, start

AGL = 12
SECS = 30


def up_on(con, mid):
    m = all_members(con).get(mid)
    return (ground(con, m['pos'])[1], m['pos']) if m else (None, None)


def released(con):
    m = re.search(r'released=(\d+)', con.call('coop.tribe anims')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(4)
    ms = all_members(host)
    if sum(1 for m in ms.values() if m['player'] and not m['leader']) < 4:
        print('  host:', host.call('coop.tribe spawn 4')['out'].strip()[:80], flush=True)
        time.sleep(6)
        ms = all_members(host)
    cands = sorted((i for i, m in ms.items() if m['player'] and not m['leader'] and not m['dead'] and m['hp'] > 0),
                   reverse=True)  # the newest first: idle, no node that places them itself
    mid, gup = None, None
    for c in cands[:4]:
        print(f'  #{c} held {AGL} m up for {SECS} s:', host.call(f'coop.tribe hang {c} {AGL} {SECS}')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 6:
            time.sleep(0.5)
            hup, _ = up_on(host, c)
            gup, _ = up_on(guest, c)
            if hup is not None and gup is not None and hup > 0.8 * AGL and gup > 6:
                mid = c
                break
        if mid is not None:
            break
        print(f'    not up on both (host {hup}, guest {gup}): another member', flush=True)
        time.sleep(max(0.0, SECS - 6 + 1))  # (that hold ends first)
    if mid is None:
        return ck.inconclusive('no member held up on the host with its mirror up on the guest')
    t_hold = time.time()
    r0 = released(guest)
    print(f'  #{mid}: the guest\'s mirror {gup:.1f} m over its ground; released so far {r0}', flush=True)
    print('  guest:', guest.call('coop.tribe statehold 1')['out'].strip()[:80], flush=True)
    t0 = time.time()
    worst = None
    while time.time() - t0 < 7:
        time.sleep(1)
        gup, gpos = up_on(guest, mid)
        hup, _ = up_on(host, mid)
        print(f'    {time.time() - t0:3.0f} s without states: host {hup and round(hup, 1)} m, guest {gup and round(gup, 1)} m up', flush=True)
    gup, gpos = up_on(guest, mid)
    hup, _ = up_on(host, mid)
    r1 = released(guest)
    if gpos:
        frame_point({'guest': guest}, gpos)
    keyframe('down', f'the guest: its mirror of member #{mid} stands on the ground (the host holds it {AGL} m up; the '
             'host\'s news stopped)', settle=0)
    ck(hup is not None and hup > 0.8 * AGL, f'the host\'s game still holds #{mid} up ({hup})')
    ck(gup is not None and gup < 2.0, f'the guest\'s mirror came down once the host\'s news stopped ({gup} m over its '
       f'ground; holds released {r0} -> {r1})')
    print('  guest:', guest.call('coop.tribe statehold 0')['out'].strip()[:80], flush=True)
    t0 = time.time()
    while time.time() - t0 < 6:
        time.sleep(1)
        worst, _ = up_on(guest, mid)
        if worst is not None and worst > 6:
            break
    left = SECS - (time.time() - t_hold)
    if left > 1:
        ck(worst is not None and worst > 6, f'with the states back the guest\'s mirror is up again ({worst} m)')
    else:
        print(f'    (the host\'s hold ended before the check: {left:.0f} s left)', flush=True)
    time.sleep(max(0.0, left) + 4)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
