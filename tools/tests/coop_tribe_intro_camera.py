#!/usr/bin/env python3
"""Tribe: the host's view still follows its minimap after the galaxy menu intro started in the stage (BUG-0626).

play_tribe failed "host: the minimap takes the view where it points" in 3 of 12 runs whose host log had the order
"cinematic 8915F9BB still playing in a stage: ended it" before "the game saved box is up" (0 of 48 with the other
order): the menu's intro starts ~16 s after the galaxy menu came up even after a load left the menu, takes the
camera in the stage, and the mod ended it a second later, here while the share's "game saved" box was up. The
view then stayed where it was for the whole session (o == a == b in the calibration).

The race by hand, on the host in Tribe right after the load (no click has moved its view yet): the intro is started as the menu
starts it (PlayCinematic "GGE_intro") right after a save (`coop.save hold`: its game saved box comes up over the
intro and stays), the mod's stage tick ends the intro, the box is confirmed; then the minimap must move the view.
With the fix the intro does not start outside the menu, and the cold start's own intro (the lab's quick load from the
menu lets it come in the stage during the share's save, as in play_tribe) does not either: the first calibration
checks the view after it. Limits (lab 20261106-125051, 20261106-125505): with SPORE_FIX_OFF=world.intro_at_once
(applied only after the start, so the start's own intro is still not started) the by-hand intro played and ended
under the box without a stuck view: the by-hand path does not reproduce it; the stuck view came only from the
menu's own intro after a cold start (play_tribe 3 of 12, lab 20261106-124414 with an earlier fix).
"""
import re
import random
import time

from coop_sync import wait_running
from play_tribe import Player
from twoinst import Checks, shot, start


def cine(con):
    m = re.search(r'cinematic (\w+) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def box(con):
    m = re.search(r'saved_box=(\d)', con.call('coop.save box')['out'])
    return bool(m and m.group(1) == '1')


def wait(f, t):
    t0 = time.time()
    while time.time() - t0 < t:
        if f():
            return True
        time.sleep(0.3)
    return False


def main():
    host, guest = start('tribe')
    ck = Checks()
    log = []
    w = wait_running(host, guest, 90)
    print(f'  both games\' time runs: {"after %.0f s" % w if w is not None else "NOT within 90 s"}', flush=True)
    p = Player('host', host, 0, random.Random(1), log, 1440, 1080)
    st = host.call('coop.status')['out']
    told = re.search(r'menu intro ended (\d+)(?: \(not started (\d+)\))?', st)
    print(f'  host after the start: {told.group(0) if told else "no menu intro in the stage"}', flush=True)
    ck(p.calibrate(), "host: the minimap takes the view where it points (after the start's own intro, if it came)")
    p.J = None
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCinematicManager::Get cdecl')['out'])
    ck(m, 'host: the cinematic manager')
    if not m:
        return ck.result()
    # the natural race: the share's save was being written when the intro started, its box came up over the intro,
    # and the intro was ended under the box (play_tribe 20261106-105201: ended at the moment the box was seen)
    print(' ', host.call('coop.save hold')['out'].strip(), flush=True)
    # as the galaxy menu's handler starts it (GalaxyGameEntry_IntroMessages: name, 1, 0, 0, 0, 0)
    print(' ', host.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {m.group(1)} s:GGE_intro 1 0 0 0 0')
          ['out'].strip(), flush=True)
    playing = cine(host)
    print(f'  host: cinematic right after the start: {playing}', flush=True)
    ck(wait(lambda: box(host), 15), 'host: the game saved box is up')
    ck(wait(lambda: cine(host) is None, 10), 'host: no cinematic plays any more')
    time.sleep(1)
    shot(host, 'intro_camera_box.png')
    print(' ', host.call('coop.save confirm')['out'].strip(), flush=True)
    ck(wait(lambda: not box(host), 10), 'host: the box is confirmed and gone')
    time.sleep(2)
    st = host.call('coop.status')['out']
    ended = re.search(r'menu intro ended (\d+)(?: \(not started (\d+)\))?', st)
    print(f'  host: {ended.group(0) if ended else "no intro ended"}', flush=True)
    p.J = None
    ok = p.calibrate()
    shot(host, 'intro_camera_after.png')
    ck(ok, 'host: the minimap takes the view where it points after the intro and the box')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
