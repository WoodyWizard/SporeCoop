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

Opening the captain screen faults in the game itself (0x5971D4, BUG-0304, also with coop unloaded):
the test expects that fault and checks that both games keep running (suite.py reports=True).

  SPORE_LANE=2 python3 tools/tests/coop_space_captain.py
"""
import time

import coop_space_finale as fz
import play_space
from twoinst import Checks, shot, start

BADGE = (1390, 890)  # the rank badge over the ship's picture, 1440x1080


def faults(con):
    out = con.call('exc')['out']
    for line in out.splitlines():
        if line.startswith('total faults:'):
            return int(line.split(':')[1])
    return 0


def main():
    host, guest = start('space', planet=fz.fixture_planet())
    ck = Checks()
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

    n0 = faults(guest)
    guest.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    guest.call(f'click {BADGE[0]} {BADGE[1]}')
    ok = fz.wait(lambda: play_space.captain_open(guest), 10)
    ck(ok, 'guest: the rank badge opened the captain screen')
    print('  guest faults after opening it:', faults(guest) - n0, '(BUG-0304, the game\'s own)')
    shot(guest, 'captain_open.png')

    fz.to_galaxy(guest, 'guest', ck)
    f = fz.fin(guest)
    ck(f.get('ctx') == '2', f'guest: in the galaxy view ({f["line"][:90]})')
    ck(not play_space.captain_open(guest), 'guest: the captain screen is closed')
    shot(guest, 'captain_galaxy.png')

    for who, c in (('host', host), ('guest', guest)):
        ck('finale:' in fz.sp(c, 'finale'), f'{who}: still answers')
    # only the game's own captain screen fault (BUG-0304) on the guest, none on the host
    ck(faults(host) == 0, 'host: no caught fault')
    ck(all('0x005971D4' in ln for ln in guest.call('exc')['out'].splitlines() if ln.startswith('0x')),
       'guest: no caught fault other than the captain screen one (0x5971D4)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
