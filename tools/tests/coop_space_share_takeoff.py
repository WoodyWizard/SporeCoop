#!/usr/bin/env python3
"""BUG-0301 (BUG-0287 again): a guest that joins while the host's ship is on a planet waits for the
world (the Space stage saves only in a solar system or the galaxy); the host leaves the planet and the
share save goes out. It was posted in the very frame the ship left the planet (the game's context
already "solar system", the mod's place one frame old, "planet, settled for a minute"), and the guest
that loaded that save had its ship go down to the planet while the host stayed in the system
(lab 20261030-075052 campaign_space#3, three guests).

The race of the lab (the share's retry every 2 s landing in that one frame) is made certain with
`coop.worldfix retry 0` (the refused save is tried every frame). Checked on the guest's own game: its
place after the load is the host's solar system, not a planet.

  SPORE_LANE=7 python3 tools/tests/coop_space_share_takeoff.py
  (SPORE_FIX_OFF=world.share_settle_context: FAILS, the guest's ship lands on the planet)
"""
import os
import re
import time

import lane
from coop_space_contracts import sp
from twoinst import Checks, host_frame, start


def place(con):
    m = re.search(r'place ([0-9A-F]{8})', sp(con, 'star'))
    return int(m.group(1), 16) if m else 0


def wait(cond, t):
    t0 = time.time()
    while time.time() - t0 < t:
        if cond():
            return True
        time.sleep(1)
    return False


def log_tail(inst, offset):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(offset)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    print('  host', sp(host, 'star'), '| guest', sp(guest, 'star'))

    # 1. the host lands on the active planet of its system
    for _ in range(5):
        r = sp(host, 'land')
        if wait(lambda: place(host) & 0x40000000, 25):
            break
        print('  land:', r)
        host.call('key enter')  # a caption that waits for OK
    ck(place(host) & 0x40000000, f'the host is on a planet ({sp(host, "star")})')
    time.sleep(5)

    # 2. the guest leaves and joins again: its share waits ("leave the planet to save")
    print(' ', host.call('coop.worldfix retry 0')['out'].strip())
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    time.sleep(8)
    waiting = 'saving for the share' not in log_tail(lane.HOST, h0)
    ck(waiting, 'the share save waits while the host is on the planet')

    # 3. the host leaves the planet: the save goes out, the guest loads it
    print('  takeoff:', sp(host, 'takeoff'))
    loaded = False
    t0 = time.time()
    while time.time() - t0 < 120:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            host.call('key enter')  # the "game saved" box
        if re.search(r'stage: Loading \S+ -> Space', log_tail(lane.GUEST, g0)):
            loaded = True
            break
    ck(loaded, 'the guest loaded the host\'s world')
    saved = re.findall(r'world: saving for the share of player \d+ \(Space, ([\d.-]+) s in this place\)',
                       log_tail(lane.HOST, h0))
    print('  host share saves (s in this place):', saved)
    time.sleep(15)  # the lab's guests went down to the planet 2-3 s after the load
    hp, gp = place(host), place(guest)
    print(f'  host place {hp:08X} | guest place {gp:08X}')
    ck(hp & 0x80000000, 'the host is in its solar system')
    ck(not gp & 0x40000000, 'the guest\'s ship did not go down to a planet after the load')
    ck(gp == hp, 'the guest is in the host\'s solar system')
    print(' ', host.call('coop.worldfix retry 2000')['out'].strip())
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
