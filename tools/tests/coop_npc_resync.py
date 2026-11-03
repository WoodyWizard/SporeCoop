#!/usr/bin/env python3
"""BUG-0487 (class G5, BUG-0328): a guest that gets states of an NPC it has no proxy of asks the host again.

The host sends an NPC's spawn once per guest and then only its states; a guest without the proxy of an
id (its proxy dropped while the host still counted it as mirrored, a spawn that came while the guest was
elsewhere) dropped those states for good: a host NPC near the guest without a proxy for the rest of the
session (the detector's `Creature:host_only:creature`), its death and health never shown.

1. Two games on the creature fixture; the guest's proxies near it (`coop.census all`, own=proxy).
2. The guest forgets three of them without telling the host (`coop.npc forget <id>`: what a lost spawn
   leaves; the host keeps sending their states).
3. PASS when, 10 s later, the guest's game has a creature again for each of those ids (census row
   own=proxy with that id, alive as on the host, within 6 m of the host's NPC) and the detector finds no
   host NPC near the guest without a proxy.

SPORE_FIX_OFF=npc.resync fails: the forgotten NPCs stay missing on the guest.

  coop_npc_resync.py [--n 3] [--wait 10]"""
import argparse
import math
import time

import worlddiff
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=3)
    ap.add_argument('--wait', type=float, default=10.0)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(15)
    hw, gw = worlddiff.pair_dump(host, guest)
    h = {x['id']: x for x in hw.rows if x['kind'] == 'c' and x['own'] == 'npc'}
    g = {x['id']: x for x in gw.rows if x['kind'] == 'c' and x['own'] == 'proxy'}
    me = gw.me
    ids = sorted((i for i in set(h) & set(g) if me and math.dist(g[i]['pos'], me) < 60 and h[i]['alive']),
                 key=lambda i: math.dist(g[i]['pos'], me))[:a.n]
    ck(len(ids) >= min(a.n, 2), f'proxies near the guest to forget: {ids} (of {len(g)} proxies)')
    took = []
    for i in ids:
        out = guest.call(f'coop.npc forget {i}')['out'].strip()
        print('  guest: ' + out, flush=True)
        took += [i] if out.startswith('forgot proxy') else []
    # (with the fix the spawn is back within about a second: the answer of the forget is the evidence)
    ck(took == ids, f'the guest destroyed the proxies of {took} without telling the host')
    time.sleep(a.wait)
    hw, gw = worlddiff.pair_dump(host, guest)
    h = {x['id']: x for x in hw.rows if x['kind'] == 'c' and x['own'] == 'npc'}
    g = {x['id']: x for x in gw.rows if x['kind'] == 'c' and x['own'] == 'proxy'}
    back, missing = [], []
    for i in ids:
        if i not in h:  # the host's NPC itself went (killed, pooled): nothing to show
            print(f'  npc {i}: gone on the host meanwhile', flush=True)
            continue
        ok = i in g and g[i]['alive'] == h[i]['alive'] and math.dist(g[i]['pos'], h[i]['pos']) < 6.0
        (back if ok else missing).append(i)
        print(f'  npc {i}: host alive={h[i]["alive"]} at {worlddiff._fmt(h[i]["pos"])}; guest '
              + (f'proxy alive={g[i]["alive"]} at {worlddiff._fmt(g[i]["pos"])}' if i in g else 'no creature'), flush=True)
    print('  guest ' + guest.call('coop.npc resync')['out'].strip(), flush=True)
    found = [f for f in worlddiff.diff(hw, gw) if f['sig'] == 'Creature:host_only:creature']
    print('  detector: ' + (found[0]['text'] if found else 'no Creature:host_only:creature'), flush=True)
    ck(back and not missing, f'the guest shows the forgotten NPCs again: back {back}, missing {missing}')
    ck(not any(str(i) in f['text'].split(' key=')[0] for f in found for i in missing),
       'the detector finds none of them without a proxy')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
