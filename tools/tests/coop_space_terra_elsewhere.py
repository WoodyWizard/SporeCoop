#!/usr/bin/env python3
"""Space: terraforming done while no partner is on the planet reaches the partner's game too (BUG-0883).

The terraforming deltas go only to partners on the same planet (K_TERRA, BUG-0749), and the game integrates a delta
into the planet record over seconds with a decay, so a partner elsewhere cannot add the sum either. The mod now sends
the terraformer's planet record values (+0xB0 atmosphere, +0xB4 temperature, +0xB8 water) while they change
(spacecoop.cpp K_TERRA_REC); a partner off that planet sets its record of the planet to them.

1. Two games on space_free; the guest lands on the colony planet 01460035, the host does not (another place).
2. Test setup: the guest gets an Atmosphere Generator (`coop.space tool add 0x931426A0`).
3. Real input on the guest: the atmosphere tab (W-266), the tool's slot, a click on the ground.
4. The guest's record moves; the host's record of the planet (`coop.space planet 01460035`, by PlanetID) is read
   while the host is elsewhere, then the host lands on the planet and it is read again.
PASS: the host's record of the planet follows the guest's (within 0.01) both while the host is away and after it
lands. SPORE_FIX_OFF=space.terra_elsewhere must FAIL (the host's record keeps the old values).

  SPORE_LANE=5 python3 tools/tests/coop_space_terra_elsewhere.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, shot, start

PLANET = '01460035'
ATMOS_GEN = '0x931426A0'  # spacetools~ Atmosphere Generator (docs/re/space.md#space-terraform-flag)
W, H = 1440, 1080


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def rec(con, planet=None):
    out = sp(con, 'planet', planet) if planet else sp(con, 'planet')
    m = re.search(r'b0=([-\d.]+) b4=([-\d.]+) b8=([-\d.]+)', out)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def stat(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con))
    return int(m.group(1)) if m else -1


def close(a, b, tol=0.01):
    return a is not None and b is not None and abs(a[0] - b[0]) <= tol and abs(a[1] - b[1]) <= tol


def main():
    host, guest = start('space_free')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    want = 0x40000000 | int(PLANET, 16)
    for _ in range(3):  # land_retry can end on another planet of the system
        pg.land_retry(PLANET)
        if pg.place()[0] == want:
            break
    ck(pg.place()[0] == want and ph.place() != pg.place(),
       f'the guest is on the planet {PLANET}, the host elsewhere ({pg.place()}, {ph.place()})')
    time.sleep(5)
    print('  guest tool:', sp(guest, 'tool add', ATMOS_GEN), flush=True)
    g0, h0 = rec(guest), rec(host, PLANET)
    print(f'  before: guest {g0}, host (record by id) {h0}', flush=True)
    used = False
    for attempt in range(3):
        st = pg.palette_open()
        y = H - 151 if st else H - 51
        guest.call(f'click {W - 426} {y}')
        time.sleep(1)
        st = pg.palette_open()
        y = H - 151 if st else H - 51
        guest.call(f'move {W - 266} {y}')
        time.sleep(0.4)
        guest.call(f'click {W - 266} {y}')
        time.sleep(1.5)
        guest.call('move 1014 1011')  # the atmosphere palette: the added tool, first slot of the second row
        time.sleep(0.4)
        guest.call('click 1014 1011')
        time.sleep(1)
        for x, yy in ((720, 640), (640, 700), (800, 700)):
            guest.call(f'move {x} {yy}')
            time.sleep(0.6)
            guest.call(f'click {x} {yy}')
            time.sleep(3)
            g1 = rec(guest)
            if g1 and g0 and abs(g1[0] - g0[0]) + abs(g1[1] - g0[1]) > 0.005:
                used = True
                break
        shot(guest, f'elsewhere_tool{attempt}.png')
        if used:
            break
    ck(used, f'the guest\'s tool hit the planet ({g0} -> {rec(guest)})')
    t0 = time.time()
    g1 = h1 = None
    while time.time() - t0 < 40:  # the record moves for a while; the last values reach the host
        time.sleep(4)
        g1, h1 = rec(guest), rec(host, PLANET)
        print(f'  {time.time() - t0:4.0f} s: guest {g1}, host away {h1}', flush=True)
        if g1 and g0 and abs(g1[0] - g0[0]) > 0.03 and close(g1, h1):
            break
    print(f'  host: terra_rec_got={stat(host, "terra_rec_got")} terra_rec_set={stat(host, "terra_rec_set")}; '
          f'guest: terra_rec_sent={stat(guest, "terra_rec_sent")} terra_sent={stat(guest, "terra_sent")}', flush=True)
    ck(g1 and g0 and abs(g1[0] - g0[0]) + abs(g1[1] - g0[1]) > 0.03, f'the guest\'s planet changed ({g0} -> {g1})')
    ck(close(g1, h1), f'the host\'s record of the planet follows while it is away (guest {g1}, host {h1})')
    for _ in range(3):
        ph.land_retry(PLANET)
        if ph.place()[0] == want:
            break
    time.sleep(5)
    g2, h2 = rec(guest), rec(host)
    ck(ph.ctx() == 'planet' and ph.place() == pg.place(), f'the host landed on {PLANET}')
    ck(close(g2, h2), f'on the planet the host\'s record is the guest\'s (guest {g2}, host {h2})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
