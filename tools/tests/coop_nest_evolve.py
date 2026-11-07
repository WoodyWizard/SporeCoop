#!/usr/bin/env python3
"""BUG-0668 (and the likely cause of BUG-0639): the NPC nests evolve in the host's game only, every guest's nest shows
the host's generation and species.

Creature fixture. 1. the host's avatar kills the members of the nearest nest of another species until the host's game
makes that herd extinct; 2. the pack's DNA goes up 250 (the game's NPC herd evolution runs: generation +0xF0 + 1, a new
species from the save's chain, in CreatureGame_Update 0xD45740); then for --secs both games' own herds are read
(coop.nest evolve: generation, species key +0xA4 -> +0x504, threshold +0x94, extinct +0x190), nest by nest (the herd's
home +0x34, the save's on both games). Asserted: the host's game evolved nests; at every sample after the host's
evolution each guest nest has the host's generation and species; the nest extinct on the host is never evolved in the
guest's game. Off (SPORE_FIX_OFF=wsync.herd_evo): the guest evolves its own nests (its own thresholds, another moment)
and the extinct nest to a new species.

  coop_nest_evolve.py [--secs S]"""
import argparse
import re
import time

from coop_nest_eggs import nest_herds
from twoinst import Checks, start

ROW = re.compile(r'herd (\S+) should_evolve=(\d) gen=(-?\d+) evolved_keys=(-?\d+) threshold=(\S+) extinct=(\d) members=(-?\d+) '
                 r'species=\S+ skey=([0-9A-F]+) next=[0-9A-F]+ home=\((\S+) (\S+) (\S+)\)')


def herds(con):
    """{home (rounded): (herd, gen, skey, threshold, extinct)} of this game's own herds"""
    out = {}
    for m in ROW.finditer(con.call('coop.nest evolve')['out']):
        home = tuple(int(round(float(v))) for v in m.group(9, 10, 11))
        out[home] = (m.group(1), int(m.group(3)), m.group(8), m.group(5), int(m.group(6)))
    return out


def near(a, b):
    return all(abs(x - y) <= 1 for x, y in zip(a, b))


def match(hs, gs):
    """(nests compared, [differences]) of the guest's nests against the host's"""
    diffs, n = [], 0
    for home, h in hs.items():
        g = next((v for k, v in gs.items() if near(k, home)), None)
        if g is None:
            continue
        n += 1
        if (h[1], h[2]) != (g[1], g[2]):
            diffs.append((home, 'host gen %d %s' % (h[1], h[2]), 'guest gen %d %s' % (g[1], g[2])))
    return n, diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=60)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    time.sleep(3)
    ns = nest_herds(host)
    print('  nests of other species (dist, herd, members, home):', ns[:4], flush=True)
    if not ck(ns, 'a nest of another species with members on the host'):
        return ck.result()
    _, h, _, home = ns[0]
    host.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 12, home[1], home[2] + 2))
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 14, home[1], home[2] + 2))
    time.sleep(5)
    extinct_home = None
    for _ in range(5):
        print('  host:', host.call(f'coop.nest kill {h} 20')['out'].strip(), flush=True)
        time.sleep(4)
        hx = herds(host)
        extinct_home = next((k for k, v in hx.items() if v[0] == h and v[4]), None)
        if extinct_home:
            break
    if not ck(extinct_home, 'the host\'s game made the nest extinct (+0x190)'):
        return ck.result()
    hs0, gs0 = herds(host), herds(guest)
    n0, d0 = match(hs0, gs0)
    print(f'  before the DNA: {n0} nests on both games, differences {d0[:4]}', flush=True)
    print('  host:', host.call('coop.dna add 250')['out'].strip().splitlines()[0], flush=True)
    t0 = time.time()
    evolved_host = 0
    samples = bad = 0
    extinct_evolved = []
    first_diff = None
    while time.time() - t0 < a.secs:
        time.sleep(5)
        for c in (host, guest):
            st = c.call('coop.status')['out']
            if 'cinematic' in st and 'playing' in st:
                c.call('key enter')
        hs, gs = herds(host), herds(guest)
        evolved_host = max(evolved_host, sum(1 for v in hs.values() if v[1] > 0))
        n, d = match(hs, gs)
        ge = next((v for k, v in gs.items() if near(k, extinct_home)), None)
        he = next((v for k, v in hs.items() if near(k, extinct_home)), None)
        if ge and he and (ge[1], ge[2]) != (he[1], he[2]):
            extinct_evolved.append((round(time.time() - t0), he[1:3], ge[1:3]))
        if time.time() - t0 < 12:
            continue  # the host's row reaches the guest within 1-2 s; the game's evolution itself runs within a frame
        samples += 1
        if d:
            bad += 1
            first_diff = first_diff or (round(time.time() - t0), d[:4])
        print(f'  [{time.time() - t0:4.0f}s] host nests evolved {sum(1 for v in hs.values() if v[1] > 0)}/{len(hs)}, '
              f'compared {n}, differ {len(d)} {d[:3]}; extinct nest host {he and he[1:4]} guest {ge and ge[1:4]}', flush=True)
    print('  guest wsync:', [l for l in guest.call('coop.wsync')['out'].splitlines() if 'herd_evo' in l], flush=True)
    ck(evolved_host > 0, f'the host\'s game evolved NPC nests after the DNA ({evolved_host})')
    ck(samples >= 3 and bad == 0, f'every guest nest has the host\'s generation and species in every sample ({samples - bad}/{samples}'
       f' samples equal; first difference {first_diff})')
    ck(not extinct_evolved, f'the nest extinct on the host is not evolved in the guest\'s game ({extinct_evolved[:3]})')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
