#!/usr/bin/env python3
"""Tribe: the members of a guest are of the guest's own Tribe species (its species line) on every game; the host's
save keeps the tribe's species (BUG-1271, WP-T1 of docs/plan-0.7.16.md, docs/re/tribe.md#tribe-own-species).

A member's species is its own field (cCreatureBase +0xB20 profile, +0xB28 key): its model, size and abilities follow
it and nothing of the game puts it back (docs/re/tribe.md#tribe-two-species). Each game gives every member of the
player tribe owned by a player with an own Tribe line (species::key(player, tribe), docs/01-architecture.md
#arch-species-line: its tribe outfit, else its creature) that species (SetSpecies), the host's members keep the tribe's
(the species slot); while a save is written the changed members carry the slot's key.

1. Two games on the tribe fixture; the host's tribe gets 2 more adults (the guest owns at least two members). The
   guest plays a creature of its own My Creations (`coop.creations choose creature`, as the co-op window's arrows) and
   sets a tribe outfit through its species line (`coop.species test tribe next`: another own creature as the stand-in
   for the outfitter's result, WP-T2): both games answer the same Tribe key for the guest (`coop.species key 1 tribe`,
   the line's shared key 0x7B......, known on both).
2. Within 25 s, on both games (`coop.tribe species`: the game's own +0xB28 of every member): every guest-owned member
   is of the guest's key, every host-owned member of the slot's key; the model the guest's members are built from (the
   profile's +0x510) has the same model record on both games (`coop.creations modelhash`) and is not the slot's.
   Key frames: the village with both kinds (the game's camera), a same view 12 m from a guest's member.
3. A baby the guest buys at its hut (its real click, as tribe_guest_baby): the new member is the guest's and is of the
   guest's species within 2 s of its first appearance on each game.
4. A member carrying food when its species changes: the switch off on both games (the guest's members go back to the
   slot's species: the revert path), a guest's adult member gathers from a fruit tree (`coop.tribe order <id> 2`, the
   host's DoAction); while it carries the food the switch on again: within 4 s it is of the guest's species on both
   games, and in the next 30 s it goes on (it moves 3 m or more or its food goes home, its animation is never 0 and
   changes: no stuck action, no T-pose on either game). A key frame of it.
5. The host saves (`coop.save`): no file of its save slot names the guest's key or the guest's own creations'
   instances (its creature, its outfit; the save strip); the guest's members are of the guest's species right after
   the save too (put back).
6. A new evening (twoinst.session_restart: both quit, the host loads that save, the guest joins): the guest's line
   comes back from the host's per-player store (the same key) and the mix comes back on both games (the host's members
   the slot's, the guest's the guest's). Then the guest's game alone is killed and joins again (a rejoin): the mix is
   back on both again.
The guest's creature choice is put back at the end (its lane profile as before).

SPORE_FIX_OFF=tribe.own_species: step 2 FAILs (every member is the slot's species on both games); the test ends there.
(SPORE_FIX_OFF=species.line: step 1 FAILs, the guest has no line.)

  SPORE_LANE=4 python3 tools/tests/coop_tribe_own_species.py
"""
import os
import re
import time

import keyviews
import lane
import twoinst
from coop_content_civ import save_slot, wait_saved
from coop_creature_desync import kill_game
from coop_tribe_gather_stall import trees
from coop_tribe_guest_baby import food, hut, player
from coop_tribe_looks import members as looks
from coop_tribe_tools import dist
from twoinst import Checks, Console, start, wait_guest_in, wait_up, watch

ROW = re.compile(r'S #(\d+) (\w+) owner=(-?\d+) key=(\w+) model=(\w+) line=(\w+) ours=(\w+) age=(-?\d+) body=(\d) '
                 r'dead=(\d) tool=(-?\d+)')
SWITCH = 'tribe.own_species'


def species(con):
    """(slot key, slot model, {id: dict(owner, key, model, line, age, body, dead, tool)}) of the player tribe"""
    out = con.call('coop.tribe species').get('out') or ''
    m = re.search(r'slot \S+ key=(\w+) model=(\w+)', out)
    ms = {}
    for r in ROW.finditer(out):
        if r.group(1) == '0':
            continue
        ms[int(r.group(1))] = dict(owner=int(r.group(3)), key=int(r.group(4), 16), model=int(r.group(5), 16),
                                   line=int(r.group(6), 16), age=int(r.group(8)), body=r.group(9) == '1',
                                   dead=r.group(10) == '1', tool=int(r.group(11)))
    return (int(m.group(1), 16) if m else None), (int(m.group(2), 16) if m else None), ms


def line_key(con, pid):
    """the Tribe key of player pid's species line on this game, when this game knows it (0: none)"""
    m = re.search(r'key=([0-9A-F]{8})/\S+ known=(\d)', con.call(f'coop.species key {pid} tribe').get('out') or '')
    return int(m.group(1), 16) if m and m.group(2) == '1' else 0


def model_hash(con, inst):
    m = re.search(r'model \w+ (\w+)', con.call('coop.creations modelhash %08X 2B978C46 40626200' % inst)['out'])
    return m.group(1) if m else '?'


def mix_ok(con, gkey):
    """(ok, text): every living guest-owned member of the guest's key, every host-owned one of the slot's"""
    slot, _, ms = species(con)
    # (BUG-1313: the pass never changes a kid, age != 1, while it grows up: a guest's kid of the tribe's species is
    # right, e.g. the baby bought before the host's save, which loads as a kid of the slot's species; a kid born in
    # the session is born in its owner's species, tribe.birth_species, and checked in step 3)
    bad = [i for i, m in ms.items() if not m['dead'] and m['body'] and
           ((m['owner'] == 1 and m['key'] != gkey and not (m['age'] != 1 and m['key'] == slot)) or
            (m['owner'] == 0 and m['key'] != slot))]
    guests = [i for i, m in ms.items() if m['owner'] == 1 and not m['dead']]
    hosts = [i for i, m in ms.items() if m['owner'] == 0 and not m['dead']]
    txt = (f'slot {slot or 0:08X}; ' + ', '.join(f'#{i}:{m["owner"]}:{m["key"]:08X}' for i, m in sorted(ms.items())))
    return bool(guests) and bool(hosts) and not bad and slot is not None, txt


def wait_mix(cons, gkey, secs):
    t0 = time.time()
    res = {}
    while time.time() - t0 < secs:
        res = {r: mix_ok(c, gkey) for r, c in cons.items()}
        if all(ok for ok, _ in res.values()):
            break
        time.sleep(1)
    return res, time.time() - t0


# BUG-1326: the files of a save slot are DBPF packages (an index of keys, records mostly RefPack-compressed). The scan
# before read the raw file bytes for each instance: it matched the instance ids of the host's OWN new resources in the
# index (planet scripts 00B1B104 / 4084AF00 in lastSave.pld and PlanetScripts.pld, planet records in
# planetRecords.pkp / .pkt) and could miss a key inside a compressed record. A game's new resource ids come from one
# counter per process that starts at the seconds since 2005-01-01 at the game's start (lab 20261119-164409: the host's
# planet scripts 29293F5F..29293F61 written at the load, the guest's new outfit 29293F61: both games started in the
# same second), so the guest's raw creation instance (0x29......) is also an id of the host's own. Now every record is
# unpacked and scanned; a shared key (0x7B line, 0x7C copy) counts wherever it is, a raw instance counts in a record's
# data and as a resource of a creation type in an index, except where it is the id of one of the host's own
# resources of this slot (its index) and no creation type stands next to it (a key always has its type beside the
# instance: {instance, type, group}, {type, instance, group} or the stream's big-endian group, type, instance).
CREATION_TYPES = (0x2B978C46, 0x24682294, 0x2399BE55, 0x476A98C7, 0x372E2C04, 0x3D97A8E4, 0x438F6347)


def _dbpf(path, data):
    """[(entry, unpacked data)] of a DBPF file, None for another file or an unreadable one"""
    if data[:4] != b'DBPF':
        return None
    try:
        import sys
        tools = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        if tools not in sys.path:
            sys.path.insert(0, tools)
        import part_substitutes as ps
        return [(e, ps.read(path, e)) for e in ps.entries(path)]
    except Exception:  # (a broken package: scanned as raw bytes, the old way)
        return None


def _typed(data, i, order):
    """a creation type within 8 bytes of the instance at i (the same byte order)"""
    for t in CREATION_TYPES:
        tb = t.to_bytes(4, order)
        j = data.find(tb, max(0, i - 8), i + 12)
        if j >= 0:
            return True
    return False


def keys_in_slot(folder, insts, own_ids=None):
    """files of the save slot that name one of the instances (little or big endian), BUG-1326's rule above"""
    files = []
    for root, _, names in os.walk(folder):
        for name in names:
            path = os.path.join(root, name)
            try:
                data = open(path, 'rb').read()
            except OSError:
                continue
            files.append((name, path, data, _dbpf(path, data)))
    host_ids = set()  # instances of the host's own resources of this slot (not of a creation type)
    for _, _, _, recs in files:
        for e, _ in recs or ():
            if e['type'] not in CREATION_TYPES:
                host_ids.add(e['inst'])
    if own_ids is not None:
        own_ids.update(i for i in insts if i in host_ids)
    hits = []
    for name, path, data, recs in files:
        for inst in insts:
            if not inst:
                continue
            shared = inst >> 24 in (0x7B, 0x7C)
            if recs is None:
                if inst.to_bytes(4, 'little') in data or inst.to_bytes(4, 'big') in data:
                    hits.append(f'{name}:{inst:08X}')
                continue
            types = set()
            for e, d in recs:
                if e['inst'] == inst and (shared or e['type'] in CREATION_TYPES):
                    types.add('%08X' % e['type'])
                for order in ('little', 'big'):
                    b = inst.to_bytes(4, order)
                    i = d.find(b)
                    while i >= 0:
                        if shared or inst not in host_ids or _typed(d, i, order):
                            types.add('%08X' % e['type'])
                            break
                        i = d.find(b, i + 1)
            if types:
                hits.append(f'{name}:{inst:08X} (records {",".join(sorted(types))})')
    return hits


def ai(con, mid):
    out = con.call(f'coop.tribe ai {mid}').get('out') or ''
    c = re.search(r'carry=([\d.]+)', out)
    a = re.search(r'anim=([0-9A-F]+)', out)
    n = re.search(r'node=([0-9A-F]+)', out)
    p = re.search(r' pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out)
    st = re.search(r' mem=(-?\d+) ', out)
    return dict(carry=float(c.group(1)) if c else 0.0, anim=a.group(1) if a else None, node=n.group(1) if n else None,
                state=int(st.group(1)) if st else None,
                pos=tuple(float(p.group(i)) for i in (1, 2, 3)) if p else None)


def switch(cons, on):
    for c in cons:
        print(f'  {SWITCH} {"on" if on else "off"}:', (c.call(f'coop.fix {SWITCH} {"on" if on else "off"}').get('out')
                                                      or '').strip()[:80], flush=True)


def closeup(host, guest, mid, tag, note):
    pos = looks(host).get(mid, {}).get('pos')
    if not pos:
        print(f'  (close-up {tag}: member #{mid} not found)', flush=True)
        return
    try:
        # BUG-1459: a camera round the member with a clear line of sight (one fixed camera looked through a hut wall,
        # from inside a bush or from inside the chief's head); the frame's manifest says whether one was found
        ok, info = keyviews.look_both_clear((host, guest), pos, dist=12, height=5, fov=45, look_up=2.0)
        print(f'  close-up {tag} at {pos} (member #{mid}): views set {ok}; {info}', flush=True)
        twoinst.keyframe(tag, note, kind='sameview', settle=0, extra=[info])
    finally:
        keyviews.view_off(host, guest)


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    rec0 = (guest.call('coop.creations worlds').get('out') or '').strip()
    m = re.search(r'creature=(\w+)', rec0)
    rec_creature = m.group(1) if m else '00000000'
    print(f'  guest record before: {rec0}', flush=True)
    try:
        return run(host, guest, ck, off_twin)
    finally:
        try:  # the guest's lane profile as it was (the next test of the tribe fixture plays the world's species)
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.species test tribe none')['out'].strip(), '|',
                  g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call(f'coop.creations worlds set creature {rec_creature}')['out'].strip(), flush=True)
        except OSError:
            pass


def run(host, guest, ck, off_twin):
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    slot, slot_model, _ = species(host)
    own = [k for k in re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
           if int(k, 16) != slot]
    if not ck(own and slot, f'the guest has creatures of its own ({own[:4]}), the tribe a species ({slot or 0:08X})'):
        return ck.result()
    orig = int(own[0], 16)
    print('  guest:', guest.call(f'coop.creations choose creature {own[0]}')['out'].strip(), flush=True)
    time.sleep(3)
    out = guest.call('coop.species test tribe next')['out'].strip()  # the guest's tribe outfit (WP-T2's stand-in)
    print('  guest:', out, flush=True)
    m = re.search(r'outfit ([0-9A-F]{8})', out)
    outfit = int(m.group(1), 16) if m and out.startswith('set') else 0
    ck(outfit, f'the guest\'s species line takes a tribe outfit ({out})')
    t0 = time.time()
    gkey = 0
    while time.time() - t0 < 60:  # (the guest's row goes out after the host's store answer: up to 10 s)
        gkey = line_key(guest, 1)
        if gkey >> 24 == 0x7B and line_key(host, 1) == gkey:
            break
        time.sleep(1)
    if not ck(gkey >> 24 == 0x7B and line_key(host, 1) == gkey, f'both games answer one Tribe key for the guest\'s '
              f'line (guest {gkey:08X}, host {line_key(host, 1):08X}, its creature {orig:08X}, outfit {outfit:08X})'):
        print('  guest:', (guest.call('coop.species')['out'] or '')[:600], flush=True)
        print('  host:', (host.call('coop.species')['out'] or '')[:600], flush=True)
        return ck.result()

    # 2. the mix on both games
    res, t = wait_mix({'host': host, 'guest': guest}, gkey, 25)
    for r, (ok, txt) in res.items():
        print(f'  {r} after {t:.0f} s: {txt}', flush=True)
        ck(ok, f'{r}: the guest\'s members are of the guest\'s species {gkey:08X}, the host\'s of the tribe\'s '
               f'{slot:08X}')
    if off_twin:  # the FAIL twin: nothing below would test the switch
        ck.faults(host=host, guest=guest)
        return ck.result()
    hashes = {}
    for r, con in (('host', host), ('guest', guest)):
        _, smodel, ms = species(con)
        models = {mm['model'] for mm in ms.values() if mm['owner'] == 1 and mm['key'] == gkey}
        hs = {model_hash(con, x) for x in models}
        hashes[r] = hs
        print(f'  {r}: guest members\' models {sorted("%08X" % x for x in models)} -> {sorted(hs)}, the slot\'s '
              f'{smodel or 0:08X} -> {model_hash(con, smodel or 0)}', flush=True)
        ck(len(hs) == 1 and '?' not in hs and model_hash(con, smodel or 0) not in hs,
           f'{r}: the guest\'s members are built from one model that is not the tribe\'s ({sorted(hs)})')
    ck(hashes['host'] and hashes['host'] == hashes['guest'],
       f'the guest\'s members have the same model record on both games ({hashes})')
    twoinst.keyframe('mixed', 'the village on both games: the guest\'s members drawn as the guest\'s own creature, '
                              'the host\'s members as the tribe\'s species (two kinds of creatures), none in a T-pose')
    _, _, ms = species(host)
    gm = sorted(i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead'])
    if gm:
        closeup(host, guest, gm[0], 'guest_member', 'same view 12 m from a guest\'s member: the guest\'s own creature '
                                                    'on both screens, standing or walking (no T-pose)')

    # 3. a baby the guest buys
    print(' ', host.call('coop.tribe level 2')['out'].strip(), flush=True)
    n0, pt = player(host)
    f = food(host) or 0
    host.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt:X} {int(60 - f)}')
    time.sleep(4)
    ids0 = {r: set(species(c)[2]) for r, c in (('host', host), ('guest', guest))}
    h = hut(guest)
    if ck(h is not None, f'the guest\'s own hut is on its screen ({h})'):
        guest.call(f'move {h[0]} {h[1]}')
        time.sleep(0.6)
        guest.call(f'click {h[0]} {h[1]}')
        first, done = {}, {}
        t0 = time.time()
        while time.time() - t0 < 30 and len(done) < 2:
            for r, c in (('host', host), ('guest', guest)):
                if r in done:
                    continue
                for i, x in species(c)[2].items():
                    if i in ids0[r] or x['dead']:
                        continue
                    first.setdefault(r, (i, time.time()))
                    if x['key'] == gkey and x['owner'] == 1:
                        done[r] = time.time() - first[r][1]
            time.sleep(0.25)
        print(f'  new member: first seen {({r: (v[0], round(v[1] - t0, 1)) for r, v in first.items()})}, '
              f'of the guest\'s species after {done}', flush=True)
        for r in ('host', 'guest'):
            ck(r in first, f'{r}: the member the guest bought is there')
            ck(r in done and done[r] <= 2.0, f'{r}: it is the guest\'s and of the guest\'s species within 2 s '
                                             f'({done.get(r)})')

    # 4. a member carrying food when its species changes
    switch((host, guest), False)
    res, t = wait_mix({'host': host, 'guest': guest}, slot, 15)  # (off: every member the slot's species)
    for r, c in (('host', host), ('guest', guest)):
        # (BUG-1313: a kid, age != 1, keeps its species while it grows up, also when the switch goes off: the bought
        # baby of step 3, lab 20261119-123654)
        left = [i for i, x in species(c)[2].items() if x['key'] == gkey and x['age'] == 1]
        print(f'  {r} with the switch off: members still of the guest\'s species {left}', flush=True)
        ck(not left, f'{r}: with the switch off the guest\'s members are the tribe\'s species again ({left})')
    _, _, ms = species(host)
    cand = [i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead']]
    cand.sort(key=lambda i: looks(host).get(i, {}).get('leader', False))  # (not the chief first: it holds the staff)
    mid = cand[0] if cand else None
    at = ai(host, mid)['pos'] if mid else None
    ts = sorted((t for t in trees(host) if t[2] and at and 15 <= dist(t[1], at) <= 70), key=lambda t: dist(t[1], at))
    carried = False
    if ck(mid and ts, f'a guest\'s adult member (#{mid}) and a fruit tree 15-70 m from it ({len(ts)})'):
        tree = ts[0][1]
        print('  host:', host.call(f'coop.tribe order {mid} 2 {tree[0]} {tree[1]} {tree[2]}')['out'].strip(), flush=True)
        t0 = time.time()
        n = 0
        while time.time() - t0 < 60:
            a = ai(host, mid)
            if a['carry'] > 0:
                carried = True
                break
            if n % 10 == 0:
                print(f'  member #{mid} after {time.time() - t0:.0f} s: {a} (tree {tree})', flush=True)
            n += 1
            time.sleep(1)
        print(f'  member #{mid} carries {ai(host, mid)["carry"]} after {time.time() - t0:.0f} s', flush=True)
    switch((host, guest), True)
    if ck(carried, f'member #{mid} carries food when the switch comes on'):
        t0 = time.time()
        got = {}
        while time.time() - t0 < 6 and len(got) < 2:
            for r, c in (('host', host), ('guest', guest)):
                x = species(c)[2].get(mid)
                if r not in got and x and x['key'] == gkey:
                    got[r] = time.time() - t0
            time.sleep(0.25)
        print(f'  member #{mid} of the guest\'s species after {got} (carry {ai(host, mid)["carry"]})', flush=True)
        for r in ('host', 'guest'):
            ck(r in got and got[r] <= 4.0, f'{r}: the carrying member #{mid} is of the guest\'s species within 4 s ({got.get(r)})')
        closeup(host, guest, mid, 'carrying', f'same view 12 m from member #{mid} that carries food: the guest\'s '
                                              'own creature on both screens, walking with its load (no T-pose)')
        seen = {'host': [], 'guest': []}
        p0 = ai(host, mid)['pos']
        moved, home = 0.0, False
        t0 = time.time()
        while time.time() - t0 < 30:
            for r, c in (('host', host), ('guest', guest)):
                seen[r].append(ai(c, mid)['anim'])
            a = ai(host, mid)
            if a['pos'] and p0:
                moved = max(moved, dist(a['pos'], p0))
            home |= a['carry'] == 0
            time.sleep(1)
        print(f'  member #{mid} in 30 s: moved {moved:.1f} m, food home {home}, animations '
              f'{ {r: sorted(set(v) - {None}) for r, v in seen.items()} }', flush=True)
        ck(moved >= 3 or home, f'member #{mid} goes on with its action after the change (moved {moved:.1f} m, home {home})')
        for r, v in seen.items():
            anims = [x for x in v if x is not None]
            ck(anims and '00000000' not in anims and len(set(anims)) >= 2,
               f'{r}: member #{mid} plays animations all along, never none ({sorted(set(anims))})')

    # 5. the host's save: the tribe's species only
    slotname, folder, planet = save_slot(host, lane.HOST_DIR)
    said, _, since = twoinst.save_taken(host, 30)  # BUG-1398: asked again while the mod refuses it for now
    print('  host:', said[:80], f'(slot {slotname})', flush=True)
    saved = wait_saved(folder, since - 1, 90)
    time.sleep(3)
    hits = keys_in_slot(folder, [gkey, orig, outfit]) if saved else ['not saved']
    print(f'  host save slot {folder}: {hits or "no key of the guest"}', flush=True)
    if hits:  # (the save itself for the report: the next save of the test overwrites it)
        try:
            import frames
            import shutil
            d = frames.frames_dir(lane.out_dir())
            os.makedirs(d, exist_ok=True)
            for f in os.listdir(folder):
                if f.endswith('.spo'):
                    shutil.copy(os.path.join(folder, f), os.path.join(d, 'host_save_' + f))
            print(f'  (the host\'s save kept in {d})', flush=True)
        except Exception as e:
            print(f'  (the save could not be kept: {e})', flush=True)
    print('  host:', next((x for x in (host.call('coop.savestrip')['out'] or '').splitlines() if 'keys=' in x), ''),
          flush=True)  # (the save's key filter, BUG-1315)
    st = next((l for l in (host.call('coop.status').get('out') or '').splitlines() if 'own species' in l), '')
    print(f'  host {st.strip()}', flush=True)
    ck(saved and not hits, f'the host\'s save names neither the guest\'s key {gkey:08X} nor its creations {orig:08X} / '
                           f'{outfit:08X} ({hits})')
    ok, txt = mix_ok(host, gkey)
    ck(ok, f'host: the guest\'s members are of the guest\'s species again after the save ({txt})')

    # 6. a new evening (the host's save loaded), then a rejoin of the guest alone
    host, guest, info = twoinst.session_restart(host, guest)
    print(f'  session restart: {info}', flush=True)
    t0 = time.time()
    while time.time() - t0 < 60 and not (line_key(host, 1) == gkey and line_key(guest, 1) == gkey):
        time.sleep(1)
    ck(line_key(host, 1) == gkey and line_key(guest, 1) == gkey, f'after the new evening the guest\'s line is back from '
       f'the host\'s store on both games ({line_key(host, 1):08X}, {line_key(guest, 1):08X}, want {gkey:08X})')
    res, t = wait_mix({'host': host, 'guest': guest}, gkey, 40)
    for r, (ok, txt) in res.items():
        print(f'  {r} after the new evening ({t:.0f} s): {txt}', flush=True)
        ck(ok, f'{r}: after the host loaded its save the mix is back (the guest\'s members its species, the host\'s the tribe\'s)')
    twoinst.keyframe('reloaded', 'after the host loaded its save: the guest\'s members as the guest\'s own creature, '
                                 'the host\'s as the tribe\'s species, on both games')
    for mon in twoinst._monitors:
        mon.stop = True
    kill_game(lane.GUEST)
    time.sleep(3)
    lane.run_guest()
    if not wait_up(lane.GUEST, 90):
        lane.run_guest()
        assert wait_up(lane.GUEST), 'the guest did not start'
    guest = Console(lane.GUEST)
    twoinst.apply_fix_off(guest, 'guest')
    t0 = time.time()
    while time.time() - t0 < 120 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 180), 'the guest is in the host\'s world again')
    watch(lane.HOST, lane.GUEST)
    res, t = wait_mix({'host': host, 'guest': guest}, gkey, 40)
    for r, (ok, txt) in res.items():
        print(f'  {r} after the rejoin ({t:.0f} s): {txt}', flush=True)
        ck(ok, f'{r}: after the guest\'s rejoin the mix is back')
    twoinst.keyframe('rejoined', 'after the guest joined again: the guest\'s members as the guest\'s own creature, '
                                 'the host\'s as the tribe\'s species, on both games')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
