#!/usr/bin/env python3
"""Tribe: after the host's game over and bounce back the guest's planet is the host's again (BUG-0564 class).

Player report (0.7.5): the host's game went Tribe -> Loading -> Tribe three times (19:06, 19:15, 19:30: a game
over, the game reloads the last save) while the guest's game stayed in its world (no stage change in its
log). From then on the guest saw villages the host's world no longer had, the quest of the old world (the
Pink village), empty plazas where the host fought at a hut, other food, other tool places
(BUG-0564, BUG-0566, BUG-0567, BUG-0569, BUG-0574, BUG-0572).

1. Tribe fixture. Before the game over the host's world gets what its last save does not have: a new NPC
   tribe (`coop.tribe form`: the controller's own forming) and 40 food (`coop.res add food 40`). The guest
   shows both (the tribe within 40 m of the host's, the same food).
2. The host knocks out its own player tribe (`coop.tribe pincer`, as coop_tribe_bounce_mirrors.py), OK on the
   game-over caption: the host's game reloads the last save (without the new tribe, the old food).
3. PASS: within 40 s the guest's game has every NPC tribe of the host's (each within 40 m), no tribe the
   host has not, and the host's food.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_bounce_world.py
"""
import math
import re
import time

from coop_tribe_gameover import frames_go_on, player_members
from coop_tribe_guest_tribes import show, tribes
from coop_tribe_food_zero import food
from twoinst import Checks, start


def diff(h, g):
    """(host NPC tribes the guest lacks, guest NPC tribes the host has not)"""
    hn, gn = [t for t in h if not t[0]], [t for t in g if not t[0]]
    lack = [t for t in hn if all(math.dist(t[1], x[1]) > 40 for x in gn)]
    extra = [t for t in gn if all(math.dist(t[1], x[1]) > 40 for x in hn)]
    return lack, extra


def bounce_ok(host):
    """the caption's OK (Enter), again when the game has not left Tribe for its reload 10 s later (a key the
    caption took before its button was live)"""
    for _ in range(3):
        host.call('key enter')
        t0 = time.time()
        while time.time() - t0 < 10:
            if 'no window' in host.call('coop.ui find "Alas"')['out']:
                return True
            time.sleep(1)
    return False


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
    print('  host: ' + host.call('coop.res add food 40')['out'].strip(), flush=True)
    time.sleep(10)
    h0, g0 = tribes(host), tribes(guest)
    lack, extra = diff(h0, g0)
    print(f'  before: host {show(h0)} food {food(host)}; guest {show(g0)} food {food(guest)}', flush=True)
    ck(not lack and not extra, f'before the game over the guest has the host\'s tribes (lacks {show(lack)}, '
       f'extra {show(extra)})')
    own, enemy = player_members(host)
    ck(own and enemy, 'the player tribe and another tribe have members')
    if not (own and enemy):
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 60:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if not alive or not enemy:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    t2, alas = time.time(), False
    while time.time() - t2 < 75 and not alas:  # ~20 s in a quiet lab, more under load
        time.sleep(3)
        alas = 'no window' not in host.call('coop.ui find "Alas"')['out']
    ck(alas, 'the game-over caption is up on the host')
    bounce_ok(host)  # OK: the game reloads the last save
    ck(frames_go_on(host, 15), 'the host bounces back and keeps drawing')
    t1 = time.time()
    while True:
        h1, g1 = tribes(host), tribes(guest)
        lack, extra = diff(h1, g1)
        hf, gf = food(host), food(guest)
        if (not lack and not extra and hf is not None and gf is not None and abs(hf - gf) < 1) or time.time() - t1 > 40:
            break
        time.sleep(3)
    print(f'  after the bounce back: host {show(h1)} food {hf}; guest {show(g1)} food {gf}', flush=True)
    ck(len([t for t in h1 if not t[0]]) < len([t for t in h0 if not t[0]]),
       'the host\'s reloaded world lacks the tribe formed after its save (the scenario happened)')
    ck(not extra, f'the guest\'s game has no tribe the host\'s world has not ({show(extra)})')
    ck(not lack, f'the guest\'s game has every tribe of the host\'s ({show(lack)} missing)')
    ck(hf is not None and gf is not None and abs(hf - gf) < 1, f'the guest has the host\'s food (host {hf}, guest {gf})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
