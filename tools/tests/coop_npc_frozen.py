#!/usr/bin/env python3
"""Creature stage: the host's game stands still (its death caption, the brain upgrade caption) and the guest
keeps seeing the host's NPCs around it in its own game (BUG-0413, class BUG-0421).

--case death    the guest walks 150-260 m off to another nest (`coop.npc gopos`: its game drops the host's NPCs at the host), the host dies
                (`coop.team die`) and leaves its death caption open (its simulation frozen: no creature is
                updated, `frozen_s` of coop.npc), then the guest comes back next to the host (as a guest that
                respawns at the nest while the host lies dead). Within 8 s the guest's game must have a proxy
                of every living host NPC within 50 m of it (census on both games).
--case caption  the host gains 3000 DNA (`coop.dna add 3000`, the campaign's end of the stage): both games
                show the brain upgrade caption (input_lock). Nobody closes it for 14 s; every 2 s the guest's
                game must show every living host NPC within 50 m of the guest (census on both games).

Both: the oracle is the census of both games (the host's NPC objects and the guest's proxy objects, by net
id, within 15 m of each other), not a mod counter.

  SPORE_LANE=N python3 tools/tests/coop_npc_frozen.py --case death|caption
  SPORE_FIX_OFF=npc.frozen_send must FAIL (the host sends nothing while frozen: 0.7.8).
"""
import argparse
import math
import re
import time

import census
from twoinst import Checks, start

NEAR = 50.0


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def missing_near(host, guest, near=NEAR, pos_m=15.0):
    """living host NPCs within `near` m of the guest's avatar that the guest's game lacks or has far off"""
    hc, gc = census.dump(host), census.dump(guest)
    me = next((r['pos'] for r in gc.rows if r['own'] == 'avatar'), None)
    if me is None:
        return None, 0, ['the guest has no avatar']
    h, g = hc.by_id('c', ('npc',)), gc.by_id('c', ('proxy',))
    bad, n = [], 0
    for i, r in sorted(h.items()):
        if not r['alive'] or math.dist(r['pos'], me) > near:
            continue
        n += 1
        if i not in g:
            bad.append(f'n{i} ({math.dist(r["pos"], me):.0f} m from the guest) not on the guest')
        elif math.dist(g[i]['pos'], r['pos']) > pos_m:
            bad.append(f'n{i} {math.dist(g[i]["pos"], r["pos"]):.0f} m off on the guest')
    return me, n, bad


def frozen_s(host):
    """s the host's game has updated no creature (its simulation stands still: a caption, the pause menu)"""
    m = re.search(r'frozen_s=([\d.]+)', host.call('coop.npc')['out'])
    return float(m.group(1)) if m else 0.0


def frozen(host):
    return frozen_s(host) > 0


NEST = re.compile(r'pos=\(([-\d.e+]+) ([-\d.e+]+) ([-\d.e+]+)\) noun=52AA6122')


def far_nest(con, p, lo=150.0, hi=260.0):
    """the position of a nest (noun 0x52AA6122 in `coop.census all`) lo..hi m from p: on land, a walk away"""
    best = None
    for m in NEST.finditer(con.call('coop.census all 3000')['out']):
        q = tuple(float(m.group(i)) for i in (1, 2, 3))
        d = math.dist(q, p)
        if lo < d < hi and (best is None or d < best[1]):
            best = (q, d)
    return best


def case_death(host, guest, ck):
    hp_ = own_pos(host)
    ck(hp_ is not None, 'the host has an avatar')
    if hp_ is None:
        return
    # the guest walks off first: 150-260 m from the host its game drops the host's NPCs there (the host
    # despawns them beyond 130 m)
    nest = far_nest(guest, hp_)
    ck(nest, f'a nest of the game 150-260 m from the host ({nest})')
    if not nest:
        return
    q = nest[0]
    print(f'  the guest goes to a nest {nest[1]:.0f} m from the host: '
          + guest.call(f'coop.npc gopos {q[0] + 6:.2f} {q[1]:.2f} {q[2]:.2f}')['out'].strip(), flush=True)
    time.sleep(8)
    me = own_pos(guest)
    print(f'  the guest is {math.dist(me, hp_):.0f} m from the host', flush=True)
    hc = census.dump(host)
    near_host = [r for r in hc.by_id('c', ('npc',)).values() if r['alive'] and math.dist(r['pos'], hp_) < NEAR]
    gids = set(census.dump(guest).by_id('c', ('proxy',)))
    unseen = [r for r in near_host if r['id'] not in gids]
    print(f'  host NPCs within {NEAR:g} m of the host: {len(near_host)}, not on the guest: {len(unseen)}', flush=True)
    ck(unseen, f'the guest\'s game does not show the host\'s NPCs at the host now ({len(unseen)} of {len(near_host)})')
    ck('lethal' in host.call('coop.team die')['out'], 'the host takes a lethal hit')
    t0 = time.time()
    while time.time() - t0 < 40 and frozen_s(host) < 2:
        time.sleep(1)
    ck(frozen(host), f'the host\'s game stands still (its death caption: no creature updated, {time.time() - t0:.0f} s)')
    # back next to the host (its body), as a guest that respawns at the nest while the host lies dead
    guest.call(f'coop.npc gopos {hp_[0] + 4:.2f} {hp_[1]:.2f} {hp_[2]:.2f}')
    worst = None
    t0 = time.time()
    while time.time() - t0 < 8:
        time.sleep(2)
        me2, n, bad = missing_near(host, guest)
        print(f'  {time.time() - t0:3.0f} s: host frozen {frozen_s(host):.0f} s, {n} host NPCs within {NEAR:g} m of the '
              f'guest, missing {len(bad)}: {bad[:4]}', flush=True)
        worst = bad
    ck(frozen(host), 'the host\'s game stood still for the whole check')
    ck(worst == [], f'8 s after the guest came back, every living host NPC near it is in its game ({worst})')
    host.call('key enter')  # the caption's OK


def case_caption(host, guest, ck):
    _, n0, bad0 = missing_near(host, guest)
    print(f'  before: {n0} host NPCs within {NEAR:g} m of the guest, missing {bad0}', flush=True)
    ck(n0 > 0, f'the host has NPCs near the guest before the caption ({n0})')
    host.call('coop.dna add 3000')
    t0 = time.time()
    while time.time() - t0 < 40 and frozen_s(host) < 1:
        time.sleep(0.5)
    print(f'  the caption after {time.time() - t0:.0f} s', flush=True)
    ck(frozen(host), 'the host\'s game stands still under the brain upgrade caption')
    t0 = time.time()
    fails = []
    while time.time() - t0 < 14:
        time.sleep(2)
        _, n, bad = missing_near(host, guest)
        lock = frozen(host)
        print(f'  {time.time() - t0:3.0f} s: host frozen={lock}, {n} host NPCs near the '
              f'guest, missing {len(bad)}: {bad[:4]}', flush=True)
        if bad and lock:
            fails.append(bad)
    ck(not fails, f'under the caption the guest keeps every host NPC near it ({len(fails)} reads with some missing)')
    for con in (host, guest):
        con.call('key enter')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('death', 'caption'), required=True)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    time.sleep(4)
    (case_death if a.case == 'death' else case_caption)(host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
