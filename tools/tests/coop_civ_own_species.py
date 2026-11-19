#!/usr/bin/env python3
"""Civilization: the citizens of a city are of its owner's species (WP-C1 of docs/plan-0.7.16.md, BUG-1273).

A city's citizens are the game's pool of the city near the camera; CityCitizens_Sync gives each the species
City_Species answers (the civilization's key), so before 0.7.16 every city of the players was the host's species.
civ.own_species hooks City_Species: a city a guest owns answers the guest's Civ species (species::key(guest, civ) of
its species line, docs/01-architecture.md#arch-species-line: its Civ outfit, else its tribe line), on every game; the shared capital is its founder's (the host's), a captured city its captor's
(docs/re/civ.md#civ-own-citizens).

1. Two games on civ_npc (the players' capital only, shared; NPC cities). The guest picks its own creature (its newest,
   `coop.creations choose`, as in the co-op window) and sets a Civ outfit through its species line (`coop.species test
   civ next`: another own creature as the stand-in for the Civ outfitter's result, WP-C3); both games know the guest's
   Civ line (the outfit's shared key 0x7B......, `coop.civ citizens` and `coop.species key 1 civ` agree) and it is not
   the civilization's species.
2. Both cameras at the capital: its citizens (the game's own objects, their species key +0xB28) are the civilization's
   species on both games.
3. Both cameras at the nearest NPC city (its citizens there: the NPC species), the host's game captures it for the guest
   (`coop.civ siege <city> 1 150 pc`: the guest's capture damage). PASS: within 30 s every citizen of that city is the
   guest's species on both games (the owner change resynced the pool); key frames of the city (a close-up of one
   citizen, the same camera on both games).
4. The host saves with the guest's citizens in its world: no record of its newest save names the guest's key.
5. Both cameras back at the capital: its citizens are still the civilization's species (the founder's) on both games;
   key frames of the capital.

SPORE_FIX_OFF=civ.own_species: step 3 FAILs (the guest's city keeps the civilization's species).
(SPORE_FIX_OFF=species.line: step 1 FAILs, the guest has no line.)
SPORE_FIX_OFF=civ.own_species_save: step 4 FAILs (the host's citizen records name the guest's key; also seen before the
strip existed, lab 20261118-180539).
--host-mode (suite civ_own_species_hostmode): the host sets creature.mode host after step 1 (F5: everybody plays the
host's creature): no game has a line for the guest and every city, the guest's too, keeps the civilization's species
(today's behaviour); the setting is put back to own at the end.

  SPORE_LANE=6 python3 tools/tests/coop_civ_own_species.py
"""
import math
import re
import sys
import time

import lane
import settings_lib
import sameview as sv
from coop_civ import civ, dist
from coop_creature_e84 import close_boxes
from coop_tribe_outfit import keys_in_save
from play_civ import frame_city
from twoinst import Checks, keyframe, start

CITIZEN = '18EB4B7'
HOST_MODE = '--host-mode' in sys.argv


def species(con):
    """this game's lines {pid: key}, the civilization's key, {city: {key: n}} of the citizen objects, the status line"""
    out = con.call('coop.civ citizens')['out']
    lines = {int(m.group(1)): int(m.group(2), 16) for m in re.finditer(r'^line (\d+) key=(\w+)', out, re.M)}
    m = re.search(r'^civ species key=(\w+)', out, re.M)
    per = {}
    for c, k, n in re.findall(r'^citizens city=(-?\d+) key=(\w+) n=(\d+)', out, re.M):
        per.setdefault(int(c), {})[int(k, 16)] = int(n)
    return lines, int(m.group(1), 16) if m else 0, per, out.splitlines()[0] if out else ''


def fmt(d):
    return {k: {f'{s:08X}': n for s, n in v.items()} for k, v in d.items()}


def wait_city(players, city, want, secs):
    """until every game's citizens of `city` are all of key `want` (at least 3): {role: {key: n}} at the end"""
    t0 = time.time()
    last = {}
    while True:
        last = {r: species(c)[2].get(city, {}) for r, c in players.items()}
        if all(sum(v.values()) >= 3 and set(v) == {want} for v in last.values()) or time.time() - t0 > secs:
            return last, time.time() - t0
        time.sleep(1)


def closeup(players, centre, tag, note):
    """key frames of one citizen of the city near `centre` up close, the same camera on every game: a citizen walking
    25-90 m from the centre (out of the buildings of the middle), seen from 12 m and 7 m up, from two sides"""
    rows = [tuple(float(x) for x in m.groups()) for m in re.finditer(
        r'^obj \S+ (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)', players['host'].call(f'coop.civ nouns {CITIZEN}')['out'], re.M)]
    near = sorted((p for p in rows if 25 < math.dist(p, centre) < 90), key=lambda p: math.dist(p, centre))
    if not near:
        print(f'  ({tag}: no citizen of the host 25-90 m from the city for a close-up)', flush=True)
        return
    for k in (0, 2):
        eye, target = sv.camera_at(near[0], k, 4, 12.0, 7.0, 0.8)
        if all('now=1' in (c.call(sv.view_cmd(eye, target, 55)).get('out') or '') for c in players.values()):
            time.sleep(0.5)
            keyframe(f'{tag}{k // 2 + 1}', note, kind='sameview', settle=0)
    for c in players.values():
        c.call('coop.view off')


def main():
    host, guest = start('civ_npc')
    players = {'host': host, 'guest': guest}
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(len(cap) == 1 and npc, f'one player city {cap} and NPC cities {npc}'):
        return ck.result()
    cap = cap[0]
    tgt = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap]['pos']))
    # 1. the guest plays its own creature (a player's pick in the co-op window; a guest that played it in Creature has
    # it recorded for this world, BUG-0345; the fixture's guest never did): the newest of its My Creations
    mine = re.findall(r'^creature (\w{8}) ', guest.call('coop.creations list')['out'], re.M)
    if not ck(mine, f'the guest has creatures of its own ({len(mine)})'):
        return ck.result()
    print('  guest:', guest.call(f'coop.creations choose creature {mine[0]}')['out'].strip(), mine[0], flush=True)
    time.sleep(3)
    out = guest.call('coop.species test civ next')['out'].strip()  # the guest's Civ outfit (WP-C3's stand-in)
    print('  guest:', out, flush=True)
    ck(out.startswith('set'), f'the guest\'s species line takes a Civ outfit ({out})')
    gkey, ckey, skey = {}, {}, {}
    t0 = time.time()
    while time.time() - t0 < 60:  # (the guest's row goes out after the host's store answer: up to 10 s)
        for r, c in players.items():
            lines, ckey[r], _, st = species(c)
            gkey[r] = lines.get(1, 0)
            m = re.search(r'key=([0-9A-F]{8})/', c.call('coop.species key 1 civ')['out'] or '')
            skey[r] = int(m.group(1), 16) if m else 0
        if all(k >> 24 == 0x7B for k in gkey.values()) and gkey == skey:
            break
        time.sleep(1)
    print(f'  {st}', flush=True)
    print(f'  guest line: {({r: f"{k:08X}" for r, k in gkey.items()})}, civilization: '
          f'{({r: f"{k:08X}" for r, k in ckey.items()})}', flush=True)
    g = gkey['host']
    ok = ck(g >> 24 == 0x7B and gkey['guest'] == g and skey == gkey,
            f'both games know the guest\'s Civ line, its outfit ({g:08X}; the species line {skey})')
    ok = ck(ckey['host'] and ckey['guest'] == ckey['host'] and ckey['host'] != g,
            f'the civilization\'s species ({ckey["host"]:08X}) is another species') and ok
    if not ok:
        return ck.result()
    civkey = ckey['host']
    want = g  # the species of the guest's city
    if HOST_MODE:
        ck(settings_lib.force(host, [guest], 'creature.mode', 'host'), 'the guest got creature.mode host')
        time.sleep(3)
        left = {r: species(c)[0] for r, c in players.items()}
        ck(not any(left.values()), f'creature.mode host: no game keeps a species line for the guest ({left})')
        want = civkey

    # 2. the capital (shared, the founder's: the host's)
    frame_city(players, hc[cap]['pos'])
    seen, t = wait_city(players, cap, civkey, 20)
    print(f'  capital {cap}: {fmt(seen)} after {t:.0f} s', flush=True)
    ck(all(set(v) == {civkey} for v in seen.values()) and all(seen.values()),
       f'the shared capital\'s citizens are the civilization\'s species on both games ({fmt(seen)})')

    # 3. the NPC city, captured by the guest
    frame_city(players, hc[tgt]['pos'])
    time.sleep(6)
    before = {r: species(c)[2].get(tgt, {}) for r, c in players.items()}
    print(f'  city {tgt} before the capture: {fmt(before)}', flush=True)
    print('  host:', host.call(f'coop.civ siege {tgt} 1 150 pc')['out'].strip(), flush=True)
    owners = {}
    t0 = time.time()
    while time.time() - t0 < 20:
        owners = {r: civ(c)[0].get(tgt, {}).get('owner') for r, c in players.items()}
        if all(o == 1 for o in owners.values()):
            break
        time.sleep(1)
    ck(all(o == 1 for o in owners.values()), f'city {tgt} is the guest\'s on both games ({owners})')
    frame_city(players, hc[tgt]['pos'])  # (answers the specialty box; the capture's cinematic may have moved the camera)
    seen, t = wait_city(players, tgt, want, 30)
    print(f'  city {tgt} after the capture: {fmt(seen)} after {t:.0f} s', flush=True)
    for r, c in players.items():
        print(f'  {r}: {species(c)[3]}', flush=True)
    ck(all(set(v) == {want} for v in seen.values()) and all(seen.values()),
       f'the guest\'s city\'s citizens are the {"civilization" if HOST_MODE else "guest"}\'s species on both games '
       f'({want:08X}: {fmt(seen)})')
    look = ('the civilization\'s species, as at the capital (creature.mode host)' if HOST_MODE else
            'the guest\'s creature (not the civilization\'s species of the capital)')
    keyframe('guest_city', f'the city {tgt} the guest captured, both games: its citizens walking about are {look}')
    closeup(players, hc[tgt]['pos'], 'guest_citizen', 'one citizen of the guest\'s city up close, the same camera on both '
                                                     f'games: {look} on both screens')

    # 4. the host's save
    t_save = time.time()
    print('  host:', host.call('coop.save')['out'].strip()[:80], flush=True)
    close_boxes(host, 12)
    time.sleep(2)
    found = keys_in_save(lane.HOST_DIR, g)
    print(f'  host save: records naming {g:08X}: {found}', flush=True)
    for ln in (host.call('coop.civ citizens')['out'] or '').splitlines()[:1] + \
            [x for x in (host.call('coop.savestrip')['out'] or '').splitlines() if 'keys=' in x]:
        print(f'  host: {ln.strip()}', flush=True)  # (the citizens' strip and the save's key filter, BUG-1315)
    ck(found != ['no save'] and not found, f'the host\'s save names no key of the guest\'s species ({found or "none"}, '
                                           f'saved {time.time() - t_save:.0f} s ago)')

    # 5. the capital again
    frame_city(players, hc[cap]['pos'])
    seen, t = wait_city(players, cap, civkey, 20)
    print(f'  capital {cap} again: {fmt(seen)} after {t:.0f} s', flush=True)
    ck(all(set(v) == {civkey} for v in seen.values()) and all(seen.values()),
       f'the capital\'s citizens stay the civilization\'s species (its founder\'s, the host\'s) ({fmt(seen)})')
    keyframe('capital', 'the players\' capital on both games: its citizens are the civilization\'s (the host\'s) species')
    closeup(players, hc[cap]['pos'], 'capital_citizen', 'one citizen of the capital up close, the same camera on both '
                                                        'games: the host\'s species on both screens')
    if HOST_MODE:
        settings_lib.force(host, [guest], 'creature.mode', 'own')
    guest.call('coop.species test civ none')  # (no outfit for the next test of the warm games)
    guest.call('coop.creations unchoose creature')  # (the lane's profile as before for the next test)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
