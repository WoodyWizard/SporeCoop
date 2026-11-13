#!/usr/bin/env python3
"""tests (BUG-0991): the finale's bot steps get past the death caption of a destroyed ship.

In the 0.7.13 final check the campaign's host ship was destroyed on the way to the core (the space bot never repairs:
8 of 1500 hp); the death caption ("...you return to your homeworld, a new spaceship at the ready") held the input, the
finale's settle pressed nothing (the finale's busy flag does not show that caption) and to_galaxy wheeled 50 notches
into it: "[stuck] space/finale: zoomed out to the galaxy view".

Fixture space_free (the tutorial lock off; on `space` the respawned ship stayed in the system view under the
tutorial mission, lab 20261113-015206-2). The guest gets the drive, travels alone to another star (no partner to rescue it) and takes a lethal
hit (`coop.space hit 100000`: the game's damage path): destroyed, the death caption up. Then the finale's own steps,
as the campaign runs them: coop_space_finale.to_galaxy (the wheel like a player).
PASS: the bot confirms the caption, the ship respawns (state 0) and the guest reaches the galaxy view.
OLD_FINALE_DEATH=1 (the 0.7.13 bot): stuck under the caption (FAIL).
Also the campaign's repair before the finale: `coop.space heal` brings a damaged ship back to its max hp.

  SPORE_LANE=2 python3 tools/tests/coop_space_finale_death.py
"""
import re
import time

import coop_space_finale as fz
from twoinst import Checks, keyframe, shot, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def field(hp, key):
    m = re.search(r'\b' + key + r'=(\d+)', hp)
    return int(m.group(1)) if m else -1


def star_of(con):
    m = re.search(r'place ([0-9A-F]+) star (\d+)', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2))) if m else (0, 0)


def main():
    host, guest = start('space_free')
    ck = Checks()
    time.sleep(3)
    print('  drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    fz.settle(guest, 'guest', timeout=30)
    # the repair the campaign does before the finale
    print('  hit 300:', sp(guest, 'hit', 300), flush=True)
    h = sp(guest, 'heal')
    m = re.search(r'hp ([\d.]+) max ([\d.]+)', h)
    ck(m and m.group(1) == m.group(2), f'coop.space heal repairs the ship to its max hp ({h})')
    _, home = star_of(guest)
    print('  guest away:', sp(guest, 'jumpto', home + 1), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and star_of(guest)[1] in (0, home):
        time.sleep(1)
    time.sleep(5)
    fz.settle(guest, 'guest', timeout=30)
    ck(star_of(guest)[1] not in (0, home), f'the guest is alone at another star ({sp(guest, "star")})')
    print('  lethal hit:', sp(guest, 'hit', 100000), flush=True)
    time.sleep(3)
    hp = sp(guest, 'hp')
    ck(field(hp, 'state') == 2 and field(hp, 'cine') == 1, f'the ship is destroyed, the death caption is up ({hp})')
    shot(guest, 'finale_death_caption.png')
    # the finale's steps as the campaign runs them
    fz.to_galaxy(guest, 'guest', ck)
    hp = sp(guest, 'hp')
    f = fz.fin(guest)
    print(f'  after the bot: {hp}; {f["line"][:120]}', flush=True)
    ck(field(hp, 'state') == 0 and field(hp, 'cine') == 0, f'the death caption was confirmed, the ship respawned ({hp})')
    keyframe('after_death', "the guest's galaxy view after its ship's death and respawn: no death caption, the "
             'galaxy map with the stars; the host in its home system')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
