#!/usr/bin/env python3
"""Space: a partner's terraforming change with a non-finite value never reaches the planet (BUG-1489, audit SPC-15).

A partner on the same planet sends the deltas of its terraforming tools (K_TERRA: atmosphere, water, temperature). They
went into the game unchecked: a NaN (only a modified game sends one) passes `d != 0`, the planet's terraforming flag is
switched on and the terrain tick integrates it into the planet record (+0xB0 atmosphere, +0xB4, +0xB8), which the world
saves.

1. Two games on space_free; both land on the colony planet 01460035 (play_space.Player.land_retry).
2. The host sends an honest delta (`coop.space terrasend 41460035 0.05 0 0`, the message its Atmosphere Generator
   sends): the guest's record moves (the path works).
3. The host sends NaN and infinite deltas (`terrasend ... nan 0 nan`, `inf -inf inf`). Over 12 s the guest's planet record
   must stay finite, and its game must have refused them (`terra_refused`).
   0.7.16.1 (SPORE_FIX_OFF=space.terra_finite): the record becomes NaN (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_space_terra_nan.py
"""
import math
import re
import time

from harness import sp
import lane
from play_space import Player
from twoinst import Checks, keyframe, start

PLANET = '01460035'
PLACE = '%08X' % (0x40000000 | int(PLANET, 16))


def rec(con):
    m = re.search(r'b0=(\S+) b4=(\S+) b8=(\S+)', sp(con, 'planet'))
    if not m:
        return None
    try:
        return tuple(float(m.group(i)) for i in (1, 2, 3))
    except ValueError:
        return (math.nan,) * 3


def stat(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con))
    return int(m.group(1)) if m else -1


def finite(r):
    return r is not None and all(math.isfinite(x) for x in r)


def main():
    host, guest = start('space_free')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 900, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 900, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.land_retry(PLANET)
    on = ph.ctx() == 'planet' and ph.place() == pg.place()
    ck(on, f'both on the planet {PLANET} ({ph.place()}, {pg.place()})')
    if not on:
        return ck.result()
    time.sleep(4)

    # 2. an honest delta
    g0 = rec(guest)
    got0 = stat(guest, 'terra_got')
    print('  host sends 0.05 atmosphere:', sp(host, 'terrasend', PLACE, 0.05, 0, 0), flush=True)
    t0 = time.time()
    while time.time() - t0 < 20 and stat(guest, 'terra_got') <= got0:
        time.sleep(1)
    time.sleep(8)  # the terrain tick integrates it
    g1 = rec(guest)
    print(f'  guest record {g0} -> {g1} (terra_got {got0} -> {stat(guest, "terra_got")})', flush=True)
    ck(stat(guest, 'terra_got') > got0, 'the guest\'s game took the honest delta')

    # 3. non-finite deltas
    ref0 = stat(guest, 'terra_refused')
    print('  host sends nan:', sp(host, 'terrasend', PLACE, 'nan', 0, 'nan'), flush=True)
    print('  host sends inf:', sp(host, 'terrasend', PLACE, 'inf', '-inf', 'inf'), flush=True)
    worst = None
    t0 = time.time()
    while time.time() - t0 < 12:
        r = rec(guest)
        if not finite(r):
            worst = r
        time.sleep(1)
    r = rec(guest)
    print(f'  guest record after: {r} (terra_refused {ref0} -> {stat(guest, "terra_refused")})', flush=True)
    ck(worst is None and finite(r), f'the guest\'s planet record stays finite (seen {worst}, now {r})')
    ck(stat(guest, 'terra_refused') >= ref0 + 2, 'the guest\'s game refused both changes')
    keyframe('terra_nan', 'both on the planet after the non-finite terraforming messages: the planet looks as before (no '
             'black or missing atmosphere / water)', players={'host': host, 'guest': guest})
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
