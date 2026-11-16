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
import os
import re
import time

import creations_state

from coop_nest_eggs import nest_herds
from twoinst import Checks, start

ROW = re.compile(r'herd (\S+) should_evolve=(\d) gen=(-?\d+) evolved_keys=(-?\d+) threshold=(\S+) extinct=(\d) members=(-?\d+) '
                 r'species=\S+ skey=([0-9A-F]+) next=[0-9A-F]+ home=\((\S+) (\S+) (\S+)\) dist=\S+ nest=(\S+) arch=([0-9A-F]+) '
                 r'mkey=([0-9A-F]+)')
# BUG-1181: only the game's nests are compared (a herd with a nest +0x160). The herds of our own (the partner's puppet,
# NPC proxies: no nest, no archetype) are not the world's: a guest on its own creature has a puppet on the host of the
# host's copy of its creation (a key the host's game made in the run) and the host's puppet on the guest is of the
# world's species; their herds stand where the two avatars stand. NEST_EVOLVE_OUR_HERDS=1: compare every herd (before
# BUG-1181, the twin: FAILS with --own)
OUR_HERDS = os.environ.get('NEST_EVOLVE_OUR_HERDS') == '1'


def nestless(nest):
    return nest in ('00000000', '(nil)', '0', 'nil')


def ours(h):
    """a herd of our own (game::create_herd: no nest, no archetype); the game's own herds always have an archetype"""
    return nestless(h[5]) and h[7] == '00000000'


def herds(con, with_ours=None):
    """{home (rounded): (herd, gen, skey, threshold, extinct, nest, mkey, arch)} of this game's herds (with_ours: also
    the herds of our own)"""
    with_ours = OUR_HERDS if with_ours is None else with_ours
    out = {}
    for m in ROW.finditer(con.call('coop.nest evolve')['out']):
        home = tuple(int(round(float(v))) for v in m.group(9, 10, 11))
        h = (m.group(1), int(m.group(3)), m.group(8), m.group(5), int(m.group(6)), m.group(12), m.group(14), m.group(13))
        if with_ours or not ours(h):
            out[home] = h
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
            diffs.append((home, 'host gen %d %s%s' % (h[1], h[2], ' (ours)' if ours(h) else ''),
                          'guest gen %d %s%s' % (g[1], g[2], ' (ours)' if ours(g) else '')))
    return n, diffs


def members_differ(hs, gs):
    """(nests with live members on both games, [(home, host members' key, guest members' key)]): the game's nests whose
    live members (the creatures the game shows) are of another species on the guest than on the host"""
    out, n = [], 0
    for home, h in hs.items():
        g = next((v for k, v in gs.items() if near(k, home)), None)
        if not g or ours(h) or '00000000' in (h[6], g[6]):
            continue
        n += 1
        if h[6] != g[6]:
            out.append((home, h[6], g[6]))
    return n, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=60)
    ap.add_argument('--own', action='store_true', help='the guest plays its own creature (BUG-1181)')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    st0 = None
    if a.own:
        st0 = creations_state.save(guest)
        own = creations_state.choose_own(guest)
        ck(own, f'the guest chose its own creature {own}')
        ok = wait(lambda: own_key(guest) == own, 40)
        ck(ok, f"the guest plays its own creature, not the world's species ({guest.call('coop.avatar')['out'].strip()[:80]})")
        time.sleep(3)
        print('  the herds of our own (puppets, proxies) on the host and on the guest:',
              {k: v[2] for k, v in herds(host, True).items() if ours(v)},
              {k: v[2] for k, v in herds(guest, True).items() if ours(v)}, flush=True)
    try:
        return run(a, host, guest, ck)
    finally:
        if st0:
            creations_state.restore(guest, st0)


def own_key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m and m.group(1)


def wait(f, secs, step=1.0):
    t = time.time()
    while time.time() - t < secs:
        if f():
            return True
        time.sleep(step)
    return False


def run(a, host, guest, ck):
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
    hs, gs = herds(host), herds(guest)
    thr = [(k, v[3], g[3]) for k, v in hs.items() for kk, g in gs.items() if near(k, kk) and v[1] > 0 and v[3] != g[3]]
    print(f'  evolved nests whose threshold +0x94 differs (home, host, guest): {thr[:4]}', flush=True)
    mn, md = members_differ(hs, gs)
    print(f'  nests with live members on both games {mn}; their members differ in species (home, host, guest): '
          f'{len(md)} {md[:6]}', flush=True)
    print('  guest wsync:', [l for l in guest.call('coop.wsync')['out'].splitlines() if 'herd_evo' in l], flush=True)
    ck(evolved_host > 0, f'the host\'s game evolved NPC nests after the DNA ({evolved_host})')
    ck(samples >= 3 and bad == 0, f'every guest nest has the host\'s generation and species in every sample ({samples - bad}/{samples}'
       f' samples equal; first difference {first_diff})')
    ck(not thr, f'every evolved guest nest has the host\'s next threshold +0x94 ({len(thr)} differ)')
    ck(not extinct_evolved, f'the nest extinct on the host is not evolved in the guest\'s game ({extinct_evolved[:3]})')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
