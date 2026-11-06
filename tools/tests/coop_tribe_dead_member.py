#!/usr/bin/env python3
"""Tribe: an NPC tribe member killed on the host lies where the host's lies on the guest too (BUG-0488).

coop_tribe_member_place.py --attach over play_tribe (lab 20261106-070213 / its rerun): the detector's
`Tribe:unmatched:citizen` rows were host citizens that stood still for 10-20 s with no member the mod tracks
near them (no t<id> within 40 m): ? the bodies of killed members, which leave their tribe's member list while the
game keeps the body, so the host's snapshot drops them and the guest removes its mirror at once.

1. Two games on the tribe fixture.
2. The host kills one NPC tribe member near the guest (`coop.tribe pincer <victim> <attacker> 1000`).
3. For 25 s: the game's citizens round the victim's place on both games (`coop.census all`, worlddiff rows).
PASS: whenever the host's game has a citizen there, the guest's has one within 15 m too.
SPORE_FIX_OFF=tribe.dead_body: must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_dead_member.py [--all: every member of that tribe]
"""
import math
import sys
import time

import worlddiff
from coop_tribe import members
from twoinst import Checks, start


def citizens_at(con, p, r=20.0):
    w = worlddiff.dump(con, 60, p)
    return [x['pos'] for x in w.rows if x['kind'] == 'g' and worlddiff.noun_name(x['noun']) == 'citizen'
            and not worlddiff._origin(x['pos']) and math.dist(x['pos'], p) <= r]


ALL = '--all' in sys.argv


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    hm = members(host)
    npc = {i: m for i, m in hm.items() if m[3] != 'player'}
    by_tribe = {}
    for i, m in npc.items():
        by_tribe.setdefault(m[3], []).append(i)
    pair = next(((v[0], v[1]) for v in by_tribe.values() if len(v) >= 2), None)
    ck(pair, f'an NPC tribe with two members ({ {k: len(v) for k, v in by_tribe.items()} })')
    if not pair:
        return ck.result()
    victim, attacker = pair
    p = npc[victim][2]
    print(f'  victim #{victim} at {worlddiff._fmt(p)}; host citizens there {len(citizens_at(host, p))}, guest '
          f'{len(citizens_at(guest, p))}', flush=True)
    tribe = npc[victim][3]
    if ALL:  # every member of that tribe: the tribe is beaten (its village raided to the end)
        # the attacker: a member of another tribe (the player's), so every one of the tribe can be hit
        att = next((i for i, m in hm.items() if m[3] != tribe), attacker)
        for v in by_tribe[tribe]:
            print(f'  host: #{v}', host.call(f'coop.tribe pincer {v} {att} 1000')['out'].strip(), flush=True)
    else:
        print('  host:', host.call(f'coop.tribe pincer {victim} {attacker} 1000')['out'].strip(), flush=True)
    t0, bad, host_body = time.time(), [], 0
    while time.time() - t0 < (60 if ALL else 25):
        t = time.time() - t0
        hc, gc = citizens_at(host, p, 6), citizens_at(guest, p, 21)
        lone = [q for q in hc if not any(math.dist(q, g) <= 15 for g in gc)]
        in_list = victim in members(host)
        if hc and not in_list:
            host_body += 1
        print(f'    {t:4.1f} s: host citizens within 6 m {len(hc)} (victim in its tribe: {in_list}), guest within 21 m '
              f'{len(gc)}, host without a partner {len(lone)}', flush=True)
        if lone:
            bad.append(t)
        time.sleep(2)
    print(f'  host samples with a body out of the member list: {host_body}', flush=True)
    ck(not bad, f'every host citizen at the victim\'s place has a partner on the guest ({len(bad)} samples not: {bad[:5]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
