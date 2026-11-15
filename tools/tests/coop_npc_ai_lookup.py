#!/usr/bin/env python3
"""BUG-0994 (Creature, guest, class BUG-0426): a guest's social click never names a copy of an animal its game has
just removed.

social.cpp's h_Post (the avatar posts the social stimulus to the NPC it clicks, inside the game's simulation) asks
npc::proxy_id_of_ai which proxy owns that AI block. It read +0xB4C of every creature key of the proxy table, also of
one the guest's game had removed and freed while the npc tick did not look (the settle second after an avatar change):
another creature the game built at that address matched, and the guest sent K_ENGAGE for the gone proxy (the host
started a social game with its NPC).

Two games on the creature fixture. On the guest, per proxy until --tries hits: `coop.npc settle 6` (the npc tick waits
as after an avatar change), `coop.npc reusehunt p<id> 300 xwkb4f40` (the game destroys the proxy's creature; once its
heap block is free, creatures of its species are made, 4 a frame, the misses kept, until one takes it: BUG-1099,
docs/re/creature.md#creature-reuse-merged); proxies whose heap neighbours are in use first, a proxy made again is
hunted again, up to 60 rounds in 170 s (SPORE_REUSE_FLAGS=x: the hunt before; SPORE_REUSE_DIAG=1: chunk lines,
SPORE_REUSE_STALE=1 also `coop.staleref`); on a hit, `coop.social post <hit>` (the avatar's social
stimulus to the new creature through the game's BT_PostStimulus, the hooked call). PASS: the guest engaged no gone
proxy (`engaged ... -> <id>`, K_ENGAGE sent for it) and no game faulted. SPORE_FIX_OFF=npc.ai_lookup_live must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_npc_ai_lookup.py [--tries 1]"""
import argparse
import math
import os
import re
import time

from coop_proxy_identity import hunt, next_proxy, plist
from twoinst import Checks, start


# BUG-1099: the hunt waits for the game to free the block (w), then makes creatures 4 a frame, the misses kept alive
# (k) until 40 tries after the free (f40); SPORE_REUSE_FLAGS=x is the hunt before (a creature a frame from the destroy
# on, each miss destroyed at once: 0 reuses in 21 rounds on a well-used guest, final check 20261114-005006)
SETTLE = float(os.environ.get('SPORE_REUSE_SETTLE', '6'))
FRAMES = int(os.environ.get('SPORE_REUSE_FRAMES', '300'))
FLAGS = os.environ.get('SPORE_REUSE_FLAGS', 'xwkb4f40')
ROUNDS = int(os.environ.get('SPORE_REUSE_ROUNDS', '60'))
HUNT_S = float(os.environ.get('SPORE_REUSE_HUNT_S', '170'))
DIAG = os.environ.get('SPORE_REUSE_DIAG') == '1'
STALE = os.environ.get('SPORE_REUSE_STALE') == '1'


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
    if DIAG:
        guest.call('coop.staleref on')
    reused, posted, named = 0, 0, []
    done, t_hunt = [], time.time()
    # BUG-1099: rounds until a reuse, up to ROUNDS proxies (also ones made again since) within HUNT_S seconds; the proxies
    # whose heap neighbours are in use first (next_proxy); a round ends at once when the block is not freed
    while reused < a.tries and len(done) < ROUNDS and time.time() - t_hunt < HUNT_S:
        pl = plist(guest)  # (a proxy the npc tick made again after a round is hunted again: a new creature)
        live = [j for j, v in sorted(pl.items()) if v[0] == 'spawned' and (j, v[2]) not in done]
        i = next_proxy(guest, live, ()) if 'w' in FLAGS else next(iter(live), None)
        if i is None:
            time.sleep(1)
            continue
        done.append((i, pl[i][2]))
        if DIAG:
            mc = re.search(r'centre=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.npctrack count')['out'])
            w = plist(guest).get(i)
            dist = math.dist(tuple(map(float, mc.groups())), w[1]) if mc and w else -1
            print(f'  {guest.call(f"coop.npc chunk p{i}")["out"].strip()} dist={dist:.0f} t={time.time() - t_hunt:.0f}',
                  flush=True)
        print('  ' + guest.call(f'coop.npc settle {SETTLE}')['out'].strip(), flush=True)
        out = guest.call(f'coop.npc reusehunt p{i} {FRAMES} {FLAGS}')['out'].strip().splitlines()
        print(f'  proxy #{i}: {out[0] if out else "?"}', flush=True)
        h = hunt(guest)
        t1 = time.time()
        while time.time() - t1 < SETTLE - 1 and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h['hit'] or not h['alive']:
            print(f'  proxy #{i}: no live creature at the freed address ({h["tries"]} tries; {h["chunk"]})', flush=True)
            if DIAG and STALE and h['chunk'] and 'freed_at=-1' in h['chunk']:  # RE (BUG-1099): who still holds the block
                m = re.search(r'reuse hunt for ([0-9A-Fa-f]+)', ' '.join(out))
                if m:
                    for ln in guest.call(f'coop.staleref {m.group(1)}')['out'].strip().splitlines()[:8]:
                        print('    staleref: ' + ln.strip()[:200], flush=True)
            if DIAG:  # (BUG-1099 RE: freed later?)
                time.sleep(3)
                print('    3 s later: ' + hunt(guest)['chunk'], flush=True)
            guest.call('coop.npc settle 0')  # (the npc tick takes the destroyed proxy in, then the next round)
            time.sleep(1.5)
            continue
        reused += 1
        out = guest.call(f'coop.social post {h["hit"]:08X}')['out'].strip()
        print(f'  proxy #{i}: new creature {h["hit"]:08X} as_proxy={h["as_proxy"]}: {out}', flush=True)
        m = re.search(r'engaged (-?\d+) -> (-?\d+), engages \+(\d+)', out)
        if m:
            posted += 1
            if int(m.group(2)) == i:
                named.append(f'#{i} (engages +{m.group(3)})')
        time.sleep(1.5)
        print('  host: ' + ' | '.join(host.call('coop.social games')['out'].strip().splitlines()[:3]), flush=True)
        time.sleep(max(0.0, SETTLE + 0.5 - (time.time() - t1)))
    ck(reused >= 1 and posted >= 1, f'a social click on a new creature at a freed proxy\'s address ({reused} reuses, {posted} posted)')
    ck(not named, f'the guest engaged no proxy whose creature its game had removed ({named})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
