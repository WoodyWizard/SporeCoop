#!/usr/bin/env python3
"""Creature: a walking host animal thrown aside jumps on the guest's screen too, at once (BUG-0997).

A guest's copy (proxy) of a walking host NPC is walked by its game and placed on the host's path (drive::walk_place).
When the host's animal jumps aside (thrown by a hit, put back by its game) the copy is teleported when it is over
2.5 m off and the host's path itself jumped that frame (the snap, BUG-0361). Since npc.place_now (BUG-0757) the copy
is aimed a frame ahead and the snap compared that aim with the path point: it never held for a moving copy, which
slid back to the host's animal at the catch-up speed (seconds) instead.

1. Two games on the creature fixture (both 30 fps). Up to 3 host NPCs the guest mirrors, outside the host's party,
   walk around the guest's avatar (`coop.npctrack walk`, their own locomotion).
2. The guest records its proxies every frame (`coop.npctrack start`); while each walks, the host puts it 5 m to the side
   and lets it walk on (`coop.npctrack shove`), three times per animal.
3. On the guest's frames: for each shove (the host's path of that animal jumps over 3 m in one frame) the time until
   the copy as drawn by the guest's game is within 1 m of the host's path again. PASS: every shove closed within
   0.3 s (median) and none slower than 0.6 s.
SPORE_FIX_OFF=npc.place_now_snap: the slide at the catch-up speed (FAIL).

  SPORE_LANE=6 SPORE_FPS=30 python3 tools/tests/coop_npc_snap.py
"""
import math
import random
import statistics
import time

from coop_npc_walk import avatar_pos, dump, host_npcs, host_party, proxies, tangent_point
from twoinst import Checks, start

SHOVE = 5.0


def closes(rows, ids):
    """[(id, t of the jump, seconds until the drawn copy is within 1 m of the path)]"""
    out = []
    for i in ids:
        r = [x for x in rows if x['id'] == i]
        for k in range(1, len(r)):
            if math.dist(r[k]['tgt'], r[k - 1]['tgt']) < 3.0 or r[k]['t'] - r[k - 1]['t'] > 0.2:
                continue
            t0, took = r[k]['t'], None
            for x in r[k + 1:]:
                if x['t'] - t0 > 3.0:
                    break
                if math.dist(x['d'], x['tgt']) < 1.0:
                    took = x['t'] - t0
                    break
            out.append((i, round(t0, 2), took if took is not None else 9.9))
    return out


def main():
    rng = random.Random(7)
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    hs, gs, party = host_npcs(host), proxies(guest), host_party(host)
    g = avatar_pos(guest)
    both = sorted((math.dist(gs[i][0], g), i) for i in hs if i in gs and g and i not in party and math.dist(gs[i][0], g) < 100)
    ids = [i for _, i in both[:3]]
    print(f'  walkers {ids} (host NPCs {len(hs)}, proxies {len(gs)}, party {sorted(party)})', flush=True)
    if not ck(ids, 'host NPCs the guest mirrors, outside the host\'s party'):
        return ck.result()
    print(' ', guest.call('coop.npctrack start 120')['out'].strip(), flush=True)
    shoves = 0
    for rnd in range(3):
        for i in ids:
            g = avatar_pos(guest) or g
            p = tangent_point(g, rng, 6, 12)
            host.call(f'coop.npctrack walk {i} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} 3')
        time.sleep(2.0)  # walking
        for i in ids:
            p = tangent_point(g, rng, 6, 12)
            out = host.call(f'coop.npctrack shove {i} {SHOVE} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} 3')['out'].strip()
            shoves += 'put from' in out
            if rnd == 0:
                print('  host:', out, flush=True)
            time.sleep(0.3)
        time.sleep(3.0)  # closed (or not) before the next round
    guest.call('coop.npctrack stop')
    rows = dump(guest)
    got = closes(rows, ids)
    print(f'  {len(rows)} guest frames, {shoves} shoves; closes (id, t, s): {got}', flush=True)
    ck(len(got) >= max(3, shoves // 2), f'the guest saw the host\'s animals jump ({len(got)} of {shoves} shoves)')
    took = [x[2] for x in got]
    med = statistics.median(took) if took else 9.9
    ck(took and med <= 0.3 and max(took) <= 0.6,
       f'the copy on the guest\'s screen jumps with the host\'s animal (median {med:.2f} s, slowest {max(took or [9.9]):.2f} s)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
