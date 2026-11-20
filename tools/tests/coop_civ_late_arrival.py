#!/usr/bin/env python3
"""A guest that missed the end of Tribe gets its own City Hall and vehicle editors after it arrived in the host's Civ
world, once per world (BUG-1290, WP-S1b of the 0.7.16 late joiners, docs/re/civ.md#civ-late-arrival).

BUG-1276 (core.own_civ_arrival) opens a guest's own City Hall and vehicle editors after its arrival in Civ only when it
saw the host's end-of-Tribe vote end in the host's Tribe; a guest that joins the host's Civ world later, or comes back
after the change, never got them. With `core.own_civ_late` the first arrival of a session in the host's Civ world of
a guest in own mode that plays an own creature there and has no own City Hall yet makes them due (the same settle,
the same editors, the results to its species line); the City Hall editor's opening is noted per world in the guest's
SporeCoop\\worlds.ini, so a later join of that world does not offer them again.

civ_npc fixture (cold start: the guest has no choice and no record, it plays the civilization's species):
  1. the first arrival (the start's join): the guest plays the host's species: no own editor (`coop.civarrival`: late
     why 'this guest plays the host's species here'); it chooses its own creature (`coop.creations choose`, recorded
     for this world, as the co-op window's row does).
  2. the guest's game is killed and started again and joins the host's Civ world (a guest that comes after the end of
     Tribe): after it settled its OWN City Hall editor opens (the game's mode, `coop.civarrival` editors 99E92F05...),
     real clicks in it (three parts, done, the dice, done), then its own vehicle editor the same way; the host plays
     Civ all along (no vote, its mode Civ). Key frames: the guest in each editor beside the host's Civ world.
  3. both games' lines name both designs (one shared key 0x7B...... each); the host's culture set is unchanged.
  4. the guest buys the NPC city nearest the capital: on both games its City Hall is drawn from the guest's new City
     Hall (`coop.creations modelhash` of the building's model key), the capital's is not; its citizens are of the
     guest's species. Key frames of the guest's city and of the capital.
  5. a second rejoin: no editor opens within 60 s of its arrival (`coop.civarrival`: late why 'offered in this world
     already', the world noted).
SPORE_FIX_OFF=core.own_civ_late (suite civ_late_arrival_off, the twin): step 2 FAILs (no editor opens); the test ends
there. SPORE_FIX_OFF=core.own_civ_tag (civ_late_arrival_tag_off, BUG-1329): step 2 FAILs at "no fault at the end of
its own City Hall editor" (the Tribe stage's listener faults at 0x645E4D on the editor's result). The games this test
kills (its two restarts) have their faults judged too (twoinst.KILLED, BUG-1329).

  SPORE_LANE=5 python3 tools/tests/coop_civ_late_arrival.py
"""
import re
import time

import lane
import twoinst
from coop_civ_guest_editors import culture, drive_editor, line_design
from coop_civ_own_arrival import arrival, guest_city
from coop_civ_own_designs import mhash, own
from coop_civ_own_species import fmt, species, wait_city
from coop_late_species import restart_guest
from coop_tribe_civ import CAPTION_OK, busy, vote_active
from harness import faults, mode, waiter
from play_civ import answer_specialty, frame_city
from twoinst import Checks, Console, keyframe, shot, start

SWITCH = 'core.own_civ_late'
CITY_HALL = 0x99E92F05
BTYPE, BGROUP = 0x2399BE55, 0x40636200
wait = waiter(step=1.0, final=True)


def late(con):
    out = con.call('coop.civarrival')['out'] or ''
    m = re.search(r"late: (.*) why='(.*)' \(BUG-1290\)", out)
    if not m:
        return {}
    d = dict(re.findall(r'(\w+)=(\S+)', m.group(1)))
    d['why'] = m.group(2)
    return d


def wait_editor(guest, host, secs, held):
    """until the guest's game is in an editor (answering what the Civ start shows on both games meanwhile)"""
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        if 'Editor' in mode(guest):
            return True
        if 'Civ' not in mode(host) or vote_active(host):
            held.append((round(time.time() - t0), mode(host).strip(), vote_active(host)))
        n += 1
        if n % 5 == 0:
            for con in (guest, host):
                if 'Editor' in mode(con):
                    continue
                answer_specialty(con)
                b = busy(con)
                if 'paused' in b and 'cinematic' in b:
                    con.call('click %d %d' % CAPTION_OK)
        time.sleep(1)
    return 'Editor' in mode(guest)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    try:
        guest = run(host, guest, ck, off_twin) or guest
    finally:
        try:  # the guest's lane profile as the cold start leaves it
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.civarrival forget')['out'].strip().splitlines()[0], '|',
                  g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call('coop.creations worlds set creature 0')['out'].strip().splitlines()[0], flush=True)
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


def run(host, guest, ck, off_twin):
    time.sleep(6)
    # 1. the start's arrival: the host's species, no own editor
    lt = late(guest)
    print(f'  guest at its first arrival: {lt}', flush=True)
    ck(lt.get('checked') == '1' and 'species here' in lt.get('why', ''), f'the first arrival (no own creature) offers no own '
                                                                f'editor ({lt})')
    ck('Editor' not in mode(guest), f'the guest plays Civ ({mode(guest).strip()})')
    mine = re.findall(r'^creature (\w{8}) ', guest.call('coop.creations list')['out'], re.M)
    if not ck(mine, f'the guest has creatures of its own ({len(mine)})'):
        return guest
    print('  guest:', guest.call(f'coop.creations choose creature {mine[0]}')['out'].strip(), mine[0], '|',
          guest.call('coop.creations worlds')['out'].strip(), flush=True)
    before = culture(host)

    # 2. the guest comes back after the end of Tribe
    guest, joined = restart_guest(host)
    if not ck(joined, 'the guest is in the host\'s Civ world again'):
        return guest
    twoinst.PLAYERS['guest'] = guest
    t_arr = time.time()
    wait(lambda: 'Civ' in mode(guest), 120)
    print(f'  guest arrived: {late(guest)}', flush=True)
    held = []
    for step, what, kind in ((0, 'City Hall', 'cityhall'), (1, 'vehicle', 'vehicle')):
        opened = wait_editor(guest, host, 300 if step == 0 else 180, held)
        st, res, out = arrival(guest)
        if not ck(opened, f'the guest\'s OWN {what} editor opened after it came back into the host\'s Civ world '
                          f'({time.time() - t_arr:.0f} s after the join; phase {st.get("phase")} why {st.get("why")}; '
                          f'late {late(guest).get("why")})'):
            print('  guest:', out.strip(), flush=True)
            shot(guest, f'civ_late_no_{kind}.png')
            return guest
        print(f'  guest {what} editor {time.time() - t_arr:.0f} s after the join; editors {st.get("editors")}', flush=True)
        if step == 0:
            ck(st.get('editors', '').startswith(f'{CITY_HALL:08X}'), f'the first is the City Hall editor ({st.get("editors")})')
        time.sleep(6)
        keyframe(f'guest_{kind}_editor', f'Civ: the guest (which joined after the end of Tribe) in its OWN {what} '
                                         'editor (parts palette, plot); the host\'s screen shows its Civ world and plays '
                                         'on (no vote window)', settle=0)
        n0 = faults(guest)
        left = drive_editor(guest, kind)
        ck(left, f'the guest left its {what} editor with a creation, back in Civ ({mode(guest).strip()})')
        time.sleep(2)
        # BUG-1329: the result of its own editor reached the Tribe stage's leftover listener (the Tribe tag): an access
        # violation at 0x645E4D on every restarted guest (core.own_civ_tag off: civ_late_arrival_tag_off FAILs here)
        n1 = faults(guest)
        exc = [l.strip() for l in guest.call('exc')['out'].splitlines()[1:3]]
        ck(n1 == n0, f'the guest\'s game took no fault at the end of its own {what} editor ({n0} -> {n1} faults; {exc})')
        if not left:
            return guest
    ck(not held, f'the host played Civ while the guest made its own designs (no other mode, no vote: {held[:4]})')
    if off_twin:
        return guest

    # 3. the guest's line, the host's culture set
    st, res, out = arrival(guest)
    print('  guest:', out.strip(), flush=True)
    ch, veh = res.get('cityhall'), res.get('vehicle')
    ck(ch and ch['taken'] and veh and veh['taken'], f'both results went to the guest\'s line ({ch}, {veh})')
    for did, what in ((CITY_HALL, 'City Hall'), (veh['editor'] if veh else 0, 'vehicle')):
        keys = {}
        for con, who in ((host, 'host'), (guest, 'guest')):
            wait(lambda: line_design(con, 1, did) >> 24 == 0x7B, 40)
            keys[who] = line_design(con, 1, did)
        ck(keys['host'] >> 24 == 0x7B and keys['host'] == keys['guest'],
           f'both games name one shared key of the guest\'s {what} design {did:08X} ({keys})')
    after = culture(host)
    ck(after == before, f'the host\'s culture set is unchanged ({len(after)} entries)')

    # 4. the guest's city: its City Hall and citizens on both games
    players = {'host': host, 'guest': guest}
    gci, capi, hc = guest_city(ck, host, guest)
    if gci is not None and ch:
        frame_city(players, hc[gci]['pos'])
        hall = mhash(guest, ch['key'], ch['type'], ch['group'])
        for con, who in ((host, 'host'), (guest, 'guest')):
            def halls(owner):
                return sorted({mhash(con, b['model'], BTYPE, BGROUP) for b in own(con)[1]
                               if b['owner'] == owner and b['did'] == CITY_HALL})
            got = wait(lambda: halls(1) == [hall], 40)
            print(f'  {who}: the guest\'s City Hall -> {halls(1)}, the others\' {halls(0) + halls(253)} (want {hall})',
                  flush=True)
            ck(got, f'{who}: the guest\'s city\'s City Hall is its own new design ({halls(1)}, want {hall})')
            ck(hall not in halls(0) + halls(253), f'{who}: the capital\'s City Hall is the culture\'s')
        want = species(host)[0].get(1, 0)
        seen, t = wait_city(players, gci, want, 30)
        print(f'  city {gci} citizens: {fmt(seen)} after {t:.0f} s (the guest\'s Civ line {want:08X})', flush=True)
        ck(want and all(set(v) == {want} for v in seen.values()) and all(seen.values()),
           f'the guest\'s city\'s citizens are of the guest\'s species on both games ({fmt(seen)})')
        frame_city(players, hc[gci]['pos'])
        keyframe('guest_city', 'Civ: the guest\'s city on both screens: its City Hall is the guest\'s own new City Hall '
                               '(made in its editor after it came back), its citizens the guest\'s creature')
        frame_city(players, hc[capi]['pos'])
        keyframe('capital', 'Civ: the capital on both screens: the culture\'s (the host\'s) City Hall, not the guest\'s')

    # 5. once per world: a second rejoin offers nothing
    guest, joined = restart_guest(host)
    twoinst.PLAYERS['guest'] = guest
    if ck(joined, 'the guest is in the host\'s Civ world a third time'):
        wait(lambda: 'Civ' in mode(guest), 120)
        t0 = time.time()
        opened = False
        while time.time() - t0 < 60 and not opened:
            opened = 'Editor' in mode(guest)
            time.sleep(1)
        lt = late(guest)
        print(f'  guest after the second rejoin: {lt}', flush=True)
        ck(not opened and 'already' in lt.get('why', ''), f'a later join of this world offers the editors no more '
                                                         f'(editor {opened}, {lt})')
    return guest


if __name__ == '__main__':
    raise SystemExit(main())
