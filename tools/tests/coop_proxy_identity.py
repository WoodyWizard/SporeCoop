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
  2. `coop.npc reusehunt p<id> 150 x`: the game destroys the proxy's creature (DestroyInstance, no bookkeeping of
     ours), then once a frame a creature of that species is made next to the avatar (destroyed again unless it is at
     the freed address) until the game gives it the freed block;
  3. 2 s after the window: where is that new creature? Driven as the proxy, it stands where the host's animal is
     (`want` of the proxy in `coop.npc list`); left alone, it stays where it was made (or the guest's own rules
     removed it).
PASS: no new creature at a freed address is moved onto the host's animal, and no game faulted.
SPORE_FIX_OFF=npc.proxy_identity must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_proxy_identity.py [--fixture creature|tribe] [--tries 4]"""
import argparse
import math
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
            'puppet': h and h.group(5) == '1', 'as_proxy': int(h.group(6)) if h else -1}


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
    for i in ids[:24]:  # (a reuse comes in about one of 3-8 hunts, rarer in Tribe)
        if reused >= a.tries:
            break
        print('  ' + guest.call('coop.npc settle 5')['out'].strip(), flush=True)
        out = guest.call(f'coop.npc reusehunt p{i} 150 x')['out'].strip().splitlines()
        print(f'  proxy #{i}: {out[0] if out else "?"}', flush=True)
        h = hunt(guest)
        t1 = time.time()
        while time.time() - t1 < 5 and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit']:
            print(f'  proxy #{i}: no creature got the freed address in {h["tries"]} tries', flush=True)
            time.sleep(max(0.0, 5.5 - (time.time() - t1)))
            continue
        reused += 1
        made_at = h['pos']
        time.sleep(max(0.0, 5.0 - (time.time() - t1)) + 2.0)  # the window ends, then 2 s of driving
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
