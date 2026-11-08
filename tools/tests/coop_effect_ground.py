#!/usr/bin/env python3
"""BUG-0608: a relayed world effect (a hit impact, a splash) is played at its height over the RECEIVER's ground, not
at the sender's absolute position (the two games' grounds can differ by metres: BUG-0332 villages, BUG-0329).

The sender appends the effect's height over its own ground to MSG_EFFECT (pool 5); the receiver puts the effect at
that height over its own ground. On the creature fixture both grounds agree, so step 1 makes the difference:

  1. the guest is given the host's world effect E1B24DDB as the host would send it where the host's ground lies 5 m
     higher than the guest's (coop.effects inject from the host: the guest's creature's place raised 5 m, height
     over the sender's ground 0): the guest's game creates it within 1 m of the guest's ground (the creature's
     radius), not 5 m up in the air
  2. the real relay: the host's game makes the effect at its creature (coop.effects load): the guest's game creates
     it near the host's creature and the message carried the height (effects onto own ground counts it)

  SPORE_FIX_OFF=effects.ground coop_effect_ground.py   (step 1: the effect 5 m over the guest's ground: must FAIL)
"""
import math
import re
import time

from coop_effects_party import FX_ID, last_rx
from twoinst import Checks, start


def pos_of(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def radius(p):
    return math.sqrt(sum(x * x for x in p))


def moved(con):
    m = re.search(r'effects onto own ground: (\d+)', con.call('coop.effects')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    a = pos_of(guest)
    ck(a is not None, f'the guest has its creature ({a})')
    if a:
        ra = radius(a)
        k = (ra + 5.0) / ra
        q = tuple(x * k for x in a)
        out = guest.call('coop.effects inject 0 0 0 5 {} 0 {:.3f} {:.3f} {:.3f} 0'.format(FX_ID, *q))['out']
        time.sleep(1)
        rx = last_rx(guest, 0)
        print(f'  guest creature radius {ra:.2f}; sent at {radius(q):.2f} (0 m over the sender\'s ground); received {rx}')
        ok = rx is not None and rx['pos'] is not None and rx['game'] == 1
        ck(ok, 'the guest\'s game created the effect')
        if ok:
            d = radius(rx['pos']) - ra
            ck(abs(d) < 1.0, f"the effect is on the guest's own ground ({d:+.2f} m from the creature's height)")
    # 2. the real relay
    m0 = moved(guest)
    h = pos_of(host)
    host.call(f'coop.effects load 1 400 {FX_ID}')
    got = None
    t0 = time.time()
    while time.time() - t0 < 12 and not got:
        time.sleep(0.5)
        rx = last_rx(guest, 0)
        if rx and rx['pool'] == 5 and rx['pos'] and moved(guest) > m0:
            got = rx
    host.call('coop.effects load 0')
    print(f'  relayed from the host: {got}, host creature at {h}')
    ck(got is not None, 'the host\'s world effect came with its height over the host\'s ground')
    if got and h:
        d = math.dist(got['pos'], h)
        ck(d < 8.0, f'the guest\'s game created it at the host\'s creature ({d:.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
