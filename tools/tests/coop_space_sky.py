#!/usr/bin/env python3
"""Space: on a planet both players' games show the same sky (time of day) (BUG-0712).

The detector's `All:state:time_of_day` in coop_space_shots.py: with both ships on the colony planet the skies stood
160-172 deg apart in every sample. The sky row of wsync (BUG-0579) was in play in Creature, Tribe and Civ only.

1. Two games on the `space_empire` fixture; both land on its colony planet (play_space.Player.land_retry).
2. For 30 s every 3 s: each game's own sky (`coop.wsync sky`: the time of day object's orientation).
PASS: in the last 15 s the two skies are within 0.1 rad (the host sends the row every second).
SPORE_FIX_OFF=wsync.space_tod must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_space_sky.py
"""
import math
import re
import time

import lane
from play_space import Player
from twoinst import Checks, start

PLANET = '01460035'  # space_empire's colony in the home system


def sky(con):
    m = re.search(r'sky (\S+) (\S+) (\S+) (\S+)', con.call('coop.wsync sky')['out'])
    return [float(x) for x in m.groups()] if m else None


def angle(a, b):
    if not a or not b:
        return None
    return 2 * math.acos(min(1.0, abs(sum(x * y for x, y in zip(a, b)))))


def main():
    host, guest = start('space_empire')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.land_retry(PLANET)
    print(f'  places: host {ph.place()} {ph.ctx()}, guest {pg.place()} {pg.ctx()}', flush=True)
    ck(ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet', 'both on the colony planet')
    t0, late = time.time(), []
    while time.time() - t0 < 30:
        a = angle(sky(host), sky(guest))
        t = time.time() - t0
        print(f'    {t:4.1f} s: skies {a if a is None else round(math.degrees(a), 1)} deg apart', flush=True)
        if t >= 15:
            late.append(a)
        time.sleep(3)
    ck(late and all(a is not None for a in late), f'both games have a sky on the planet ({len(late)} samples)')
    ck(late and all(a is not None and a < 0.1 for a in late),
       f'the guest\'s sky is the host\'s (deg apart: {[a if a is None else round(math.degrees(a), 1) for a in late]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
