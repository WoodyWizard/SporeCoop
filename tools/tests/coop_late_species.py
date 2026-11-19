#!/usr/bin/env python3
"""A guest without an own creature in this world gets to its own species in Tribe and Civ (BUG-1289, WP-S1a of the
0.7.16 late joiners, docs/re/creations.md#creations-late-species).

A guest that joined at Tribe or Civ (or whose host loaded a save of a planet it never played) has no record of an own
creature for this world (worlds.ini, BUG-0345) and its old choice is dropped there (BUG-0638): it played the host's
species, its species line had no creature, and the co-op window showed the creation row only in Cell, Creature and
Space. With `creations.late_species` the row shows in Tribe and Civ for such a guest (and in the galaxy menu), and a
choice made outside a world holds for the next world it plays. Its line then names its creature and every game draws
its tribe members / city citizens in it (tribe_species.cpp, civ_species.cpp).

Tribe (default; tribe fixture: the cold start leaves the guest without choice and world record):
  1. the host's tribe gets 2 more adults; the guest has own creatures and plays the host's species: no game has a
     Tribe line for it and every member of the player tribe is of the slot's species on both games (`coop.tribe
     species`: the game's own +0xB28 of every member);
  2. the guest's co-op window shows "My creature: ... < >" in Tribe; its ">" (a real click): within 30 s both games
     answer one Tribe key for the guest (its creature's shared copy 0x7C......), every guest-owned member is of it and
     every host-owned member of the slot's on both games, the guest's members have one model record on both games;
     key frames: the guest's window with the row, the village, a same view 12 m from a guest's member;
  3. BUG-0638 kept: the world record forgotten (`coop.creations worlds forget`, as a world the choice was not made in):
     the guest plays the host's species again, its members are the slot's on both games;
  4. a choice outside a world: the guest's game is killed and started again, in its galaxy menu (no world) it chooses
     another own creature (`coop.creations choose`, what the menu's row does), then it joins: within 40 s that creature
     is recorded for this world, both games answer its key, the guest's members wear it on both games; key frame.
--civ (civ_npc fixture): the guest buys the NPC city nearest the capital; its citizens are the civilization's species
  on both games (no line); the guest's window shows the creature row in Civ (not "My role"); its ">": both games answer
  its creature for its Civ line and the city's citizens are of it on both games (`coop.civ citizens`: the citizen
  objects' +0xB28) within 30 s; the guest's Civ role is what it was; key frames of the city and a citizen up close.

SPORE_FIX_OFF=creations.late_species (suites late_species_off, late_species_civ_off: the twins): step 2 FAILs (no row
in Tribe / Civ: 0.7.15) and step 4 FAILs (the menu's choice is dropped by BUG-0638).

  SPORE_LANE=4 python3 tools/tests/coop_late_species.py [--civ]
"""
import re
import sys
import time

import lane
import twoinst
from coop_civ import civ, dist
from coop_civ_own_species import closeup as civ_closeup, fmt, species as citizens, wait_city
from coop_creature_desync import kill_game
from coop_tribe_own_species import closeup, line_key, model_hash, species, wait_mix
from coop_ui import Ui
from harness import field
from play_civ import frame_city
from twoinst import Checks, Console, keyframe, start, wait_guest_in, wait_up, watch

SWITCH = 'creations.late_species'
CIV = '--civ' in sys.argv


def mine(con):
    return field(con.call('coop.creations')['out'], r'mine creature: ([0-9A-F]{8})') or '00000000'


def late(con):
    out = con.call('coop.creations')['out'] or ''
    m = re.search(r'late: (.*) \(BUG-1289\)', out)
    return m.group(1) if m else '?'


def own_list(con):
    return re.findall(r'^creature ([0-9A-F]{8}) ', con.call('coop.creations list')['out'], re.M)


def line_civ(con, pid=1):
    m = re.search(r'key=([0-9A-F]{8})/\S+ known=(\d)', con.call(f'coop.species key {pid} civ').get('out') or '')
    return int(m.group(1), 16) if m and m.group(2) == '1' else 0


def wait_line(cons, getter, secs, pred):
    t0 = time.time()
    got = {}
    while time.time() - t0 < secs:
        got = {r: getter(c) for r, c in cons.items()}
        if all(pred(k) for k in got.values()) and len(set(got.values())) == 1:
            return got, True
        time.sleep(1)
    return got, False


def pick_row(ck, guest, stage):
    """2.: the creature row in this stage on the guest's window, its '>' clicked. The row text before the click"""
    gu = Ui(guest, 'guest')
    guest.call('coop.uistate 1')
    st = gu.wait(lambda s: s['open'] == '1' and s.get('creation', '').startswith('My creature') and s['cnext']['vis'],
                 30, f'the creature row in {stage}')
    row = st.get('creation') if st else None
    print(f'  guest window in {stage}: {row!r}; {late(guest)}', flush=True)
    if not ck(st, f'the guest\'s co-op window offers its own creatures in {stage} ("My creature: ... < >": {row})'):
        guest.call('coop.uistate 0')
        return None
    keyframe(f'{stage.lower()}_row', f'{stage}: the guest\'s co-op window shows "My creature: the host\'s species" with '
                                     '< > and the count of its own creatures; the host\'s screen plays on', settle=0)
    gu.click('cnext')
    time.sleep(2)
    print(f'  guest after ">": mine {mine(guest)}, {late(guest)}', flush=True)
    guest.call('coop.uistate 0')
    return row


def restart_guest(host, before_join=None, close=False):
    """the guest's game killed and started again; `before_join(guest)` in its galaxy menu; then the join.
    close: the guest's game ends by its own exit first (`coop.close`, WM_CLOSE: the player closes the window), killed
    only when it is still there after 60 s. BUG-1327: a second SIGKILL of the guest in civ_late_arrival (after its own
    Civ editors) took the lane's wineserver with it: the host's game lost its sockets (console refused, log ends) and
    the test died in wait_guest_in (lab 20261119-122505, -130049, a manual run on lane 2)"""
    for mon in twoinst._monitors:
        mon.stop = True
    if close:
        try:
            print('  guest:', Console(lane.GUEST).call('coop.close')['out'].strip(), flush=True)
        except OSError:
            pass
        import subprocess

        def running():
            return subprocess.run([lane.SPORE, 'pids', str(lane.GUEST)], capture_output=True, text=True).stdout.split()
        t0 = time.time()
        while time.time() - t0 < 60 and running():
            time.sleep(1)
        print(f'  guest: its game ended by itself: {not running()} ({time.time() - t0:.0f} s)', flush=True)
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
    if before_join:
        before_join(guest)
    guest.call(lane.JOIN)
    ok = wait_guest_in(host, guest, 180)
    watch(lane.HOST, lane.GUEST)
    return guest, ok


def tribe(host, guest, ck):
    cons = {'host': host, 'guest': guest}
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    slot, _, _ = species(host)
    own = [k for k in own_list(guest) if int(k, 16) != slot]
    if not ck(len(own) >= 2 and slot, f'the guest has creatures of its own ({own[:4]}), the tribe a species '
                                      f'({slot or 0:08X})'):
        return

    # 1. the host's species, no line
    print(f'  guest: mine {mine(guest)}, {late(guest)}', flush=True)
    ck(line_key(host, 1) == 0 and line_key(guest, 1) == 0, f'no game has a Tribe line for the guest yet '
       f'({line_key(host, 1):08X}, {line_key(guest, 1):08X})')
    res, t = wait_mix(cons, slot, 20)
    for r, (ok, txt) in res.items():
        print(f'  {r} before: {txt}', flush=True)
        ck(ok, f'{r}: every member is of the tribe\'s species {slot:08X} (the guest plays the host\'s)')

    # 2. the row in Tribe
    pick_row(ck, guest, 'Tribe')
    got, ok = wait_line(cons, lambda c: line_key(c, 1), 30, lambda k: k >> 24 == 0x7C)
    gkey = got.get('host', 0)
    print(f'  Tribe line of the guest: {({r: f"{k:08X}" for r, k in got.items()})} (mine {mine(guest)})', flush=True)
    if ck(ok, f'both games answer one Tribe key for the guest, its creature\'s shared copy ({got})'):
        res, t = wait_mix(cons, gkey, 25)
        for r, (ok2, txt) in res.items():
            print(f'  {r} after {t:.0f} s: {txt}', flush=True)
            ck(ok2, f'{r}: the guest\'s members are of its own creature {gkey:08X}, the host\'s of the tribe\'s '
                    f'{slot:08X}')
        hashes = {}
        for r, con in cons.items():
            _, smodel, ms = species(con)
            hashes[r] = {model_hash(con, m['model']) for m in ms.values() if m['owner'] == 1 and m['key'] == gkey}
        print(f'  guest members\' model records: {hashes}', flush=True)
        ck(hashes['host'] and len(hashes['host']) == 1 and hashes['host'] == hashes['guest'] and '?' not in hashes['host'],
           f'the guest\'s members have one model record, the same on both games ({hashes})')
        keyframe('tribe_picked', 'Tribe: the village on both games: the guest\'s members drawn as the guest\'s own '
                                 'creature (picked in the co-op window), the host\'s as the tribe\'s species, no T-pose')
        _, _, ms = species(host)
        gm = sorted(i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead'])
        if gm:
            closeup(host, guest, gm[0], 'tribe_member', 'same view 12 m from a guest\'s member: the guest\'s own '
                                                        'creature on both screens')

    # 3. BUG-0638 kept: no record of this world, the choice of "another world" does not hold
    print('  guest:', guest.call('coop.creations worlds forget')['out'].strip().splitlines()[0], flush=True)
    got, ok = wait_line(cons, lambda c: line_key(c, 1), 30, lambda k: k == 0)
    res, t = wait_mix(cons, slot, 25)
    for r, (ok2, txt) in res.items():
        print(f'  {r} without the record ({t:.0f} s): {txt}', flush=True)
        ck(ok and ok2, f'{r}: without a record of this world the guest\'s old choice does not hold (BUG-0638): its '
                       f'members are the tribe\'s species again ({got})')

    # 4. a choice in the galaxy menu (no world), then the join
    pick = own[1] if own[1] != mine(guest) else own[0]

    def choose_in_menu(g):
        print(f'  guest in its galaxy menu ({g.call("game.mode")["out"].strip()}): '
              f'{g.call(f"coop.creations choose creature {pick}")["out"].strip()} {pick}; {late(g)}', flush=True)

    guest, joined = restart_guest(host, choose_in_menu)
    cons['guest'] = guest
    twoinst.PLAYERS['guest'] = guest
    if not ck(joined, 'the guest is in the host\'s Tribe world again'):
        return guest
    got, ok = wait_line(cons, lambda c: line_key(c, 1), 40, lambda k: k >> 24 == 0x7C)
    rec = field(guest.call('coop.creations worlds')['out'], r'creature=([0-9A-F]{8})')
    print(f'  after the join: mine {mine(guest)} (chosen {pick}), record {rec}, line {got}; {late(guest)}', flush=True)
    ck(mine(guest) == pick and rec == pick, f'the creature chosen in the galaxy menu holds in the host\'s world and is '
                                            f'recorded for it (mine {mine(guest)}, record {rec}, chosen {pick})')
    if ck(ok, f'both games answer one Tribe key for the guest after the join ({got})'):
        res, t = wait_mix(cons, got['host'], 40)
        for r, (ok2, txt) in res.items():
            print(f'  {r} after the join ({t:.0f} s): {txt}', flush=True)
            ck(ok2, f'{r}: the guest\'s members wear the creature it chose in the galaxy menu')
        keyframe('tribe_menu_choice', 'Tribe, after the guest joined with a creature chosen in its galaxy menu: its '
                                      'members drawn as that creature, the host\'s as the tribe\'s species, both games')
    return guest


def civilization(host, guest, ck):
    players = {'host': host, 'guest': guest}
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(len(cap) == 1 and npc and own_list(guest), f'one player city {cap}, NPC cities {npc}, own creatures of '
                                                         f'the guest ({len(own_list(guest))})'):
        return
    cap = cap[0]
    gci = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap]['pos']))
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)
    print(f'  guest buys city {gci}:', guest.call(f'coop.civ buycity {gci} 500')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not all((c := civ(x)[0].get(gci)) and c['owner'] == 1 for x in (host, guest)):
        time.sleep(1)
    if not ck(all((c := civ(x)[0].get(gci)) and c['owner'] == 1 for x in (host, guest)),
              f'city {gci} is the guest\'s on both games'):
        return
    _, civkey, _, _ = citizens(host)
    frame_city(players, hc[gci]['pos'])
    seen, t = wait_city(players, gci, civkey, 30)
    print(f'  city {gci} before: {fmt(seen)} (the civilization {civkey:08X}); guest mine {mine(guest)}, '
          f'{late(guest)}', flush=True)
    ck(line_civ(host) == 0 and all(set(v) == {civkey} for v in seen.values()) and all(seen.values()),
       f'the guest has no Civ line and its city\'s citizens are the civilization\'s species on both games ({fmt(seen)})')
    role0 = field(guest.call('coop.role')['out'], r'(?m)^role 1 .* (\w+)$')
    row = pick_row(ck, guest, 'Civ')
    ck(row is None or not row.startswith('My role'), f'the row picks a creature, not a role ({row})')
    got, ok = wait_line(players, line_civ, 30, lambda k: k >> 24 == 0x7C)
    print(f'  Civ line of the guest: {({r: f"{k:08X}" for r, k in got.items()})}', flush=True)
    if ck(ok, f'both games answer one Civ key for the guest, its creature\'s shared copy ({got})'):
        frame_city(players, hc[gci]['pos'])
        seen, t = wait_city(players, gci, got['host'], 30)
        print(f'  city {gci} after: {fmt(seen)} after {t:.0f} s', flush=True)
        ck(all(set(v) == {got['host']} for v in seen.values()) and all(seen.values()),
           f'the guest\'s city\'s citizens are of the guest\'s own creature on both games ({fmt(seen)})')
        keyframe('civ_picked', f'Civ: the guest\'s city {gci} on both games: its citizens walking about are the guest\'s '
                               'own creature (picked in the co-op window), not the capital\'s species')
        civ_closeup(players, hc[gci]['pos'], 'civ_citizen', 'one citizen of the guest\'s city up close, the same camera '
                                                           'on both games: the guest\'s own creature on both screens')
    role1 = field(guest.call('coop.role')['out'], r'(?m)^role 1 .* (\w+)$')
    ck(role0 == role1, f'the guest\'s Civ role is unchanged by the row\'s arrows ({role0} -> {role1})')
    return guest


def main():
    host, guest = start('civ_npc' if CIV else 'tribe')
    ck = Checks()
    try:
        guest = (civilization if CIV else tribe)(host, guest, ck) or guest
    finally:
        try:  # the guest's lane profile as the cold start leaves it (no choice, no record of this world)
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call('coop.creations worlds set creature 0')['out'].strip().splitlines()[0], flush=True)
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
