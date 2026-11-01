#!/usr/bin/env python3
"""Space: a guest that waits for the host's world while the host's ship is on a planet is told why,
and the host's trace says the share waits (BUG-0382).

After Civ -> Space every guest stayed in its Civ world for ~90 s (campaign_space, the monitor's
stage_apart, 11 violations) and nobody but the host was told why: the Space stage saves only off a
planet (docs/07-modes.md 7.2), so the host's world goes out only when its ship leaves the home
planet. The same wait comes when a guest joins while the host is on a planet; that is the scenario
here (a new Space stage needs the whole campaign).

1. Two games on space. The host lands on the active planet of its system.
2. The guest leaves and joins again: the host's share save waits.
3. Within 25 s (a trace line comes every 10 s) the guest's game shows the host's notice (its feed /
   notification line in the guest's log: "... world goes to the others when ... ship leaves the
   planet") and the host's own trace line says `share_wait=planet` (what the monitor reads).
4. The host takes off: the guest loads the host's world and plays Space in the host's system.

SPORE_FIX_OFF=world.share_wait_notice: FAILS (no notice on the guest, no share_wait on the host).

  SPORE_LANE=9 python3 tools/tests/coop_space_share_wait.py
"""
import re
import time

import lane
from coop_space_contracts import sp
from coop_space_share_takeoff import log_size, log_tail, place, wait
from twoinst import Checks, host_frame, start

NOTICE = re.compile(r"coop: \S+'s world goes to the others when \S+'s ship leaves the planet")


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    for _ in range(5):
        r = sp(host, 'land')
        if wait(lambda: place(host) & 0x40000000, 25):
            break
        print('  land:', r)
        host.call('key enter')
    ck(place(host) & 0x40000000, f'the host is on a planet ({sp(host, "star")})')
    time.sleep(5)

    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    t0 = time.time()
    told = trace = False
    while time.time() - t0 < 25 and not (told and trace):
        time.sleep(1)
        told = told or bool(NOTICE.search(log_tail(lane.GUEST, g0)))
        trace = trace or 'share_wait=planet' in log_tail(lane.HOST, h0)
    m = NOTICE.search(log_tail(lane.GUEST, g0))
    print(f'  guest told: {m.group(0) if m else None}', flush=True)
    ck('saving for the share' not in log_tail(lane.HOST, h0), 'the share save waits while the host is on the planet')
    ck(told, 'the guest\'s game shows why it waits (the host\'s notice)')
    ck(trace, 'the host\'s trace line says share_wait=planet')

    print('  takeoff:', sp(host, 'takeoff'))
    loaded = False
    t1 = time.time()
    while time.time() - t1 < 120:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            host.call('key enter')  # the "game saved" box
        if re.search(r'stage: Loading \S+ -> Space', log_tail(lane.GUEST, g0)):
            loaded = True
            break
    ck(loaded, 'the guest loaded the host\'s world after the takeoff')
    time.sleep(10)
    hp, gp = place(host), place(guest)
    print(f'  host place {hp:08X} | guest place {gp:08X}', flush=True)
    ck(gp == hp and hp & 0x80000000, 'the guest plays Space in the host\'s solar system')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
