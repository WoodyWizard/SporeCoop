#!/usr/bin/env python3
"""A guest far from the host (450 m) keeps the NPCs around it (BUG-0280).

The game simulates its world only around its own camera (SporeCoop::Relevance_Update: herds and
spawn points within 100 m are enabled, creatures farther than 250 m go back to the pool). Without
the fix the host's game pooled every NPC around a guest that stood 300 m from the host, the guest
had none left, and the nest spawned a missing member that was pooled again 1-3 s later: on the
guest a family member appeared and vanished every 3-6 s, each time with a new net id.

The guest stays where both start (at the nest), the host walks 450 m away on land (BUG-0377: the
walk of 0.7.3.3 went into the sea, see coop_npc_pooled.SEA) and waits 40 s. Checked
on the guest's game (its census: proxies are creatures in its world) and on the host's (NPCs in its
world, not at the planet's centre, near the guest's puppet):
  * the guest still has the NPCs within 120 m of its avatar (at least 85 % of the count it had
    before, at least 2), in most samples of the hold (without the fix: 12 of 20 left, lane 9);
  * few new NPC ids appear around the guest during the hold (no spawn-and-pool cycle);
  * the host's game has NPCs in its world near the guest.

  coop_npc_far.py   (SPORE_FIX_OFF=npc.view_centres must FAIL)"""
import math
import time

import census
from coop_npc_pooled import drawn_apart, land_path, walk_to
from twoinst import Checks, start

NEAR = 120.0


def near(rows, centre, own):
    return {r['id']: r for r in rows
            if r['kind'] == 'c' and r['own'] in own and r['id'] >= 0 and r['alive'] > 0
            and math.dist(r['pos'], (0, 0, 0)) > 50 and math.dist(r['pos'], centre) < NEAR}


def main():
    host, guest = start()
    ck = Checks()
    hc = census.dump(host)
    av = next(r for r in hc.rows if r['own'] == 'avatar')
    a = list(av['pos'])
    b, dry = land_path(host, a, 450)  # BUG-0377: on land, not into the sea
    print(f'  the walk stays {dry:.1f} m or more over the water', flush=True)
    guest.call('coop.abilities goto %.1f %.1f %.1f' % tuple(a))
    time.sleep(8)

    def guest_centre(g):
        me = next((r for r in g.rows if r['own'] == 'avatar'), None)
        return me['pos'] if me else None

    base = []
    for _ in range(8):
        g = census.dump(guest)
        c = guest_centre(g)
        if c:
            base.append(len(near(g.rows, c, ('proxy',))))
        time.sleep(0.5)
    n0 = sorted(base)[len(base) // 2] if base else 0
    print(f'  before: the guest has {n0} NPCs within {NEAR:.0f} m (samples {base})', flush=True)

    for k in range(21):  # the host walks away in 20 steps
        walk_to(host, a, b, k / 20)
        time.sleep(1.2)
    time.sleep(8)  # the relevance pass runs every 5 s
    seen, counts, host_counts, samples = set(), [], [], 0
    first = None
    t0 = time.time()
    while time.time() - t0 < 40:
        g, h = census.dump(guest), census.dump(host)
        c = guest_centre(g)
        if not (g.complete and h.complete) or c is None or g.world != h.world:
            time.sleep(0.5)
            continue
        samples += 1
        ids = set(near(g.rows, c, ('proxy',)))
        if first is None:
            first = set(ids)
        seen |= ids
        counts.append(len(ids))
        host_counts.append(len(near(h.rows, c, ('npc',))))
        time.sleep(0.8)
    hp = next((r['pos'] for r in census.dump(host).rows if r['own'] == 'avatar'), a)
    print(f'  host {math.dist(hp, a):.0f} m from the guest; {samples} samples', flush=True)
    print(f'  guest NPCs near it: {counts}', flush=True)
    print(f'  host NPCs near the guest: {host_counts}', flush=True)
    new_ids = len(seen - (first or set()))
    print(f'  new NPC ids near the guest during the hold: {new_ids}', flush=True)
    print('  host ' + host.call('coop.views')['out'].strip().replace('\n', '\n  '), flush=True)
    apart = drawn_apart(host, guest)
    print(f'  the guest draws the host {apart if apart is not None else -1:.1f} m from where the host is', flush=True)
    need = max(2, math.ceil(0.85 * n0))  # without the fix the host's game pooled 8 of 20 (lane 9)
    good = sum(1 for n in counts if n >= need)
    ck(math.dist(hp, a) > 350, 'the host stands far from the guest')
    ck(dry > 0, f'the host walks on land ({dry:.1f} m over the water at the lowest)')
    ck(apart is not None and apart < 3.0, 'the guest draws the host\'s creature where the host is (BUG-0377)')
    ck(samples >= 20, f'censuses of both games compared ({samples})')
    ck(n0 >= 2, f'the guest had NPCs around it before ({n0})')
    ck(good >= 0.8 * max(1, samples), f'the guest keeps NPCs around it ({good}/{samples} samples with >= {need})')
    ck(sorted(host_counts)[len(host_counts) // 2] >= need if host_counts else False,
       'the host\'s game simulates NPCs next to the guest')
    ck(new_ids <= 3, f'no nest member appears and vanishes again and again ({new_ids} new ids in 40 s)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
