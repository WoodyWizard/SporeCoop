#!/usr/bin/env python3
"""Space: terrain sculpting and planet colouring tools change the planet in the partner's game too (BUG-0763).

A sculpting tool's hit posts message 0x4A32D09 (a terrain effect at a transform) to the Space tool listener, which
starts the effect on the active planet's terrain; a colouring tool's hit sets the terrain's colours (SetColors). The
mod sends both to the partners on the same planet (spacecoop.cpp K_SCULPT / K_COLOUR) and each partner's game repeats
the call (docs/re/space.md#space-sculpt-colour).

1. Two games on space_free; both land on the colony planet 01460035 (play_space.Player.land_retry).
2. Real input on the host: the sculpting tab of the tool palette (W-226), its first tool, a click on the ground;
   twice, at the same screen point, measuring the terrain height (coop.ground, cPlanetModel::GetHeightAt) at the
   first hit's spot in both games before and after the second hit.
3. Real input on the host: the colouring tab (W-186), its first colour, a click on the ground.
PASS: the second hit raised the ground at the spot in the host's game and in the guest's game by the same height,
both games' terrain heights agree around the spot, and the guest's planet colours (cTerrainSphere::GetColors) are the
host's new ones. SPORE_FIX_OFF=space.terra_sculpt must FAIL (the guest's terrain and colours stay).

  SPORE_LANE=5 python3 tools/tests/coop_space_sculpt_tool.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, shot, start

PLANET = '01460035'
W, H = 1440, 1080
SCULPT_TAB, COLOUR_TAB = 226, 186
SCULPT_SLOT = (1108, 966)   # the sculpting palette's first tool (first row)
COLOUR_SLOT = (1121, 966)   # the colouring palette's first colour (first row)
GROUND = (720, 640)


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def num(con, key):
    m = re.search(key + r'=(-?\d+)', sp(con, 'sculpt'))
    return int(m.group(1)) if m else -1


def colours(con):
    m = re.search(r'colours ramp=(-?\d+) a=(-?\d+) b=(-?\d+)', sp(con, 'sculpt'))
    return tuple(int(m.group(i)) for i in (1, 2, 3)) if m else None


def ground(con, p):
    out = con.call('coop.ground %.3f %.3f %.3f' % tuple(p))['out']
    m = re.search(r'ground ([-\d.]+)', out)
    return float(m.group(1)) if m else None


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

    def open_tab(tab):
        # the palette's state decides the tab row (open: H-151, closed: H-51); the main tab first
        st = ph.palette_open()
        y = H - 151 if st else H - 51
        host.call(f'click {W - 426} {y}')
        time.sleep(1)
        st = ph.palette_open()
        y = H - 151 if st else H - 51
        host.call(f'move {W - tab} {y}')
        time.sleep(0.4)
        host.call(f'click {W - tab} {y}')
        time.sleep(1.5)

    def use(tab, slot, key, name):
        """the tool of this tab and slot on the ground; True when the host's game saw the hit (key counter)"""
        for attempt in range(3):
            n0 = num(host, key)
            open_tab(tab)
            host.call(f'move {slot[0]} {slot[1]}')
            time.sleep(0.4)
            host.call(f'click {slot[0]} {slot[1]}')
            time.sleep(1)
            host.call(f'move {GROUND[0]} {GROUND[1]}')
            time.sleep(0.6)
            host.call(f'click {GROUND[0]} {GROUND[1]}')
            for _ in range(10):
                time.sleep(1)
                if num(host, key) > n0:
                    shot(host, f'{name}{attempt}.png')
                    return True
            shot(host, f'{name}{attempt}_miss.png')
        return False

    # ---- sculpting ----
    hit1 = use(SCULPT_TAB, SCULPT_SLOT, 'seen', 'sculpt_a')
    ck(hit1, f'the host\'s sculpting tool hit the planet ({sp(host, "sculpt")})')
    m = re.search(r'last=([-\d.]+),([-\d.]+),([-\d.]+)', sp(host, 'sculpt'))
    pos = [float(m.group(i)) for i in (1, 2, 3)] if m else [0, 0, 0]
    time.sleep(10)  # the terrain effect and the regeneration of the terrain
    b_h, b_g = ground(host, pos), ground(guest, pos)
    print(f'  spot {pos}: before the second hit host {b_h}, guest {b_g}', flush=True)
    hit2 = use(SCULPT_TAB, SCULPT_SLOT, 'seen', 'sculpt_b')
    ck(hit2, 'the host\'s second sculpting hit')
    a_h = a_g = None
    for t in range(8):
        time.sleep(3)
        a_h, a_g = ground(host, pos), ground(guest, pos)
        print(f'  {3 * t + 3:3d} s: host {a_h}, guest {a_g}', flush=True)
        if None not in (a_h, a_g, b_h, b_g) and abs(a_h - b_h) > 0.5 and abs(a_g - a_h) < 0.3:
            break
    print(f'  host {sp(host, "sculpt")}\n  guest {sp(guest, "sculpt")}', flush=True)
    ok = None not in (a_h, a_g, b_h, b_g)
    dh = a_h - b_h if ok else 0
    dg = a_g - b_g if ok else 0
    ck(ok and abs(dh) > 0.5, f'the host\'s second hit changed its terrain at the spot ({b_h} -> {a_h})')
    ck(ok and abs(dg) > 0.5 and abs(dg - dh) < 0.3 * abs(dh) + 0.2,
       f'the guest\'s terrain changed the same way ({b_g} -> {a_g}; host {dh:+.2f}, guest {dg:+.2f})')
    worst = 0.0
    for d in ((0, 0, 0), (8, 0, 0), (0, 8, 0), (0, 0, 8), (-8, 0, 0), (0, -8, 0)):
        q = [pos[i] + d[i] for i in range(3)]
        gh, gg = ground(host, q), ground(guest, q)
        if gh is not None and gg is not None:
            worst = max(worst, abs(gh - gg))
    ck(worst < 0.3, f'the two games\' terrain heights around the spot agree (worst difference {worst:.2f} m)')
    shot(guest, 'sculpt_guest.png')

    # ---- colouring ----
    c0_h, c0_g = colours(host), colours(guest)
    hitc = use(COLOUR_TAB, COLOUR_SLOT, 'own_hits', 'colour')
    time.sleep(3)
    c1_h, c1_g = colours(host), colours(guest)
    print(f'  colours: host {c0_h} -> {c1_h}, guest {c0_g} -> {c1_g}', flush=True)
    ck(hitc and c1_h != c0_h, f'the host\'s colouring tool changed its planet colours ({c0_h} -> {c1_h})')
    ck(c1_g == c1_h, f'the guest\'s planet colours are the host\'s new ones ({c1_g} / {c1_h})')
    shot(guest, 'colour_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
