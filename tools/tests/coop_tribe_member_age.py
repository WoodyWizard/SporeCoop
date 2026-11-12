#!/usr/bin/env python3
"""Tribe: a baby of the player tribe grows up in the host's game; its mirror on the guest grows up too (BUG-0700;
class BUG-0419: state sent but not applied; the member's age went to the guest at the mirror's spawn only).

1. Two games on the tribe fixture. The host's game makes a baby in the player tribe (`coop.tribe spawn 1 0`: the
   game's SpawnMember(0), as the hut's purchase does).
2. For up to 300 s: the member's age (+0xB34, `coop.tribe` list) on the host and on its mirror on the guest. When the
   host's grows up (0 -> 1), the guest's mirror must have age 1 within 10 s.
Prints the times (a probe as well: how long a baby takes to grow, whether the guest's game grows its mirror itself).

--rejoin (BUG-0857): 12 s after the baby's birth the guest leaves and joins again (`coop.leave`, the join command):
its mirror of the baby is made or bound in the middle of the growth. When the host's baby grows up the guest's mirror
must have age 1 within 3 s. SPORE_FIX_OFF=tribe.mirror_grow: the mirror grows by its own timer, from its making (FAIL).
"""
import argparse
import re
import time

import lane
from twoinst import Checks, start


def ages(con):
    """{member id: age} of the player tribe"""
    out, inside = {}, False
    for ln in con.call('coop.tribe')['out'].splitlines():
        if ln.startswith('tribe '):
            inside = '(player)' in ln
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+)', ln)
        if m and inside:
            out[int(m.group(1))] = int(m.group(3))
    return out


def in_tribe(con):
    return '01654C02' in con.call('game.mode')['out'] and 'receiving' not in con.call('coop.status')['out']


def rejoin(guest, ck):
    guest.call('coop.leave')
    time.sleep(4)
    print('  rejoin:', guest.call(lane.JOIN)['out'].strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 60 and not left:  # the host's world comes: the old copy goes
        m = guest.call('game.mode')['out']
        left = 'Load' in m or 'GalaxyGameEntry' in m or 'receiving world' in guest.call('coop.status')['out']
        time.sleep(0.3)
    t1 = time.time()
    while time.time() - t1 < 120 and not (in_tribe(guest) and 'Load' not in guest.call('game.mode')['out']):
        time.sleep(0.5)
    back = in_tribe(guest)
    ck(left and back, f'the guest left ({left}) and plays the host\'s Tribe again ({back}) {time.time() - t0:.0f} s')
    return back


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rejoin', action='store_true', help='BUG-0857: the guest rejoins mid-growth')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    before = ages(host)
    print(' ', host.call('coop.tribe spawn 1 0')['out'].strip(), flush=True)
    time.sleep(3)
    now = ages(host)
    new = [i for i in now if i not in before and now[i] == 0]
    if not ck(new, f"the host's game has a baby in the player tribe ({now})"):
        return ck.result()
    bid = new[0]
    t0 = time.time()
    grown_h = grown_g = None
    if a.rejoin:
        time.sleep(12)
        if not rejoin(guest, ck):
            return ck.result()
        print(f'  {time.time() - t0:.0f} s after the birth the guest is back: host age {ages(host).get(bid)}, '
              f'guest mirror age {ages(guest).get(bid)}', flush=True)
    while time.time() - t0 < 300:
        h, g = ages(host).get(bid), ages(guest).get(bid)
        t = time.time() - t0
        if grown_h is None and h == 1:
            grown_h = t
            print(f'  {t:.0f} s: the host\'s baby #{bid} grew up (guest mirror age {g})', flush=True)
        if grown_g is None and g == 1:
            grown_g = t
            print(f'  {t:.0f} s: the guest\'s mirror of #{bid} has age 1 (host {h})', flush=True)
        if grown_h is not None and (grown_g is not None or t - grown_h > 10):
            break
        time.sleep(1)
    print(f'  host grown at {grown_h}, guest mirror at {grown_g}', flush=True)
    ck(grown_h is not None, "the host's baby grew up within 300 s")
    ck(grown_h is None or (grown_g is not None and grown_g - grown_h <= (3 if a.rejoin else 10)),
       f"the guest's mirror grew up with it ({grown_h} / {grown_g})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
