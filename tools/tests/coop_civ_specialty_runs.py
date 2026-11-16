#!/usr/bin/env python3
"""Civilization: the guest's world goes on while its specialty question is up (BUG-1175).

Since 0.7.14 (BUG-0982) the specialty question of a city a guest bought or captured comes up on that guest's screen:
the game's own box, which also pauses the game (Pause 0x4BF38A7). The host's world does not stand for it: a city the
host's game founds meanwhile (its next site, as after every purchase or capture) waited on the guest for the guest's
game time (the founding's appearance is a timed callback), and with it every vehicle of the new nation: the guest saw
neither until it answered (civ_owners_reload: "v: 2 missing on the guest" while the box was up, 16 s).

1. Two games on civ_npc. The guest buys the NPC city nearest the capital whose specialty is not Economic
   (`coop.civ buycity`: the game's cCity buy), so the game asks the buyer; the box is on the guest's screen.
2. With the box still up, the host's city manager founds the next site (the hooked SpawnNextCity, broadcast).
3. Within 20 s, the box still up, the guest's game has the new city (at the host's position) and mirrors of the new
   nation's vehicles (as many as the host's game has).
   SPORE_FIX_OFF=civ.specialty_runs: the guest's game stands under the box: no new city, no vehicle: FAIL.
4. The guest answers Economic: the city has it in both games and the guest's game time is not left standing.

  SPORE_LANE=6 python3 tools/tests/coop_civ_specialty_runs.py
"""
import re
import time

import uifind
from coop_civ import dist
from play_civ import SPECIALTY, answer_specialty
from twoinst import Checks, keyframe, start

CITY = re.compile(r'city (-?\d+) \S+ pol=(\w+)( \(player\))? owner=(-?\d+) buildings=\d+ '
                  r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*? spec=(-?\d+)')
VEH = re.compile(r'\s+veh #(\d+) \S+ pol=(\w+) ')


def state(con):
    """[city dict] and [(net id, pol)] of the vehicles this game has (a guest: its bound mirrors have an id)"""
    cities, veh = [], []
    for line in con.call('coop.civ')['out'].splitlines():
        m = CITY.match(line)
        if m:
            cities.append(dict(i=int(m.group(1)), pol=m.group(2), player=bool(m.group(3)), owner=int(m.group(4)),
                               pos=tuple(float(m.group(k)) for k in (5, 6, 7)), spec=int(m.group(8))))
        m = VEH.match(line)
        if m:
            veh.append((int(m.group(1)), m.group(2)))
    return cities, veh


def near(cities, pos):
    return next((c for c in cities if dist(c['pos'], pos) < 10.0), None)


def box_up(con):
    """the specialty box's buttons are on this screen (not the HUD's tab labels)"""
    for cap in SPECIALTY:
        b = uifind.shown(con, cap)
        if b and 600 < b[0] < 1100 and 500 < b[1] < 750:
            return True
    return False


def pauses(con):
    return con.call('coop.pauses')['out'].strip()


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = state(host)
    cap = [c for c in hc if c['player']]
    npc = [c for c in hc if not c['player'] and c['spec'] != 2]
    if not ck(cap and npc, f'a player city and an NPC city that is not Economic ({len(npc)})'):
        return ck.result()
    tgt = min(npc, key=lambda c: dist(c['pos'], cap[0]['pos']))
    print(f"  guest buys city {tgt['i']} (spec {tgt['spec']}):", guest.call(f"coop.civ buycity {tgt['i']} 500")['out'].strip(),
          flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not box_up(guest):
        time.sleep(0.5)
    up = box_up(guest)
    print(f'  the guest\'s box after {time.time() - t0:.0f} s: {up}; guest {pauses(guest)}', flush=True)
    if not ck(up, "the specialty question is on the guest's screen"):
        return ck.result()

    # 2. the host's game founds its next site while the guest's box is up
    hc, hv = state(host)
    old = {c['pos'] for c in hc}
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    if not ck(m, "the host's city expansion manager"):
        return ck.result()
    print('  host founds the next site:',
          host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')['out'].strip(), flush=True)

    # 3. the guest's game shows the new city and its nation's vehicles, the box still up
    new = None
    want = got = 0
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(1)
        hc, hv = state(host)
        gc, gv = state(guest)
        new = next((c for c in hc if c['pos'] not in old), None)
        if not new:
            continue
        want = sum(1 for _, p in hv if p == new['pol'])
        got = sum(1 for i, p in gv if p == new['pol'] and i)
        if near(gc, new['pos']) and want and got >= want:
            break
    still = box_up(guest)
    print(f'  after {time.time() - t0:.0f} s: host {len(hc)} cities, new {new and (new["pol"], new["pos"])}; '
          f'guest {len(gc)} cities, its copy {new and near(gc, new["pos"])}; the new nation\'s vehicles host {want} '
          f'guest {got}; box up {still}; guest {pauses(guest)}', flush=True)
    print('  guest', [l for l in guest.call('coop.civ tables')['out'].splitlines() if l.startswith('specialty')], flush=True)
    keyframe('question', "the specialty box on the guest's screen while the world goes on behind it")
    ck(new, "the host's game founded a new city")
    ck(still, "the guest's specialty box is still up (the check runs under the question)")
    ck(new and near(gc, new['pos']), "the guest's game shows the host's new city while its question is up")
    ck(want and got >= want, f"the guest's game has the new nation's vehicles while its question is up "
                             f"(host {want}, guest {got})")

    # 4. the guest answers like a player: the city's specialty in both games, the guest's game time goes on
    gq = answer_specialty(guest, 5, prefer='Economic')
    time.sleep(3)
    hc, _ = state(host)
    gc, _ = state(guest)
    h, g = near(hc, tgt['pos']), near(gc, tgt['pos'])
    gp = pauses(guest)
    print(f'  answered {gq}: host {h and h["spec"]}, guest {g and g["spec"]}; guest {gp}', flush=True)
    ck(gq == 'Economic', f'the guest answered Economic ({gq})')
    ck(h and h['spec'] == 2 and g and g['spec'] == 2,
       f"the guest's answer is the city's specialty in both games (host {h and h['spec']}, guest {g and g['spec']})")
    ck('paused=0' in gp, f"the guest's game time is not left standing after the answer ({gp})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
