#!/usr/bin/env python3
"""Space: a building a guest buys in a colony's city planner is in the host's game too (BUG-0751).

In Space a colony's city planner is the same cCommunityEditor as in Civ (docs/re/civ.md#civ-planner-palette), but the
civ module's planner relay (civ.cpp h_AddBuilding, CMD_BUILD) works by the host's Civ city index, which Space cities
do not have: the guest's building stayed in the guest's game. Now the guest sends it with its planet and the city's
position (CMD_SPACE_BUILD) and the host builds it in its city there when its ship is on the same planet.

1. Two games on space_empire, both land on the home planet 00460035 (9 full cities of the player's empire).
2. Test setup: one house of the city nearest to the guest is destroyed in both games (`coop.civ unbuild`, the game's
   cCity::DestroyBuilding): a free plot.
3. Real input on the guest: it flies over that city (keys), clicks it (the planner opens), drags the house tile
   onto the free plot (its screen point from `coop.civ slots`), closes with the green check.
PASS: the guest's planner took the price, the host's game has a new house in that city (the game's building list),
the shared money fell by the same amount on both. SPORE_FIX_OFF=civ.space_planner_send must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_space_planner_build.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, shot, start

W, H = 1440, 1080
HOME = '00460035'
HOUSE = 'noun=018EB106'
TILE_HOUSE = (123, 385)  # the planner palette's house tile (1440x1080), as in Civ
DONE = (1412, 1054)  # the planner's green check


def cities(con):
    out = con.call('coop.space allies cities')['out'].splitlines()
    return [(float(m.group(1)), m.group(2) == '1', float(m.group(3)), float(m.group(4)), i)
            for i, ln in enumerate(out) for m in [re.search(r'dist (-?[\d.]+) screen (\d) (-?[\d.]+) (-?[\d.]+)', ln)] if m]


def houses(con, ci):
    bl = con.call(f'coop.civ buildings s{ci}')['out'].splitlines()
    return len([ln for ln in bl if ln.startswith('building ')]), len([ln for ln in bl if HOUSE in ln]), bl


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def city_positions(con):
    return {int(m.group(1)): tuple(float(m.group(i)) for i in (2, 3, 4))
            for m in re.finditer(r's(\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.civ scities')['out'])}


def drag(con, sx, sy, tx, ty):
    con.call(f'move {sx - 10} {sy - 10}')
    time.sleep(0.5)
    con.call(f'move {sx} {sy}')
    time.sleep(1.0)
    con.call(f'button {sx} {sy} down')
    time.sleep(0.5)
    for i in range(1, 31):
        con.call(f'move {sx + (tx - sx) * i // 30} {sy + (ty - sy) * i // 30}')
        time.sleep(0.05)
    time.sleep(0.8)
    con.call(f'button {tx} {ty} up')
    time.sleep(3)


def fly_over_city(con, secs=60):
    """the nearest distance reached (the ship flies at a height: 260-300 m is over the city)"""
    t0 = time.time()
    best = None
    while time.time() - t0 < secs:
        cs = cities(con)
        if not cs:
            time.sleep(1)
            continue
        d, on, x, y, i = min(cs)
        best = d if best is None else min(best, d)
        if d < 260:
            return d
        keys = ['0x57']
        if on and d < 900:
            dx, dy = x - W // 2, y - H // 2
            keys = ([] if abs(dy) <= 30 else ['0x57' if dy < 0 else '0x53']) + \
                   ([] if abs(dx) <= 30 else ['0x44' if dx > 0 else '0x41'])
            keys = keys or ['0x57']
        for k in keys:
            con.call(f'key {k} down')
        time.sleep(1.2 if d > 900 else 0.5)
        for k in keys:
            con.call(f'key {k} up')
    return best


def main():
    host, guest = start('space_empire')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    for p in (ph, pg):
        p.land_retry(HOME)
    ck(ph.ctx() == 'planet' and ph.place() == pg.place(), f'both on the home planet ({ph.place()}, {pg.place()})')
    time.sleep(4)
    d = fly_over_city(guest)
    print(f'  the guest flew toward a city of its empire (nearest {d} m)', flush=True)  # the planner click below is the check
    time.sleep(2)
    _, _, _, _, gi = min(cities(guest))
    gp = city_positions(guest).get(gi)
    hp = city_positions(host)
    hi = next((k for k, p in hp.items() if gp and sum((a - b) ** 2 for a, b in zip(p, gp)) < 100), None)
    ck(gp is not None and hi is not None, f'the same city in both games (guest s{gi} {gp}, host s{hi})')
    if hi is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # test setup: a free plot (a house destroyed by the game's own call in both games)
    for con, ci, who in ((guest, gi, 'guest'), (host, hi, 'host')):
        bl = houses(con, ci)[2]
        k = next((n for n, ln in enumerate(bl) if HOUSE in ln), None)
        print(f'  {who}: {con.call(f"coop.civ unbuild s{ci} {k}")["out"]}', flush=True)
    time.sleep(2)
    nh0, hh0, _ = houses(host, hi)
    ng0, hg0, _ = houses(guest, gi)
    mh0, mg0 = money(host), money(guest)
    print(f'  before: host {nh0} buildings ({hh0} houses), guest {ng0} ({hg0}); money host {mh0} guest {mg0}', flush=True)
    # the planner by a click on the city
    opened = False
    for _ in range(3):
        _, on, x, y, _ = min(cities(guest))
        guest.call(f'move {int(x)} {int(y)}')
        time.sleep(0.8)
        guest.call(f'click {int(x)} {int(y)}')
        time.sleep(6)
        if 'city=00000000' not in guest.call('coop.civ planner')['out']:
            opened = True
            break
    shot(guest, 'space_planner_open.png')
    ck(opened, 'the guest opened the city planner by a click on the city')
    free = []
    for ln in guest.call(f'coop.civ slots s{gi}')['out'].splitlines():
        m = re.match(r'slot (\d+) free \S+ \S+ \S+ (-?\d+) (-?\d+)', ln)
        if m and 150 < int(m.group(2)) < 1300 and 80 < int(m.group(3)) < 1000:
            free.append((int(m.group(2)), int(m.group(3))))
    ck(free, f'a free plot on the guest\'s planner screen ({free})')
    if opened and free:
        drag(guest, TILE_HOUSE[0], TILE_HOUSE[1], free[0][0], free[0][1])
    shot(guest, 'space_planner_built.png')
    mg1 = money(guest)
    paid = mg0 - mg1 if mg0 is not None and mg1 is not None else None
    ck(paid and paid > 0, f'the guest\'s planner took the house\'s price (${paid})')
    for _ in range(3):
        guest.call(f'click {DONE[0]} {DONE[1]}')
        time.sleep(3)
        if 'city=00000000' in guest.call('coop.civ planner')['out']:
            break
    ng1, hg1, _ = houses(guest, gi)
    ck(hg1 == hg0 + 1, f'the guest\'s game has the new house ({hg0} -> {hg1} houses)')
    got = None
    t0 = time.time()
    while time.time() - t0 < 20 and not got:
        nh1, hh1, _ = houses(host, hi)
        got = hh1 == hh0 + 1
        time.sleep(1)
    shot(host, 'space_planner_host.png')
    print(f'  host: {nh0} -> {nh1} buildings, {hh0} -> {hh1} houses; '
          f'{host.call("coop.civ")["out"].splitlines()[-1] if host.call("coop.civ")["out"] else ""}', flush=True)
    ck(got, f'the host\'s game has the guest\'s new house in that city ({hh0} -> {hh1} houses)')
    time.sleep(3)
    mh1, mg2 = money(host), money(guest)
    ck(mh0 is not None and mh1 is not None and paid and abs((mh0 - mh1) - paid) < 1 and abs(mh1 - mg2) < 1,
       f'the shared money fell by the price on both games (host {mh0} -> {mh1}, guest {mg0} -> {mg2})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
