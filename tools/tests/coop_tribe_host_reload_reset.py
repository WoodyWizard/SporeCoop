#!/usr/bin/env python3
"""BUG-1054: the host's reload after the live Creature -> Tribe change (BUG-0332) is forgotten with that world.

After the live change the host loads its own share save once that save went out to every guest. When the session
ended before (every guest gone, the host's Leave), the flag stayed: after the host had loaded another game, the first
share of a later session made the host's game load its save again, a few seconds after a guest joined.

1. Two games on the tribe fixture; the host's reload is made due as by the live change (`coop.tribe reload arm`, the
   same call the stage change makes); the host leaves the session before any share went out.
2. The host loads the tribe fixture again (another world), hosts, the guest joins (the host shares its world).
3. PASS: in the 40 s after the guest is in, the host's game starts no load (its mode never shows a load, the
   reload count `host_reloads` stays 0).

  SPORE_FIX_OFF=tribe.host_reload_reset   FAILS (the host's game loads its share save after the join)
"""
import re
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, lane_state, start, wait_guest_in


def reloads(con):
    m = re.search(r'host_reloads=(\d+)', con.call('coop.tribe reload')['out'])
    return int(m.group(1)) if m else -1


def worlds(con):
    m = re.search(r'left world: .* \(resets (\d+)\)', con.call('coop.lifecycle held')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    lane_state(**{**lane_state(), 'fixture': 'tribe-reload-dirty'})
    print(' ', host.call('coop.tribe reload arm')['out'].strip(), flush=True)
    r0 = reloads(host)
    w0 = worlds(host)
    print(' ', host.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(3)
    slot = 21
    sh('fixture', 'slot', 'tribe', lane.HOST_PROFILE, str(slot))
    host.call(f'coop.load Game{slot} {fixtures.planet_of("tribe").split("/")[1]}')
    t0 = time.time()
    while time.time() - t0 < 30 and not any(m in host.call('game.mode')['out'] for m in ('Load', 'Menu', 'GalaxyGameEntry')):
        time.sleep(0.2)  # the load starts
    while time.time() - t0 < 180:
        m = host.call('game.mode')['out']
        if 'Load' not in m and 'Menu' not in m and 'GalaxyGameEntry' not in m and host.call('coop.avatar')['ok']:
            break
        time.sleep(0.5)
    w1 = worlds(host)
    print(f'  the host loaded another world ({time.time() - t0:.0f} s, world changes {w0} -> {w1})', flush=True)
    ck(w1 > w0, 'the host\'s game changed its world')
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)  # (the host's Leave dropped it)
    wait_guest_in(host, guest, 150)
    t1 = time.time()
    while time.time() - t1 < 30 and not any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load')):
        time.sleep(0.3)
    ck(wait_guest_in(host, guest, 150), 'the guest joined the host')
    loads = 0
    t2 = time.time()
    while time.time() - t2 < 40:
        if 'Load' in host.call('game.mode')['out']:
            loads += 1
        time.sleep(0.5)
    r1 = reloads(host)
    print(f'  host_reloads {r0} -> {r1}, load frames seen {loads}', flush=True)
    ck(r1 == r0 and loads == 0, f'the host\'s game does not load its save again after the join ({r0} -> {r1}, '
       f'{loads} looks at a load)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
