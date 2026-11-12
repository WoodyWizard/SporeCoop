#!/usr/bin/env python3
"""Civilization: the players' city owners stay the same when the host loads its world again (BUG-0950).

The mod's owner table of the players' cities lived only in the host's memory and every world change cleared it: a
host that saved and loaded its Civ world again dealt every city out anew in turns, so a city the host bought or
captured became the guest's (and the other way round).

1. Two games on civ_npc. The host buys the NPC city nearest its capital, the guest the next one (`coop.civ buycity
   <city> 500`: the game's cCity buy). On both games each bought city is the player civilization's, the host's (0)
   and the guest's (1).
2. The host quits to the galaxy menu (the game saves) and loads the same save again (`coop.menu`, `coop.load` of
   its slot); the guest follows into the reloaded world.
3. On both games each city (found by its position) is still the player civilization's and has the owner it had
   before the load.
   SPORE_FIX_OFF=civ.owners_kept: the cities are dealt anew (the host's purchase becomes the guest's): FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_civ_owners_reload.py
"""
import re
import time

import fixtures
from coop_civ import civ, dist
from twoinst import Checks, keyframe, start, wait_guest_in


def mode(con):
    try:
        return con.call('game.mode')['out']
    except OSError:
        return ''


def wait(pred, timeout, step=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def by_pos(cities, pos):
    """the city nearest pos (within 10 m), or None"""
    near = [c for c in cities.values() if dist(c['pos'], pos) < 10.0]
    return min(near, key=lambda c: dist(c['pos'], pos)) if near else None


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and len(npc) >= 2, f'a player city {cap} and two NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    buys = [(npc[0], host, 0), (npc[1], guest, 1)]
    want = {}  # position -> owner
    for city, con, who in buys:
        pos = hc[city]['pos']
        print(f'  player {who} buys city {city}:', con.call(f'coop.civ buycity {city} 500')['out'].strip(), flush=True)
        got = wait(lambda: all((c := by_pos(civ(x)[0], pos)) and c['player'] and c['owner'] == who for x in (host, guest)), 20)
        ck(got, f'city {city} is the player civilization\'s and player {who}\'s on both games')
        want[pos] = who
    hc, _ = civ(host)
    for c in hc.values():
        if c['player'] and c['pos'] not in want:
            want[c['pos']] = c['owner']
    print('  before the load (pos: owner):', {tuple(round(x) for x in p): o for p, o in want.items()}, flush=True)
    keyframe('bought', 'the two bought cities in the player colours, one per player')
    time.sleep(3)
    tables = host.call('coop.civ tables')['out']
    print('  host', next((l for l in tables.splitlines() if l.startswith('kept:')), '-'), flush=True)
    m = re.search(r'key=(\S+)', tables)
    slot = re.search(r'\\(game\d+)\\[^\\]+$', m.group(1), re.I) if m else None
    planet = fixtures.planet_of('civ_npc').split('/')[1]
    if not ck(slot, f'the host\'s save slot is known ({m.group(1) if m else None})'):
        return ck.result()
    slot = slot.group(1).capitalize()

    # 2. the host saves, goes to the galaxy menu and loads the same save again
    print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in mode(host), 90), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print(f'  host load {slot} {planet}:', host.call(f'coop.load {slot} {planet}')['out'].strip(), flush=True)
    ck(wait(lambda: 'Civ' in mode(host) and host.call('coop.avatar')['ok'], 180, 2), 'the host plays its Civ world again')
    ck(wait_guest_in(host, guest, 180), 'the guest is in the reloaded world')
    time.sleep(8)

    # 3. the same owners on both games
    hc, _ = civ(host)
    gc, _ = civ(guest)
    print('  host', next((l for l in host.call('coop.civ tables')['out'].splitlines() if l.startswith('kept:')), '-'),
          flush=True)
    for pos, who in want.items():
        h, g = by_pos(hc, pos), by_pos(gc, pos)
        print(f'  city at {tuple(round(x) for x in pos)}: before {who}, host {h and (h["player"], h["owner"])}, '
              f'guest {g and (g["player"], g["owner"])}', flush=True)
        ck(h and h['player'], f'the host\'s save kept the city at {tuple(round(x) for x in pos)} for the player civilization')
        ck(h and h['owner'] == who and g and g['owner'] == who,
           f'the city at {tuple(round(x) for x in pos)} is still player {who}\'s on both games '
           f'(host {h and h["owner"]}, guest {g and g["owner"]})')
    keyframe('reloaded', 'after the host\'s load: the same cities in the same players\' colours')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
