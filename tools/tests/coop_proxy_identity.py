#!/usr/bin/env python3
"""BUG-0842: a guest never drives (or destroys) another creature that its game built where a removed animal's copy was.

The game frees a destroyed creature some frames later (when its last reference goes) and gives the block to the
next creature of the same species (lab 2026-11-12: 8-37 creations, within 0.3-1.3 s). A guest's NPC proxy whose
creature the guest's own game destroyed was then only recognised by its address and its mbIsDestroyed byte, which
the new creature has clear: where the mod did not look in that time (the second after an avatar or a world change,
the fork's held death caption; in Tribe the list check never ran), the next frame drove the new creature as the
host's animal, or the stale drop destroyed it.

Two games on the creature fixture (--fixture tribe: the tribe fixture). On the guest, for up to --tries proxies:
  1. `coop.npc settle 5`: its npc tick waits 5 s, as after an avatar change (the window);
  2. `coop.npc reusehunt p<id> 300 xwkb4f40`: the game destroys the proxy's creature (DestroyInstance, no bookkeeping
     of ours); once its heap block is free, creatures of that species are made next to the avatar, 4 a frame, the
     misses kept until the round ends, until the game gives one the freed block (BUG-1099: proxies whose heap
     neighbours are in use first, a proxy made again is hunted again, up to 60 rounds in 170 s; SPORE_REUSE_FLAGS=x:
     the hunt before, a creature a frame from the destroy on, each miss destroyed at once);
  3. 2 s after the window: where is that new creature? Driven as the proxy, it stands where the host's animal is
     (`want` of the proxy in `coop.npc list`); left alone, it stays where it was made (or the guest's own rules
     removed it).
PASS: no new creature at a freed address is moved onto the host's animal, and no game faulted.
SPORE_FIX_OFF=npc.proxy_identity must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_proxy_identity.py [--fixture creature|tribe] [--tries 4]"""
import argparse
import math
import os
import re
import time

from twoinst import Checks, start


def plist(con):
    out = {}
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ (spawned|pending) .*?want=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*?ptr=(\S+)', line)
        if m:
            out[int(m.group(1))] = (m.group(2), tuple(float(x) for x in m.group(3, 4, 5)), m.group(6))
    return out


def hunt(con):
    out = con.call('coop.npc reusehunt')['out']
    m = re.search(r'left=(-?\d+) tries=(\d+) .*?hit=([0-9A-Fa-f]+)', out)
    h = re.search(r'hit alive=(\d) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) puppet=(\d) as_proxy=(-?\d+)', out)
    return {'left': int(m.group(1)) if m else 0, 'tries': int(m.group(2)) if m else 0,
            'hit': int(m.group(3), 16) if m else 0,
            'alive': h and h.group(1) == '1', 'pos': tuple(float(x) for x in h.group(2, 3, 4)) if h else None,
            'puppet': h and h.group(5) == '1', 'as_proxy': int(h.group(6)) if h else -1,
            'chunk': next((ln.strip() for ln in out.splitlines() if ln.startswith('chunk ')), '')}



def chunk(con, i):
    """BUG-1099: (prev_used, used, next_used) of proxy #i's heap chunk (`coop.npc chunk p<id>`), None unknown"""
    m = re.search(r'prev_used=(-?\d) used=(-?\d) next_used=(-?\d)', con.call(f'coop.npc chunk p{i}')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def next_proxy(con, ids, done):
    """BUG-1099: the next proxy to hunt: one whose heap neighbours are both in use (its freed block cannot merge
    with a free one, which no creation then starts at), else the next one in order; None when all are done"""
    left = [i for i in ids if i not in done]
    for i in left:
        if chunk(con, i) == (1, 1, 1):
            return i
    return left[0] if left else None


# BUG-1099: the hunt waits for the game to free the block (w), then makes creatures 4 a frame, the misses kept alive
# (k) until 40 tries after the free (f40); SPORE_REUSE_FLAGS=x is the hunt before (a creature a frame from the destroy
# on, each miss destroyed at once)
SETTLE = float(os.environ.get('SPORE_REUSE_SETTLE', '5'))
FRAMES = int(os.environ.get('SPORE_REUSE_FRAMES', '300'))
FLAGS = os.environ.get('SPORE_REUSE_FLAGS', 'xwkb4f40')
ROUNDS = int(os.environ.get('SPORE_REUSE_ROUNDS', '60'))
HUNT_S = float(os.environ.get('SPORE_REUSE_HUNT_S', '170'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature', choices=('creature', 'tribe'))
    ap.add_argument('--tries', type=int, default=2)
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40 and sum(1 for v in plist(guest).values() if v[0] == 'spawned') < 3:
        time.sleep(1)
    ids = [i for i, v in sorted(plist(guest).items()) if v[0] == 'spawned']
    ck(len(ids) >= 2, f'the guest has spawned proxies of the host\'s animals ({len(ids)})')
    reused, driven = 0, []
    done, t_hunt = [], time.time()
    # BUG-1099: rounds until --tries reuses, up to ROUNDS proxies (also ones made again since) within HUNT_S seconds; the
    # proxies whose heap neighbours are in use first (next_proxy); a round ends at once when the block is not freed
    while reused < a.tries and len(done) < ROUNDS and time.time() - t_hunt < HUNT_S:
        pl = plist(guest)  # (a proxy the npc tick made again after a round is hunted again: a new creature)
        live = [j for j, v in sorted(pl.items()) if v[0] == 'spawned' and (j, v[2]) not in done]
        i = next_proxy(guest, live, ()) if 'w' in FLAGS else next(iter(live), None)
        if i is None:
            time.sleep(1)
            continue
        done.append((i, pl[i][2]))
        print('  ' + guest.call(f'coop.npc settle {SETTLE}')['out'].strip(), flush=True)
        out = guest.call(f'coop.npc reusehunt p{i} {FRAMES} {FLAGS}')['out'].strip().splitlines()
        print(f'  proxy #{i}: {out[0] if out else "?"}', flush=True)
        h = hunt(guest)
        t1 = time.time()
        while time.time() - t1 < SETTLE and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit']:
            print(f'  proxy #{i}: no creature got the freed address in {h["tries"]} tries ({h["chunk"]})', flush=True)
            guest.call('coop.npc settle 0')  # (the npc tick takes the destroyed proxy in, then the next round)
            time.sleep(1.5)
            continue
        reused += 1
        made_at = h['pos']
        time.sleep(max(0.0, SETTLE - (time.time() - t1)) + 2.0)  # the window ends, then 2 s of driving
        h = hunt(guest)
        p = plist(guest).get(i)
        want = p[1] if p else None
        d_want = math.dist(h['pos'], want) if h['alive'] and h['pos'] and want else -1
        moved = math.dist(h['pos'], made_at) if h['alive'] and h['pos'] and made_at else -1
        print(f'  proxy #{i}: new creature {h["hit"]:08X} alive={int(bool(h["alive"]))} made at {made_at}, now {h["pos"]} '
              f'(moved {moved:.1f} m, {d_want:.1f} m from the host animal {want}); the proxy names {p and p[2]}, '
              f'puppet={int(bool(h["puppet"]))} as_proxy={h["as_proxy"]}', flush=True)
        if h['alive'] and 0 <= d_want < 1.5:
            driven.append(i)
    ck(reused >= 1, f'the game gave a freed proxy\'s address to a new creature ({reused} times)')
    ck(not driven, f'no new creature at a freed proxy\'s address was driven as the host\'s animal (driven: {driven})')
    print('  guest ' + guest.call('coop.live')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
