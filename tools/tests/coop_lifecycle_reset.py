#!/usr/bin/env python3
"""BUG-1055 (class: module state that outlives its session or its world): every module registers the state of a
session and of a world in one registry (mods/coop/lifecycle.h); coop.cpp resets it at every session end and at every
new world. Right after each reset the registry notes what is still held (`coop.lifecycle held`: "left session: ...",
"left world: ..."), so the check does not depend on what the next world fills in again.

For each stage fixture (SPORE_STAGES, default cell,creature,tribe,civ,space), on the same two games:
  1. (after the first stage) the host loads the stage's fixture (a world change on the host), hosts again, the guest
     joins (it loads the host's world: a world change on the guest): both games' last world reset left nothing;
  2. the two play for 20 s; what the registry holds is printed (at least one stage must hold something);
  3. the session ends, alternately by the guest's Leave then the host's Leave (Leave path), or by the host's Leave
     first, so that the guest is dropped (Disconnect path): both games' last session reset left nothing.
(The unload path runs the same end_session and reset; its state goes with the unloaded image, nothing to read.)

  SPORE_FIX_OFF=core.lifecycle_reset   FAILS: the registry resets nothing, the entries are left
  SPORE_FIX_OFF=cell.session_reset SPORE_STAGES=cell          FAILS (cell.own_good / cell.join left), BUG-1002
  SPORE_FIX_OFF=tribe.cycle_reset SPORE_STAGES=tribe,creature FAILS (tribe.cycle left on the guest), BUG-1051
"""
import os
import re
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, lane_state, start, wait_guest_in

STAGES = [s for s in os.environ.get('SPORE_STAGES', 'cell,creature,tribe,civ,space').split(',') if s]


def held(con):
    out = con.call('coop.lifecycle held').get('out', '')
    now = re.search(r'now: (.*)', out)
    ses = re.search(r'left session: (.*) \(resets (\d+)\)', out)
    wld = re.search(r'left world: (.*) \(resets (\d+)\)', out)
    if not (now and ses and wld):
        return None
    return {'now': now.group(1).strip(), 'session': ses.group(1).strip(), 'sessions': int(ses.group(2)),
            'world': wld.group(1).strip(), 'worlds': int(wld.group(2))}


def wait_reset(con, kind, before, secs=20):
    """the game's next reset of `kind` ('sessions' / 'worlds') after the count `before`; its record or None"""
    t0 = time.time()
    while time.time() - t0 < secs:
        h = held(con)
        if h and h[kind] > before:
            return h
        time.sleep(0.5)
    return None


def in_game(con, secs=180):
    """after a coop.load: the load starts (a load or menu mode), then the game is in the world"""
    t0 = time.time()
    while time.time() - t0 < 30 and not any(m in con.call('game.mode')['out'] for m in ('Load', 'Menu', 'GalaxyGameEntry')):
        time.sleep(0.2)
    while time.time() - t0 < secs:
        m = con.call('game.mode')['out']
        if 'Load' not in m and 'Menu' not in m and 'GalaxyGameEntry' not in m and con.call('coop.avatar')['ok']:
            return True
        time.sleep(0.5)
    return False


def main():
    ck = Checks()
    host, guest = start(STAGES[0])
    lane_state(**{**lane_state(), 'fixture': 'lifecycle-dirty'})  # these games change worlds: no warm reuse
    any_held = []
    for i, stage in enumerate(STAGES):
        print(f'== {stage}', flush=True)
        if i:
            hb, gb = held(host), held(guest)
            slot = 20 + i
            planet = fixtures.planet_of(stage)
            sh('fixture', 'slot', stage, lane.HOST_PROFILE, str(slot))
            host.call(f'coop.load Game{slot} {planet.split("/")[1]}')
            ck(in_game(host), f'{stage}: the host is in the fixture\'s world')
            h = wait_reset(host, 'worlds', hb['worlds'], 5)
            print(f'  host world reset: {h}', flush=True)
            ck(h is not None and h['world'] == 'none', f'{stage}: the host\'s world change left nothing '
               f'({h and h["world"]})')
            host.call(lane.HOST_CMD)
            guest.call(lane.JOIN)
            wait_guest_in(host, guest, 150)  # (closes the host's "game saved" box while it shares)
            wait_reset(guest, 'worlds', gb['worlds'], 90)  # the guest loads the host's world
            ok = wait_guest_in(host, guest, 150)
            ck(ok, f'{stage}: the guest joined the host\'s world')
            g = held(guest)
            print(f'  guest after its world change: {g}', flush=True)
            ck(g is not None and g['worlds'] > gb['worlds'] and g['world'] == 'none',
               f'{stage}: the guest\'s world change left nothing ({g and g["world"]})')
        time.sleep(20)
        hn, gn = held(host), held(guest)
        print(f'  held while playing: host {hn["now"]} | guest {gn["now"]}', flush=True)
        any_held += [x for x in (hn['now'], gn['now']) if x != 'none']
        drop = i % 2 == 1
        if drop:  # the host goes first: the guest's session is dropped (cb.disconnected)
            print('  ' + host.call('coop.leave')['out'].strip(), flush=True)
            h = wait_reset(host, 'sessions', hn['sessions'])
            g = wait_reset(guest, 'sessions', gn['sessions'], 40)
        else:
            print('  ' + guest.call('coop.leave')['out'].strip(), flush=True)
            g = wait_reset(guest, 'sessions', gn['sessions'])
            host.call('coop.leave')
            h = wait_reset(host, 'sessions', hn['sessions'])
        how = 'dropped' if drop else 'left'
        print(f'  after the session end ({how}): host {h} | guest {g}', flush=True)
        ck(g is not None and g['session'] == 'none', f'{stage}: the guest\'s session end ({how}) left nothing '
           f'({g and g["session"]})')
        ck(h is not None and h['session'] == 'none', f'{stage}: the host\'s session end left nothing '
           f'({h and h["session"]})')
    ck(bool(any_held), 'the sessions made registered state (something to reset)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
