#!/usr/bin/env python3
"""Civilization: a host that played alone keeps the cities it really had, and a joiner gets a city of its own
(BUG-1418, audit CIV-6).

A host is connected() alone, so its Civ tick dealt every city to itself and the owner claims (E1, BUG-1153) stamped
the host's key on each of them 2 s later: when a guest joined, every non-capital city was claimed by the host and the
joiner got only the shared capital. Owner's rule (2026-11-21): the host keeps what it really had (a claim recorded in
the store, a capture, a gift or a purchase: those claim at the event), but a deal made while it plays alone is no
claim, so the unclaimed cities are dealt at the join.

1. Two games on civ_npc. The guest leaves; the host, alone, buys the two NPC cities nearest its capital
   (`coop.civ buycity`, a purchase: claimed by the host at the event).
2. Test setup: city A as a Civ save played without co-op leaves it: no claim, no capture mark
   (`coop.civ claims forget <A>`); city B keeps its purchase claim. The host plays alone 7 s (3 claim refreshes).
3. The guest joins again.
4. PASS: on both games city A is the guest's (the host's owner table and the guest's view of it, the same player), city
   B is still the host's.
SPORE_FIX_OFF=civ.solo_claims must FAIL (city A was claimed by the host's solo deal: the guest gets no city).

  SPORE_LANE=3 python3 tools/tests/coop_civ_solo_deal.py
"""
import time

import lane
from coop_civ import civ, dist
from coop_civ_owners_reload import by_pos
from twoinst import Checks, start, wait_guest_in

NOT_A_PLAYER = (-1, 0, 253, 254, 255)  # nobody, the host, the shared capital, an NPC nation, no owner


def wait(pred, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return True
        time.sleep(1)
    return pred()


def index_at(con, pos):
    cs, _ = civ(con)
    near = [(dist(c['pos'], pos), i) for i, c in cs.items() if dist(c['pos'], pos) < 10.0]
    return min(near)[1] if near else None


def line(con, head):
    return next((ln for ln in con.call('coop.civ tables')['out'].splitlines() if ln.startswith(head)), '-')


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and len(npc) >= 2, f'a player city {cap} and two NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    pa, pb = hc[npc[0]]['pos'], hc[npc[1]]['pos']
    print('  guest:', guest.call('coop.leave')['out'].strip()[:80], flush=True)
    time.sleep(4)
    for pos, name in ((pa, 'A'), (pb, 'B')):
        ci = index_at(host, pos)
        print(f'  host alone buys city {name} ({ci}):', host.call(f'coop.civ buycity {ci} 500')['out'].strip(), flush=True)
        got = wait(lambda: (c := by_pos(civ(host)[0], pos)) is not None and c['player'] and c['owner'] == 0, 20)
        ck(got, f'city {name} is the player civilization\'s and the host\'s')
    ia = index_at(host, pa)
    print('  host:', host.call(f'coop.civ claims forget {ia}')['out'].strip(), flush=True)
    time.sleep(7)
    print('  host alone:', line(host, 'claims:'), '|', line(host, 'solo_claims:'), flush=True)
    guest.call(lane.JOIN)
    ok = wait_guest_in(host, guest, 180)
    ck(ok, 'the guest joined the host\'s civilization again')
    if not ok:
        ck.faults(host=host, guest=guest)
        return ck.result()
    got = wait(lambda: (h := by_pos(civ(host)[0], pa)) is not None and h['owner'] not in NOT_A_PLAYER and
               (g := by_pos(civ(guest)[0], pa)) is not None and g['owner'] == h['owner'], 25)
    hc, _ = civ(host)
    gc, _ = civ(guest)
    print('  host:', line(host, 'claims:'), '|', line(host, 'solo_claims:'), flush=True)
    for name, pos in (('A', pa), ('B', pb)):
        h, g = by_pos(hc, pos), by_pos(gc, pos)
        print(f'  city {name}: host {h and (h["player"], h["owner"])}, guest {g and (g["player"], g["owner"])}', flush=True)
    h, g = by_pos(hc, pa), by_pos(gc, pa)
    ck(got, f'city A (no claim, dealt while the host was alone) is the joiner\'s on both games '
            f'(host {h and h["owner"]}, guest {g and g["owner"]})')
    h, g = by_pos(hc, pb), by_pos(gc, pb)
    ck(h and g and h['owner'] == 0 and g['owner'] == 0,
       f'city B (bought by the host alone) is still the host\'s on both games (host {h and h["owner"]}, guest {g and g["owner"]})')
    caps = [c for c in hc.values() if c['player'] and c['owner'] == 253]
    print(f'  shared capital: {len(caps)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
