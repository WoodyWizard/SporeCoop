#!/usr/bin/env python3
"""The species line through the stages: a guest's own creature, Creature -> Tribe -> Civ the campaign's way, is its
look in every stage on both games (WP-X2 of docs/plan-0.7.16.md, BUG-1277; docs/01-architecture.md#arch-species-line,
docs/06-testing.md#testing-line-stages).

Two games from the creature fixture; every stage change is the campaign's own (coop_campaign.py Campaign.advance and
follow: the host fills the stage's bar, presses History / the advance button like a player, the guest answers the vote
with its real "I'm ready" click, the editors are left with "done", the captions with their check; the guest follows
through the world share). Every check reads the game's own fields on both games, never a counter of the mod:

1. Creature. The guest plays its own creature (creations_state.choose_own). On the guest its avatar (the creature's
   species key +0xB28, `coop.census creatures` own=avatar) is its own creation; on the host the guest's puppet (own=puppet
   owner=<guest>) has the session's copy of it (0x7C......, `coop.creations` "player 1 creature") and the host's game
   builds it from the guest's model (`coop.creations modelhash`: the same model record as the guest's own); the line's
   Creature key (`coop.species key 1 creature`) is that copy on both games. Key frames: the game cameras and a same view
   10 m from the guest's creature. The host saves (Campaign.segment_save, the campaign's segment save): no file it writes
   names the guest's copy or its own creation (records parsed, coop_civ_own_designs.keys_in).
2. Creature -> Tribe (Campaign.advance('creature'): `coop.dna add 3000`, History, the vote, the editors, the fire
   cinematic; follow: the guest in the host's Tribe world). The campaign's phase key frame.
3. Tribe. The host's tribe gets members for every player (`coop.tribe spawn 6`, the campaign's set-up). The guest still
   plays its own creature (the host's "player 1 creature" is a 0x7C copy, not the tribe's slot); its Tribe line is that
   creature on both games; within 40 s every guest-owned member of the player tribe is of it (+0xB28, `coop.tribe
   species`), every host-owned member of the slot's; the guest's members are built from one model, the same record on
   both games and not the slot's. Key frames: the village and a same view 12 m from a guest's member.
   --outfit: then the guest's own Tribal Outfitter (coop_tribe_own_outfitter.outfitter_route: its hut, the card's edit
   badge, a body piece, Done): its Tribe line becomes the new outfit (one shared key 0x7B...... on both games, not the
   creature) and its members wear it on both games; a same view of a guest's member again.
   The host saves: no file names the guest's Tribe key, its creature (copy and own) or the outfit's creation.
4. Tribe -> Civ (Campaign.advance('tribe'): the goal bar filled by `coop.tribe advance` as tribe_civ does, then the
   campaign's advance button, stage card, captions, votes, the host's City Hall and vehicle editors; --raids: the
   campaign's raids fill it instead, ~15-20 min more). The campaign's phase key frame.
5. Civ. The guest's Civ line is its Tribe line on both games (no Civ outfit: the rule of species_line_core.h).
   --civ-editors (once WP-C3 / WP-X1 are in rel/0.7.16): the guest's own City Hall and vehicle editors open after its
   arrival (the campaign's driver answers them: three parts, a name, done) and their results are its designs on both
   games. Without the flag the guest names a vehicle of its My Creations as its land vehicles' design
   (`coop.civ owndesign set`, WP-C2's lab setter: the stand-in for the editor; the vehicle models of
   coop_civ_own_designs, the host's own My Creations hold another model under the same id).
   The capital (shared, founded by the host) has citizens of the civilization's species on both games. The host's game
   captures the nearest NPC city for the guest (`coop.civ siege`; the campaign's SpawnNextCity shortcut first when no NPC
   city stands yet): within 30 s every citizen of it is of the guest's line (+0xB28, `coop.civ citizens`) on both
   games. The guest buys a land vehicle in its city: on both games its model key (+0xC4, `coop.civ owndesign`) is the
   guest's design and is built from the guest's model (the guest's vehicle model, not the host's of the same id).
   --civ-editors also: the guest's city's City Hall is of the guest's City Hall design on both games. Key frames: the
   guest's city, a same view of one of its citizens, a same view of the vehicle. The host saves: no file names the
   guest's Civ key, its creature or its designs.
Campaign findings other than `info` (a dead game, a crash report, a freeze, a caught fault, a vote time-out, a guest
left behind) fail the test too.

SPORE_FIX_OFF=species.line (suite line_stages_off, the twin): the guest has no line; step 1's line check and step 3's
members FAIL (every member the tribe's slot species on both games); the test stops after step 3.

  SPORE_LANE=2 python3 tools/tests/coop_line_stages.py [--outfit] [--civ-editors] [--raids] [--seed N]
"""
import argparse
import os
import random
import re
import time
from types import SimpleNamespace

import coop_civ_own_designs as cod
import creations_state
import keyviews
import lane
import sameview as sv
import twoinst
from coop_campaign import CIV_ARRIVAL_PARTS, Campaign, Player, Service, phase_frame
from coop_civ import civ, dist
from coop_civ_designs import two_models
from coop_civ_own_species import check_paint, closeup as citizen_closeup, fmt, species as citizens, wait_city
from coop_creature_e84 import close_boxes
from coop_species_line import key as line_of
from coop_tribe_outfit import my_creations, profile
from coop_tribe_own_outfitter import outfitter_route, own_tribe, tribe_line
from coop_tribe_own_species import closeup as member_closeup, line_key, model_hash, species as members, wait_mix
from harness import field, fnv, mode, waiter
from play_civ import answer_talk, frame_city
from twoinst import Checks, Console, keyframe, start

wait = waiter(step=1.0, final=True)
CITY_HALL = 0x99E92F05
VTYPE, VGROUP = cod.VTYPE, cod.VGROUP


def talk_before_frames(players):
    """BUG-1459: a nation's talk (a trade route offer) that waits on a game covers its view: answered before the Civ
    close-ups (line_stages_outfit 09-11 of lab 20261122-194210 showed the "Red Nation" offer, not the city)"""
    for r, c in players.items():
        said = answer_talk(c)
        if said:
            print(f'  civ: {r}: a nation\'s talk answered before the key frames: {said}', flush=True)


def hx(k):
    return f'{k or 0:08X}'


def creature_view(host, guest, pid):
    """the game's own creatures: the guest's avatar on the guest, its puppet and the host's avatar on the host, and the
    session's copy of the guest's creature on the host (creations' player table)"""
    hrows = sv.parse_census(host.call('coop.census creatures')['out'])
    grows = sv.parse_census(guest.call('coop.census creatures')['out'])
    pup = next((r for r in hrows if r['own'] == 'puppet' and r['owner'] == pid), None)
    hav = next((r for r in hrows if r['own'] == 'avatar'), None)
    gav = next((r for r in grows if r['own'] == 'avatar'), None)
    copy = field(host.call('coop.creations')['out'], rf"player {pid} creature: ([0-9A-F]{{8}})")
    mine = field(guest.call('coop.creations')['out'], r"mine creature: ([0-9A-F]{8})")
    return dict(puppet=pup and pup['key'], pos=pup and pup['pos'], host=hav and hav['key'], guest=gav and gav['key'],
                copy=copy, mine=mine)


def save_check(ck, camp, host, stage, insts, what):
    """the host saves the campaign's way (Campaign.segment_save: coop.save, retried while an editor or a box is up, the
    slot kept in the run folder) and every file of the slot it wrote is parsed (DBPF records, else the file): none may
    hold one of `insts` (little or big endian)"""
    insts = sorted({i for i in insts if i})
    t0 = time.time()
    camp.segment_save(stage)
    close_boxes(host, 8)
    seg = camp.segments.get(stage) or {}
    if not ck(seg.get('slot'), f'{stage}: the host saved its game ({seg or "no save"})'):
        return
    folder = os.path.join(lane.HOST_DIR, 'Games', seg['slot'])
    files = cod.save_files(folder, t0 - 2)
    found = cod.keys_in(files, insts)
    hits = {hx(i): v[:4] for i, v in found.items() if v}
    print(f'  {stage} save {seg["slot"]}: {len(files)} files written; keys {[hx(i) for i in insts]}: '
          f'{hits or "none found"}', flush=True)
    ck(files and not hits, f'{stage}: the host\'s save holds no key of the guest\'s ({what}: {hits or "none"})')


def run_creature(ck, camp, host, guest, pid, off_line):
    own = creations_state.choose_own(guest)
    if not ck(own, f'creature: the guest has a creature of its own ({own})'):
        return None

    def good():
        v = creature_view(host, guest, pid)
        ok = v['copy'] and v['copy'].startswith('7C') and v['puppet'] == v['copy'] and v['guest'] in (v['mine'], v['copy']) \
            and v['guest'] != v['host'] and v['mine'] == own
        return v if ok else None
    v = wait(good, 60) or creature_view(host, guest, pid)
    print(f'  creature: {v}', flush=True)
    ok = ck(v['copy'] and v['copy'].startswith('7C') and v['puppet'] == v['copy'],
            f'creature: the host\'s game has the guest\'s puppet of the session\'s copy of its own creature '
            f'(+0xB28 {v["puppet"]}, the copy {v["copy"]})')
    ck(v['guest'] in (v['mine'], v['copy']) and v['mine'] == own and v['guest'] != v['host'],
       f'creature: the guest\'s game plays its own creature (+0xB28 {v["guest"]}, its own {own}; the host\'s {v["host"]})')
    if ok and v['guest']:
        hh, gh = model_hash(host, int(v['copy'], 16)), model_hash(guest, int(v['guest'], 16))
        ck(hh != '?' and hh == gh, f'creature: the host\'s game builds the guest\'s creature from the guest\'s model '
                                   f'({hh}, the guest\'s {gh})')
    lines = {r: line_of(c, pid, 'creature') for r, c in (('host', host), ('guest', guest))}
    print(f'  creature line: {lines}', flush=True)
    ck(v['copy'] and all(k == v['copy'] and known for k, known, _ in lines.values()),
       f'creature: the line\'s Creature key is the guest\'s copy on both games ({lines}){" (species.line off)" if off_line else ""}')
    keyframe('creature', 'Creature, both games: the guest plays its own creature (not the host\'s species), the host '
             'draws the guest with the same model and paint', calm=5)
    if v['pos']:
        try:
            set_ok = keyviews.look_both((host, guest), v['pos'], dist=10, height=4, k=0, fov=45, look_up=1.0)
            print(f'  creature same view at {v["pos"]}: {set_ok}', flush=True)
            keyframe('creature_guest', 'same view 10 m from the guest\'s creature: its own creature (the same model and '
                     'paint) on both screens, standing on the ground', kind='sameview', settle=0)
        finally:
            keyviews.view_off(host, guest)
    save_check(ck, camp, host, 'creature', [int(x, 16) for x in (v['copy'], own) if x], 'its creature')
    return v


def run_tribe(ck, camp, a, host, guest, pid, off_line):
    """3: the Tribe part; returns (the guest's Tribe key, the keys a save must not hold) or (None, keys)"""
    t0 = time.time()
    while time.time() - t0 < 90 and '(player)' not in host.call('coop.tribe')['out']:
        time.sleep(2)  # the player tribe exists once the intro is over
    if a.raids:
        camp.bots('tribe', time.time())  # the campaign's set-up: members for every player, the bot game of the raid clicks
    else:
        camp.note('tribe set-up: ' + host.call('coop.tribe spawn 6')['out'].strip()[:120])
    time.sleep(6)
    slot, slot_model, _ = members(host)
    v = wait(lambda: (x := creature_view(host, guest, pid))['copy'] and x['copy'].startswith('7C') and x, 60) or \
        creature_view(host, guest, pid)
    copy = int(v['copy'], 16) if v['copy'] else 0
    mine = int(v['mine'], 16) if v['mine'] else 0
    keys = [copy, mine]
    print(f'  tribe: slot {hx(slot)} model {hx(slot_model)}; the guest\'s creature copy {hx(copy)}, own {hx(mine)}',
          flush=True)
    ck(copy >> 24 == 0x7C and copy != slot, f'tribe: the guest still plays its own creature in Tribe (the host\'s copy '
                                            f'{hx(copy)}, the tribe\'s slot {hx(slot)})')
    want = copy
    got = wait(lambda: line_key(host, 1) == want and line_key(guest, 1) == want, 60)
    ck(want and got, f'tribe: the guest\'s Tribe line is its creature on both games (host {hx(line_key(host, 1))}, '
                     f'guest {hx(line_key(guest, 1))}, want {hx(want)})')
    mixed = check_mix(ck, host, guest, want, slot, 'tribe')
    keyframe('tribe_mix', 'Tribe after the live arrival, both games: the guest\'s members drawn as the guest\'s own '
             'creature, the host\'s as the tribe\'s species (two kinds), none in a T-pose', calm=5)
    member_view(host, guest, 'tribe_guest_member', 'same view 12 m from a guest\'s member after the live Creature -> '
                'Tribe change: the guest\'s own creature on both screens (no T-pose)')
    if not mixed and off_line:
        return None, keys  # the twin: nothing below would test the line
    if a.outfit:
        gk0, _, _, _ = profile(guest)
        res = outfitter_route(guest, ck, gk0, own_tribe(guest), my_creations(lane.GUEST_DIR))
        if res and res['back']:
            keys += [res['result']] + sorted(res['new'])
            got = wait(lambda: (h := tribe_line(host)[0]) and h != copy and tribe_line(guest)[0] == h and h, 40)
            lines = {r: tribe_line(c) for r, c in (('host', host), ('guest', guest))}
            print(f'  tribe: the guest\'s line after its outfitter: {lines}', flush=True)
            ck(got and got >> 24 == 0x7B, f'tribe: the guest\'s Tribe line is its new outfit on both games, not its '
                                          f'creature ({lines})')
            if got:
                want = got
                # a caption of the Tribe stage holds a game in its cinematic (paused) and the member pass waits for it
                # (lab 20261119-153243: the host's from 15:36:57 to 15:38:10, its members changed 72 s after the line
                # was known, after the 40 s of check_mix): the players' OK on each game's caption first
                from coop_editor_sync import cinematic
                t0 = time.time()
                while time.time() - t0 < 60 and any(cinematic(c) for c in (host, guest)):
                    for c in (host, guest):
                        if cinematic(c):
                            c.call('key enter')
                    time.sleep(2)
                print(f'  tribe outfit: captions closed after {time.time() - t0:.0f} s', flush=True)
                check_mix(ck, host, guest, want, slot, 'tribe outfit')
                member_view(host, guest, 'tribe_guest_outfit', 'same view 12 m from a guest\'s member: the guest\'s own '
                            'creature with its new piece of clothing (a leaf on the chest) on both screens')
    keys.append(want)
    save_check(ck, camp, host, 'tribe', keys, 'its Tribe line, creature and outfit')
    return want, keys


def check_mix(ck, host, guest, want, slot, what):
    res, t = wait_mix({'host': host, 'guest': guest}, want, 40)
    ok_all = True
    for r, (ok, txt) in res.items():
        print(f'  {what}: {r} after {t:.0f} s: {txt}', flush=True)
        ok_all &= bool(ck(ok, f'{what}: {r}: every guest member is of the guest\'s line {hx(want)} (+0xB28), every '
                              f'host member of the tribe\'s slot {hx(slot)}'))
    hashes = {}
    for r, con in (('host', host), ('guest', guest)):
        _, smodel, ms = members(con)
        hashes[r] = {model_hash(con, m['model']) for m in ms.values() if m['owner'] == 1 and m['key'] == want}
        ck(len(hashes[r]) == 1 and '?' not in hashes[r] and model_hash(con, smodel or 0) not in hashes[r],
           f'{what}: {r}: the guest\'s members are built from one model, not the slot\'s ({sorted(hashes[r])})')
    ck(hashes['host'] and hashes['host'] == hashes['guest'], f'{what}: the guest\'s members have the same model record '
                                                             f'on both games ({hashes})')
    return ok_all


def member_view(host, guest, tag, note):
    _, _, ms = members(host)
    gm = sorted(i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead'])
    if gm:
        member_closeup(host, guest, gm[0], tag, note)
    else:
        print(f'  ({tag}: no adult member of the guest)', flush=True)


def arrival_editors(ck, camp, guest_p, host, guest, pid, secs=360, required=True):
    """--civ-editors (WP-C3 / WP-X1): the guest's own City Hall and vehicle editors after its arrival in Civ, answered by
    the campaign's driver (the follow phase: the City Hall palette's parts, a name, done) until the guest's line has
    designs on both games. required=False (without --civ-editors): the same editors come on the merged tree
    (core.own_civ_arrival is on in own mode) and are answered the same way, unchecked (lab 20261119-164538
    line_stages_outfit: the guest arrived fast, its editors opened in the play phase with no driver and the guest stayed
    in its City Hall editor: no city of the host's on its game); nothing due: nothing to wait for"""
    if not required:
        st = re.search(r'phase=(\w+)', guest.call('coop.civarrival')['out'] or '')
        if not st or st.group(1) == 'none':
            print(f'  civ: no own editors due at the guest\'s arrival ({st.group(0) if st else "?"})', flush=True)
            return
    camp.phase = 'follow'
    svc = Service(camp, guest_p)
    svc.start()

    # (lab 20261119-151550 / 20261119-153243: "any design of the guest's" was true in the 6 s between its City Hall
    # editor and its vehicle editor; the driver stopped and the guest stayed in its vehicle editor for the rest of the
    # test: no city of the host's on its game ("no such city"), no vehicle design. Both editors end first: the
    # arrival's phase done, the guest back in Civ, the City Hall and the vehicle editor's design id in its line on
    # both games)
    t_ed, shots = time.time(), []

    def both_done():
        if 'Editor' in mode(guest):
            # (lab 20261119-165858 / 20261119-171852 line_stages_outfit: the guest's City Hall editor never ended: what
            # its screen shows)
            due = [t for t in (120, 240) if time.time() - t_ed > t and t not in shots]
            for t in due:
                shots.append(t)
                print(f'  civ: the guest still in its editor after {t} s: {twoinst.shot(guest, f"arrival_editor_{t}.png")}; '
                      f'{(guest.call("coop.vote")["out"] or "").splitlines()[:3]}', flush=True)
            return False
        st = re.search(r'phase=(\w+) .*editors=(\w+)/(\w+)', guest.call('coop.civarrival')['out'] or '')
        if not st or st.group(1) != 'done':
            return False
        want = {CITY_HALL, int(st.group(3), 16)} - {0}
        return all(want <= {d for p, d in cod.own(c)[2] if p == pid} for c in (host, guest))
    try:
        got = wait(both_done, secs)
    finally:
        svc.stop = True
        svc.join(30)
    print(f'  civ: the guest\'s editors: {svc.done}; designs host {cod.own(host)[2]}, guest {cod.own(guest)[2]}; '
          f'{(guest.call("coop.civarrival")["out"] or "").splitlines()[:1]}', flush=True)
    if required:
        ck(got, f'civ: the guest\'s own City Hall and vehicle editors at its arrival gave it both designs on both games '
                f'({svc.done})')
    else:
        why = re.search(r"why='([^']*)'", guest.call('coop.civarrival')['out'] or '')
        ck('Editor' not in mode(guest), f'civ: the guest is back in Civ after its own arrival editors ({svc.done}; '
                                        f'{why.group(1) if why else "?"})')


def run_civ(ck, camp, a, host, guest, guest_p, pid, want, keys):
    players = {'host': host, 'guest': guest}
    arrival_editors(ck, camp, guest_p, host, guest, pid, required=a.civ_editors)
    camp.phase = 'play'
    time.sleep(5)
    got = wait(lambda: all(line_of(c, pid, 'civ')[0] == hx(want) and line_of(c, pid, 'civ')[1] for c in (host, guest)), 60)
    lines = {r: line_of(c, pid, 'civ') for r, c in players.items()}
    ck(got, f'civ: the guest\'s Civ line is its Tribe line on both games ({lines}, want {hx(want)})')
    _, civkey, _, st = citizens(host)
    print(f'  civ: {st}; the civilization\'s species {hx(civkey)}', flush=True)
    ck(civkey and civkey != want, f'civ: the civilization\'s species ({hx(civkey)}) is another one than the guest\'s')
    hc = civ(host)[0]
    if not any(not c['player'] for c in hc.values()):
        camp.kick_civ({'n': 0})  # the campaign's shortcut: the cities still to come appear (SpawnNextCity)
        # (the campaign's SpawnNextCity(ex, 0, 0) only starts the game's own founding: lab 20261119-095414 and
        # 20261119-114816 had no new city 5 s / 61 s after "9 cities appeared", line_stages_outfit found 9 later. After
        # 30 s without one the test founds them at once, SpawnNextCity(ex, 1, 0) as civ_city_burst does: the guest's
        # game gets them through the host's city catch-up, BUG-0380)
        # (lab 20261119-123654: the host stayed in a cinematic for 2 min after the founding, `busy=cinematic,input_lock,
        # paused`: the game paused, no city appeared; the test ends it as a player's Enter, else EndCinematic)
        t0, instant, ended = time.time(), 0, 0
        while True:
            time.sleep(2)
            if 'movie=1' in (host.call('coop.status')['out'] or ''):  # (a cinematic plays: the campaign's sign)
                host.call('key enter')
                ended += 1
                if ended % 3 == 0:
                    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCinematicManager::Get cdecl')['out']
                                  or '')
                    if m:
                        host.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall {m.group(1)}')
            hc = civ(host)[0]
            if any(not c['player'] for c in hc.values()) or time.time() - t0 > 90:
                break
            if time.time() - t0 > 30 and not instant:
                m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'] or '')
                for _ in range(4 if m else 0):
                    r = re.search(r'eax=0x([0-9A-Fa-f]+)',
                                  host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 1 0')['out']
                                  or '')
                    if not r or int(r.group(1), 16) & 0xFF == 0:
                        break
                    instant += 1
                    host.call('wait.frames 30 10000')
        print(f'  civ: cities after the shortcut ({time.time() - t0:.0f} s, {instant} founded at once, {ended} cinematic '
              f'ends): {sorted(hc)}',
              flush=True)
    cap = [k for k, c in hc.items() if c['player']]
    npc = [k for k, c in hc.items() if not c['player']]
    if not ck(cap and npc, f'civ: the players\' capital {cap} and NPC cities {npc}'):
        return
    cap = cap[0]
    frame_city(players, hc[cap]['pos'])
    seen, t = wait_city(players, cap, civkey, 20)
    print(f'  civ: capital {cap}: {fmt(seen)} after {t:.0f} s', flush=True)
    ck(all(set(s) == {civkey} for s in seen.values()) and all(seen.values()),
       f'civ: the shared capital\'s citizens are the civilization\'s (its founder\'s) species on both games ({fmt(seen)})')

    # the guest's city
    tgt = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap]['pos']))
    gpos = hc[tgt]['pos']
    frame_city(players, gpos)
    time.sleep(4)
    print('  host:', host.call(f'coop.civ siege {tgt} {pid} 150 pc')['out'].strip(), flush=True)
    own_ok = wait(lambda: all(civ(c)[0].get(tgt, {}).get('owner') == pid for c in players.values()), 25)
    ck(own_ok, f'civ: city {tgt} is the guest\'s on both games')
    frame_city(players, gpos)  # (answers the specialty box of the capture)
    seen, t = wait_city(players, tgt, want, 30)
    print(f'  civ: the guest\'s city {tgt}: {fmt(seen)} after {t:.0f} s', flush=True)
    ck(all(set(s) == {want} for s in seen.values()) and all(seen.values()),
       f'civ: the guest\'s city\'s citizens are of the guest\'s line {hx(want)} (+0xB28) on both games ({fmt(seen)})')
    check_paint(ck, players, tgt, f'civ: the guest\'s city {tgt}')  # (BUG-1321: not drawn white)
    keyframe('civ_guest_city', f'Civ after the live Tribe -> Civ change: the city {tgt} the guest captured, both games: '
             'its citizens walking about are the guest\'s own creature (the same as in Creature and Tribe), not the '
             'civilization\'s species')
    talk_before_frames(players)
    citizen_closeup(players, gpos, 'civ_guest_citizen', 'one citizen of the guest\'s city up close, the same camera on '
                    'both games: the guest\'s own creature on both screens')

    # the guest's vehicle
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)
    m = re.search(r'did (\w+) purpose=(\d+)', host.call(f'coop.civ owndesign did {tgt} 0')['out'])
    buy_did = int(m.group(1), 16) if m else 0
    buy_at = tgt
    if a.civ_editors:
        # BUG-1323 (lab 20261119-122505): the arrival's vehicle editor is the one of the archetype (F670AA43 /
        # 9AD7D4AA / 7D433FAD, civ_arrival.cpp vehicle_editor), not always the land design of the city the guest
        # captured: buy in the guest's city or the shared capital whose land design id is that editor's, as
        # civ_own_arrival does
        ve = re.search(r'editors=\w+/(\w+)', guest.call('coop.civarrival')['out'] or '')
        vdid = int(ve.group(1), 16) if ve else 0
        # (lab 20261119-150332: a city the guest takes by siege becomes military, 7D433FAD; a guest may buy only in a
        # city of its own: when the captured one is not of the editor's design id, the guest buys an NPC city that is,
        # `coop.civ buycity` as civ_own_arrival, which keeps the city's kind)
        land = {}
        for ci in [tgt] + [c for c in sorted(npc, key=lambda i: dist(hc[i]['pos'], hc[cap]['pos'])) if c != tgt]:
            d = re.search(r'did (\w+) ', host.call(f'coop.civ owndesign did {ci} 0')['out'])
            land[ci] = int(d.group(1), 16) if d else 0
            if land[ci] == vdid:
                break
        print(f'  civ: land design ids {{{", ".join(f"{c}: {v:08X}" for c, v in land.items())}}} (the guest\'s vehicle '
              f'editor {vdid:08X})', flush=True)
        buy_at = next((c for c, v in land.items() if v == vdid), None)
        if buy_at is not None and buy_at != tgt:
            print(f'  guest buys city {buy_at}:', guest.call(f'coop.civ buycity {buy_at} 500')['out'].strip(), flush=True)
            if not wait(lambda: all(civ(c)[0].get(buy_at, {}).get('owner') == pid for c in players.values()), 30):
                buy_at = None
        if ck(buy_at is not None, f'civ: a city of the guest whose land vehicles are of its vehicle editor\'s design id '
                                  f'{vdid:08X} ({buy_at})'):
            buy_did = vdid
        else:
            buy_at = tgt
    want_hash = None
    if not a.civ_editors:
        for p in (0, 1, 2):
            d = re.search(r'did (\w+)', host.call(f'coop.civ owndesign did {tgt} 0 {p}')['out'])
            if d:
                print('  guest:', guest.call(f'coop.civ owndesign set {d.group(1)} {cod.G:08X}')['out'].strip(), flush=True)
        want_hash = f'{fnv(cod.VMODELS[1]):08X}'
    got = wait(lambda: all(cod.own(c)[2].get((pid, buy_did)) for c in players.values()), 40)
    ours = {r: cod.own(c)[2].get((pid, buy_did)) for r, c in players.items()}
    ours_v = ours['host'] or 0
    ck(got and ours_v >> 24 == 0x7B and ours['guest'] == ours_v,
       f'civ: the guest\'s line has its land vehicle design {hx(buy_did)} on both games, one shared key ({ours})')
    keys += [ours_v]
    if ours_v and want_hash is None:
        want_hash = cod.mhash(guest, ours_v, VTYPE, VGROUP)
    before = set(cod.own(host)[0])
    print('  guest:', guest.call(f'coop.civ buy {buy_at} 0')['out'].strip(), flush=True)
    vid = wait(lambda: next((i for i, x in cod.own(host)[0].items() if i not in before and x['owner'] == pid), None), 20)
    if ck(vid is not None, f'civ: the guest\'s purchase made a land vehicle on the host ({vid})'):
        for r, con in players.items():
            okv = wait(lambda: (x := cod.own(con)[0].get(vid)) and x['model'] == ours_v and
                       cod.mhash(con, x['model'], VTYPE, VGROUP) == want_hash, 15)
            x = cod.own(con)[0].get(vid) or {}
            h = cod.mhash(con, x.get('model', 0), VTYPE, VGROUP)
            hcul = cod.mhash(con, x.get('culture', 0), VTYPE, VGROUP)
            print(f'  civ: {r}: the guest\'s vehicle #{vid} {x} -> {h} (the culture\'s {hcul})', flush=True)
            if not okv:  # (the whole model key: a key of another type or group is "built from none")
                print('   ', [ln.strip() for ln in (con.call('coop.civ owndesign')['out'] or '').splitlines()
                              if f'ownveh #{vid} ' in ln or 'pdesign' in ln], flush=True)
            ck(okv and h != hcul, f'civ: {r}: the guest\'s vehicle is of the guest\'s design (+0xC4 {hx(x.get("model"))}, '
                                  f'built from {h}, the guest\'s model {want_hash}; the culture\'s {hcul})')
        vpos = (civ(host)[1].get(vid) or {}).get('pos')
        if vpos:
            talk_before_frames(players)
            try:
                set_ok = keyviews.look_both((host, guest), vpos, dist=12, height=6, k=0, fov=50, look_up=1.0)
                print(f'  civ: vehicle same view at {vpos}: {set_ok}', flush=True)
                keyframe('civ_guest_vehicle', 'same view 12 m from the guest\'s new land vehicle: the guest\'s own vehicle '
                         'design (not the host\'s vehicle of the capital) on both screens', kind='sameview', settle=0)
            finally:
                keyviews.view_off(host, guest)
    if a.civ_editors:
        ch = {r: cod.own(c)[2].get((pid, CITY_HALL)) for r, c in players.items()}
        ck(ch['host'] and ch['host'] >> 24 == 0x7B and ch['guest'] == ch['host'],
           f'civ: the guest\'s City Hall design is one shared key on both games ({ch})')
        keys.append(ch['host'] or 0)
        for r, con in players.items():
            halls = [b for b in cod.own(con)[1] if b['city'] == tgt and b['did'] == CITY_HALL]
            ck(halls and all(b['model'] == ch['host'] for b in halls),
               f'civ: {r}: the guest\'s city\'s City Hall is of the guest\'s design ({[hx(b["model"]) for b in halls]})')
    keys.append(want)
    save_check(ck, camp, host, 'civ', keys, 'its Civ line, creature and designs')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--outfit', action='store_true', help='Tribe: the guest\'s own Tribal Outfitter dresses its line')
    ap.add_argument('--civ-editors', action='store_true',
                    help='Civ: the guest\'s own City Hall and vehicle editors at its arrival (WP-C3 / WP-X1)')
    ap.add_argument('--raids', action='store_true', help='the end of Tribe by the campaign\'s raids (no goal bar shortcut)')
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    off_line = 'species.line' in twoinst.fix_off_names()
    if not a.civ_editors:  # the stand-in vehicle design: another model under one id in each profile (WP-C2's set-up)
        cod.BMODELS[:] = two_models(cod.BGROUP)
        cod.VMODELS[:] = two_models(VGROUP)
        twoinst.restore_creations = cod.restore_with_designs
    t_start = time.time()
    host, guest = start('creature')
    ck = Checks()
    players = [Player('Alice', lane.HOST), Player('Bob', lane.GUEST, lane.JOIN)]
    ca = SimpleNamespace(start='creature', end='civ', minutes=0, advance_minutes=None, players=2, noise=0.0,
                         rejoins=False, finale=False, finale_from_galaxy=False, jump=False, seed=seed)
    run_dir = lane.out_dir('line_stages', time.strftime('%Y%m%d-%H%M%S'))
    camp = Campaign(ca, players, run_dir, seed)
    if a.civ_editors:  # (the guest's own vehicle must not be the host's model byte for byte: its check against the culture's)
        camp.arrival_parts = CIV_ARRIVAL_PARTS
    camp.ids()
    camp.health()
    pid = players[1].pid
    print(f'  line stages: seed {seed}, outfit {a.outfit}, civ editors {a.civ_editors}, raids {a.raids}, run '
          f'{os.path.relpath(run_dir, lane.ROOT)}', flush=True)
    st0 = creations_state.save(guest)
    try:
        stages(ck, camp, a, host, guest, players[1], pid, off_line)
    finally:
        try:
            creations_state.restore(Console(lane.GUEST), st0)
        except OSError:
            pass
    bad = [it for it in camp.F.items if it['kind'] != 'info']
    for it in camp.F.items:
        print(f'    finding [{it["kind"]}] {it["stage"]}: {it["text"]}', flush=True)
    ck(not bad, f'no campaign finding (dead game, crash, freeze, fault, vote time-out, a guest left behind): {len(bad)}')
    print('  timeline:\n' + '\n'.join('    ' + ln for ln in camp.timeline[-50:]), flush=True)
    print(f'  ({time.time() - t_start:.0f} s)', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


def stages(ck, camp, a, host, guest, guest_p, pid, off_line):
    """Creature, the change to Tribe, Tribe, the change to Civ, Civ; stops at the first part that cannot go on"""
    if not ck(pid == 1, f'the guest is player 1 ({pid})'):
        return
    if run_creature(ck, camp, host, guest, pid, off_line) is None:
        return
    if not ck(camp.advance('creature'), 'the host changed Creature -> Tribe the players\' way and the guest followed'):
        return
    phase_frame(camp, 'advance', 'creature')
    want, keys = run_tribe(ck, camp, a, host, guest, pid, off_line)
    if want is None:
        return
    if not a.raids:
        camp.note('shortcut: coop.tribe advance (the goal bar full, as tribe_civ): '
                  + host.call('coop.tribe advance')['out'].strip()[:100])
    if not ck(camp.advance('tribe'), 'the host changed Tribe -> Civ the players\' way and the guest followed'):
        return
    phase_frame(camp, 'advance', 'tribe')
    run_civ(ck, camp, a, host, guest, guest_p, pid, want, keys)


if __name__ == '__main__':
    raise SystemExit(main())
