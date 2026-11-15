#!/usr/bin/env python3
"""Civilization: a city the guest's vehicles take is the guest's, though a host vehicle stands nearer (BUG-0775).

Discord 2026-11-09 ("when a friend is attacking an enemy, their settlement is captured, but the host's is not; the
same in reverse"): the host's capture hook gave a captured city to the player whose vehicle was NEAREST the city,
not to the player whose vehicles took it.

1. Two games on civ_npc; the host spawns three military land vehicles and gives them to the guest (`coop.give`), and
   puts the fixture's own vehicle (the host's) 20 m from the nearest NPC city's hall (`coop.civ vput`).
2. The other buildings of that city are cut to 5 hp on the host (`coop.civ bhp`) and the guest's vehicles kept at
   their health (`coop.civ vhp`) so the siege ends in minutes.
3. The guest's real attack: it selects its vehicles, clicks the city and the crosshair of the city menu.
4. Up to 5 min: the game's own siege (the vehicles' shots, the buildings falling, the hall's capture meter) takes the
   city. Then on both games: the city's political id is the player civilization's and its owner is the guest (1).
   SPORE_FIX_OFF=civ.capture_credit: the city is the host's (owner 0, its vehicle was nearest): FAIL.
"""
import random
import re
import time

from play_civ import BTN_ATTACK, Player, civ_state, frame_city
from coop_civ import civ, dist
from twoinst import Checks, keyframe, start


def veh_ids(con):
    out = set()
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) ', ln)
        if m and m.group(1) != '0':
            out.add(int(m.group(1)))
    return out


def veh_hp(con, i):
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(rf'\s+veh #{i} .* hp=([-\d.]+) ', ln)
        if m:
            return float(m.group(1))
    return None


def attack(guest, p, ids, pos):
    sp = p.look_at(pos)
    for i in ids:
        guest.call(f'coop.civ select {i} 1')
    if not sp:
        return False
    guest.call(f'click {int(sp[0])} {int(sp[1])}')
    time.sleep(0.8)
    ok = p.click_button(BTN_ATTACK)
    time.sleep(1)
    lo = re.search(r'last_order=(-?\d+)/(\d+)/(\d+)', guest.call('coop.civ tables')['out'])
    print('  guest last order (type/target kind/index):', lo and lo.group(0), flush=True)
    return ok and lo and lo.group(1) == '2'


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return ck.result()
    tgt = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    tpos = hc[tgt]['pos']
    before = veh_ids(host)
    for _ in range(3):
        print('  host:', host.call(f'coop.civ spawnveh {cap[0]} 0 0')['out'].strip(), flush=True)
    time.sleep(2)
    ids = sorted(veh_ids(host) - before)
    for i in ids:
        host.call(f'coop.civ select {i} 1')
    print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
    time.sleep(2)
    _, hv = civ(host)
    mine = sorted(i for i in before if i in hv and hv[i]['owner'] == 0)[:1]  # the fixture's own vehicle, the host's
    full = {i: veh_hp(host, i) for i in ids}
    ck(len(ids) == 3 and mine, f"the guest's vehicles {ids} (hp {full}), the host's {mine}")
    cp = hc[cap[0]]['pos']
    d = dist(tpos, cp) or 1
    near = tuple(t + (c - t) / d * 20 for t, c in zip(tpos, cp))  # 20 m from the hall, on the side of the capital
    if mine:  # (a move order ends ~100 m off an enemy city: the vehicle is put there)
        print('  host:', host.call('coop.civ vput %d %.1f %.1f %.1f' % ((mine[0],) + near))['out'].strip(), flush=True)
    print('  host:', host.call(f'coop.civ bhp {tgt} 5')['out'].strip(), flush=True)
    time.sleep(3)
    p = Player('guest', guest, 1, random.Random(1), [])
    ck(attack(guest, p, ids, tpos), f"the guest's game ordered the attack of city {tgt}")
    t0, last, h = time.time(), time.time(), {}
    while time.time() - t0 < 300:
        h, hv = civ(host)
        if h.get(tgt, {}).get('player'):
            break
        if mine:
            host.call('coop.civ vput %d %.1f %.1f %.1f' % ((mine[0],) + near))
        for i in ids:  # the besiegers live through it (the city's defence would end them before the capture)
            if full.get(i):
                host.call(f'coop.civ vhp {i} {full[i]:.0f}')
        if time.time() - last > 45:  # the vehicles turned to other targets: the order again
            last = time.time()
            host.call(f'coop.civ bhp {tgt} 5')
            attack(guest, p, ids, tpos)
        ln = [x for x in host.call('coop.civ')['out'].splitlines() if x.startswith(f'city {tgt} ')]
        hd = {i: round(dist(hv[i]['pos'], tpos)) for i in ids + mine if i in hv}
        print(f'  {time.time() - t0:4.0f} s: {ln[0][:200] if ln else "?"}; vehicles to the hall (m) {hd}', flush=True)
        time.sleep(5)
    time.sleep(4)
    h, hv = civ(host)
    g, _ = civ(guest)
    hd = {i: round(dist(hv[i]['pos'], tpos)) for i in ids + mine if i in hv}
    print(f'  city {tgt}: host {h.get(tgt)} | guest {g.get(tgt)}; vehicles to the hall (m) {hd}', flush=True)
    print('  host', host.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    ck(h.get(tgt, {}).get('player'), f"the guest's vehicles took city {tgt} in the host's game")
    ck(g.get(tgt, {}).get('player'), f"the guest's game shows city {tgt} as the player civilization's")
    ck(h.get(tgt, {}).get('owner') == 1 and g.get(tgt, {}).get('owner') == 1,
       f"the city is the guest's in both games (host {h.get(tgt, {}).get('owner')}, guest {g.get(tgt, {}).get('owner')})")
    frame_city({'host': host, 'guest': guest}, tpos)  # BUG-0981: the city in view, not the capital
    keyframe('captured', "the city the guest's vehicles took, in the player's colours on both screens")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
