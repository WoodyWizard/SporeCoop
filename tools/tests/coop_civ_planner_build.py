#!/usr/bin/env python3
"""Civilization: a building the guest places in its city planner is in the host's game (BUG-0189).

The planner places a building by a mouse-down on a palette tile and a drag onto a plot (docs/re/civ.md
#civ-planner-palette): cCommunityEditor HandleMessage -> AddBuilding at 0xD14197; the guest's mod sees that
return address (SporeCoop::CityPlanner_AddBuilding_Ret) and sends the building to the host (civ.cpp
h_AddBuilding, CMD_BUILD), which builds it in its game (the guest already paid with the shared money).

1. Two games on civ_npc. Test setup: the player civilization of a new Civ game has no house design yet
   (`coop.civ design 0` key 0: its palette tile says "Design a new house!" and does not drag, see
   docs/re/civ.md#civ-planner-palette-runtime); both games get the house design the NPC cities of the save use
   (`coop.civ bdesign <pol> house <key>`).
2. The guest opens the city planner of its city with the city menu (real clicks, as play_civ.py), holds the
   house tile and drags it onto a free plot (the plot's screen point, `coop.civ slots`), closes with the green check.
3. PASS: the host's game has a new house in that city at the plot (the game's own building list), the shared
   money fell by the same amount on both games (the house's price), the guest's building list is the host's.
SPORE_FIX_OFF=civ.planner_send must FAIL (the guest's placement stays in the guest's game).

  SPORE_LANE=3 python3 tools/tests/coop_civ_planner_build.py
"""
import random
import re
import time

from play_civ import BTN_PLANNER, PLANNER_DONE, PLANNER_NAME, Player, civ_state
from twoinst import Checks, shot, start

HOUSE = 0x18EB106
TILE_HOUSE = (123, 385)  # the planner palette's house tile (1440x1080): City Hall, house, factory, fun, turret


def buildings(con, ci):
    out = []
    for ln in con.call(f'coop.civ buildings {ci}')['out'].splitlines():
        m = re.match(r'building (\S+) noun=([0-9A-F]+) state=(\d+) hp=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln)
        if m:
            out.append((int(m.group(2), 16), tuple(float(m.group(i)) for i in (4, 5, 6))))
    return out


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def slots(con, ci):
    out = []
    for ln in con.call(f'coop.civ slots {ci}')['out'].splitlines():
        m = re.match(r'slot (\d+) (busy|free) (\S+) (\S+) (\S+) (-?\d+) (-?\d+)', ln)
        if m:
            out.append(dict(k=int(m.group(1)), free=m.group(2) == 'free', pos=tuple(float(m.group(i)) for i in (3, 4, 5)),
                            at=(int(m.group(6)), int(m.group(7)))))
    return out


def d(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def drag(con, sx, sy, tx, ty):
    """hover, hold the button on the tile, move in small steps, let go on the plot"""
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
    time.sleep(2)


def close_enough(a, b):
    return len(a) == len(b) and all(any(n == m and d(p, q) < 3 for m, q in b) for n, p in a)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    gc, _, _ = civ_state(guest)
    mine = sorted(i for i, c in gc.items() if c['owner'] in (1, 253) and c['player'])
    ck(mine, f'the guest has a city ({mine})')
    if not mine:
        return ck.result()
    ci = mine[0]
    pol = gc[ci]['pol']
    # 1. the house design (test setup): the one the NPC cities of this save use
    keys = re.findall(r'noun=018EB106 model=([0-9A-F]{8})/', host.call('coop.civ bmodels')['out'])
    key = keys[0] if keys else '067CEDDC'
    for con, who in ((host, 'host'), (guest, 'guest')):
        print(f'  {who}: before {con.call(f"coop.civ design {ci} house")["out"].splitlines()[0]}', flush=True)
        print(f'  {who}: bdesign {pol} house {key}:', con.call(f'coop.civ bdesign {pol} house {key}')['out'].strip(), flush=True)
    b0h, b0g = buildings(host, ci), buildings(guest, ci)
    m0h, m0g = money(host), money(guest)
    print(f'  before: host {len(b0h)} buildings ${m0h}, guest {len(b0g)} buildings ${m0g}', flush=True)
    # 2. the planner
    p = Player('guest', guest, 1, random.Random(5), [])
    opened = False
    for _ in range(3):
        if p.open_city(gc[ci]['pos']) and p.click_button(BTN_PLANNER) and p.wait_ui(PLANNER_NAME, True, 8):
            opened = True
            break
        guest.call('key esc')
        time.sleep(1)
    ck(opened, f'the guest opened the city planner of city {ci}')
    if not opened:
        ck.faults(host=host, guest=guest)
        return ck.result()
    time.sleep(2)
    free = [s for s in slots(guest, ci) if s['free'] and 400 < s['at'][0] < 1300 and 150 < s['at'][1] < 950]
    ck(free, f'a free plot on the guest\'s planner screen ({len(free)})')
    s = free[0] if free else None
    if s:
        print(f'  guest drags the house tile onto plot {s["k"]} at {s["at"]}', flush=True)
        drag(guest, TILE_HOUSE[0], TILE_HOUSE[1], s['at'][0], s['at'][1])
    paid = None
    t0 = time.time()
    while s and time.time() - t0 < 6 and paid is None:
        mg = money(guest)
        paid = m0g - mg if mg is not None and m0g is not None and mg < m0g else None
        time.sleep(0.5)
    shot(guest, 'planner_build_guest.png')
    print(f'  guest: money {m0g} -> {money(guest)} in the planner; planner: '
          f'{guest.call("coop.civ planner")["out"].strip().replace(chr(10), " | ")[:300]}', flush=True)
    ck(paid is not None, f'the guest\'s planner took the house\'s price (${paid})')
    for _ in range(3):
        guest.call(f'click {PLANNER_DONE[0]} {PLANNER_DONE[1]}')
        if p.wait_ui(PLANNER_NAME, False, 5):
            break
    time.sleep(2)
    new = [b for b in buildings(guest, ci) if not any(b[0] == n and d(b[1], q) < 1 for n, q in b0g)]
    ck(any(n == HOUSE for n, _ in new), f'after the planner the guest\'s game has a new house in city {ci} ({new})')
    if paid is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    at = [q for n, q in new if n == HOUSE][0] if any(n == HOUSE for n, _ in new) else None
    # 3. the host's game
    t0 = time.time()
    got = None
    while time.time() - t0 < 25 and not got:
        got = [b for b in buildings(host, ci) if b[0] == HOUSE and (at is None or d(b[1], at) < 6) and
               not any(b[0] == n and d(b[1], q) < 1 for n, q in b0h)]
        time.sleep(1)
    bh, bg = buildings(host, ci), buildings(guest, ci)
    print(f'  after {time.time() - t0:.0f} s: host {len(bh)} buildings, guest {len(bg)}', flush=True)
    shot(host, 'planner_build_host.png')
    ck(bool(got), f'the host\'s game has the guest\'s new house in city {ci} at the plot ({got[0][1] if got else None} ~ {at})')
    time.sleep(3)
    m1h, m1g = money(host), money(guest)
    dh = m0h - m1h if m0h is not None and m1h is not None else None
    dg = m0g - m1g if m0g is not None and m1g is not None else None
    print(f'  money: host {m0h} -> {m1h}, guest {m0g} -> {m1g}', flush=True)
    ck(dh is not None and dh > 0 and dh == dg, f'the shared money fell by the house\'s price on both games (host -{dh}, guest -{dg})')
    ok = None
    t0 = time.time()
    while time.time() - t0 < 15 and not ok:
        ok = close_enough(buildings(guest, ci), buildings(host, ci))
        time.sleep(1)
    ck(ok, f'the guest\'s building list of city {ci} is the host\'s ({len(buildings(guest, ci))} / {len(buildings(host, ci))})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
