#!/usr/bin/env python3
"""BUG-0836: the disabled state's invincibility and a new ship object in the same Space world.

The disabled state set the ship's invincible byte every frame on whatever ship the player had and cleared it at the
end only on the ship it was set on: a new player UFO object in the same world would keep it until a load.

1. Probe: which of the trips a player makes inside one Space world gives the player a new UFO object (pointer or the
   game's object serial, `coop.space downstate` ship= serial=): landing and taking off, a jump to another star and
   back, the galaxy view and back, the captain screen's outfitter (an editor) when it opens.
2. For the first trip that does: the guest's ship is disabled (`disable 1`, nobody repairs it), makes that trip, and
   once the state is over the own ship must take damage again (`hit 20`, invincible byte 0).
   SPORE_FIX_OFF=space.down_ship_only: FAIL then.
No such trip: the test fails with "no trip made a new ship object" (the case the ticket feared is not reachable by
these trips; the probe's lines are the evidence).

  SPORE_LANE=2 python3 tools/tests/coop_space_down_editor.py
"""
import re
import time

import play_space
import uifind
from coop_space_finale import fin, get_drive, settle, to_galaxy
from twoinst import Checks, start

BADGE = (1390, 890)  # the rank badge over the ship's picture, 1440x1080
HOME_STAR = 4587573


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ds(con):
    return dict(re.findall(r'(\w+)=(\S+)', sp(con, 'downstate')))


def ship(con):
    d = ds(con)
    return d.get('ship'), d.get('serial')


def mode(con):
    return con.call('game.mode')['out'].strip()


def ctx(con):
    return fin(con).get('ctx')


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def trip_land(con):
    print('    land:', sp(con, 'land'))
    wait(lambda: ctx(con) == '0', 40, 1)
    time.sleep(4)
    print('    takeoff:', sp(con, 'takeoff'))
    return wait(lambda: ctx(con) == '1', 40, 1)


def trip_jump(con):
    print('    jumpto:', sp(con, 'jumpto', HOME_STAR + 1))
    time.sleep(15)
    print('    jump back:', sp(con, 'jump'))
    time.sleep(15)
    return ctx(con) == '1'


def trip_galaxy(con, ck):
    to_galaxy(con, 'guest', ck)
    print('    back into the home system:', sp(con, 'finale goto', HOME_STAR, 'enter'))
    return wait(lambda: ctx(con) == '1', 60, 2)


def trip_outfitter(con):
    con.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    con.call(f'click {BADGE[0]} {BADGE[1]}')
    if not wait(lambda: play_space.captain_open(con), 10):
        return False
    hits = uifind.find(con, 'Enter Outfitter')
    print('    Enter Outfitter:', hits)
    pick = [h for h in hits if h[2]] or hits  # (its parent is hidden: the click at its place, like a player's)
    if pick:
        con.call(f'move {pick[0][0]} {pick[0][1]}')
        time.sleep(0.4)
        con.call(f'click {pick[0][0]} {pick[0][1]}')
    opened = wait(lambda: 'Space' not in mode(con), 15, 1)
    print('    outfitter opened:', bool(opened), mode(con))
    for _ in range(8):
        if 'Space' in mode(con) and not play_space.captain_open(con):
            break
        if 'Space' not in mode(con):
            con.call('click 1380 1062')  # the editor's exit
        else:
            play_space.close_captain(con)
        time.sleep(3)
    return wait(lambda: 'Space' in mode(con), 60, 1)


def trip_respawn(con):
    """a death of the own ship (nobody near: the native death) and the respawn at the colony: "a new spaceship at
    the ready"""
    # (a lethal hit next to the host would disable the ship: the disabled state with nobody to repair ends in the
    # game's own death after 20 s)
    print('    disable:', sp(con, 'disable 1'))
    wait(lambda: 'state=2' in sp(con, 'hp'), 40, 1)
    print('    dead:', sp(con, 'hp'))
    for _ in range(40):  # the death caption and the respawn caption wait for OK
        time.sleep(3)
        h = sp(con, 'hp')
        if 'state=0' in h and 'movie=0' in h:
            break
        con.call('key enter')
    print('    after the respawn:', sp(con, 'hp'))
    return 'state=0' in sp(con, 'hp')


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(4)
    for c in (host, guest):
        sp(c, 'repairdist 0')
    get_drive(guest, 'guest', ck)
    trips = [('land and take off', lambda: trip_land(guest)), ('a jump and back', lambda: trip_jump(guest)),
             ('the galaxy view and back', lambda: trip_galaxy(guest, ck)), ('the outfitter', lambda: trip_outfitter(guest)),
             ('a death and the respawn', lambda: trip_respawn(guest))]
    found = None
    for name, fn in trips:
        before = ship(guest)
        ok = fn()
        settle(guest, 'guest')
        after = ship(guest)
        new = before != after
        print(f'  probe: {name}: back={bool(ok)} ship {before} -> {after}: {"NEW SHIP" if new else "the same ship"}',
              flush=True)
        if new and found is None and 'respawn' not in name:  # (a death ends the disabled state itself)
            found = (name, fn)
    if not found:
        ck(False, 'no trip made a new ship object (the probe lines above are the evidence)')
    else:
        name, fn = found
        print('  guest:', sp(guest, 'disable 1'), flush=True)
        time.sleep(1)
        d0 = ds(guest)
        ck(d0.get('down') == '1' and d0.get('inv') == '1', f'the guest\'s ship is disabled and invincible ({d0})')
        fn()
        settle(guest, 'guest')
        print('  after', name + ':', sp(guest, 'downstate'), flush=True)
        ck(wait(lambda: ds(guest).get('down') == '0', 40, 1), 'the disabled state ended')
        time.sleep(2)
        out = sp(guest, 'hit 20')
        print('  hit 20:', out, '|', sp(guest, 'downstate'), flush=True)
        m = re.search(r'hp ([\d.]+) -> ([\d.]+) .* invincible=(\d)', out)
        ck(m and m.group(3) == '0', f'the own ship is not invincible after the state ended ({name})')
        ck(m and float(m.group(2)) < float(m.group(1)) - 10, f'a hit takes its HP ({out})')
    for c in (host, guest):
        sp(c, 'repairdist 60')
    # only the game's own captain screen fault (BUG-0304) on the guest, none on the host (suite: reports=True)
    ck(not [ln for ln in host.call('exc')['out'].splitlines() if ln.startswith('0x')], 'host: no caught fault')
    ck(all('0x005971D4' in ln for ln in guest.call('exc')['out'].splitlines() if ln.startswith('0x')),
       'guest: no caught fault other than the captain screen one (0x5971D4)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
