#!/usr/bin/env python3
"""BUG-0229, BUG-0228: the co-op "ship disabled" state and trips out of Space.

 (a) The same Space world (BUG-0229): the guest is disabled (`coop.space disable 1`) and then acts
     as outside Space for 25 s (`coop.space away 1`: what an editor or an adventure entered from
     Space gives the mod; the Space world and the ship live on). Back in Space the ship must not
     be destroyed (the game's state 2) and most of the 20 s must be left: the timer stood still.
     Before the fix the wall-clock deadline passed meanwhile and the first Space frame killed the
     ship with the game's own death.
 (b) Another Space world (BUG-0229): the host is disabled (nobody can repair it: `repairdist 0`),
     goes to the galaxy menu and loads the game again. After the load the state is over without a
     death: the loaded game's ship is not destroyed 25 s after the disable. Before the fix the old
     state went on with the new game's ship and killed it after 20 s.
 (c) The state ends outside Space (BUG-0228): the guest is disabled (its ship invincible), acts as
     outside Space and leaves the session. Back in Space a 20 HP hit through the game's damage path
     must take 20 HP (`invincible=0`). Before the fix the byte was cleared only when the session
     ended in Space, so the ship stayed invincible.

  coop_space_down_away.py         (SPORE_FIX_OFF=space.down_world: (a) and (b) FAIL;
                                   SPORE_FIX_OFF=space.down_clear: (c) FAILS)"""
import re
import time

import fixtures
from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def hp(con):
    out = sp(con, 'hp')
    m = re.search(r'hp ([\d.]+) max ([\d.]+) down=(\d) left=(-?[\d.]+) repair=\S+ state=(\d+)', out)
    return dict(hp=float(m.group(1)), down=int(m.group(3)), left=float(m.group(4)), state=int(m.group(5)),
                out=out) if m else dict(hp=-1, down=-1, left=0, state=-1, out=out)


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    for con in (host, guest):
        sp(con, 'repairdist 0')  # nobody repairs: only the timer decides

    # (a) the same world: the timer stands still while away
    print('  guest:', sp(guest, 'disable 1'))
    time.sleep(1)
    h0 = hp(guest)
    print('  guest disabled:', h0['out'], '|', sp(guest, 'downstate'))
    ck(h0['down'] == 1 and h0['left'] > 15, f'the guest is disabled with ~20 s left ({h0["left"]})')
    print('  guest:', sp(guest, 'away 1'))
    time.sleep(25)
    print('  guest:', sp(guest, 'away 0'))
    time.sleep(2)
    h1 = hp(guest)
    print('  guest back:', h1['out'], '|', sp(guest, 'downstate'))
    ck(h1['state'] != 2, f'(a) back in the same world the ship is not destroyed (state {h1["state"]})')
    ck(h1['down'] == 1 and h1['left'] > 12, f'(a) the disabled state goes on with most of its 20 s ({h1["left"]:.1f} s)')
    print('  guest:', sp(guest, 'disable 0'))
    if h1['state'] == 2:  # the death caption waits for its OK: let the game respawn the ship first
        wait(lambda: hp(guest)['state'] != 2, 60, 2)

    # (b) another world: the host loads the game again while disabled
    print('  host:', sp(host, 'disable 1'), '|', sp(host, 'downstate'))
    t_dis = time.time()
    planet = fixtures.planet_of('space')
    print('  menu:', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90, 1), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {" ".join(planet.split("/"))}')['out'].strip())
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), 'the host is in Space again')
    wait(lambda: hp(host)['state'] >= 0, 30, 1)
    left = 27 - (time.time() - t_dis)
    if left > 0:
        time.sleep(left)  # past the 20 s of the old state
    h2 = hp(host)
    print(f'  host {time.time() - t_dis:.0f} s after the disable:', h2['out'], '|', sp(host, 'downstate'))
    ck(h2['state'] != 2, f'(b) the loaded game\'s ship is not destroyed by the old state (state {h2["state"]})')
    ck(h2['down'] == 0, '(b) the disabled state of the old world is over')
    if h2['state'] == 2:
        wait(lambda: hp(host)['state'] != 2, 60, 2)
    sp(host, 'disable 0')

    # (c) the state ends outside Space: the ship loses its invincibility on return
    ck(wait(lambda: hp(guest)['state'] not in (-1, 2), 30, 1), 'the guest flies')
    print('  guest:', sp(guest, 'disable 1'))
    time.sleep(1)
    print('  guest hit 1:', sp(guest, 'hit 1'))
    print('  guest:', sp(guest, 'away 1'))
    print('  guest leaves:', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    print('  guest:', sp(guest, 'away 0'), '|', sp(guest, 'downstate'))
    time.sleep(1)
    out = sp(guest, 'hit 20')
    print('  guest hit 20:', out)
    m = re.search(r'hp ([\d.]+) -> ([\d.]+) .* invincible=(\d)', out)
    ck(m and m.group(3) == '0', '(c) the ship is no longer invincible after the state ended outside Space')
    ck(m and float(m.group(2)) < float(m.group(1)) - 10, '(c) the hit takes its HP')

    for con in (host, guest):
        sp(con, 'repairdist 60')
        sp(con, 'away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
