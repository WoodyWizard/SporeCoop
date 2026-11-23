#!/usr/bin/env python3
"""The captain screen left open must not stop the finale's zoom-out (BUG-0303), and its known game
fault (BUG-0304) is counted, not hidden.

Lab 20261030-075052 campaign#3: the Space play bot clicked a planet whose point lay under the rank
badge over the ship's picture (bottom right); that opens the captain screen. The screen is no
caption (finale busy=0) and the game takes no mouse wheel while it is up, so the finale's zoom-out to
the galaxy view did nothing and the guest stayed next to the core (zone 2).

Here the guest opens the captain screen with a real click on the badge, then the finale's to_galaxy()
zooms out: it must close the screen (its OK button) and reach the galaxy view. With
OLD_FINALE_TOOL=1 the tool does not close it: the zoom-out fails (the FAIL run). The bot's target
filter (play_space.Player.targets) must not offer a point under the badge.

Opening the captain screen faulted in the game itself (0x5971D4, BUG-0304, also with coop unloaded): the
captain's item list is null when this game has no property list of the captain (the fixture's captain, and a
guest in the host's world always: GitHub #203, fatal on Windows, the rank badge and the Accept of an adventure
mission). With `game.captain_items` the screen opens without that fault: the test requires no caught fault on
either game, on both routes of the report:
  1. the Galactic Adventures mission screen (opened as the game's Space update does, `coop.captain open`) and a
     real click on its Accept (CaptainPanel_OnMessage 0x075DD108 -> CaptainPanel_Show(2) -> the items palette;
     the probe on SporeCoop::CaptainItems_Palette proves the route ran);
  2. the rank badge with a real click.
`SPORE_FIX_OFF=game.captain_items` (the game's code, 0.7.15.2) must FAIL: the guest logs the caught fault at
0x5971D4 (handled on Wine, a crash on the reporter's Windows).
The mission screen's Accept is found by its control id (`uifind.where`): `coop.ui find "Accept"` returns the captain
panel's hidden copy first (lab 20261119-094350 failed there with the screen up, and the screen left open then kept
the badge from opening the captain screen). The fixture's captain has no species profile in the guest's game
either (lane 3, 2026-11-19: `stats: skipped 3` per opening), so the panel's stats need `game.captain_stats` here
too (BUG-1278): `SPORE_FIX_OFF=game.captain_stats` must FAIL as well (the guest's caught fault at 0xC02A14 after
each route; lab 20261119-112459). This is BUG-1278's twin: captain 0 (space_captain_none) reaches neither fault.

  SPORE_LANE=2 python3 tools/tests/coop_space_captain.py
"""
import re
import time

import coop_space_finale as fz
import play_space
import uifind
from twoinst import Checks, shot, start

BADGE = (1390, 890)  # the rank badge over the ship's picture, 1440x1080
ACCEPT = 0x075DD108  # the adventure mission screen's Accept (SporeCoop::CaptainPanel_OnMessage)


def faults(con):
    out = con.call('exc')['out']
    for line in out.splitlines():
        if line.startswith('total faults:'):
            return int(line.split(':')[1])
    return 0


def fault_lines(con):
    return [ln for ln in con.call('exc')['out'].splitlines() if ln.startswith('0x')]


def hits(con, sym):
    m = re.search(re.escape(sym) + r'.*?hits=(\d+)', con.call('probe.list')['out'])
    return int(m.group(1)) if m else 0


def accept_route(guest, ck):
    """the adventure mission screen and a real click on Accept (the reporter's second route)"""
    guest.call('probe.clear')
    guest.call('probe SporeCoop::CaptainItems_Palette')
    n0 = faults(guest)
    print('  guest:', guest.call('coop.captain open')['out'].strip())
    # the mission screen's own buttons by control id: `coop.ui find "Accept"` returns the captain panel's hidden copy
    # first (lab 20261119-094350: "the adventure mission screen is up" failed with the screen on the picture)
    up = fz.wait(lambda: uifind.where(guest, ACCEPT) is not None, 10)
    if not ck(up, 'guest: the adventure mission screen is up (Decline, Accept)'):
        return
    # BUG-1455: the control is "shown" (coop.ui where) before the screen is drawn and takes clicks (lab 20261122-194210:
    # captain_mission.png without the screen, the click missed, the screen came after it and then blocked the rank
    # badge's click): its place must hold for 1.5 s, and a click the screen did not take (no palette, Accept still
    # there) is given again, up to 3 times, printed
    p, t_same = uifind.where(guest, ACCEPT), time.time()
    while time.time() - t_same < 1.5:
        time.sleep(0.25)
        q = uifind.where(guest, ACCEPT)
        if q != p:
            p, t_same = q, time.time()
    shot(guest, 'captain_mission.png')
    ran = False
    for k in range(3):
        if p is None:
            break
        guest.call(f'move {p[0]} {p[1]}')
        time.sleep(0.3)
        guest.call(f'click {p[0]} {p[1]}')
        ran = fz.wait(lambda: hits(guest, 'CaptainItems_Palette') > 0, 4 if k < 2 else 10)
        if ran or play_space.captain_open(guest):
            break
        p = uifind.where(guest, ACCEPT)
        print(f'  guest: the Accept click {k + 1} was not taken (Accept now at {p}): again', flush=True)
    time.sleep(2)
    shot(guest, 'captain_accepted.png')
    ck(ran, 'guest: Accept ran the captain\'s items palette (the route of the report\'s accept crash)')
    ck(play_space.captain_open(guest), 'guest: Accept opened the captain screen (its Equipment panel)')
    n = faults(guest) - n0
    print('  guest faults after Accept:', n, '|', guest.call('coop.captain')['out'].strip())
    ck(n == 0, f'guest: no caught fault after Accept (0x5971D4: BUG-0304, 0xC02A14: BUG-1278): {fault_lines(guest)[:3]}')
    # close the captain panel like a player (its OK). Not Decline: on the mission screen of no offer it leaves the Space
    # UI hidden behind the drive's message and the rank badge does nothing after it (lane 3, 2026-11-19)
    play_space.close_captain(guest)
    closed = not play_space.captain_open(guest) and uifind.where(guest, ACCEPT) is None
    ck(closed, 'guest: after Accept the captain screen and the mission screen are closed (OK)')


def main():
    host, guest = start('space', planet=fz.fixture_planet())
    ck = Checks()
    for c in (host, guest):  # BUG-1475: the guest keeps the fixture's captain (no species profile there: BUG-1278's twin)
        c.call('coop.fix space.own_captain off')
    time.sleep(3)
    fz.get_drive(guest, 'guest', ck)

    # the bot's targets: a point under the badge is not offered
    class Fake:
        W, H = 1440, 1080

        @staticmethod
        def cmd(line):
            return f'obj P/C1 10 {BADGE[0]} {BADGE[1]}\nobj P/C2 10 700 500\nme 0 0 720 540'
    _, _, objs = play_space.Player.targets(Fake())
    ck([o[0] for o in objs] == ['P/C2'], f'the bot does not click under the rank badge ({objs})')

    print('  guest:', guest.call('coop.captain')['out'].strip())
    # 1. the adventure mission's Accept
    accept_route(guest, ck)

    # 2. the rank badge
    n0 = faults(guest)
    guest.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    guest.call(f'click {BADGE[0]} {BADGE[1]}')
    ok = fz.wait(lambda: play_space.captain_open(guest), 10)
    ck(ok, 'guest: the rank badge opened the captain screen')
    n = faults(guest) - n0
    print('  guest faults after opening it:', n, '|', guest.call('coop.captain')['out'].strip())
    ck(n == 0, f'guest: no caught fault after the rank badge (0x5971D4: BUG-0304, 0xC02A14: BUG-1278): {fault_lines(guest)[:3]}')
    shot(guest, 'captain_open.png')

    fz.to_galaxy(guest, 'guest', ck)
    f = fz.fin(guest)
    ck(f.get('ctx') == '2', f'guest: in the galaxy view ({f["line"][:90]})')
    ck(not play_space.captain_open(guest), 'guest: the captain screen is closed')
    shot(guest, 'captain_galaxy.png')

    for who, c in (('host', host), ('guest', guest)):
        ck('finale:' in fz.sp(c, 'finale'), f'{who}: still answers')
    # no caught fault on either game (BUG-0304: the captain screen's 0x5971D4 is guarded now)
    ck(faults(host) == 0, f'host: no caught fault {fault_lines(host)[:3]}')
    ck(faults(guest) == 0, f'guest: no caught fault {fault_lines(guest)[:3]}')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
