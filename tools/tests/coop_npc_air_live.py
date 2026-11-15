#!/usr/bin/env python3
"""BUG-0995 (Creature, guest, class BUG-0426): a proxy's hold in the air never lifts another creature the game built
where the proxy was.

game.cpp keeps the creatures held in the air (a host NPC off its ground: its proxy, BUG-0550) and after every update
and animation start of a creature puts a held one back at the held spot when the game grounded it. The holds were
matched by address only: a held proxy the guest's game removed and freed while the npc tick did not look (the settle
second after an avatar change) gave its block to the next creature the game built, and that animal of the guest's
own world was put in the air at the held spot.

Two games on the creature fixture. Per proxy until --tries hits: the host lifts its NPC 4 m (`coop.npc lift`, its
proxy is held in the air on the guest: `coop.track air` lists the hold); on the guest `coop.npc settle 8` and
`coop.npc reusehunt p<id> 150 x` (the game destroys the proxy's creature, then creatures of its species are made
6 m from the avatar until one takes the freed block); on a hit, the new creature's position for 3 s (the game's).
PASS: the new creature never stands at the held spot (over 1 m from it) and no game faulted.
SPORE_FIX_OFF=game.air_live must FAIL (the new creature is put at the held spot).

  SPORE_LANE=7 python3 tools/tests/coop_npc_air_live.py [--tries 1]"""
import argparse
import math
import re
import time

from coop_proxy_identity import hunt, plist
from twoinst import Checks, start


def holds(con):
    """{ptr: pos} of the guest's air holds"""
    return {int(m.group(1), 16): tuple(float(x) for x in m.group(2, 3, 4))
            for m in re.finditer(r'hold (\S+) serial=\d+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.track air')['out'])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=1)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40 and sum(1 for v in plist(guest).values() if v[0] == 'spawned') < 3:
        time.sleep(1)
    ids = [i for i, v in sorted(plist(guest).items()) if v[0] == 'spawned']
    ck(len(ids) >= 2, f'the guest has spawned proxies of the host\'s animals ({len(ids)})')
    reused, lifted, placed = 0, 0, []
    for i in ids[:24]:
        if reused >= a.tries:
            break
        print('  host: ' + host.call(f'coop.npc lift {i} 4 25')['out'].strip(), flush=True)
        ptr, held = 0, None
        t1 = time.time()
        while time.time() - t1 < 6 and held is None:
            time.sleep(0.5)
            p = plist(guest).get(i)
            ptr = int(p[2], 16) if p and re.fullmatch(r'[0-9A-Fa-f]+', p[2]) else 0
            held = holds(guest).get(ptr)
        if held is None:
            print(f'  proxy #{i}: not held in the air on the guest', flush=True)
            continue
        lifted += 1
        print('  ' + guest.call('coop.npc settle 8')['out'].strip(), flush=True)
        held = holds(guest).get(ptr, held)
        out = guest.call(f'coop.npc reusehunt p{i} 150 x')['out'].strip().splitlines()
        print(f'  proxy #{i} {ptr:08X} held at {held}: {out[0] if out else "?"}', flush=True)
        h = hunt(guest)
        t2 = time.time()
        while time.time() - t2 < 5 and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit'] or not h['alive']:
            print(f'  proxy #{i}: no live creature at the freed address ({h["tries"]} tries)', flush=True)
            time.sleep(max(0.0, 8.5 - (time.time() - t2)))
            continue
        reused += 1
        dmin = 1e9
        for _ in range(12):
            h = hunt(guest)
            if h['alive'] and h['pos']:
                dmin = min(dmin, math.dist(h['pos'], held))
            time.sleep(0.25)
        print(f'  proxy #{i}: new creature {h["hit"]:08X} at {h["pos"]}, nearest to the held spot {dmin:.2f} m; '
              f'{guest.call("coop.track air")["out"].strip().splitlines()[1] if len(guest.call("coop.track air")["out"].strip().splitlines()) > 1 else ""}',
              flush=True)
        if dmin < 1.0:
            placed.append(f'#{i} ({dmin:.2f} m)')
        time.sleep(max(0.0, 8.5 - (time.time() - t2)))
    ck(reused >= 1, f'a new creature at a held proxy\'s freed address ({lifted} held, {reused} reuses)')
    ck(not placed, f'no new creature was put at a held spot ({placed})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
