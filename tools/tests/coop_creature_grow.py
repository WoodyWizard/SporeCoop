#!/usr/bin/env python3
"""BUG-0732 / BUG-0733 (class BUG-0418): the creatures grow with the brain upgrades on both screens.

A creature's size is the game's creature vt 0x80 (0xC04B50): the brain factor (1.0, 1.1, 1.2 ... by brain level)
and the avatar normalizing scale are applied only to the world's player species
(docs/re/creature.md#creature-player-species-callers). The brain upgrade (0xD3FCA0) gives the avatar SetScale(vt
0x80(0)).
 - BUG-0732: a guest playing its own creature is another species in its own game: its creature never grew.
 - BUG-0733: the partner's puppet got its size once at its spawn and kept it: neither player saw the other grow.

1. Two games on the creature fixture; the guest plays its own creature (not the world's species, checked).
2. The host gains 250 DNA (the co-op DNA sync gives the guest the same total): both games reach brain level 1
   (Enter closes the captions).
3. Read in each game (`coop.hp`): the own avatar's size (cSpatialObject GetScale) and the size of the partner's
   puppet, before and after.

PASS: the guest's own creature grows by the factor the host's grows by (within 2 %: the brain factor 1.1 and the
normalizing scale of the level, x1.15 on the fixture), and in each game the
partner's puppet is the partner's own size x 1.15 (the spawn's factor) after the upgrade, within 2 %.
FAIL: SPORE_FIX_OFF=creature.own_scale (the guest's creature stays the same size), SPORE_FIX_OFF=creature.puppet_scale
(the puppets keep their spawn size).

  SPORE_LANE=7 python3 tools/tests/coop_creature_grow.py
"""
import re
import time

from own_creature import back_to_world, play_own
from twoinst import Checks, start

K = 1.15  # the puppet's spawn factor (modes.cpp creature_spawn)


def info(con, partner):
    out = con.call('coop.hp')['out']
    m = re.search(r'^own: hp=\S+ level=(\d+) .* (same|other) base_game=\S+ scale=([\d.]+)', out, re.M)
    p = re.search(rf'^puppet {partner}: .* scale=([\d.]+)', out, re.M)
    return dict(level=int(m.group(1)), same=m.group(2) == 'same', own=float(m.group(3)),
                puppet=float(p.group(1)) if p else None) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    ck(play_own(guest), 'the guest plays one of its own creatures (not the world\'s species)')
    time.sleep(3)
    h0, g0 = info(host, 1), info(guest, 0)
    print(f'  before: host {h0}, guest {g0}', flush=True)
    ck(h0 and g0 and h0['level'] == 0 and g0['level'] == 0, 'both at brain level 0')
    ck(g0 and not g0['same'], 'the guest plays its own creature (not the world\'s player species)')
    host.call('coop.dna add 250')
    h1 = g1 = None
    t0 = time.time()
    while time.time() - t0 < 40:  # the brain upgrade captions wait for Enter
        for con in (host, guest):
            con.call('key enter')
        time.sleep(1.5)
        h1, g1 = info(host, 1), info(guest, 0)
        if h1 and g1 and h1['level'] >= 1 and g1['level'] >= 1 and 'playing' not in host.call('coop.status')['out'] \
                and 'playing' not in guest.call('coop.status')['out']:
            break
    time.sleep(3)
    h1, g1 = info(host, 1), info(guest, 0)
    print(f'  after: host {h1}, guest {g1}', flush=True)
    print('  guest:', [ln for ln in guest.call('coop.hp')['out'].splitlines() if ln.startswith('fixes')], flush=True)
    ck(h1 and g1 and h1['level'] == 1 and g1['level'] == 1, 'both games at brain level 1')
    if not (h0 and g0 and h1 and g1):
        ck.faults(host=host, guest=guest)
        return ck.result()
    hr = h1['own'] / h0['own'] if h0['own'] else 0
    gr = g1['own'] / g0['own'] if g0['own'] else 0
    ck(hr >= 1.05, f'the host\'s creature grew with the brain ({h0["own"]} -> {h1["own"]}, x{hr:.3f})')
    ck(abs(gr - hr) <= 0.02, f'the guest\'s own creature grew by the same factor in its game ({g0["own"]} -> {g1["own"]}, '
                             f'x{gr:.3f}, host x{hr:.3f})')
    for who, me, other in (('host', h1, g1), ('guest', g1, h1)):
        want = other['own'] * K
        ck(me['puppet'] is not None and abs(me['puppet'] - want) <= 0.02 * want,
           f'the {who}\'s game shows the partner\'s creature at its size ({me["puppet"]}, the partner\'s own '
           f'{other["own"]} x {K})')
    back_to_world(guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
