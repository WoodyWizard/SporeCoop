#!/usr/bin/env python3
"""BUG-1261: a bone the host's game no longer has does not stay on the guest's game (players' video: "the guest sees
bones the host does not have and cannot click them; the guest digs a fossil where the host sees empty grass").

The game fades a dug bone out and destroys it 3.5 s after a real dig (the interact node's Exit: cInteractiveOrnament_
FadeOut(orn, 0.5, true)); a part tier's TierBoneCleanup fades the bones on screen and destroys the others at once. The
guest's copy of a dig only marked the reward used (an unclickable bone stayed for good), and a bone the host's game
destroyed stayed on the guest, clickable (docs/re/creature.md#creature-ornament-fadeout).

Creature fixture (the 12 interactive ornaments are the same objects on both games), both players near the host's nest.
  dig      the host digs bone A the game's way with its fade (`coop.wobj use <A> fade`): within 8 s the guest's game
           has no bone A (its copy faded and destroyed); bone C, not touched, is still on both games.
  gone     the host's game destroys bone B (`coop.wobj ornkill <B>`, as TierBoneCleanup's off-screen path): within 10 s
           the guest's game has no bone B; bone C stays on both.
  settle   the guest takes the host as away and back (`coop.awaytest 1` then 0: the host's editor), the host destroys
           bone B at once: the guest keeps its copy for the first 6 s (the host's list is not trusted yet after its
           return, wobj_orn::kGoneSettleS = 10 s), and has none 14 s later.
Key frames of both games at the bone's place before and after (the same camera, `coop.view set`): the bone on both,
then on neither.

  SPORE_FIX_OFF=wobj.ornament_fade   FAILS (dig and gone: the guest's bone stays)

  coop_ornament_fade.py --case dig|gone|settle
"""
import argparse
import math
import time

from coop_ornament_model import at, go_near, look_at, orns
from twoinst import Checks, keyframe, start


def bones(host, guest):
    """bones (sort 2, an unused reward) on both games, nearest the host first: [(host row, guest row)]"""
    g = orns(guest)
    out = []
    for h in orns(host):
        if h['sort'] != 2 or h['used'] != 0:
            continue
        m = [o for o in g if math.dist(o['pos'], h['pos']) < 1.5 and o['used'] == 0]
        if m:
            out.append((h, m[0]))
    return out


def gone_from(con, pos, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if at(con, pos) is None:
            return True
        time.sleep(0.5)
    return at(con, pos) is None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', default='dig', choices=('dig', 'gone', 'settle'))
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    try:
        return run(host, guest, ck, a.case)
    finally:
        guest.call('coop.awaytest 0')
        for c in (host, guest):
            c.call('coop.view off')


def run(host, guest, ck, case):
    time.sleep(12)  # past wobj_orn::kGoneSettleS after the guest's load
    pairs = bones(host, guest)
    print(f'  bones on both games: {len(pairs)}', flush=True)
    ck(len(pairs) >= 2, f'two unused bones on both games ({len(pairs)})')
    if len(pairs) < 2:
        ck.faults(host=host, guest=guest)
        return ck.result()
    (ha, ga), (hc, gc) = pairs[0], pairs[-1]
    pos = ha['pos']
    for c, side in ((host, 1), (guest, -1)):
        print(f'  {c is host and "host" or "guest"}: {go_near(c, pos, side)}', flush=True)
    time.sleep(3)
    look_at((host, guest), pos)
    keyframe(f'{case}_before', 'the bone on both games, the same camera', settle=0)
    if case == 'settle':
        print('  guest: ' + guest.call('coop.awaytest 1')['out'].strip(), flush=True)
        time.sleep(7)
        print('  guest: ' + guest.call('coop.awaytest 0')['out'].strip(), flush=True)
        time.sleep(1.5)
    if case == 'dig':
        print('  host: ' + host.call(f'coop.wobj use {ha["p"]} fade')['out'].strip(), flush=True)
    else:
        print('  host: ' + host.call(f'coop.wobj ornkill {ha["p"]}')['out'].strip(), flush=True)
    ck(gone_from(host, pos, 6), "the host's game has no bone there any more")
    if case == 'settle':
        time.sleep(4.5)
        ck(at(guest, pos) is not None, "the guest keeps its bone right after the host's return (the host's list not trusted yet)")
        ck(gone_from(guest, pos, 14), "the guest's bone is gone once the host's list is trusted again")
    else:
        ck(gone_from(guest, pos, 8 if case == 'dig' else 10), "the guest's game has no bone there either")
    ck(at(host, hc['pos']) is not None and at(guest, hc['pos']) is not None, 'the bone nobody touched is still on both games')
    keyframe(f'{case}_after', "neither game shows the bone at that place (the ground only)", settle=0)
    for ln in guest.call('coop.wobj')['out'].splitlines():
        if 'ornaments faded' in ln:
            print('  guest: ' + ln[ln.index('ornaments faded'):], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
