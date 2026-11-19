#!/usr/bin/env python3
"""The guest's own City Hall and vehicle editors after its arrival in Civ, at the live Tribe -> Civ change (WP-X1 of
docs/plan-0.7.16.md, BUG-1276, docs/re/civ.md#civ-own-arrival; the Cell -> Creature pattern of BUG-1240).

At the end of Tribe the host's game opens the City Hall and the vehicle editor (each after the host's vote); a guest's
game never runs that end: it follows into the host's Civ world with the host's designs (0.7.15). With
`core.own_civ_arrival` (own mode) the guest's game opens the same two editors once it has arrived in Civ and settled;
the host plays on meanwhile (no vote, no hold, no black screen: BUG-1263's lesson); the results go to the guest's
species line (species::set_design), never into the civilization's culture set.

tribe fixture, the guest plays a creature of its own My Creations (its Civ line differs from the civilization's).
Real clicks through the change, as tribe_civ does: the host's advance button (the goal bar filled by `coop.tribe
advance`), the stage card's check, the captions' checks, the guest's "I'm ready" in both votes, the host's City Hall
and vehicle editors (three parts, the name's dice, done). Both screens measured every 2 s (coop_evo_screens.Screens).
Checked:
  1. both games reach Civ; the guest's own City Hall editor opens AFTER its game has loaded the Civ world (its mode
     Civ first; `coop.civarrival` says which editor), then (real clicks in it: three parts, the dice, done) its own
     vehicle editor; the guest leaves both back in Civ;
  2. while the guest sits in its editors the host is not held: its game stays in Civ, no vote runs on it, its screen is
     not black for 20 s or more;
  3. the guest's line names both designs on both games (one shared key 0x7B......); the host's culture set is what it
     was when the host reached Civ (its City Hall key in cPlayer, the civilization's species, the world's player
     species too); the guest's copy keeps every entry it had;
  4. the guest gets a city (it buys the NPC city nearest the capital; the cities still to come are spawned first when
     none stands): on both games its City Hall is drawn from the guest's new City Hall (WP-C2's bem hash of the model
     key, `coop.creations modelhash`), the capital's is not; within 30 s its citizens are of the guest's Civ line
     (WP-C1, `coop.civ citizens`);
  5. the guest buys a land vehicle (its city menu's land button; `coop.civ buy` when not shown) in the city whose land
     design is the one its vehicle editor made: on both games it shows the guest's new vehicle design;
  6. key frames: the guest in its editor and the host's Civ world at the same moment; the guest's city (its City Hall,
     its citizens, its vehicle) on both screens; the capital on both screens;
  7. the host saves (`coop.save`): no file of the save names a key of the guest's new designs.
SPORE_FIX_OFF=core.own_civ_arrival (suite civ_own_arrival_off, the twin): step 1 FAILs (no own editor opens: 0.7.15);
the test ends there.

  SPORE_LANE=9 python3 tools/tests/coop_civ_own_arrival.py
"""
import os
import random
import re
import time

import lane
import twoinst
from coop_civ import civ, dist
from coop_civ_guest_editors import culture, drive_editor, line_design
from coop_civ_own_designs import keys_in, mhash, own, save_files
from coop_civ_own_species import fmt, species, wait_city
from coop_evo_screens import BLACK_LIMIT, Screens
from coop_tribe_civ import ADVANCE, CAPTION_OK, CARD_OK, busy, finish_editor, vote_active, where
from coop_ui import Ui
from harness import mode, waiter
from play_civ import BTN_LAND, Player, answer_specialty, frame_city
from twoinst import Checks, Console, keyframe, shot, start

SWITCH = 'core.own_civ_arrival'
CITY_HALL = 0x99E92F05
BTYPE, BGROUP = 0x2399BE55, 0x40636200
VTYPE, VGROUP = 0x24682294, 0x40646200
wait = waiter(step=1.0, final=True)


def arrival(con):
    """coop.civarrival: (status dict, results {editor: row}, text)"""
    out = con.call('coop.civarrival')['out'] or ''
    st = dict(re.findall(r'(\w+)=(\S+)', out.splitlines()[0])) if out else {}
    res = {}
    for m in re.finditer(r'result (\w+) editor=(\w+) seen=(\d) type=(\w+) key=(\w+)/(\w+)/(\w+) cancelled=(\d) taken=(\d)',
                         out):
        res[m.group(1)] = dict(editor=int(m.group(2), 16), seen=m.group(3) == '1', key=int(m.group(5), 16),
                               type=int(m.group(6), 16), group=int(m.group(7), 16), cancelled=m.group(8) == '1',
                               taken=m.group(9) == '1')
    return st, res, out


def game_fields(con):
    out = con.call('coop.civedit')['out'] or ''
    g = re.search(r'civ_species=(\w+)/\w+ world_species=(\w+)/\w+ city_hall=(\w+)/', out)
    return g.groups() if g else None


def tribe_to_civ(ck, host, guest, gu):
    """the live Tribe -> Civ change the players' way (coop_tribe_civ.py); True when the host is in Civ"""
    print(' ', host.call('coop.tribe advance')['out'].strip(), flush=True)
    r = wait(lambda: where(host, ADVANCE), 15)
    if not ck(r, 'the HUD shows the advance button (the goal bar is full)'):
        return False
    host.call('click %d %d' % ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    time.sleep(5)
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_active(host) and 'Editor' not in mode(host):
        b = busy(host)
        if 'cinematic' not in b:
            host.call('click %d %d' % CARD_OK)
        elif 'paused' in b:
            time.sleep(2)
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    if not ck(vote_active(host), "the game's City Hall editor request starts the host's vote"):
        return False
    keyframe('tribe_vote', 'the end of Tribe: the host\'s vote for its City Hall editor on both screens (the co-op '
                           'window with I\'m ready on the guest)', settle=0)
    t0 = time.time()
    editors = 0
    while time.time() - t0 < 420 and 'Civ' not in mode(host):
        if vote_active(host):
            if gu.wait(lambda s: s['ready']['vis'], 15, 'ready button'):
                gu.click('ready')
            wait(lambda: 'Editor' in mode(host) or not vote_active(host), 90, 0.5)
            continue
        if 'Editor' in mode(host):
            editors += 1
            print(f'  host editor {editors}: {host.call("coop.vote")["out"].strip().splitlines()[-1]}', flush=True)
            finish_editor(host)
            wait(lambda: 'Editor' not in mode(host), 30)
            continue
        b = busy(host)
        if 'paused' in b and 'cinematic' in b:
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    ck(editors >= 2, f'the host went through its City Hall and vehicle editors ({editors})')
    return ck('Civ' in mode(host), f'the host reaches the Civ stage ({mode(host).strip()})')


def guest_editors(ck, host, guest, scr):
    """1. and 2.: the guest's own editors after its arrival; the host not held. (t_arrive, ok)"""
    sh, sg = scr
    g = wait(lambda: 'Civ' in mode(guest), 400, 1)
    t_arr = time.time()
    if not ck(g, f'the guest\'s game arrives in the host\'s Civ world ({mode(guest).strip()})'):
        return t_arr, False
    print('  guest:', arrival(guest)[2].splitlines()[0], flush=True)
    keyframe('guest_arrived', 'Civ: both games in the host\'s Civ world (the capital); the guest\'s own editor not '
                              'open yet', settle=0)
    held = []

    def sample():
        hm = mode(host)
        if 'Civ' not in hm or vote_active(host):
            held.append((round(time.time() - t_arr), hm.strip(), vote_active(host)))

    done = []
    for step, what in ((0, 'City Hall'), (1, 'vehicle')):
        t0 = time.time()
        n = 0
        while time.time() - t0 < 300 and 'Editor' not in mode(guest):
            sample()
            n += 1
            if n % 5 == 0:  # a player answers what the Civ start shows (a caption, a city's specialty question)
                for con in (guest, host):
                    if 'Editor' in mode(con):
                        continue
                    if answer_specialty(con):
                        print(f'  {"guest" if con is guest else "host"}: a specialty question answered', flush=True)
                    b = busy(con)
                    if 'paused' in b and 'cinematic' in b:
                        con.call('click %d %d' % CAPTION_OK)
            time.sleep(1)
        st, res, out = arrival(guest)
        opened = 'Editor' in mode(guest)
        if not ck(opened, f'the guest\'s OWN {what} editor opened after its arrival in Civ ({time.time() - t_arr:.0f} s '
                          f'after it; phase {st.get("phase")} why {st.get("why")})'):
            print('  guest:', out.strip(), flush=True)
            shot(guest, f'civ_arrival_no_{step}.png')
            return t_arr, False
        print(f'  guest {what} editor {time.time() - t_arr:.0f} s after the arrival; editors {st.get("editors")}', flush=True)
        if step == 0:
            ck(st.get('editors', '').startswith(f'{CITY_HALL:08X}'), f'the first is the City Hall editor ({st.get("editors")})')
        time.sleep(6)
        keyframe(f'guest_{"cityhall" if step == 0 else "vehicle"}_editor',
                 f'Civ: the guest in its OWN {what} editor (parts palette, plot); the host\'s screen shows its Civ world '
                 'and plays on (not black, no vote window)', settle=0)
        left = drive_editor(guest, 'cityhall' if step == 0 else 'vehicle')
        sample()
        ck(left, f'the guest left its {what} editor with a creation, back in Civ ({mode(guest).strip()})')
        done.append(left)
        if not left:
            return t_arr, False
        time.sleep(2)
    t_end = time.time()
    time.sleep(10)
    ck(not held, f'the host played Civ while the guest made its own designs (no other mode, no vote: {held[:4]})')
    lb = sh.longest_black(t_arr, t_end)
    print(f'  host screen from the guest\'s arrival to the end of its editors: {sh.text(t_arr, t_end)}', flush=True)
    ck(lb < BLACK_LIMIT, f'the host\'s screen is not black while the guest is in its own editors (longest {lb:.0f} s)')
    print(f'  guest screen: {sg.text(t_arr, t_end)}', flush=True)
    return t_arr, all(done)


def guest_city(ck, host, guest, land_did=0):
    """4.: the NPC city nearest the capital bought for the guest (the cities still to come spawned first); land_did:
    the nearest one whose land vehicles are of that design id when there is one (BUG-1323, lab 20261119-130049: the
    nearest city was religious, 9AD7D4AA, the guest's vehicle editor F670AA43 only matched the capital, where a guest
    may not buy: "this city belongs to player 0")"""
    hc, _ = civ(host)
    if not [k for k, v in hc.items() if not v['player']]:
        m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
        for _ in range(6):
            if m:
                host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')
                host.call('wait.frames 60 20000')
        time.sleep(3)
        hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return None, None, hc
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    gci = npc[0]
    if land_did:
        for ci in npc:
            m = re.search(r'did (\w+) ', host.call(f'coop.civ owndesign did {ci} 0')['out'])
            if m and int(m.group(1), 16) == land_did:
                gci = ci
                break
        print(f'  the guest\'s city: {gci} (land design {land_did:08X} wanted; nearest {npc[0]})', flush=True)
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)
    print(f'  guest buys city {gci}:', guest.call(f'coop.civ buycity {gci} 500')['out'].strip(), flush=True)
    got = wait(lambda: all((c := civ(x)[0].get(gci)) and c['player'] and c['owner'] == 1 for x in (host, guest)), 30)
    ck(got, f'city {gci} is the player civilization\'s and the guest\'s on both games')
    return (gci if got else None), cap[0], hc


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    rec0 = (guest.call('coop.creations worlds').get('out') or '').strip()
    m = re.search(r'creature=(\w+)', rec0)
    rec_creature = m.group(1) if m else '00000000'
    scr = (Screens(host, 'host'), Screens(guest, 'guest'))
    for x in scr:
        x.start()
    try:
        return run(host, guest, ck, off_twin, scr)
    finally:
        for x in scr:
            x.stop()
        try:  # the guest's lane profile as it was (the next test of the tribe fixture plays the world's species)
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call(f'coop.creations worlds set creature {rec_creature}')['out'].strip(), flush=True)
        except OSError:
            pass


def run(host, guest, ck, off_twin, scr):
    players = {'host': host, 'guest': guest}
    time.sleep(4)
    ck('Tribe' in mode(host) and 'Tribe' in mode(guest), 'both games in Tribe')
    mine = re.findall(r'^creature (\w{8}) ', guest.call('coop.creations list')['out'], re.M)
    if mine:  # the guest's own creature: its Civ line is not the civilization's species
        print('  guest:', guest.call(f'coop.creations choose creature {mine[0]}')['out'].strip(), mine[0], flush=True)
    ck(mine, f'the guest has creatures of its own ({len(mine)})')
    gu = Ui(guest, 'guest')
    if not tribe_to_civ(ck, host, guest, gu):
        ck.faults(host=host, guest=guest)
        return ck.result()
    time.sleep(3)
    before = {'host': (culture(host), game_fields(host))}
    print(f'  host in Civ: culture {len(before["host"][0])} entries, fields {before["host"][1]}', flush=True)
    t_arr, ok = guest_editors(ck, host, guest, scr)
    if off_twin or not ok:
        ck.faults(host=host, guest=guest)
        return ck.result()

    # 3. the guest's line, the host's culture set
    st, res, out = arrival(guest)
    print('  guest:', out.strip(), flush=True)
    ch, veh = res.get('cityhall'), res.get('vehicle')
    ck(ch and ch['taken'] and veh and veh['taken'], f'both results went to the guest\'s line ({ch}, {veh})')
    vdid = veh['editor'] if veh else 0
    shared = {}
    for did, what in ((CITY_HALL, 'City Hall'), (vdid, 'vehicle')):
        for con, who in ((host, 'host'), (guest, 'guest')):
            got = wait(lambda: line_design(con, 1, did) >> 24 == 0x7B, 40)
            shared[(did, who)] = line_design(con, 1, did)
            ck(got, f'{who}: the guest\'s line names its own {what} design {did:08X} ({shared[(did, who)]:08X})')
        ck(shared[(did, 'host')] == shared[(did, 'guest')], f'one key of the guest\'s {what} design on both games')
    c1, f1 = culture(host), game_fields(host)
    ck(c1 == before['host'][0], f'the host\'s culture set is unchanged ({len(c1)} entries)')
    ck(f1 == before['host'][1], f'the host\'s civilization species, player species and City Hall key are unchanged '
                                f'({before["host"][1]} -> {f1})')
    gc = culture(guest)
    changed = {f'{k:08X}': (f'{v:08X}', f'{gc.get(k, 0):08X}') for k, v in before['host'][0].items() if v and gc.get(k) != v}
    ck(not changed, f'the guest\'s copy of the culture set is the host\'s ({changed or "same"})')

    # 4. the guest's city: its City Hall, its citizens
    gci, capi, hc = guest_city(ck, host, guest, vdid)
    if gci is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    gpos = hc[gci]['pos']
    frame_city(players, gpos)
    hall = mhash(guest, ch['key'], ch['type'], ch['group']) if ch else '?'
    for con, who in ((host, 'host'), (guest, 'guest')):
        def halls(city_owner):
            return sorted({mhash(con, b['model'], BTYPE, BGROUP) for b in own(con)[1]
                           if b['owner'] == city_owner and b['did'] == CITY_HALL})
        got = wait(lambda: halls(1) == [hall], 40)
        print(f'  {who}: the guest\'s City Hall -> {halls(1)}, the others\' {halls(0) + halls(253)} (want {hall})', flush=True)
        ck(got, f'{who}: the guest\'s city\'s City Hall is the guest\'s own new design ({halls(1)}, want {hall})')
        ck(hall not in halls(0) + halls(253), f'{who}: the capital\'s City Hall is the culture\'s ({halls(0) + halls(253)})')
    want = species(host)[0].get(1, 0)
    seen, t = wait_city(players, gci, want, 30)
    print(f'  city {gci} citizens: {fmt(seen)} after {t:.0f} s (the guest\'s Civ line {want:08X})', flush=True)
    ck(want and all(set(v) == {want} for v in seen.values()) and all(seen.values()),
       f'the guest\'s city\'s citizens are of the guest\'s species on both games ({want:08X}: {fmt(seen)})')

    # 5. a land vehicle of the guest's own design
    vb = mhash(guest, veh['key'], veh['type'], veh['group']) if veh else '?'
    where_buy = None
    for ci in (gci, capi):
        m = re.search(r'did (\w+) ', host.call(f'coop.civ owndesign did {ci} 0')['out'])
        d = int(m.group(1), 16) if m else 0
        print(f'  city {ci}: land design id {d:08X} (the guest\'s vehicle editor {vdid:08X})', flush=True)
        if d == vdid and where_buy is None:
            where_buy = ci
    vid = None
    if ck(where_buy is not None, f'a city whose land vehicles are of the guest\'s vehicle design id {vdid:08X}'):
        before_v = set(own(host)[0])
        p = Player('guest', guest, 1, random.Random(7), [])
        if p.open_city(hc[where_buy]['pos']) and p.click_button(BTN_LAND):
            time.sleep(3)
            print('  ROUTE purchase: the city menu\'s land button', flush=True)
        if not any(i not in before_v and v['owner'] == 1 for i, v in own(host)[0].items()):
            print('  guest:', guest.call(f'coop.civ buy {where_buy} 0')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 20 and vid is None:
            new = [i for i, v in own(host)[0].items() if i not in before_v and v['owner'] == 1]
            vid = new[0] if new else None
            time.sleep(0.5)
        if ck(vid is not None, f'the guest\'s purchase made a vehicle on the host ({vid})'):
            for con, who in ((host, 'host'), (guest, 'guest')):
                def vh():
                    v = own(con)[0].get(vid)
                    return mhash(con, v['model'], VTYPE, VGROUP) if v else None
                got = wait(lambda: vh() == vb, 20)
                print(f'  {who}: the guest\'s vehicle #{vid} -> {vh()} (its design {vb})', flush=True)
                ck(got, f'{who}: the guest\'s new vehicle shows the guest\'s own vehicle design ({vh()}, want {vb})')

    # 6. key frames
    frame_city(players, gpos)
    keyframe('guest_city', 'Civ: the guest\'s city on both screens: its City Hall is the guest\'s own new City Hall '
                           '(made in its editor after its arrival), its citizens are the guest\'s creature, the same on '
                           'both screens' + ('; the guest\'s new land vehicle nearby' if where_buy == gci else ''))
    frame_city(players, hc[capi]['pos'])
    keyframe('capital', 'Civ: the capital on both screens: the host\'s (the culture\'s) City Hall and designs, not the '
                        'guest\'s' + ('; the guest\'s new land vehicle (its own design) stands here' if where_buy == capi
                                      else ''))

    # 7. the host's save
    keys = [k for k in (ch and ch['key'], veh and veh['key'], shared.get((CITY_HALL, 'host')),
                        shared.get((vdid, 'host'))) if k]
    t_save = time.time() - 1
    print('  host:', host.call('coop.save')['out'].strip()[:100], flush=True)
    time.sleep(8)
    files = save_files(os.path.join(lane.HOST_DIR, 'Games'), t_save)
    found = keys_in(files, keys)
    print(f'  save: {len(files)} files; ' + ', '.join(f'{k:08X}: {found[k][:4]}' for k in keys), flush=True)
    ck(files and not any(found[k] for k in keys), 'the host\'s save holds no key of the guest\'s new designs')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
