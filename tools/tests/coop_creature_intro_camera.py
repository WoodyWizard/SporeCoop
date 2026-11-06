#!/usr/bin/env python3
"""Creature: the guest's own camera is at its creature after a join whose load met the galaxy menu intro (BUG-0607).

Lab shots of creature_ufo_mirror (20261105-231804, 20261105-233051) showed the guest's planet from space after its
join; their guest logs had `world: cinematic 8915F9BB still playing in a stage: ended it`. The galaxy menu's intro
"GGE_intro" starts 15.8-15.9 s after the menu came up; a load requested 0.2-0.7 s before that holds the frame (~0.65 s)
and the intro starts in the next one, already in the Loading mode 01654C08; it plays through the Creature setup, the
mod ends it a second later, and the creature camera stays at a default point (0, -1000, 500), ~800 m from the avatar,
for the session (lab 20261106-141833 #5 with world.intro_at_once off). BUG-0626's fix (world.intro_at_once) does not
start it outside the menu mode, so it never plays there.

The race: the guest joins SPORE_JOIN_AT (12.5) s after its menu came up (twoinst.start(join_at=): its load request
comes ~3.0-3.5 s after the join); about one start in three hits the Loading mode. SPORE_FIX_OFF=world.intro_at_once
is applied on the guest before its join. Checks the game's own camera (App::cViewer, the screen-centre ray's origin)
against the avatar after the join; then twice the intro by hand in Creature (as the menu starts it), which with a
healthy camera comes back to the creature after the mod ends it (and with the stuck one does not).
"""
import math
import os
import re
import time

from twoinst import Checks, shot, start

NEAR = 80.0  # m: the creature camera is ~10-40 m from its avatar; the intro's camera is in space (km)


def cine(con):
    m = re.search(r'cinematic (\w+) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def camera(con):
    """(camera position, avatar position, distance) from the game's viewer: the ray through the screen centre"""
    out = con.call('coop.cam')['out']
    vp = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', out)
    if not vp:
        return None
    out = con.call(f'coop.cam {int(vp.group(1)) // 2} {int(vp.group(2)) // 2}')['out']
    o = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    a = re.search(r'avatar=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if not (o and a):
        return None
    o, a = [float(v) for v in o.groups()], [float(v) for v in a.groups()]
    return o, a, math.dist(o, a)


def wait(f, t):
    t0 = time.time()
    while time.time() - t0 < t:
        if f():
            return True
        time.sleep(0.3)
    return False


def show(name, c):
    if c:
        print(f'  {name}: camera ({c[0][0]:.1f} {c[0][1]:.1f} {c[0][2]:.1f}), avatar ({c[1][0]:.1f} {c[1][1]:.1f} '
              f'{c[1][2]:.1f}), distance {c[2]:.1f} m', flush=True)
    else:
        print(f'  {name}: no camera / avatar', flush=True)


def main():
    join_at = float(os.environ.get('SPORE_JOIN_AT', '12.5'))
    print(f'  the guest joins {join_at:.2f} s after its galaxy menu came up', flush=True)
    host, guest = start('creature', join_at=join_at)
    ck = Checks()
    time.sleep(3)
    st = guest.call('coop.status')['out']
    told = re.search(r'menu intro ended \d+ \([^)]*\)', st)
    print(f'  guest after the join: {told.group(0) if told else "no menu intro in the stage"}', flush=True)
    c = camera(guest)
    show('guest after the join', c)
    shot(guest, 'intro_camera_join.png')
    ck(c and c[2] < NEAR, f'guest: its camera is at its creature after the join (< {NEAR:.0f} m)')
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', guest.call('call Simulator::cCinematicManager::Get cdecl')['out'])
    ck(m, 'guest: the cinematic manager')
    if not m:
        return ck.result()
    for i in (1, 2):
        # as the galaxy menu's handler starts it (GalaxyGameEntry_IntroMessages: name, 1, 0, 0, 0, 0)
        print(' ', guest.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {m.group(1)} s:GGE_intro 1 0 0 0 0')
              ['out'].strip(), flush=True)
        print(f'  guest #{i}: cinematic right after the start: {cine(guest)}', flush=True)
        time.sleep(0.5)
        show(f'guest #{i} 0.5 s into the intro', camera(guest))
        ck(wait(lambda: cine(guest) is None, 10), f'guest #{i}: no cinematic plays any more')
        time.sleep(3)
        c = camera(guest)
        show(f'guest #{i} after the intro', c)
        shot(guest, f'intro_camera_after{i}.png')
        ck(c and c[2] < NEAR, f'guest #{i}: its own camera is back at its creature (< {NEAR:.0f} m)')
        st = guest.call('coop.status')['out']
        busy = re.search(r'busy=\S+|input_lock=\d+', st)
        print(f'  guest #{i}: {busy.group(0) if busy else "not busy"}', flush=True)
    ended = re.search(r'menu intro ended \d+ \([^)]*\)', guest.call('coop.status')['out'])
    print(f'  guest: {ended.group(0) if ended else "no intro ended"}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
