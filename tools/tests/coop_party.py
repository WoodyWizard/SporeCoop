#!/usr/bin/env python3
"""BUG-9260 (the Lineages port, phase 2): the host keeps this planet's party, one seat per player who played it,
recognised by the persistent identity (BUG-9258), saved next to the planet (<planet>.spo.duoparty).

Checks:
  1. the host is in seat 0, the guest gets seat 1 under its own identity, and the party is saved
  2. the guest leaves: its seat stays (offline); it joins again: the same seat 1, no new seat
  3. the host leaves the world for the galaxy menu and plays it again: the party is read back from the file with the
     guest in seat 1 (the seat survives a reload, not only the session)

Bookkeeping, on purpose: the party has no effect in the game yet; the nests, villages and nations of the next phases
are indexed by these seats, and their tests check what the other player sees. A new feature, no fix switch: on main
`coop.party` is an unknown command and the test fails.
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def party(inst):
    return inst.call('coop.party').get('out', '')


def own(inst):
    m = re.search(r'own=([0-9A-F]{16})', inst.call('coop.identity').get('out', ''))
    return m.group(1) if m else ''


def seat(out, n):
    m = re.search(rf'^\s+seat {n}: .* id=([0-9A-F]{{8}}) species=[0-9A-F]{{8}} player=(\S+)$', out, re.MULTILINE)
    return (m.group(1), m.group(2)) if m else (None, None)


def seats(out):
    m = re.search(r'seats=(\d+)', out)
    return int(m.group(1)) if m else -1


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError):
            pass
        time.sleep(step)
    return False


def main():
    host, guest = start('creature')
    ck = Checks()
    h_id, g_id = own(host)[:8], own(guest)[:8]
    ck(h_id and g_id and h_id != g_id, f'both games have their own identity ({h_id}, {g_id})')
    # 1. seats
    ck(wait(lambda: seat(party(host), 1)[0] == g_id and seat(party(host), 1)[1] not in (None, 'offline'), 30), f'the guest takes seat 1 under its identity ({party(host).strip()})')
    out = party(host)
    ck(seat(out, 0)[0] == h_id, f'the host is in seat 0 ({seat(out, 0)})')
    ck(seats(out) == 2, f'two seats ({seats(out)})')
    # a fixture planet has no party file yet: the new seats are written once (saves counts the writes)
    ck(wait(lambda: re.search(r'saves=[1-9]', party(host)) and 'dirty=0' in party(host) and 'errors=0' in party(host), 20),
       f'the party is saved ({party(host).strip()})')
    ck('seats=0' in party(guest), f'a guest keeps no party ({party(guest).strip()})')
    # 2. a rejoin
    guest.call('coop.leave')
    ck(wait(lambda: seat(party(host), 1)[1] == 'offline', 20), f'the guest\'s seat stays while it is away ({party(host).strip()})')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: seat(party(host), 1)[1] not in (None, 'offline'), 45), f'the same seat 1 again ({party(host).strip()})')
    ck(seats(party(host)) == 2, f'no new seat for the same player ({seats(party(host))})')
    # 3. a reload of the planet
    planet = re.search(r'planet=(\S+)\.spo', party(host))
    ck(planet is not None, 'the party names its planet')
    host.call('coop.menu')
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 120), 'the host is in the galaxy menu')
    ck(wait(lambda: 'seats=0' in party(host), 10), 'outside a planet the host has no party')
    host.call('wait.frames 240 90000')
    if planet:
        host.call(f'coop.load Game0 {planet.group(1)}')
    ck(wait(lambda: seat(party(host), 1)[0] == g_id and seat(party(host), 1)[1] not in (None, 'offline'), 300, 3), f'after the reload the guest is in seat 1 again ({party(host).strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
