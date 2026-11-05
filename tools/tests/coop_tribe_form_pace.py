#!/usr/bin/env python3
"""Tribe: new villages appear at one player's pace, not at the party's (BUG-0571).

Player report (host): "villages in tribe spawn so much faster" than alone. The host's NPC tribe controller forms
the third tribe (and the fourth and fifth at once) when the player tribe's food passes 100 before any village was
allied or conquered (docs/re/tribe.md#tribe-guest-a1); alone and in a session the same, 2 s after the food passed it
(lab 20261105-113623 / 20261105-113252). In a session that food is the whole party's.

1. Two games on the tribe fixture (2 tribes: the players' and one NPC village). The party's food goes to 135 (over
   one tribe's 100, under 2 x 100): for 25 s the GUEST's game lists 2 tribes (the game's own tribe nouns,
   `coop.wstate`) and so does the host's.
2. The party's food goes over 200: within 20 s the guest's game lists 5 tribes (the pace goes on, the host's
   villages are on the guest).

SPORE_FIX_OFF=tribe.form_pace: 5 tribes within seconds of step 1 (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_form_pace.py
"""
import re
import time

from twoinst import Checks, start


def tribes(con):
    return len(re.findall(r'^tribe i=\d+ player=', con.call('coop.wstate')['out'], re.M))


def food(con):
    m = re.search(r'shared food=([-\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else -1.0


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    h0, g0 = tribes(host), tribes(guest)
    print(f'  tribes host {h0} guest {g0}, food {food(host):.0f}', flush=True)
    ck(h0 == 2 and g0 == 2, f'the fixture has 2 tribes on both games ({h0}, {g0})')
    host.call(f'coop.res add food {135 - food(host):.0f}')
    print(f'  food {food(host):.0f}', flush=True)
    most = 0
    t0 = time.time()
    while time.time() - t0 < 25:
        most = max(most, tribes(guest))
        time.sleep(1)
    h1 = tribes(host)
    print(f'  after 25 s at food 135: host {h1}, the guest at most {most}', flush=True)
    print(' ', host.call('coop.tribe form_pace')['out'].strip()[:200], flush=True)
    ck(most == 2 and h1 == 2, f'no new village at the party\'s food 135 (host {h1}, guest at most {most})')
    host.call(f'coop.res add food {230 - food(host):.0f}')
    print(f'  food {food(host):.0f}', flush=True)
    t0, g2 = time.time(), 0
    while time.time() - t0 < 20:
        g2 = tribes(guest)
        if g2 >= 5:
            break
        time.sleep(1)
    print(f'  {time.time() - t0:.0f} s after food 230: host {tribes(host)}, guest {g2}', flush=True)
    ck(g2 >= 5, f'over 2 x 100 the villages come, on the guest too ({g2} >= 5)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
