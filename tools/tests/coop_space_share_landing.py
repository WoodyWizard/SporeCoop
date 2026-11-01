#!/usr/bin/env python3
"""BUG-0357: a guest that rejoins while the host's ship is in a solar system gets a share save; the
save is posted as a message and the game writes it frames later (1.1 s under load in the lab). The
host's ship landed on a planet in between, the save was written on the planet, and the guest that
loaded it faulted every frame at 0xEBDD12 (and at 0xFD9420), thousands of times, its console busy
for seconds (lab 20261031-185843 campaign_space#3, the third player).

The race is made certain with `coop.worldfix landsave 300`: the host's ship lands on the active planet
(the game's own LandOnActivePlanet, as the landing of Respawn) 0.3 s after the share save is posted
(as in the lab; a landing in the frame of the post makes the game drop the save, nothing is written;
env LANDSAVE_MS changes the delay).
The host then takes off again. Checked on the guest's own game: no caught fault after the load, and
its ship is in the host's solar system.

  SPORE_LANE=9 python3 tools/tests/coop_space_share_landing.py
  (SPORE_FIX_OFF=world.share_planet_save: FAILS, the guest faults every frame)
"""
import os
import re
import time

import lane
from coop_space_contracts import sp
from coop_space_share_takeoff import log_size, log_tail, place, wait
from twoinst import Checks, host_frame, start


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    print('  host', sp(host, 'star'), '| guest', sp(guest, 'star'))
    if place(host) & 0x40000000:
        print('  takeoff:', sp(host, 'takeoff'))
        wait(lambda: place(host) & 0x80000000, 25)
        time.sleep(5)
    ck(place(host) & 0x80000000, f'the host is in its solar system ({sp(host, "star")})')
    print('  land check:', sp(host, 'star'))

    # 1. the guest leaves and joins again; the host's ship lands right after the share save is posted
    print(' ', host.call('coop.worldfix landsave ' + os.environ.get('LANDSAVE_MS', '300'))['out'].strip())
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    ok = wait(lambda: 'test: landing after the share save' in log_tail(lane.HOST, h0), 60)
    ck(ok, 'the host posted the share save and landed')
    print('  host:', re.findall(r'test: landing after the share save: .*', log_tail(lane.HOST, h0))[:1])
    landed = wait(lambda: place(host) & 0x40000000, 25)
    print(f'  host place {place(host):08X} (landed {landed})')

    # 2. the host takes off again (a share that waits for "leave the planet" goes out in the system)
    time.sleep(8)
    for _ in range(5):
        if not place(host) & 0x40000000:
            break
        print('  takeoff:', sp(host, 'takeoff'))
        wait(lambda: not place(host) & 0x40000000, 15)
        host.call('key enter')  # a caption that waits for OK
    loaded = False
    t0 = time.time()
    while time.time() - t0 < 150:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            host.call('key enter')  # the "game saved" box
        if re.search(r'stage: Loading \S+ -> Space', log_tail(lane.GUEST, g0)):
            loaded = True
            break
    ck(loaded, 'the guest loaded the host\'s world')
    tail = log_tail(lane.HOST, h0)
    print('  host:', re.findall(r'world: (?:saving for the share|the save for player \d+ was written on a planet)[^\n]*', tail))
    f0 = faults(guest)
    time.sleep(15)
    f1 = faults(guest)
    hp, gp = place(host), place(guest)
    print(f'  guest faults {f0} -> {f1} | host place {hp:08X} | guest place {gp:08X}')
    print('  guest', sp(guest, 'star'))
    ck(f1 == 0, 'the guest\'s game caught no fault after it loaded the world')
    ck(f1 == f0, 'no fault every frame on the guest after the load')
    ck(not gp & 0x40000000, 'the guest\'s ship is not on a planet after the load')
    ck(gp == hp, 'the guest is in the host\'s solar system')
    print(' ', host.call('coop.worldfix landsave -1')['out'].strip())
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
