#!/usr/bin/env python3
"""Space: terraforming with a real tool changes the planet in the partner's game too (BUG-0749).

The host's own terraforming tool (the Atmosphere Generator) hits the planet: the game's tool code (cToolStrategy::OnHit)
adds atmosphere / water / temperature deltas (cTerraformingManager::Add*Delta), the mod sends their sum to the partners
on the same planet (spacecoop.cpp K_TERRA), and each partner's game adds them to its planet. The planet record
(+0xB0 atmosphere, +0xB4, +0xB8) moves over the next seconds while the terrain tick integrates the deltas, which it
does only while the planet's terraforming flag is on (docs/re/space.md#space-terraform-flag).

1. Two games on space_free; both land on the colony planet 01460035 (play_space.Player.land_retry).
2. Test setup: the host gets an Atmosphere Generator (`coop.space tool add 0x931426A0`, the fixture has no
   atmosphere tool).
3. Real input on the host: the atmosphere tab of the tool palette (W-266), the tool's slot, a click on the ground.
PASS: the host's planet record changes (its own tool worked) and the guest's record follows within 40 s, close to the
host's. SPORE_FIX_OFF=space.terra_apply must FAIL (the guest gets the deltas, its planet does not change).

  SPORE_LANE=6 python3 tools/tests/coop_space_terra_tool.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, shot, start

PLANET = '01460035'
ATMOS_GEN = '0x931426A0'  # spacetools~ Atmosphere Generator (docs/re/space.md 14.1)


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def rec(con):
    m = re.search(r'b0=([-\d.]+) b4=([-\d.]+) b8=([-\d.]+)', sp(con, 'planet'))
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def stat(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space_free')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.land_retry(PLANET)
    ck(ph.ctx() == 'planet' and ph.place() == pg.place(), f'both on the planet {PLANET} ({ph.place()}, {pg.place()})')
    time.sleep(5)
    print('  host tool:', sp(host, 'tool add', ATMOS_GEN), flush=True)
    h0, g0 = rec(host), rec(guest)
    print(f'  before: host {h0}, guest {g0}', flush=True)
    W, H = 1440, 1080
    used = False
    for attempt in range(3):
        # the palette's state decides the tab row (open: H-151, closed: H-51); another tab first refreshes the
        # palette after the added tool
        st = ph.palette_open()
        y = H - 151 if st else H - 51
        host.call(f'click {W - 426} {y}')
        time.sleep(1)
        st = ph.palette_open()
        y = H - 151 if st else H - 51
        host.call(f'move {W - 266} {y}')
        time.sleep(0.4)
        host.call(f'click {W - 266} {y}')
        time.sleep(1.5)
        shot(host, f'terra_tool_tab{attempt}.png')
        # the atmosphere palette: the tool in the first slot of its second row
        host.call('move 1014 1011')
        time.sleep(0.4)
        host.call('click 1014 1011')
        time.sleep(1)
        s0 = stat(host, 'terra_sent')
        for x, yy in ((720, 640), (640, 700), (800, 700)):
            host.call(f'move {x} {yy}')
            time.sleep(0.6)
            host.call(f'click {x} {yy}')
            time.sleep(3)
            if stat(host, 'terra_sent') > s0:
                used = True
                break
        shot(host, f'terra_tool_used{attempt}.png')
        if used:
            break
    ck(used, f'the host\'s tool hit the planet and its deltas went to the guest (terra_sent={stat(host, "terra_sent")})')
    best = None
    t0 = time.time()
    while time.time() - t0 < 40:
        h1, g1 = rec(host), rec(guest)
        print(f'  {time.time() - t0:4.0f} s: host {h1}, guest {g1}', flush=True)
        if h1 and g1 and h0 and g0:
            dh = abs(h1[0] - h0[0]) + abs(h1[2] - h0[2])
            dg = abs(g1[0] - g0[0]) + abs(g1[2] - g0[2])
            best = (dh, dg, h1, g1)
            if dh > 0.03 and dg > 0.03 and abs(h1[0] - g1[0]) < 0.3 * max(h1[0], 0.01) + 0.01:
                break
        time.sleep(4)
    print(f'  guest: terra_got={stat(guest, "terra_got")} terra_enabled={stat(guest, "terra_enabled")}', flush=True)
    dh, dg, h1, g1 = best or (0, 0, None, None)
    ck(dh > 0.03, f'the host\'s planet changed by its own tool (atmosphere / temperature +{dh:.3f})')
    ck(dg > 0.03, f'the guest\'s planet changed too (+{dg:.3f}: host {h1}, guest {g1})')
    ck(h1 and g1 and abs(h1[0] - g1[0]) < 0.3 * max(h1[0], 0.01) + 0.01,
       f'the guest\'s atmosphere is close to the host\'s ({h1[0] if h1 else None} / {g1[0] if g1 else None})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
