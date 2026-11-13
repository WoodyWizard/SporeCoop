#!/usr/bin/env python3
"""BUG-1051: a guest that joins a restarted host (or another host) gets the host's Tribe snapshots.

The guest's game keeps the cycle number of the last Tribe snapshot it got (net::CycleTracker) and drops a chunk of an
"older" cycle (BUG-0612). The tracker was never reset: after a long session (cycle ~20000) the guest left and the
host's game was restarted (its counter starts at 1 again); on the rejoin every snapshot of the new host was "older"
(up to 32768 cycles behind) and dropped: no host member moved on the guest, no mirror was made, for up to an hour.

1. Two games on the tribe fixture; the host's snapshot counter is set to 20000 (`coop.tribe cycle 20000`: a session
   of about 40 minutes; newer than the guest's, so it follows it) for 6 s.
2. The guest leaves; the host's counter goes back to 1 (`coop.tribe cycle 1`, as a restarted game's); the guest joins
   again (it loads the host's world).
3. The host puts one of its members 15 m away (`coop.tribe put`, on the ground). PASS: within 10 s the guest's game
   has that member's mirror within 4 m of the host's member (the game's own positions, `coop.tribe` on both), and the
   guest dropped no chunk as older after the rejoin.

  SPORE_FIX_OFF=tribe.cycle_reset   FAILS (no mirror moves, older_chunks grows)
"""
import re
import time

import lane
from twoinst import Checks, lane_state, start, wait_guest_in


def members(con):
    """{id: (x, y, z)} of the player tribe's members in this game (host: its members, guest: its mirrors)"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        if re.match(r'tribe \d+', line):
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and player and m.group(1) != '0':
            out[int(m.group(1))] = tuple(float(m.group(i)) for i in (3, 4, 5))
    return out


def older(con):
    m = re.search(r'older_chunks=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def main():
    host, guest = start('tribe')
    ck = Checks()
    lane_state(**{**lane_state(), 'fixture': 'tribe-cycle-dirty'})  # the host's counter was set by hand
    print(' ', host.call('coop.tribe cycle 20000')['out'].strip(), flush=True)
    time.sleep(6)
    print(f'  guest before the leave: older_chunks={older(guest)}', flush=True)
    print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(2)
    print(' ', host.call('coop.tribe cycle 1')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    wait_guest_in(host, guest, 150)
    t0 = time.time()  # the guest rejoins in its own copy first, then loads the host's world
    while time.time() - t0 < 30 and not any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load')):
        time.sleep(0.3)
    ck(wait_guest_in(host, guest, 150), 'the guest joined the host again')
    time.sleep(8)
    o0 = older(guest)
    hm = members(host)
    ck(bool(hm), f'the host has player members ({len(hm)})')
    if not hm:
        return ck.result()
    mid = sorted(hm)[-1]
    x, y, z = hm[mid]
    print(' ', host.call(f'coop.tribe put {mid} {x + 15:.1f} {y:.1f} {z:.1f}')['out'].strip(), flush=True)
    best = None
    t0 = time.time()
    while time.time() - t0 < 10:
        time.sleep(1)
        h, g = members(host).get(mid), members(guest).get(mid)
        if h and g:
            d = dist(h, g)
            best = d if best is None else min(best, d)
            if d < 4:
                break
    o1 = older(guest)
    print(f'  member #{mid}: closest guest mirror {best} m from the host\'s member; older_chunks {o0} -> {o1}', flush=True)
    ck(best is not None and best < 4, f'the guest\'s game shows the host\'s member #{mid} where the host\'s is '
       f'({best if best is None else round(best, 1)} m)')
    ck(o1 == o0, f'the guest drops no snapshot of the restarted host as older ({o0} -> {o1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
