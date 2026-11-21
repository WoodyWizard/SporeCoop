#!/usr/bin/env python3
"""BUG-9283 (the Lineages port: its downed creature, revive and party wipe): a player's lethal hit in the Creature stage.

The guest plays its own creature (creations_state.choose_own) and has its own nest (BUG-9262). Checks, each on the game
that shows it:
  1. guest: a lethal hit (`coop.rescue down`, through the game's damage path) leaves its creature alive at 1 health
     (`coop.avatar` hp), held where it fell, a further hit not dealt; host: a window for player 1 (`coop.rescue`); guest:
     the host's line for its own creature; no world save on the host meanwhile
  2. the host's creature put next to it revives it: 40 % of its maximum health on the guest's game
  3. down again 30 m from everybody (creature.down_60 off: 30 s): the window runs out, the guest's game kills it and it
     respawns at its own nest; the pack's DNA drops by 10 % on both games
  4. both down: the party wipe within a few seconds; each respawns at its own nest
A new feature: on main `coop.rescue` is unknown; with SPORE_FIX_OFF=creature.rescue check 1 fails (the guest dies at once).
"""
import math
import re
import time

import creations_state
from twoinst import Checks, shot, start


def out(con, line):
    return con.call(line).get('out', '') or ''


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, AttributeError, TypeError):
            pass
        time.sleep(step)
    return False


def pos(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.avatar'))
    return tuple(map(float, m.groups())) if m else None


def hp(con):
    m = re.search(r'hp=(-?[\d.]+)/([\d.]+)', out(con, 'coop.avatar'))
    return (float(m.group(1)), float(m.group(2))) if m else None


def dist(a, b):
    return math.dist(a, b) if a and b else -1.0


def counter(con, name):
    m = re.search(r'\b' + name + r'=(\d+)', out(con, 'coop.rescue'))
    return int(m.group(1)) if m else -1


def dna(con):
    m = re.search(r'dna=(-?[\d.]+)', out(con, 'coop.dna'))
    return float(m.group(1)) if m else None


def home(con):
    m = re.search(r'home nest at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.nest home'))
    return tuple(map(float, m.groups())) if m else None


def put(con, p):
    out(con, 'coop.abilities put %.1f %.1f %.1f' % p)


def unpause(*cons):
    """the game's captions that pause it (the brain upgrade, "You were killed") are OKed"""
    for con in cons:
        if 'paused' in ''.join(l for l in out(con, 'coop.status').splitlines() if l.startswith('busy')):
            out(con, 'key enter')


def up_at(con, nest):
    unpause(con)
    h = hp(con)
    return h and h[0] > 1.5 and dist(pos(con), nest) < 25


def main():
    host, guest = start('creature')
    ck = Checks()
    own = creations_state.choose_own(guest)
    ck(own is not None, f'the guest has a creature of its own ({own})')
    ck(wait(lambda: 'placed=1' in out(guest, 'coop.nest home'), 300, 3), 'guest: it lives in its own nest')
    gn, hn = home(guest), pos(host)
    mid = tuple((a + b) / 2 for a, b in zip(gn, hn))
    out(host, 'coop.dna add 100')
    time.sleep(4)
    for _ in range(3):
        unpause(host, guest)
        time.sleep(2)
    # 1. down
    put(guest, mid)
    time.sleep(3)
    out(guest, 'coop.rescue down')
    h = hp(guest)
    ck(h and h[0] == 1.0, f'guest: its creature is alive at 1 health after a lethal hit ({h})')
    ck(wait(lambda: 'player 1: knockout' in out(host, 'coop.rescue'), 5, 0.5), 'host: a window for player 1')
    ck(wait(lambda: 'player 1 left=' in out(guest, 'coop.rescue'), 5, 0.5), 'guest: the host\'s line for its creature')
    ck('down' in out(host, 'coop.save'), 'host: no world save while a player is down')
    out(guest, 'coop.team hit 50')
    ck(hp(guest)[0] == 1.0, 'guest: a hit on the downed creature is not dealt')
    p0 = pos(guest)
    put(guest, hn)
    time.sleep(1)
    ck(dist(pos(guest), p0) < 1, 'guest: held where it fell')
    shot(host, 'creature_rescue_down_host.png')
    shot(guest, 'creature_rescue_down_guest.png')
    # 2. the revive
    put(host, (p0[0] + 2, p0[1], p0[2]))
    ck(wait(lambda: counter(host, 'revives') >= 1, 10, 0.5), 'host: the guest was revived')
    ck(wait(lambda: hp(guest)[0] >= 0.39 * hp(guest)[1], 5, 0.5), f'guest: up with 40 % health ({hp(guest)})')
    # 3. nobody comes
    out(host, 'coop.fix creature.down_60 off')
    out(guest, 'coop.fix creature.down_60 off')
    put(host, hn)
    put(guest, mid)
    time.sleep(4)
    d0 = dna(host)
    out(guest, 'coop.rescue down')
    ck(wait(lambda: counter(host, 'expired') >= 1, 45), 'host: the window ran out')
    ck(wait(lambda: up_at(guest, gn), 120, 2), f'guest: respawned at its own nest ({dist(pos(guest), gn):.0f} m)')
    d1 = dna(host)
    ck(d0 and d1 is not None and abs((d0 - d1) - math.ceil(d0 * 0.1)) < 0.6, f'host: the pack lost 10 % DNA ({d0} -> {d1})')
    ck(wait(lambda: abs(dna(guest) - d1) < 0.6, 10), 'guest: the same DNA')
    out(host, 'coop.fix creature.down_60 on')
    out(guest, 'coop.fix creature.down_60 on')
    # 4. the party wipe
    time.sleep(8)
    put(host, mid)
    put(guest, (mid[0] + 15, mid[1], mid[2]))
    time.sleep(4)
    out(guest, 'coop.rescue down')
    out(host, 'coop.rescue down')
    ck(wait(lambda: counter(host, 'wipes') >= 1, 8, 0.5), 'host: the party wipe')
    ck(wait(lambda: up_at(guest, gn), 120, 2), f'guest: respawned at its own nest ({dist(pos(guest), gn):.0f} m)')
    ck(wait(lambda: up_at(host, hn), 120, 2), f'host: respawned at its own nest ({dist(pos(host), hn):.0f} m)')
    ck.done()


if __name__ == '__main__':
    main()
