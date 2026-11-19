#!/usr/bin/env python3
"""Civilization: a guest's vehicles and buildings show the guest's own designs on every game; the host's save keeps the
culture's (WP-C2 of docs/plan-0.7.16.md, BUG-1274).

The civilization is one, its culture set the host's (BUG-1157 / BUG-1158), but the game draws each vehicle and building
from a model key of its own (+0xC4) and SetModelKey changes it with the design's stats (docs/re/civ.md#civ-own-species,
#civ-own-designs). With civ.own_designs every game gives the objects of a guest's city and the guest's vehicles the
guest's own design (its species line, species::design: the design goes once as a line creation, MSG_SPECIES_LINE, and
is the same shared key 0x7B...... on every game; docs/01-architecture.md#arch-species-line); the save gets the culture
key.

Set-up (cold start, civ_npc): the host's My Creations hold a base game vehicle model VA under a test id G and a building
model BA under B; the guest's My Creations other models VB / BB under the same ids (the BUG-0647 / BUG-0670 collision:
only the bundle gives the host's game the guest's model).

1. The guest buys the NPC city nearest the capital (`coop.civ buycity`), the host adds money.
2. The guest names its own designs through its species line (`coop.civ owndesign set`, the lab's setter for a vehicle
   or building of its My Creations: species::set_design; `coop.species test design` takes creatures only): G for the
   three land vehicle designs, B for the house; both games list them under the same shared key 0x7B...... (`owndesign`
   and `coop.species` agree).
3. The guest's city's houses (the bought ones and one the guest builds) show BB on both games
   (`coop.creations modelhash` of the building's own model key); the host capital's houses not.
4. The guest buys a land vehicle in its city: on the host and in the guest's mirror it shows VB; the host's own
   vehicles not. Its hit points (and base maximum) are the design's: those of a reference vehicle the host's game
   makes with that key itself (`owndesign hpref`, cVehicle::Load).
5. The host's game sets its culture's designs of that vehicle and the house to G / B (Civ_SetDesign, as its planner
   pick): the host's objects of those designs show VA / BA, the guest's still VB / BB right after the call (the hook
   at its end) on the host and within 5 s on the guest.
6. Key frames: the guest's city (its house, its vehicle) and the host's capital, on both screens.
7. A gift: the host spawns a land vehicle and gives it to the guest (`coop.give 1`): VB on both within 3 s.
8. The host goes to the galaxy menu (the game saves): every file of the slot written then is parsed: no key of ours
   (the guest's design keys, little or big endian) in any record, the culture key G is there.
9. The host loads that save again, the guest follows: the guest's vehicles and its city's houses show VB / BB again
   on both games (the owners come back from the claims, the design table stays with the session).

SPORE_FIX_OFF=civ.own_designs: steps 3, 4, 5, 7 and 9 FAIL (every object the culture's design).

  SPORE_LANE=10 python3 tools/tests/coop_civ_own_designs.py
"""
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from harness import fnv, waiter  # noqa: E402
import part_substitutes as ps  # noqa: E402

import fixtures  # noqa: E402
import lane  # noqa: E402
import twoinst  # noqa: E402
from coop_civ import civ, dist  # noqa: E402
from coop_civ_designs import two_models  # noqa: E402
from coop_nest_species import fixtures_dir, read_pkg, write_pkg  # noqa: E402
from play_civ import frame_city  # noqa: E402
from twoinst import Checks, keyframe, start, wait_guest_in  # noqa: E402

G, B = 0x2903A651, 0x2903A653  # the test's creation ids, in both profiles (another creation in each)
BEM = 0x1A99B06B
BGROUP, BTYPE = 0x40636200, 0x2399BE55
VGROUP, VTYPE = 0x40646200, 0x24682294
HOUSE = 0x4E3F7777
wait = waiter(step=1.0, final=True)

_orig_restore = twoinst.restore_creations
BMODELS, VMODELS = [], []


def restore_with_designs(guest=True):
    _orig_restore(guest)
    for i, d in enumerate((lane.HOST_DIR, lane.GUEST_DIR)):
        p = os.path.join(d, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package' if i == 0 else 'creations-guest.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] not in (G, B)]
        recs.append((BEM, BGROUP, B, BMODELS[i]))
        recs.append((BEM, VGROUP, G, VMODELS[i]))
        write_pkg(p, recs)


def mode(con):
    try:
        return con.call('game.mode')['out']
    except OSError:
        return ''


def own(con):
    """(vehicles {net id: row}, buildings [row], player designs {(player, id): ours}, status line)"""
    out = con.call('coop.civ owndesign')['out']
    vs, bs, pd = {}, [], {}
    for ln in out.splitlines():
        m = re.match(r'\s+ownveh #(\d+) \S+ owner=(-?\d+) purpose=(\d+) loco=(\d+) did=(\w+) model=(\w+) culture=(\w+) '
                     r'hp=([-\d.]+) max=([-\d.]+)', ln)
        if m:
            vs[int(m.group(1))] = dict(owner=int(m.group(2)), purpose=int(m.group(3)), loco=int(m.group(4)),
                                       did=int(m.group(5), 16), model=int(m.group(6), 16), culture=int(m.group(7), 16),
                                       hp=float(m.group(8)), max=float(m.group(9)))
        m = re.match(r'\s+ownbld city=(\d+) host=(-?\d+) owner=(-?\d+) \S+ noun=(\w+) did=(\w+) model=(\w+) culture=(\w+) '
                     r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln)
        if m:
            bs.append(dict(city=int(m.group(2)), owner=int(m.group(3)), did=int(m.group(5), 16),
                           model=int(m.group(6), 16), culture=int(m.group(7), 16),
                           pos=tuple(float(m.group(i)) for i in (8, 9, 10))))
        m = re.match(r'\s+pdesign player=(\d+) id=(\w+) orig=\w+ ours=(\w+)/', ln)
        if m:
            pd[(int(m.group(1)), int(m.group(2), 16))] = int(m.group(3), 16)
    st = next((ln for ln in out.splitlines() if ln.startswith('own designs:')), '')
    return vs, bs, pd, st


_hash_cache = {}


def mhash(con, inst, t, g):
    if not inst:
        return '?'
    k = (con.port, inst, t)
    if k not in _hash_cache:
        m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X} {t:08X} {g:08X}')['out'])
        _hash_cache[k] = m.group(1) if m else '?'
    return _hash_cache[k]


def houses_of(con, owner):
    return [b for b in own(con)[1] if b['owner'] == owner and b['did'] == HOUSE]


def house_hashes(con, owner):
    return sorted({mhash(con, b['model'], BTYPE, BGROUP) for b in houses_of(con, owner)})


def veh_hash(con, vid):
    v = own(con)[0].get(vid)
    return mhash(con, v['model'], VTYPE, VGROUP) if v else None


def save_files(folder, since):
    out = []
    for root, _, files in os.walk(folder):
        for n in files:
            p = os.path.join(root, n)
            try:
                if os.path.getmtime(p) >= since:
                    out.append(p)
            except OSError:
                pass
    return out


def keys_in(files, insts):
    """{instance: ['<file>:<record type>', ...]} of the records (DBPF files) or files that hold each instance"""
    found = {i: [] for i in insts}
    pats = {i: (i.to_bytes(4, 'little'), i.to_bytes(4, 'big')) for i in insts}
    for p in files:
        blobs = []
        try:
            for e in ps.entries(p):
                blobs.append((f'{os.path.basename(p)}:{e["type"]:08X}', ps.read(p, e)))
        except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error):  # (struct: an empty index,
            # BUG-1307: lab 20261119-111652; the whole file is searched then)
            try:
                blobs.append((os.path.basename(p), open(p, 'rb').read()))
            except OSError:
                continue
        for name, data in blobs:
            for i, (le, be) in pats.items():
                if le in data or be in data:
                    found[i].append(name)
    return found


def main():
    BMODELS[:] = two_models(BGROUP)
    VMODELS[:] = two_models(VGROUP)
    ba, bb = f'{fnv(BMODELS[0]):08X}', f'{fnv(BMODELS[1]):08X}'
    va, vb = f'{fnv(VMODELS[0]):08X}', f'{fnv(VMODELS[1]):08X}'
    print(f'  fixture: vehicle {G:08X}: host {va}, guest {vb}; building {B:08X}: host {ba}, guest {bb}', flush=True)
    twoinst.restore_creations = restore_with_designs
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)

    # 1. the guest's city
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    gci, gpos, cpos = npc[0], hc[npc[0]]['pos'], hc[cap[0]]['pos']
    print(f'  guest buys city {gci}:', guest.call(f'coop.civ buycity {gci} 500')['out'].strip(), flush=True)
    got = wait(lambda: all((c := civ(x)[0].get(gci)) and c['player'] and c['owner'] == 1 for x in (host, guest)), 30)
    if not ck(got, f'city {gci} is the player civilization\'s and the guest\'s on both games'):
        return ck.result()
    frame_city({'host': host, 'guest': guest}, gpos)  # (answers the specialty box of the purchase)
    time.sleep(2)

    # 2. the guest's own designs (the species line)
    vdids = []
    for p in (0, 1, 2):
        m = re.search(r'did (\w+)', host.call(f'coop.civ owndesign did {gci} 0 {p}')['out'])
        if m:
            vdids.append(int(m.group(1), 16))
    m = re.search(r'did (\w+) purpose=(\d+)', host.call(f'coop.civ owndesign did {gci} 0')['out'])
    buy_did, buy_purpose = (int(m.group(1), 16), int(m.group(2))) if m else (None, 0)
    print(f'  land vehicle designs {[f"{d:08X}" for d in vdids]}, a purchase in city {gci}: '
          f'{buy_did and f"{buy_did:08X}"} (purpose {buy_purpose})', flush=True)
    for did in vdids:
        print('  guest:', guest.call(f'coop.civ owndesign set {did:08X} {G:08X}')['out'].strip(), flush=True)
    print('  guest:', guest.call(f'coop.civ owndesign set {HOUSE:08X} {B:08X}')['out'].strip(), flush=True)
    want = [(1, d) for d in vdids + [HOUSE]]
    ok = wait(lambda: all(all(k in own(c)[2] for k in want) for c in (host, guest)), 30)
    hpd, gpd = own(host)[2], own(guest)[2]
    ours_v, ours_b = hpd.get((1, buy_did)), hpd.get((1, HOUSE))
    print(f'  designs: host {hpd}, guest {gpd}', flush=True)
    ck(ok and ours_v and ours_b and gpd.get((1, buy_did)) == ours_v and gpd.get((1, HOUSE)) == ours_b,
       'both games know the guest\'s own designs under the same keys')
    ck(ours_v and ours_b and ours_v >> 24 == 0x7B and ours_b >> 24 == 0x7B,
       f'the designs are the species line\'s shared keys ({ours_v or 0:08X}, {ours_b or 0:08X})')
    for con, who in ((host, 'host'), (guest, 'guest')):
        line = con.call('coop.species')['out'] or ''
        ck(ours_b and f'design {HOUSE:08X}={ours_b:08X}' in line,
           f'{who}: coop.species names the guest\'s house design {ours_b or 0:08X}')
    if not (ours_v and ours_b):
        ck.faults(host=host, guest=guest)
        return ck.result()
    ck(mhash(host, ours_v, VTYPE, VGROUP) == vb and mhash(host, ours_b, BTYPE, BGROUP) == bb,
       f'the host\'s game answers the guest\'s models for those keys ({mhash(host, ours_v, VTYPE, VGROUP)}, '
       f'{mhash(host, ours_b, BTYPE, BGROUP)}; the guest\'s {vb} / {bb}, the host\'s own of the same ids {va} / {ba})')

    # 3. the guest's city's houses (the bought ones and a new one)
    print('  guest:', guest.call(f'coop.civ build {gci} house')['out'].strip(), flush=True)
    time.sleep(3)
    for con, who in ((host, 'host'), (guest, 'guest')):
        got = wait(lambda: houses_of(con, 1) and house_hashes(con, 1) == [bb], 10)
        hs = houses_of(con, 1)
        print(f'  {who}: the guest\'s houses {len(hs)} -> {house_hashes(con, 1)}; the host\'s {house_hashes(con, 0)}',
              flush=True)
        ck(got, f'{who}: every house of the guest\'s city shows the guest\'s design ({house_hashes(con, 1)}, want {bb})')
        ck(bb not in house_hashes(con, 0), f'{who}: the host\'s houses keep the host\'s ({house_hashes(con, 0)})')

    # 4. the guest's vehicle
    before = set(own(host)[0])
    print('  guest:', guest.call(f'coop.civ buy {gci} 0')['out'].strip(), flush=True)
    vid = None
    t0 = time.time()
    while time.time() - t0 < 20 and vid is None:
        hv = own(host)[0]
        new = [i for i, v in hv.items() if i not in before and v['owner'] == 1]
        vid = new[0] if new else None
        time.sleep(0.5)
    if not ck(vid is not None, f'the guest\'s purchase made a vehicle on the host ({vid})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    hh = None
    for con, who in ((host, 'host'), (guest, 'guest')):
        got = wait(lambda: veh_hash(con, vid) == vb, 15)
        v = own(con)[0].get(vid)
        print(f'  {who}: the guest\'s vehicle #{vid} {v} -> {veh_hash(con, vid)}', flush=True)
        ck(got, f'{who}: the guest\'s vehicle shows the guest\'s design ({veh_hash(con, vid)}, want {vb})')
        others = {mhash(con, x['model'], VTYPE, VGROUP) for x in own(con)[0].values() if x['owner'] == 0}
        ck(vb not in others, f'{who}: the host\'s vehicles keep the host\'s designs ({sorted(others)})')
    gv = own(host)[0].get(vid)
    print('  host:', host.call(f'coop.civ owndesign hpref {buy_purpose} 0 {ours_v:08X}')['out'].strip(), flush=True)
    time.sleep(2)
    ref = host.call('coop.civ owndesign hpref')['out'].strip()
    print('  host:', ref, flush=True)
    m = re.search(r'hp=([-\d.]+) max=([-\d.]+)', ref)
    if m and gv:
        rhp, rmax = float(m.group(1)), float(m.group(2))
        ck(abs(gv['max'] - rmax) < 0.5 and abs(gv['hp'] - rhp) < 0.5,
           f'the guest\'s vehicle has the design\'s hit points (hp {gv["hp"]} max {gv["max"]}; '
           f'a vehicle the game makes of that design: hp {rhp} max {rmax})')
    else:
        ck(False, f'the reference vehicle of the guest\'s design ({ref})')

    # 5. the host's Civ_SetDesign of those designs
    hv0 = own(host)[0]
    for did, k in ((buy_did, G), (HOUSE, B)):
        print('  host:', host.call(f'coop.designs set {did:08X} {k:08X}')['out'].strip(), flush=True)
    h1 = veh_hash(host, vid)
    ck(h1 == vb, f'host: right after its Civ_SetDesign the guest\'s vehicle still shows the guest\'s design ({h1})')
    st = own(host)[3]
    print('  host:', st, flush=True)
    hown = [x for i, x in own(host)[0].items() if x['owner'] == 0 and x['did'] == buy_did]
    if hown:
        ck(all(mhash(host, x['model'], VTYPE, VGROUP) == va for x in hown),
           'host: its own vehicles of that design took the culture\'s new design (the setter ran)')
    ck(wait(lambda: house_hashes(host, 1) == [bb] and bb not in house_hashes(host, 0), 2),
       f'host: after the house design the guest\'s houses still show the guest\'s ({house_hashes(host, 1)}), '
       f'the host\'s {house_hashes(host, 0)}')
    t0 = time.time()
    while time.time() - t0 < 40 and not re.search(rf'id={buy_did:08X} key={G:08X}', guest.call('coop.designs culture')['out']):
        time.sleep(1)
    time.sleep(1)
    ck(wait(lambda: veh_hash(guest, vid) == vb and house_hashes(guest, 1) == [bb], 5),
       f'guest: after the host\'s new culture designs the guest\'s vehicle and houses still show the guest\'s '
       f'({veh_hash(guest, vid)}, {house_hashes(guest, 1)})')
    print('  guest:', own(guest)[3], '| host vehicles before the set:', len(hv0), flush=True)

    # 6. key frames
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('guest_city', 'the guest\'s city: its houses and its new land vehicle are of the guest\'s own designs, the '
                           'same models on both screens (other models than the host\'s capital)')
    frame_city({'host': host, 'guest': guest}, cpos)
    keyframe('host_city', 'the host\'s capital: its houses and vehicles are of the host\'s designs, the same on both '
                          'screens (not the guest\'s models of the frame before)')

    # 7. a gift to the guest
    before = set(own(host)[0])
    print('  host:', host.call(f'coop.civ spawnveh {cap[0]} {buy_purpose} 0')['out'].strip(), flush=True)
    time.sleep(2)
    gift = sorted(i for i, v in own(host)[0].items() if i not in before and v['owner'] == 0)
    if ck(gift, f'the host spawned a land vehicle ({gift})'):
        gh0 = veh_hash(host, gift[0])
        host.call(f'coop.civ select {gift[0]} 1')
        print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
        for con, who in ((host, 'host'), (guest, 'guest')):
            got = wait(lambda: veh_hash(con, gift[0]) == vb, 5)
            ck(got, f'{who}: the vehicle given to the guest takes the guest\'s design ({gh0} -> {veh_hash(con, gift[0])})')

    # 8. the host's save
    tables = host.call('coop.civ tables')['out']
    m = re.search(r'key=(\S+)', tables)
    slot = re.search(r'\\(game\d+)\\[^\\]+$', m.group(1), re.I) if m else None
    planet = fixtures.planet_of('civ_npc').split('/')[1]
    if not ck(slot, f'the host\'s save slot is known ({m.group(1) if m else None})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    slot = slot.group(1).capitalize()
    t_save = time.time() - 1
    print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in mode(host), 90), 'the host is in the galaxy menu (its game saved)')
    host.call('wait.frames 240 90000')
    files = save_files(os.path.join(lane.HOST_DIR, 'Games', slot), t_save)
    found = keys_in(files, [ours_v, ours_b, G])
    print(f'  save: {len(files)} files written; ours {ours_v:08X}: {found[ours_v][:6]}, {ours_b:08X}: '
          f'{found[ours_b][:6]}; culture {G:08X}: {found[G][:4]}', flush=True)
    ck(files and not found[ours_v] and not found[ours_b], 'the host\'s save holds no key of the guest\'s designs')
    ck(found[G], 'the host\'s save holds the culture\'s key (the parse sees the design keys)')

    # 9. the reload
    print(f'  host load {slot} {planet}:', host.call(f'coop.load {slot} {planet}')['out'].strip(), flush=True)
    ck(wait(lambda: 'Civ' in mode(host) and host.call('coop.avatar')['ok'], 180, 2), 'the host plays its Civ world again')
    ck(wait_guest_in(host, guest, 180), 'the guest is in the reloaded world')
    time.sleep(6)
    for con, who in ((host, 'host'), (guest, 'guest')):
        got = wait(lambda: (vs := [x for x in own(con)[0].values() if x['owner'] == 1 and x['did'] in vdids]) and
                   all(mhash(con, x['model'], VTYPE, VGROUP) == vb for x in vs) and house_hashes(con, 1) == [bb], 30)
        vs = [x for x in own(con)[0].values() if x['owner'] == 1]
        print(f'  {who} after the load: the guest\'s vehicles {[(f"{x["did"]:08X}", mhash(con, x["model"], VTYPE, VGROUP)) for x in vs]}, '
              f'houses {house_hashes(con, 1)}', flush=True)
        ck(got, f'{who}: after the load the guest\'s vehicles and houses show the guest\'s designs again')
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('reloaded', 'after the host\'s load: the guest\'s city with its houses and vehicles of the guest\'s own '
                         'designs again, the same on both screens')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
