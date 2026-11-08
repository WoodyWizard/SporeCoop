#!/usr/bin/env python3
"""BUG-0683: a partner's standing creature pressed into the ground on this game is put back onto it.

campaign_tribe (rel/0.7.5, rel/0.7.10): after the evolution editor all four creatures stood on one point of the nest;
while the brain upgrade caption played the game separated the stacked creatures sideways and pressed one partner's
puppet 0.55-0.74 m under its terrain on another game. The partner stood still, so the mod's driving placed nothing and
the puppet stayed in the ground (agl -0.65 on two trace lines, the monitor's ground gate) until the caption ended.
creature.own_ground (BUG-0592) covers the own creature only.

Two games on the creature fixture, team off, both creatures standing 6 m apart. On the host the guest's puppet is put
as the separation left it: 0.7 m to the side and 0.69 m under its terrain there (`coop.puppet sink 0.69 0.7`, a
teleport), twice; for 4 s every 0.25 s the puppet's height over the host's terrain (`coop.puppet agl`: the game's own
position and GetHeightAt). Checks: the sink put it in the ground at first (a test without that proves nothing); after
the first second it is never more than UNDER m under its ground on 2 samples in a row; the guest's own creature stood
on its ground meanwhile (the partner really stood).

SPORE_FIX_OFF=creature.puppet_unground: the puppet stays in the ground, it FAILS.

  SPORE_LANE=9 python3 tools/tests/coop_puppet_unground.py
"""
import re
import time

from coop_own_ground import above, avatar, at, basis
from twoinst import Checks, start

UNDER = 0.25  # m


def puppet_agl(con, cmd='agl'):
    m = re.search(r'puppet 1 agl=([-\d.]+)', con.call(f'coop.puppet {cmd}')['out'])
    return float(m.group(1)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    hp = avatar(host)
    if not ck(hp is not None, 'the host\'s creature'):
        return ck.result()
    guest.call('coop.npc gopos %.3f %.3f %.3f' % at(hp, *basis(hp), 0.0, 6.0))
    time.sleep(5)
    print(f'  guest\'s puppet on the host: agl {puppet_agl(host)}, the guest\'s own agl {above(guest)}', flush=True)
    for n in range(2):
        a0 = puppet_agl(host, 'sink 0.69 0.7')
        rows, t0 = [], time.time()
        while time.time() - t0 < 4:
            rows.append((time.time() - t0, puppet_agl(host), above(guest)))
            time.sleep(0.25)
        print(f'  round {n + 1}: after the sink {a0}; ' + ' '.join(f'{t:.1f}:{a}' for t, a, _ in rows), flush=True)
        ck(a0 is not None and a0 < -UNDER, f'round {n + 1}: the sink put the guest\'s puppet in the host\'s ground ({a0})')
        late = [a for t, a, _ in rows if t > 1.0 and a is not None]
        runs = sum(1 for x, y in zip(late, late[1:]) if x < -UNDER and y < -UNDER)
        ck(late and runs == 0, f'round {n + 1}: after a second the puppet is never {UNDER} m under the host\'s ground on '
                               f'2 samples in a row (min {min(late or [0]):.2f} m, {runs} pairs)')
        own = [g for _, _, g in rows if g is not None]
        ck(own and max(abs(g) for g in own) < 0.3, f'round {n + 1}: the guest\'s own creature stood on its ground '
                                                   f'({min(own or [0]):.2f}..{max(own or [0]):.2f})')
        time.sleep(2)
    print(' ', host.call('coop.puppet agl')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
