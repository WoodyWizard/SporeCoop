#!/usr/bin/env python3
"""Tribe: a guest that leaves and joins again gets all the host's NPCs again (BUG-0328).

The campaign's guest Dave left and joined again in Tribe and then lacked 6 host NPCs (coverage 75%)
for the last ~50 s of the play. Suspected (not shown): the host counted it present for the 4 s it
still played its old copy and while it loaded (its last state still said "this place"; in Tribe
the host does not check that a guest is in the world), so NPCs it noted as known then were never
sent again.

1. Two games on the tribe fixture; the guest's coverage of the host's NPCs near it (coop_multi).
2. The guest leaves (`coop.leave`), joins again, loads the host's world and plays Tribe again.
3. For 20 s, 25 s after it is back: the guest's game has the host's NPCs (coverage, the game's
   objects of both machines through `coop.sync`).
PASS: coverage at least 85 % in every sample after the rejoin (the campaign's limit) and no NPC
missing in the last one. A regression scenario: the campaign's loss was not reproduced by it (it passed
with and without the candidate fix npc.rts_inworld, which was then left out); --nofix is kept
for that switch if it comes back.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_rejoin_npcs.py [--nofix] [--rounds 2]
"""
import argparse
import time

import coop_multi as cm
import lane
from twoinst import Checks, start


def coverage(host, guest):
    missed = {}
    _, cov, total, _ = cm.measure(host, guest, missed=missed)
    return cov, total, missed.get('n', [])


def in_tribe(con):
    try:
        return 'Tribe' in con.call('game.mode')['out']
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--rounds', type=int, default=2)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:  # (no switch of this name now: coop.fix answers "unknown fix")
        print(' ', host.call('coop.fix npc.rts_inworld off')['out'].strip(), flush=True)
    time.sleep(10)
    cov, total, miss = coverage(host, guest)
    print(f'  before: coverage {cov:.0%} of {total}, missing NPCs {miss}', flush=True)
    for r in range(1, a.rounds + 1):
        guest.call('coop.leave')
        time.sleep(4)
        print('  rejoin:', guest.call(lane.JOIN)['out'].strip()[:60], flush=True)
        t0, left = time.time(), False
        while time.time() - t0 < 60 and not left:  # the host's world comes: the old copy goes
            m = guest.call('game.mode')['out']
            left = 'Load' in m or 'GalaxyGameEntry' in m or 'receiving world' in guest.call('coop.status')['out']
            time.sleep(0.3)
        t1 = time.time()
        while time.time() - t1 < 120 and not (in_tribe(guest) and 'Load' not in guest.call('game.mode')['out']):
            time.sleep(0.5)
        back = in_tribe(guest)
        ck(left and back, f'round {r}: the guest left its old copy ({left}) and plays the host\'s Tribe again ({back})')
        if not back:
            break
        time.sleep(25)
        worst, last = 1.0, []
        for _ in range(4):
            cov, total, miss = coverage(host, guest)
            worst = min(worst, cov)
            last = miss
            time.sleep(5)
        print(f'  round {r}: coverage at least {worst:.0%} of {total}, missing NPCs in the last sample {last}', flush=True)
        ck(worst >= 0.85 and not last, f'round {r}: the rejoined guest has the host\'s NPCs (coverage {worst:.0%}, missing {last})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
