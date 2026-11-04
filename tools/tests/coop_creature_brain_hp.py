#!/usr/bin/env python3
"""A guest playing its own creature gets the brain level's health too (BUG-0375).

Player report (0.7.3.3, host marks): "after growing brain more my friend didnt got extra HP i have 29
he has only 14", later "44 / 14". The game adds the brain level's health (10 15 25 40 ... by level)
only to the world's player species (docs/re/creature.md#creature-max-hit-points); a guest's own
creature is another species in its own game and kept level 0's.

1. Two games on the creature fixture; the guest plays its own creature (another species than the
   host's world species, checked).
2. The host gains 250 DNA (AddEvolutionPoints; the co-op DNA sync gives the guest the same total):
   both games reach brain level 1 and play CRG_BrainUpgrade1 (Enter closes the caption).
3. Read in each game: the brain level, the own avatar's max health (GetMaxHitPoints). The guest's own
   max health must rise by the level's step like the host's, and the host's puppet of the guest must
   show the guest's new max health.

  coop_creature_brain_hp.py   (SPORE_FIX_OFF=creature.own_brain_hp must FAIL)"""
import re
import time

from twoinst import Checks, start


def hp_info(con):
    out = con.call('coop.hp')['out']
    m = re.search(r'^own: hp=([\d.]+)/([\d.]+) level=(\d+) species=(\S+) player_species=(\S+) (\w+)', out, re.M)
    return dict(hp=float(m.group(1)), max=float(m.group(2)), level=int(m.group(3)), same=m.group(6) == 'same') if m else None


def puppet_max(con, pid):
    m = re.search(rf'^puppet {pid}: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return float(m.group(2)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    h0, g0 = hp_info(host), hp_info(guest)
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
        h1, g1 = hp_info(host), hp_info(guest)
        if h1 and g1 and h1['level'] >= 1 and g1['level'] >= 1 and 'playing' not in host.call('coop.status')['out'] \
                and 'playing' not in guest.call('coop.status')['out']:
            break
    time.sleep(2)
    h1, g1 = hp_info(host), hp_info(guest)
    print(f'  after: host {h1}, guest {g1}', flush=True)
    ck(h1 and g1 and h1['level'] == 1 and g1['level'] == 1, 'both games at brain level 1')
    if not (h0 and g0 and h1 and g1):
        ck.faults(host=host, guest=guest)
        return ck.result()
    step = h1['max'] - h0['max']
    ck(step >= 4.9, f'the host\'s own max health rose with the brain ({h0["max"]} -> {h1["max"]})')
    ck(abs((g1['max'] - g0['max']) - step) <= 0.05,
       f'the guest\'s own max health rose by the same step in its game ({g0["max"]} -> {g1["max"]}, host +{step})')
    pm = None
    for _ in range(10):
        pm = puppet_max(host, 1)
        if pm is not None and abs(pm - g1['max']) <= 0.05:
            break
        time.sleep(0.3)
    ck(pm is not None and abs(pm - g1['max']) <= 0.05, f'the host\'s game shows the guest\'s new max health ({pm} / {g1["max"]})')
    pm = puppet_max(guest, 0)
    ck(pm is not None and abs(pm - h1['max']) <= 0.05, f'the guest\'s game shows the host\'s new max health ({pm} / {h1["max"]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
