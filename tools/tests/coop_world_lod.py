#!/usr/bin/env python3
"""Creature: are plants (`game_plant`) and rocks world objects both players share, or objects each game
makes around its own camera? (BUG-0473 plants and rocks, BUG-0498; docs/06-testing.md#testing-detector)

The host stays; the guest walks away from it in steps (coop.abilities goto, --step m each, --steps of
them) and back. After every step both games' whole object lists (`coop.census all 6000`) give every
`game_plant` and `rock` with its serial and position. Per kind the test prints, for each game, how far
its objects are from its own player and from the other player, and how many have a partner of the same
kind in the other game within 3 m.

Checked (the evidence for leaving a kind out of the detector's compare, worlddiff.LOCAL_LOD):
  * an object of the kind that exists in one game only is near that game's own player (its camera),
    not near the other player: each game streams it around its own camera;
  * objects that are in both games' lists at the same place are counted (shared, e.g. the save's own).
The test FAILS when a kind has one-game-only objects near the OTHER player (a real difference a
player could walk to) in more than --tolerate of the samples.
"""
import argparse
import math
import time

import worlddiff
from twoinst import Checks, start

KINDS = {'game_plant': 0x018C84A9, 'rock': 0x02A8FB3F}


def objs(w, noun):
    return [x for x in w.rows if x['kind'] == 'g' and x['noun'] == noun and not worlddiff._origin(x['pos'])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--step', type=float, default=35.0)
    ap.add_argument('--steps', type=int, default=5)
    ap.add_argument('--tolerate', type=float, default=0.1)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    stats = {k: dict(samples=0, bad=0, only_h_near_own=0, only_h_near_other=0, only_g_near_own=0, only_g_near_other=0,
                     shared=0, ser_shared=0) for k in KINDS}
    try:
        time.sleep(5)
        path = [i for i in range(1, a.steps + 1)] + [a.steps - i for i in range(1, a.steps + 1)]
        start_pos = None
        for n, k in enumerate(path):
            hw = worlddiff.dump(host, 6000)
            gw = worlddiff.dump(guest, 6000)
            if not (hw.me and gw.me):
                print('  no positions', hw.me, gw.me)
                continue
            if start_pos is None:
                start_pos = gw.me
            d_players = math.dist(hw.me, gw.me)
            line = [f'  step {n}: players {d_players:.0f} m apart']
            for name, noun in KINDS.items():
                h, g = objs(hw, noun), objs(gw, noun)
                pairs, uh, ug = worlddiff._match(h, g, 3.0)
                st = stats[name]
                st['samples'] += 1
                st['shared'] += len(pairs)
                st['ser_shared'] += sum(1 for x, y in pairs if x['ser'] == y['ser'])
                # a one-game-only object: nearer its own game's player, or nearer the other one?
                bad = 0
                for x in uh:
                    own, other = math.dist(x['pos'], hw.me), math.dist(x['pos'], gw.me)
                    if own <= other:
                        st['only_h_near_own'] += 1
                    else:
                        st['only_h_near_other'] += 1
                        bad += other < 70  # where the guest is and the host is not: the guest would miss it
                for y in ug:
                    own, other = math.dist(y['pos'], gw.me), math.dist(y['pos'], hw.me)
                    if own <= other:
                        st['only_g_near_own'] += 1
                    else:
                        st['only_g_near_other'] += 1
                        bad += other < 70
                st['bad'] += bad > 0
                dh = sorted(math.dist(x['pos'], hw.me) for x in uh)[:3]
                dg = sorted(math.dist(y['pos'], gw.me) for y in ug)[:3]
                line.append(f'{name}: host {len(h)} guest {len(g)} same place {len(pairs)} '
                            f'(same serial {sum(1 for x, y in pairs if x["ser"] == y["ser"])}), host only {len(uh)} '
                            f'(m from the host {",".join(f"{v:.0f}" for v in dh)}), guest only {len(ug)} '
                            f'(m from the guest {",".join(f"{v:.0f}" for v in dg)})')
            print(' | '.join(line), flush=True)
            # the next place: k steps away from where the guest started, along x
            x, y, z = start_pos
            guest.call(f'coop.abilities goto {x + a.step * k:.1f} {y:.1f} {z + a.step * k * 0.5:.1f}')
            time.sleep(6)
        for name, st in stats.items():
            print(f'  {name}: {st}', flush=True)
            ck(st['samples'] > 0, f'{name}: samples taken')
            ck(st['bad'] <= a.tolerate * max(1, st['samples']),
               f"{name}: objects one game has and the other has not lie near that game's own player "
               f"({st['bad']} of {st['samples']} samples had one near the other player instead)")
    finally:
        ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
