#!/usr/bin/env python3
"""Civilization: the guest's military vehicles attack an NPC city and take it down on the host (BUG-0648).

Player report 2026-11-06 (0.7.8): "as guest with military vehicles I can't shoot at the enemy city centre", "the
guest's vehicles can't capture cities but trigger them". The guest's attack is the game's own: a click on the
city opens its menu, its crosshair orders the selected vehicles (SetOrder type 2 with the city: civ.cpp h_SetOrder
sends CMD_ORDER with the host's city index; the host's game gives its vehicles that order).

1. Two games on civ_npc; the host spawns two military land vehicles and gives them to the guest (`coop.give`).
2. The guest selects them, clicks the nearest NPC city and the crosshair of its menu (real clicks).
3. For up to 120 s, on the host: the vehicles' order (the game's order list), the city's hit points (the
   game's building hit points), its capture meter.
PASS: the guest's order reached the host's vehicles as an attack of that city, the city's hit points on the
host fall and its capture meter for the player civilization rises, and the guest's game shows the same.
(No twin FAIL switch: the report did not reproduce on 0.7.10, docs/tickets/BUG-0648.)

  SPORE_LANE=4 python3 tools/tests/coop_civ_guest_attack.py
"""
import random
import re
import time

from play_civ import BTN_ATTACK, Player, civ_state, dist
from twoinst import Checks, start



def city_line(con, i):
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(rf'city {i} \S+ pol=\w+ owner=-?\d+ buildings=\d+ pos=.* capture=(\d+)/(\w+) .* hp=(\d+)', ln)
        if m:
            return int(m.group(1)), m.group(2), int(m.group(3))
    return None


def veh_orders(con, ids):
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) .* owner=(-?\d+) .* order=(-?\d+)', ln)
        if m and int(m.group(1)) in ids:
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    return out


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    before = set(veh_orders(host, range(1, 200)))
    for _ in range(2):
        print('  host:', host.call('coop.civ spawnveh 0 0 0')['out'].strip(), flush=True)
    time.sleep(2)
    ids = sorted(set(veh_orders(host, range(1, 200))) - before)
    for i in ids:
        host.call(f'coop.civ select {i} 1')
    print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
    time.sleep(3)
    gv = veh_orders(guest, ids)
    ck(len(ids) == 2 and all(gv.get(i, (-1,))[0] == 1 for i in ids), f'the guest owns the two vehicles ({gv})')
    cities, veh, _ = civ_state(guest)
    mine = [veh[i]['pos'] for i in ids if i in veh]
    npc = [i for i, c in cities.items() if not c['player']]
    tgt = min(npc, key=lambda i: dist(cities[i]['pos'], mine[0])) if npc and mine else None
    ck(tgt is not None, f'an NPC city to attack ({tgt})')
    if tgt is None:
        return ck.result()
    h0 = city_line(host, tgt)
    p = Player('guest', guest, 1, random.Random(1), [])
    sp = p.look_at(cities[tgt]['pos'])
    for i in ids:
        guest.call(f'coop.civ select {i} 1')
    ck(sp is not None, f'the city is on the guest\'s screen ({sp})')
    if sp:
        guest.call(f'click {int(sp[0])} {int(sp[1])}')  # the city menu (as play_civ: a click, then the crosshair)
        time.sleep(0.8)
        ck(p.click_button(BTN_ATTACK), 'the guest\'s city menu has the attack button')
        time.sleep(1)
    lo = re.search(r'last_order=(-?\d+)/(\d+)/(\d+)', guest.call('coop.civ tables')['out'])
    print('  guest last order (type/target kind/index):', lo and lo.group(0), flush=True)
    ck(lo and lo.group(1) == '2' and lo.group(2) == '2' and int(lo.group(3)) == tgt,
       f'the guest\'s game ordered the attack of city {tgt} ({lo and lo.group(0)})')
    t0, ho, h, g = time.time(), {}, h0, None
    while time.time() - t0 < 120:
        ho = veh_orders(host, ids)
        h, g = city_line(host, tgt), city_line(guest, tgt)
        if h and h0 and h[2] < h0[2] - 200 and h[0] > 0:
            break
        time.sleep(3)
    print(f'  host vehicles {ho} | city {tgt} host {h0} -> {h} | guest {g}', flush=True)
    ck(any(o == 2 for _, o in ho.values()) or (h and h0 and h[2] < h0[2]),
       f'the host\'s vehicles took the guest\'s attack order ({ho})')
    ck(h and h0 and h[2] < h0[2] - 200, f'the city\'s hit points fall on the host ({h0 and h0[2]} -> {h and h[2]})')
    ck(h and h[0] > 0 and h[1] == '800021CA', f'the player civilization\'s capture meter rises ({h and h[:2]})')
    ck(g and h and abs(g[2] - h[2]) < 400, f'the guest\'s game shows the city\'s hit points ({g and g[2]} / {h and h[2]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
